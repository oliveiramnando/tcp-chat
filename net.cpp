#include "net.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cerrno>
#include <string>
#include <cstring>
#include <iostream>
#include <cstdint>

using namespace std;

bool readNBytes(int clientSocket, char* buffer, size_t n)
{   
    size_t bytesRead = 0;
    while (bytesRead < n)
    {
        ssize_t dataSize = recv(clientSocket, buffer + bytesRead, n - bytesRead, 0);
        if (dataSize < 0)
        {
            cerr << "Error: " << strerror(errno) << endl;
            return false;
        }
        if (dataSize == 0)
        {
            return false;
        }
        bytesRead += dataSize;
    }
    return true;
}

bool sendAll(int clientSocket, const char* buffer, size_t n)
{
    size_t bytesSent = 0;
    while (bytesSent < n)
    {
        ssize_t dataSize = send(clientSocket, buffer + bytesSent, n - bytesSent, 0);
        if (dataSize < 0)
        {
            cerr << "Error: " << strerror(errno) << endl;
            return false;
        }
        bytesSent += dataSize;
    }
    return true;
}

bool receiveFrame(int clientSocket, MsgType& type, string& encryptedMessage)
{
    // Read prefix for message length
    char lengthBytes[4] = {};
    if (!readNBytes(clientSocket, lengthBytes, sizeof(uint32_t)))
    {
        return false;
    }

    char typeByte;
    if (!readNBytes(clientSocket, &typeByte, sizeof(MsgType)))
    {
        return false;
    }
    
    type = static_cast<MsgType>(static_cast<uint8_t>(typeByte));

    // length of entire encryption [nonce + ciphertext + tag]
    uint32_t encryptedLength;
    memcpy(&encryptedLength, lengthBytes, sizeof(encryptedLength));
    encryptedLength = ntohl(encryptedLength); 

    if (encryptedLength == 0 || encryptedLength > MAX_MESSAGE_SIZE)
    {
       return false;
    }

    // read rest of data using message length to ensure all bytes get processed
    encryptedMessage.resize(encryptedLength);
    if (!readNBytes(clientSocket, encryptedMessage.data(), encryptedLength))
    {
        return false;
    }

    return true;
}

bool sendFrame(int clientSocket, const vector<unsigned char>& encrypted)
{
    // rename this to frame size instead of encrypted length
    uint32_t frameLength = htonl(sizeof(MsgType) + static_cast<uint32_t>(encrypted.size()));

    // send buffer = [4 bytes for length][1 byte for messagetype][nonce + ciphertext + tag]
    vector<char> sendBuffer(sizeof(uint32_t) + sizeof(MsgType) + encrypted.size());
    MsgType msg = MsgType::MESSAGE;

    memcpy(sendBuffer.data(), &frameLength, sizeof(frameLength));
    sendBuffer[sizeof(frameLength)] = static_cast<char>(msg);
    memcpy(sendBuffer.data() + sizeof(frameLength) + sizeof(MsgType), encrypted.data(), encrypted.size());


    if (!sendAll(clientSocket, sendBuffer.data(), sendBuffer.size()))
    {
        return false;
    }

    return true;
}
