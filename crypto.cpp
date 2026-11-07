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

bool deriveSessionKeys(Session &s, bool isInitiator, const string &peerPk)
{
    if (!s.hasKeyPair || peerPk.size() != crypto_kx_PUBLICKEYBYTES)
    {
        return false;
    }
    unsigned char pk[crypto_kx_PUBLICKEYBYTES];
    memcpy(&pk, peerPk.data(), crypto_kx_PUBLICKEYBYTES);
    if (isInitiator)
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

    sodium_memzero(s.sk, crypto_kx_SECRETKEYBYTES);     // sk no longer used so wipe it

    return true;
}

void wipeSession(Session &s)
{
    sodium_memzero(s.pk , crypto_kx_PUBLICKEYBYTES);
    sodium_memzero(s.sk , crypto_kx_SECRETKEYBYTES);
    sodium_memzero(s.rx , crypto_kx_SESSIONKEYBYTES);
    sodium_memzero(s.tx , crypto_kx_SESSIONKEYBYTES);
    s.hasKeyPair = false;
    s.ready = false;
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


