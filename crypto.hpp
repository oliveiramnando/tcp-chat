#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <string>
#include <vector>
#include <cstdint>

bool keyGen();

bool loadKey(const std::string& filename, unsigned char* key);

bool encrypt(const std::string& message, std::vector<unsigned char>& encrypted, const unsigned char* key);

bool decrypt(std::vector<unsigned char>& decryptedMessage, const std::string& encryptedMessage, const unsigned char* key);

#endif