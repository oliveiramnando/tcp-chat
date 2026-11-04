#include "crypto.hpp"

#include <sodium.h>
#include <fstream>
#include <cerrno>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

using namespace std;

bool keyGen()
{
    if (sodium_init() < 0)
    {
        cout << "Error initializing sodium" << endl;
        return false;
    }

    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    crypto_aead_xchacha20poly1305_ietf_keygen(key);

    const string filename = "chat.key";
    ofstream outFile(filename, ios::binary | ios::out);

    if (!outFile)
    {
        cout << "Error opening file" << endl;
        return false;
    }

    outFile.write(reinterpret_cast<const char*>(key), sizeof(key));

    outFile.close();
    return true;
}


bool loadKey(const string& filename, unsigned char* key)
{
    ifstream keyFile(filename, ios::binary);
    if (!keyFile)
    {
        cerr << "Error opening key file" << endl;
        return false;
    }

    keyFile.read(reinterpret_cast<char*>(key), crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    if (!keyFile)
    {
        cerr << "Error reading key from file" << endl;
        return false;
    }

    keyFile.close();
    return true;
}


bool encrypt(const string& message, vector<unsigned char>* encrypted, const unsigned char* key)
{
    unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    memcpy(encrypted->data(), nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);

    if (crypto_aead_xchacha20poly1305_ietf_encrypt(encrypted->data() + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, nullptr, 
                                                reinterpret_cast<const unsigned char*>(message.data()), message.size(), 
                                                nullptr, 0, nullptr, nonce, key) != 0)
    {
        cerr << "Encryption failed" << endl;
        return false;
    }
    return true;
}

