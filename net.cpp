#include "net.hpp"

#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <iostream>

using namespace std;

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

