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

bool receiveFrame(int clientSocket, MsgType& type, string& body)
{
    // read 4 bytes for length
    char lengthBytes[sizeof(uint32_t)] = {}; 
    if (!readNBytes(clientSocket, lengthBytes, sizeof(uint32_t)))
    {
        return false;
    }
    
    // convert length to ntohl
    uint32_t networkLength{};
    memcpy(&networkLength, lengthBytes, sizeof(networkLength));

    uint32_t frameLength = ntohl(networkLength);
    if (frameLength < 1 || frameLength > MAX_MESSAGE_SIZE)
    {
        return false;
    }

    // build recveive buffer
    vector<char> receiveBuffer(static_cast<size_t>(frameLength));

    // read rest of bytes, which include the msgType
    if (!readNBytes(clientSocket, receiveBuffer.data(), receiveBuffer.size()))
    {
        return false;
    }

    // get and convert message type
    type = static_cast<MsgType>(static_cast<unsigned char>(receiveBuffer[0]));
    
    body.assign(receiveBuffer.begin() + 1, receiveBuffer.end());
    
    return true;
}

bool sendFrame(int clientSocket, MsgType type, const vector<unsigned char>& body)
{
    const uint8_t typeByte = static_cast<uint8_t>(type);

    const uint32_t bodyLength = static_cast<uint32_t>(sizeof(typeByte) + body.size());
    const uint32_t networkLength = htonl(bodyLength);

    vector<char> sendBuffer(sizeof(networkLength) + bodyLength);
    
    memcpy(sendBuffer.data(), &networkLength, sizeof(networkLength));
    memcpy(sendBuffer.data() + sizeof(networkLength), &typeByte, sizeof(typeByte));
    memcpy(sendBuffer.data() + sizeof(networkLength) + sizeof(uint8_t), body.data(), body.size());

    if (!sendAll(clientSocket, sendBuffer.data(), sendBuffer.size()))
    {
        return false;
    }

    return true;
}
