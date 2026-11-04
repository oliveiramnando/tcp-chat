#include "crypto.hpp"

#include <sodium.h>
#include <fstream>
#include <cerrno>
#include <iostream>
#include <string>

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