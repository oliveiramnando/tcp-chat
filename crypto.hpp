#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <string>
#include <vector>
#include <cstdint>

using namespace std;

bool keyGen();

bool loadKey(const string& filename, unsigned char* key);

bool encrypt(const string& message, vector<unsigned char>* encrypted, const unsigned char* key);

vector<unsigned char> decrypt(uint32_t encryptedLength, const string& encryptedMessage, const unsigned char* key);

#endif