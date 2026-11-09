#ifndef NET_HPP
#define NET_HPP

#include <cstddef>
#include <string>
#include <vector>
#include <cstdint>
#include <array>

#define MAX_MESSAGE_SIZE 8192

using PeerId = std::array<unsigned char, 32>;

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

bool parseBody(const std::string& body, PeerId& id, std::vector<unsigned char>& payload);

std::vector<unsigned char> buildBody(const PeerId& id, const std::vector<unsigned char>& payload);

bool receiveFrame(int clientSocket, MsgType& type, std::string& body);

bool sendFrame(int clientSocket, MsgType type, const std::vector<unsigned char>& body);

#endif
