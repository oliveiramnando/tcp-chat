#include "crypto.hpp"

#include <sodium.h>
#include <fstream>
#include <cerrno>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

using namespace std;

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
}

string sessionFingerPrint(Session& s)
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

    for (int i = 0; hex[i] != '\0'; ++i) {
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


