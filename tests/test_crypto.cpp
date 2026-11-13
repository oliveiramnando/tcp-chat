#include "doctest.h"
#include "../src/crypto.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace std;

// Helper: a fresh in-memory identity (no file involved).
static Identity makeIdentity()
{
    Identity id;
    crypto_sign_keypair(id.pk, id.sk);
    return id;
}

// Helper: a 32-byte buffer filled with one repeated byte.
static vector<unsigned char> filled(unsigned char value)
{
    return vector<unsigned char>(32, value);
}

// ---------- buildHandshakeMessage ----------

TEST_CASE("buildHandshakeMessage: length and field order")
{
    vector<unsigned char> sender = filled(0x11);
    vector<unsigned char> recipient = filled(0x22);
    vector<unsigned char> eph = filled(0x33);

    vector<unsigned char> msg = buildHandshakeMessage(sender.data(), recipient.data(), eph.data());

    const size_t labelLen = sizeof(HANDSHAKE_LABEL) - 1;   // 20, no '\0'
    REQUIRE(labelLen == 20);
    REQUIRE(msg.size() == labelLen + 32 * 3);

    CHECK(memcmp(msg.data(), HANDSHAKE_LABEL, labelLen) == 0);
    CHECK(memcmp(msg.data() + labelLen,      sender.data(),    32) == 0);
    CHECK(memcmp(msg.data() + labelLen + 32, recipient.data(), 32) == 0);
    CHECK(memcmp(msg.data() + labelLen + 64, eph.data(),       32) == 0);
}

// ---------- signHandshake / verifyHandshake ----------

TEST_CASE("verifyHandshake accepts a valid signature and rejects any change")
{
    Identity alice = makeIdentity();
    Identity bob = makeIdentity();
    Identity carol = makeIdentity();

    unsigned char eph[crypto_kx_PUBLICKEYBYTES];
    randombytes_buf(eph, sizeof(eph));

    // Alice signs her ephemeral key for Bob.
    unsigned char sig[crypto_sign_BYTES];
    REQUIRE(signHandshake(alice, bob.pk, eph, sig));

    SUBCASE("valid handshake passes")
    {
        CHECK(verifyHandshake(alice.pk, bob.pk, eph, sig));
    }

    SUBCASE("different sender fails")
    {
        CHECK_FALSE(verifyHandshake(carol.pk, bob.pk, eph, sig));
    }

    SUBCASE("different recipient fails (redirected to Carol)")
    {
        CHECK_FALSE(verifyHandshake(alice.pk, carol.pk, eph, sig));
    }

    SUBCASE("changed ephemeral key fails")
    {
        unsigned char otherEph[crypto_kx_PUBLICKEYBYTES];
        memcpy(otherEph, eph, sizeof(eph));
        otherEph[0] ^= 0x01;
        CHECK_FALSE(verifyHandshake(alice.pk, bob.pk, otherEph, sig));
    }

    SUBCASE("changed signature fails")
    {
        unsigned char badSig[crypto_sign_BYTES];
        memcpy(badSig, sig, sizeof(sig));
        badSig[10] ^= 0x01;
        CHECK_FALSE(verifyHandshake(alice.pk, bob.pk, eph, badSig));
    }

    SUBCASE("signature from the wrong identity fails")
    {
        unsigned char carolSig[crypto_sign_BYTES];
        REQUIRE(signHandshake(carol, bob.pk, eph, carolSig));
        CHECK_FALSE(verifyHandshake(alice.pk, bob.pk, eph, carolSig));
    }
}

// ---------- deriveSessionKeys ----------

// Helper: build two sessions that completed a handshake with each other.
static void handshakePair(Session& a, Session& b)
{
    a.isInitiator = true;
    b.isInitiator = false;
    makeKeyPair(a);
    makeKeyPair(b);

    string aPk(reinterpret_cast<const char*>(a.pk), crypto_kx_PUBLICKEYBYTES);
    string bPk(reinterpret_cast<const char*>(b.pk), crypto_kx_PUBLICKEYBYTES);

    REQUIRE(deriveSessionKeys(a, bPk));
    REQUIRE(deriveSessionKeys(b, aPk));
}

TEST_CASE("deriveSessionKeys: each side's tx is the other's rx")
{
    Session a, b;
    handshakePair(a, b);

    CHECK(memcmp(a.tx, b.rx, crypto_kx_SESSIONKEYBYTES) == 0);
    CHECK(memcmp(a.rx, b.tx, crypto_kx_SESSIONKEYBYTES) == 0);
    CHECK(memcmp(a.tx, a.rx, crypto_kx_SESSIONKEYBYTES) != 0);   // directions differ

    CHECK(sessionFingerPrint(a) == sessionFingerPrint(b));
}

TEST_CASE("deriveSessionKeys wipes the ephemeral secret key")
{
    Session a, b;
    handshakePair(a, b);

    unsigned char zeros[crypto_kx_SECRETKEYBYTES]{};
    CHECK(memcmp(a.sk, zeros, sizeof(zeros)) == 0);
    CHECK(memcmp(b.sk, zeros, sizeof(zeros)) == 0);
}

TEST_CASE("deriveSessionKeys rejects a wrong-size key")
{
    Session s;
    makeKeyPair(s);

    CHECK_FALSE(deriveSessionKeys(s, string(31, 'x')));
    CHECK_FALSE(deriveSessionKeys(s, string(33, 'x')));
    CHECK_FALSE(deriveSessionKeys(s, string()));
}

TEST_CASE("deriveSessionKeys rejects a session with no key pair")
{
    Session s;   // makeKeyPair never called
    CHECK_FALSE(deriveSessionKeys(s, string(crypto_kx_PUBLICKEYBYTES, 'x')));
}

// ---------- encrypt / decrypt ----------

// Helper: a random 32-byte encryption key.
static vector<unsigned char> randomKey()
{
    vector<unsigned char> key(crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    randombytes_buf(key.data(), key.size());
    return key;
}

TEST_CASE("encrypt and decrypt round trip")
{
    vector<unsigned char> key = randomKey();
    string message = "alice : hello bob";

    vector<unsigned char> encrypted;
    REQUIRE(encrypt(message, encrypted, key.data()));
    CHECK(encrypted.size() == message.size() + 24 + 16);   // nonce + tag

    vector<unsigned char> decrypted;
    REQUIRE(decrypt(decrypted, string(encrypted.begin(), encrypted.end()), key.data()));
    CHECK(string(decrypted.begin(), decrypted.end()) == message);
}

TEST_CASE("encrypting the same message twice gives different ciphertexts")
{
    vector<unsigned char> key = randomKey();
    vector<unsigned char> first, second;
    REQUIRE(encrypt("same", first, key.data()));
    REQUIRE(encrypt("same", second, key.data()));
    CHECK(first != second);   // random nonce each time
}

TEST_CASE("decrypt rejects a ciphertext with any byte flipped")
{
    vector<unsigned char> key = randomKey();
    vector<unsigned char> encrypted;
    REQUIRE(encrypt("tamper with me", encrypted, key.data()));

    // Try every position: nonce, ciphertext and tag.
    for (size_t i = 0; i < encrypted.size(); ++i)
    {
        CAPTURE(i);   // printed if this iteration fails
        vector<unsigned char> tampered = encrypted;
        tampered[i] ^= 0x01;

        vector<unsigned char> decrypted;
        CHECK_FALSE(decrypt(decrypted, string(tampered.begin(), tampered.end()), key.data()));
    }
}

TEST_CASE("decrypt rejects input shorter than nonce + tag")
{
    vector<unsigned char> key = randomKey();
    vector<unsigned char> decrypted;

    CHECK_FALSE(decrypt(decrypted, string(), key.data()));
    CHECK_FALSE(decrypt(decrypted, string(39, 'x'), key.data()));   // 24 + 16 - 1
}

TEST_CASE("decrypt rejects the wrong key")
{
    vector<unsigned char> key = randomKey();
    vector<unsigned char> otherKey = randomKey();

    vector<unsigned char> encrypted;
    REQUIRE(encrypt("secret", encrypted, key.data()));

    vector<unsigned char> decrypted;
    CHECK_FALSE(decrypt(decrypted, string(encrypted.begin(), encrypted.end()), otherKey.data()));
}
