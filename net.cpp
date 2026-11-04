#include "net.hpp"
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <iostream>

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
