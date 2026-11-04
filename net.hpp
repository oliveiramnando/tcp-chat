#ifndef NET_HPP
#define NET_HPP

#include <cstddef>

#define MAX_MESSAGE_SIZE 8192

bool readNBytes(int clientSocket, char* buffer, size_t n);

bool sendAll(int clientSocket, const char* buffer, size_t n);

#endif