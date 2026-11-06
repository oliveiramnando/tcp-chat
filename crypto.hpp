#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <sodium.h>

struct Session
{
    unsigned char pk[crypto_kx_PUBLICKEYBYTES]{}; 
    unsigned char sk[crypto_kx_SECRETKEYBYTES]{};
    unsigned char rx[crypto_kx_SESSIONKEYBYTES]{};
    unsigned char tx[crypto_kx_SESSIONKEYBYTES]{};
    bool hasKeyPair = false;
    bool ready = false;
};

void makeKeyPair(Session& s);

bool deriveSessionKeys(Session& s, bool isInititor, const std::string& peerPk);

void wipeSession(Session& s);

bool keyGen();

bool loadKey(const std::string& filename, unsigned char* key);

bool encrypt(const std::string& message, std::vector<unsigned char>& encrypted, const unsigned char* key);

bool decrypt(std::vector<unsigned char>& decryptedMessage, const std::string& encryptedMessage, const unsigned char* key);

#endif
