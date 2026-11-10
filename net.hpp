#ifndef NET_HPP
#define NET_HPP

#include <cstddef>
#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include <set>

constexpr std::size_t MAX_MESSAGE_SIZE = 8192;
constexpr std::size_t CHALLENGE_BYTES = 32;
constexpr char LOGIN_LABEL[] = "tcpchat-login-v1";

using PeerId = std::array<unsigned char, 32>;

enum class MsgType : uint8_t {
    MESSAGE=1,
    HANDSHAKE=2,
    PEER_JOINED=3,
    PEER_LEFT=4,
    ERROR_MSG=5,
    CHALLENGE=6,
    AUTH=7,
    AUTH_OK=8,
    USER_LIST=9,
    USER_ONLINE=10,
    USER_OFFLINE=11,
};

enum class Role : uint8_t {
    RESPONDER=0,
    INITIATOR=1,
};

std::vector<unsigned char> buildLoginMessage(const std::array<unsigned char, CHALLENGE_BYTES>& challenge);

bool readNBytes(int clientSocket, char* buffer, size_t n);
bool sendAll(int clientSocket, const char* buffer, size_t n);

bool parseUserList(const std::string& payload, std::set<PeerId>& ids);

bool parseBody(const std::string& body, PeerId& id, std::vector<unsigned char>& payload);
std::vector<unsigned char> buildBody(const PeerId& id, const std::vector<unsigned char>& payload);

bool receiveFrame(int clientSocket, MsgType& type, std::string& body);
bool sendFrame(int clientSocket, MsgType type, const std::vector<unsigned char>& body);

#endif
