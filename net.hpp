#ifndef NET_HPP
#define NET_HPP

#include <cstddef>
#include <string>
#include <vector>

#define MAX_MESSAGE_SIZE 8192

enum class MsgType : uint8_t {
    MESSAGE=1,
    HANDSHAKE=2,
    PEER_JOINED=3,
    PEER_LEFT=4,
    ERROR_MSG=5,
};

enum class Role : uint8_t {
    RESPONDER=0,
    INITIATOR=1,
};

bool readNBytes(int clientSocket, char* buffer, size_t n);

bool sendAll(int clientSocket, const char* buffer, size_t n);

bool receiveFrame(int clientSocket, MsgType& type, std::string& body);

bool sendFrame(int clientSocket, MsgType type, const std::vector<unsigned char>& body);

#endif
