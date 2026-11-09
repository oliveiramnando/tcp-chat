#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <sodium.h>
#include <string>

struct Session
{
    unsigned char pk[crypto_kx_PUBLICKEYBYTES]{}; 
    unsigned char sk[crypto_kx_SECRETKEYBYTES]{};
    unsigned char rx[crypto_kx_SESSIONKEYBYTES]{};
    unsigned char tx[crypto_kx_SESSIONKEYBYTES]{};

    unsigned char peerPk[crypto_kx_PUBLICKEYBYTES]{};

    bool hasKeyPair = false;
    bool ready = false;

    bool isInitiator = false;
};

void makeKeyPair(Session& s);

bool deriveSessionKeys(Session& s, const std::string& peerPk);

void wipeSession(Session& s);

std::string sessionFingerPrint(Session& s);

bool encrypt(const std::string& message, std::vector<unsigned char>& encrypted, const unsigned char* key);

bool decrypt(std::vector<unsigned char>& decryptedMessage, const std::string& encryptedMessage, const unsigned char* key);

#endif
