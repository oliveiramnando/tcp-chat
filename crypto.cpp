#include "crypto.hpp"

#include <sodium.h>
#include <fstream>
#include <cerrno>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>

using namespace std;

bool loadOrCreateIdentity(const string& path, Identity& id)
{
    if (filesystem::exists(path))
    {   
        ifstream inFile(path, ios::binary);
        if (!inFile)
        {
            return false;   // couldn't open the file
        }

        // read exactly 64 bytes into id.sk
        inFile.read(reinterpret_cast<char*>(id.sk), crypto_sign_SECRETKEYBYTES);
        if (!inFile)
        {
            sodium_memzero(id.sk, crypto_sign_SECRETKEYBYTES);
            return false;   // file shorter than 64 bytes
        }

        // make sure there is nothing after those 64 bytes
        if (inFile.peek() != ifstream::traits_type::eof())
        {
            sodium_memzero(id.sk, crypto_sign_SECRETKEYBYTES);
            return false;   // file longer than 64 bytes
        }

        // recover the public key from the secret key
        if (crypto_sign_ed25519_sk_to_pk(id.pk, id.sk) != 0)
        {
            sodium_memzero(id.sk, crypto_sign_SECRETKEYBYTES);
            return false;
        }      

        return true;
    }

    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(pk, sk);

    ofstream outFile(path, ios::binary | ios::out);
    if (!outFile)
    {
        return false;
    }

    error_code ec;
    filesystem::permissions(path, filesystem::perms::owner_read | filesystem::perms::owner_write, filesystem::perm_options::replace, ec);
    if (ec)
    {
        sodium_memzero(sk, crypto_sign_SECRETKEYBYTES);
        return false;
    }

    outFile.write(reinterpret_cast<const char*>(sk), sizeof(sk));
    if (!outFile)
    {
        sodium_memzero(sk, crypto_sign_SECRETKEYBYTES);
        return false;
    }

    outFile.close();
    if (!outFile)
    {
        sodium_memzero(sk, crypto_sign_SECRETKEYBYTES);
        return false;
    }

    memcpy(id.pk, pk, crypto_sign_PUBLICKEYBYTES);
    memcpy(id.sk, sk, crypto_sign_SECRETKEYBYTES);

    sodium_memzero(sk, crypto_sign_SECRETKEYBYTES);

    return true;
}

bool signLogin(const Identity& id, const array<unsigned char, CHALLENGE_BYTES>& challenge, unsigned char signature[crypto_sign_BYTES])
{
    vector<unsigned char> message = buildLoginMessage(challenge);

    return crypto_sign_detached(signature, nullptr, message.data(), message.size(), id.sk) == 0;
}

string idFingerPrint(const unsigned char* pk)
{
    constexpr size_t BUFFER_SIZE = crypto_sign_PUBLICKEYBYTES;

    unsigned char buffer[BUFFER_SIZE]{};
    memcpy(buffer, pk, crypto_sign_PUBLICKEYBYTES);

    unsigned char hash[crypto_generichash_BYTES]{}; 
    crypto_generichash(hash, crypto_generichash_BYTES, buffer, BUFFER_SIZE, nullptr, 0);

    // format it; convert part of it with sodium_bin2hex and insert space every 4 char
    char hex[crypto_generichash_BYTES * 2 + 1]{};
    sodium_bin2hex(hex, sizeof(hex), hash, crypto_generichash_BYTES);

    string fingerPrint = "";

    for (int i = 0; i < 32; ++i) {
        // Add a space every 4 characters (but not at the very beginning)
        if (hex[i] == '\0') { break; }
        if (i > 0 && i % 4 == 0) {
            fingerPrint += ' ';
        }
        fingerPrint += hex[i];
    }

    return fingerPrint;
}

void makeKeyPair(Session &s)
{
    crypto_kx_keypair(s.pk, s.sk);
    s.hasKeyPair = true;
    s.ready = false;
}

bool deriveSessionKeys(Session &s, const string &peerPk)
{
    if (!s.hasKeyPair || peerPk.size() != crypto_kx_PUBLICKEYBYTES)
    {
        return false;
    }
    unsigned char pk[crypto_kx_PUBLICKEYBYTES];
    memcpy(&pk, peerPk.data(), crypto_kx_PUBLICKEYBYTES);
    if (s.isInitiator)
    {
        if (crypto_kx_server_session_keys(s.rx, s.tx, 
                                          s.pk, s.sk, pk) != 0)
        {
            return false;
        }
    } else
    {
        if (crypto_kx_client_session_keys(s.rx, s.tx,
                                          s.pk, s.sk, pk) != 0)
        {
            return false;
        }
    }

    memcpy(s.peerPk, pk, crypto_kx_PUBLICKEYBYTES);

    sodium_memzero(s.sk, crypto_kx_SECRETKEYBYTES);     // sk no longer used so wipe it

    return true;
}

void wipeSession(Session &s)
{
    sodium_memzero(s.pk , crypto_kx_PUBLICKEYBYTES);
    sodium_memzero(s.sk , crypto_kx_SECRETKEYBYTES);
    sodium_memzero(s.rx , crypto_kx_SESSIONKEYBYTES);
    sodium_memzero(s.tx , crypto_kx_SESSIONKEYBYTES);

    sodium_memzero(s.peerPk, crypto_kx_PUBLICKEYBYTES);

    s.hasKeyPair = false;
    s.ready = false;

    s.isInitiator = false;
}

string sessionFingerPrint(const Session& s)
{
    constexpr size_t BUFFER_SIZE = crypto_kx_PUBLICKEYBYTES * 2;

    unsigned char buffer[BUFFER_SIZE]{};   // [intiator 32 bytes][responder 32 bytes]
    if (s.isInitiator)
    {
        memcpy(buffer, s.pk, crypto_kx_PUBLICKEYBYTES);
        memcpy(buffer + crypto_kx_PUBLICKEYBYTES, s.peerPk, crypto_kx_PUBLICKEYBYTES);

    } else
    {
        memcpy(buffer, s.peerPk, crypto_kx_PUBLICKEYBYTES);
        memcpy(buffer + crypto_kx_PUBLICKEYBYTES, s.pk, crypto_kx_PUBLICKEYBYTES);
    }

    unsigned char hash[crypto_generichash_BYTES]{}; 
    crypto_generichash(hash, crypto_generichash_BYTES, buffer, BUFFER_SIZE, nullptr, 0);

    // format it; convert part of it with sodium_bin2hex and insert space every 4 char
    char hex[crypto_generichash_BYTES * 2 + 1]{};
    sodium_bin2hex(hex, sizeof(hex), hash, crypto_generichash_BYTES);

    string fingerPrint = "";

    for (int i = 0; i < 32; ++i) {
        if (hex[i] == '\0') { break; }
        // Add a space every 4 characters (but not at the very beginning)
        if (i > 0 && i % 4 == 0) {
            fingerPrint += ' ';
        }
        fingerPrint += hex[i];
    }

    return fingerPrint;
}

bool encrypt(const string& message, vector<unsigned char>& encrypted, const unsigned char* key)
{
    encrypted.resize(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + message.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);

    unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    memcpy(encrypted.data(), nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);

    if (crypto_aead_xchacha20poly1305_ietf_encrypt(encrypted.data() + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, nullptr, 
                                                reinterpret_cast<const unsigned char*>(message.data()), message.size(), 
                                                nullptr, 0, nullptr, nonce, key) != 0)
    {
        return false;
    }
    return true;
}

bool decrypt(vector<unsigned char>& decryptedMessage, const string& encryptedMessage, const unsigned char* key)
{
    // Nonce extraction
    unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    if (encryptedMessage.size() < sizeof(nonce) + crypto_aead_xchacha20poly1305_ietf_ABYTES)
    {
        return false;
    }
    memcpy(nonce, encryptedMessage.data(), sizeof(nonce));

    // Encrypted message + tag extraction
    size_t messageTagLength = encryptedMessage.size() - sizeof(nonce);   // length of message + tag
    vector<unsigned char> encrypted(messageTagLength);
    memcpy(encrypted.data(), encryptedMessage.data() + sizeof(nonce), messageTagLength);

    // Decryption
    decryptedMessage.resize(messageTagLength - crypto_aead_xchacha20poly1305_ietf_ABYTES); // length of plaintext
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(decryptedMessage.data(), nullptr, nullptr, encrypted.data(), messageTagLength, nullptr, 0, nonce, key) != 0)
    {
        return false;
    }

    return true;
}


