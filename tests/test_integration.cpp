// Integration tests: start the real ./server and talk to it over TCP,
// using the same net/crypto functions as the client but without a terminal.
//
// Run from the project root after building the server (make test does both).

#include "doctest.h"
#include "net.hpp"
#include "crypto.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace std;

namespace
{

constexpr int WAIT_MS  = 2000;   // how long to wait for a frame we expect
constexpr int QUIET_MS = 300;    // how long to wait to confirm nothing arrives

// ---------- sockets ----------

// Asks the OS for a port nobody is using, so tests never clash with a running server.
int freePort()
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;                                  // 0 = "pick one for me"
    bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    socklen_t len = sizeof(addr);
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    close(s);
    return ntohs(addr.sin_port);
}

// Connects to 127.0.0.1:port. Returns the socket, or -1 if nothing is listening.
int connectTo(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        close(s);
        return -1;
    }

    // Never block forever in the middle of a frame.
    timeval timeout{2, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return s;
}

// ---------- the server under test ----------

// Starts ./server on a free port and stops it when the object goes out of scope.
class ServerProcess
{
public:
    ServerProcess() : port(freePort())
    {
        signal(SIGPIPE, SIG_IGN);   // writing to a socket the server closed must not kill the tests

        pid = fork();
        if (pid == 0)
        {
            // Child: hide the server's own output, then become ./server.
            int devNull = open("/dev/null", O_WRONLY);
            dup2(devNull, STDOUT_FILENO);
            dup2(devNull, STDERR_FILENO);

            string portArg = to_string(port);
            execl("./server", "./server", portArg.c_str(), static_cast<char*>(nullptr));
            _exit(127);   // only reached if ./server could not be started
        }

        // Parent: wait until the server accepts connections (up to 3 seconds).
        for (int attempt = 0; attempt < 60 && !ready; ++attempt)
        {
            int probe = connectTo(port);
            if (probe >= 0)
            {
                close(probe);
                ready = true;
            }
            else
            {
                this_thread::sleep_for(chrono::milliseconds(50));
            }
        }
    }

    ~ServerProcess()
    {
        if (pid > 0)
        {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
    }

    // True while the server process is still running (it has not crashed).
    bool alive() const
    {
        int status = 0;
        return waitpid(pid, &status, WNOHANG) == 0;
    }

    ServerProcess(const ServerProcess&) = delete;
    ServerProcess& operator=(const ServerProcess&) = delete;

    int port;
    pid_t pid = -1;
    bool ready = false;
};

// ---------- a headless client ----------

struct TestClient
{
    Identity id;
    PeerId pid{};
    int sock = -1;

    TestClient()                       // brand-new identity
    {
        crypto_sign_keypair(id.pk, id.sk);
        memcpy(pid.data(), id.pk, pid.size());
    }

    explicit TestClient(const Identity& existing) : id(existing)   // reuse an identity
    {
        memcpy(pid.data(), id.pk, pid.size());
    }

    ~TestClient() { disconnect(); }

    void disconnect()
    {
        if (sock >= 0)
        {
            close(sock);
            sock = -1;
        }
    }

    TestClient(const TestClient&) = delete;
    TestClient& operator=(const TestClient&) = delete;
};

// ---------- frame helpers ----------

// Waits up to `ms` for the next frame. False on timeout or closed connection.
bool nextFrame(int sock, MsgType& type, string& body, int ms = WAIT_MS)
{
    pollfd p{sock, POLLIN, 0};
    if (poll(&p, 1, ms) <= 0)
    {
        return false;
    }
    return receiveFrame(sock, type, body);
}

// Reads the next frame, requires it to be of type `want`, and returns its body.
string expectFrame(int sock, MsgType want)
{
    MsgType type{};
    string body;
    REQUIRE(nextFrame(sock, type, body));
    REQUIRE(static_cast<int>(type) == static_cast<int>(want));
    return body;
}

// True if no data at all arrives within `ms`.
bool nothingArrives(int sock, int ms = QUIET_MS)
{
    pollfd p{sock, POLLIN, 0};
    return poll(&p, 1, ms) == 0;
}

// True if the server closes this connection (it may send an ERROR_MSG first).
bool closedByServer(int sock)
{
    for (int frames = 0; frames < 5; ++frames)
    {
        pollfd p{sock, POLLIN, 0};
        if (poll(&p, 1, WAIT_MS) <= 0)
        {
            return false;               // timed out: still open
        }

        MsgType type{};
        string body;
        if (!receiveFrame(sock, type, body))
        {
            return true;                // end of stream or reset: closed
        }
        // Got a frame (e.g. ERROR_MSG): keep reading until it closes.
    }
    return false;
}

PeerId toPeerId(const string& body)
{
    REQUIRE(body.size() == PeerId{}.size());
    PeerId id{};
    memcpy(id.data(), body.data(), id.size());
    return id;
}

// Sends raw bytes, for frames sendFrame() would never build.
void sendRaw(int sock, const vector<unsigned char>& bytes)
{
    REQUIRE(sendAll(sock, reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// ---------- protocol steps ----------

// Connects and runs the full login. Returns the USER_LIST the server sends.
set<PeerId> login(TestClient& c, int port)
{
    c.sock = connectTo(port);
    REQUIRE(c.sock >= 0);

    string challengeBody = expectFrame(c.sock, MsgType::CHALLENGE);
    REQUIRE(challengeBody.size() == CHALLENGE_BYTES);

    array<unsigned char, CHALLENGE_BYTES> challenge{};
    memcpy(challenge.data(), challengeBody.data(), challenge.size());

    unsigned char sig[crypto_sign_BYTES];
    REQUIRE(signLogin(c.id, challenge, sig));

    vector<unsigned char> auth(c.id.pk, c.id.pk + crypto_sign_PUBLICKEYBYTES);
    auth.insert(auth.end(), sig, sig + crypto_sign_BYTES);
    REQUIRE(sendFrame(c.sock, MsgType::AUTH, auth));

    expectFrame(c.sock, MsgType::AUTH_OK);

    set<PeerId> online;
    REQUIRE(parseUserList(expectFrame(c.sock, MsgType::USER_LIST), online));
    return online;
}

// Makes a key pair in `s` and sends a signed HANDSHAKE from `from` to `to`.
void sendHandshake(const TestClient& from, const PeerId& to, Session& s)
{
    s.isInitiator = memcmp(from.id.pk, to.data(), PeerId{}.size()) < 0;
    makeKeyPair(s);

    unsigned char sig[crypto_sign_BYTES];
    REQUIRE(signHandshake(from.id, to.data(), s.pk, sig));

    vector<unsigned char> payload(s.pk, s.pk + crypto_kx_PUBLICKEYBYTES);
    payload.insert(payload.end(), sig, sig + crypto_sign_BYTES);
    REQUIRE(sendFrame(from.sock, MsgType::HANDSHAKE, buildBody(to, payload)));
}

// Receives a HANDSHAKE, checks the server stamped the right sender and the
// signature is valid for us. Returns the sender's ephemeral public key.
string receiveVerifiedHandshake(const TestClient& me, const PeerId& expectedSender)
{
    PeerId sender{};
    vector<unsigned char> payload;
    REQUIRE(parseBody(expectFrame(me.sock, MsgType::HANDSHAKE), sender, payload));
    CHECK(sender == expectedSender);
    REQUIRE(payload.size() == HANDSHAKE_PAYLOAD_SIZE);

    const unsigned char* ephPk = payload.data();
    const unsigned char* sig = ephPk + crypto_kx_PUBLICKEYBYTES;
    REQUIRE(verifyHandshake(sender.data(), me.id.pk, ephPk, sig));

    return string(ephPk, ephPk + crypto_kx_PUBLICKEYBYTES);
}

void sendMessage(const TestClient& from, const PeerId& to, const Session& s, const string& text)
{
    vector<unsigned char> encrypted;
    REQUIRE(encrypt(text, encrypted, s.tx));
    REQUIRE(sendFrame(from.sock, MsgType::MESSAGE, buildBody(to, encrypted)));
}

string receiveMessage(const TestClient& me, const PeerId& expectedSender, const Session& s)
{
    PeerId sender{};
    vector<unsigned char> payload;
    REQUIRE(parseBody(expectFrame(me.sock, MsgType::MESSAGE), sender, payload));
    CHECK(sender == expectedSender);

    vector<unsigned char> plain;
    REQUIRE(decrypt(plain, string(payload.begin(), payload.end()), s.rx));
    return string(plain.begin(), plain.end());
}

} // namespace

// =====================================================================

TEST_CASE("integration: login, presence, signed handshake and private messages")
{
    ServerProcess server;
    REQUIRE_MESSAGE(server.ready, "./server did not start. Run 'make server' first.");

    TestClient alice, bob, carol;

    // --- presence ---
    CHECK(login(alice, server.port).empty());

    CHECK(login(bob, server.port) == set<PeerId>{alice.pid});
    CHECK(toPeerId(expectFrame(alice.sock, MsgType::USER_ONLINE)) == bob.pid);

    CHECK(login(carol, server.port) == set<PeerId>{alice.pid, bob.pid});
    CHECK(toPeerId(expectFrame(alice.sock, MsgType::USER_ONLINE)) == carol.pid);
    CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_ONLINE)) == carol.pid);

    // --- signed handshake, the same order the real client uses ---
    Session aliceSession, bobSession;

    sendHandshake(alice, bob.pid, aliceSession);                     // alice: /chat bob

    string aliceEph = receiveVerifiedHandshake(bob, alice.pid);       // bob: verify first
    sendHandshake(bob, alice.pid, bobSession);                        // bob: reply
    REQUIRE(deriveSessionKeys(bobSession, aliceEph));

    string bobEph = receiveVerifiedHandshake(alice, bob.pid);         // alice: verify reply
    REQUIRE(deriveSessionKeys(aliceSession, bobEph));

    CHECK(memcmp(aliceSession.tx, bobSession.rx, crypto_kx_SESSIONKEYBYTES) == 0);
    CHECK(memcmp(bobSession.tx, aliceSession.rx, crypto_kx_SESSIONKEYBYTES) == 0);
    CHECK(sessionFingerPrint(aliceSession) == sessionFingerPrint(bobSession));

    // --- encrypted messages both ways ---
    sendMessage(alice, bob.pid, aliceSession, "alice : hi bob");
    CHECK(receiveMessage(bob, alice.pid, bobSession) == "alice : hi bob");

    sendMessage(bob, alice.pid, bobSession, "bob : hi alice");
    CHECK(receiveMessage(alice, bob.pid, aliceSession) == "bob : hi alice");

    // --- carol saw none of it ---
    CHECK(nothingArrives(carol.sock));

    // --- disconnect is announced to everyone left ---
    alice.disconnect();
    CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_OFFLINE)) == alice.pid);
    CHECK(toPeerId(expectFrame(carol.sock, MsgType::USER_OFFLINE)) == alice.pid);

    CHECK(server.alive());
}

TEST_CASE("integration: login with a bad signature or a duplicate identity is rejected")
{
    ServerProcess server;
    REQUIRE_MESSAGE(server.ready, "./server did not start. Run 'make server' first.");

    SUBCASE("bad signature")
    {
        TestClient mallory;
        mallory.sock = connectTo(server.port);
        REQUIRE(mallory.sock >= 0);

        string challengeBody = expectFrame(mallory.sock, MsgType::CHALLENGE);
        array<unsigned char, CHALLENGE_BYTES> challenge{};
        memcpy(challenge.data(), challengeBody.data(), challenge.size());

        unsigned char sig[crypto_sign_BYTES];
        REQUIRE(signLogin(mallory.id, challenge, sig));
        sig[0] ^= 0x01;                                    // break the signature

        vector<unsigned char> auth(mallory.id.pk, mallory.id.pk + crypto_sign_PUBLICKEYBYTES);
        auth.insert(auth.end(), sig, sig + crypto_sign_BYTES);
        REQUIRE(sendFrame(mallory.sock, MsgType::AUTH, auth));

        expectFrame(mallory.sock, MsgType::ERROR_MSG);
        CHECK(closedByServer(mallory.sock));

        // Nobody was registered: the next user sees an empty list.
        TestClient bob;
        CHECK(login(bob, server.port).empty());
    }

    SUBCASE("same identity logged in twice")
    {
        TestClient alice;
        login(alice, server.port);

        TestClient copy(alice.id);
        copy.sock = connectTo(server.port);
        REQUIRE(copy.sock >= 0);

        string challengeBody = expectFrame(copy.sock, MsgType::CHALLENGE);
        array<unsigned char, CHALLENGE_BYTES> challenge{};
        memcpy(challenge.data(), challengeBody.data(), challenge.size());

        unsigned char sig[crypto_sign_BYTES];
        REQUIRE(signLogin(copy.id, challenge, sig));
        vector<unsigned char> auth(copy.id.pk, copy.id.pk + crypto_sign_PUBLICKEYBYTES);
        auth.insert(auth.end(), sig, sig + crypto_sign_BYTES);
        REQUIRE(sendFrame(copy.sock, MsgType::AUTH, auth));

        expectFrame(copy.sock, MsgType::ERROR_MSG);
        CHECK(closedByServer(copy.sock));

        // The original session is untouched.
        CHECK(nothingArrives(alice.sock));
    }

    CHECK(server.alive());
}

TEST_CASE("integration: sending to an offline user returns that user's ID")
{
    ServerProcess server;
    REQUIRE_MESSAGE(server.ready, "./server did not start. Run 'make server' first.");

    TestClient alice;
    login(alice, server.port);

    PeerId ghost{};
    ghost.fill(0x42);
    REQUIRE(sendFrame(alice.sock, MsgType::MESSAGE, buildBody(ghost, {1, 2, 3})));

    CHECK(toPeerId(expectFrame(alice.sock, MsgType::ERROR_MSG)) == ghost);

    // Alice is still connected and still gets presence updates.
    TestClient bob;
    login(bob, server.port);
    CHECK(toPeerId(expectFrame(alice.sock, MsgType::USER_ONLINE)) == bob.pid);

    CHECK(server.alive());
}

TEST_CASE("integration: malformed frames close that connection but not the server")
{
    ServerProcess server;
    REQUIRE_MESSAGE(server.ready, "./server did not start. Run 'make server' first.");

    TestClient bob;                     // a well-behaved user who should never be affected
    login(bob, server.port);

    // Before login: frame with length 0.
    {
        int s = connectTo(server.port);
        REQUIRE(s >= 0);
        expectFrame(s, MsgType::CHALLENGE);
        sendRaw(s, {0x00, 0x00, 0x00, 0x00});
        CHECK(closedByServer(s));
        close(s);
    }

    // Before login: length larger than MAX_MESSAGE_SIZE.
    {
        int s = connectTo(server.port);
        REQUIRE(s >= 0);
        expectFrame(s, MsgType::CHALLENGE);
        sendRaw(s, {0x00, 0x00, 0x23, 0x29});           // 9001 bytes
        CHECK(closedByServer(s));
        close(s);
    }

    // After login: unknown frame type.
    {
        TestClient mallory;
        login(mallory, server.port);
        CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_ONLINE)) == mallory.pid);

        REQUIRE(sendFrame(mallory.sock, static_cast<MsgType>(99), {}));
        CHECK(closedByServer(mallory.sock));
        CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_OFFLINE)) == mallory.pid);
    }

    // After login: a client pretending to be the server (forged USER_ONLINE).
    {
        TestClient mallory;
        login(mallory, server.port);
        CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_ONLINE)) == mallory.pid);

        PeerId fake{};
        fake.fill(0x66);
        vector<unsigned char> forged(fake.begin(), fake.end());
        REQUIRE(sendFrame(mallory.sock, MsgType::USER_ONLINE, buildBody(bob.pid, forged)));

        CHECK(closedByServer(mallory.sock));
        // Bob's next frame is mallory leaving, not the forged announcement.
        CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_OFFLINE)) == mallory.pid);
    }

    // After login: MESSAGE too short to hold a recipient ID.
    {
        TestClient mallory;
        login(mallory, server.port);
        CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_ONLINE)) == mallory.pid);

        REQUIRE(sendFrame(mallory.sock, MsgType::MESSAGE, vector<unsigned char>(10, 0xAA)));
        CHECK(closedByServer(mallory.sock));
        CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_OFFLINE)) == mallory.pid);
    }

    // The server is still up and serving new users, and bob is still online.
    CHECK(server.alive());

    TestClient dave;
    CHECK(login(dave, server.port) == set<PeerId>{bob.pid});
    CHECK(toPeerId(expectFrame(bob.sock, MsgType::USER_ONLINE)) == dave.pid);
}
