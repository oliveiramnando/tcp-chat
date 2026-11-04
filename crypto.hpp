#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <string>

using namespace std;

bool keyGen();

bool loadKey(const string& filename, unsigned char* key);


#endif