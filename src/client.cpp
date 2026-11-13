#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cstdint>
#include <set>
#include <map>
#include <optional>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>

#include <termios.h>
#include <csignal>

#include <sodium.h>

#include <filesystem>
#include <cctype>

#include "net.hpp"
#include "crypto.hpp"
#include "util.hpp"

using namespace std;

string inputBuffer;
mutex terminalMutex;
mutex userIdsMutex;
termios origTerminal{};
atomic<bool> quitting = false;

set<PeerId> userIds;

void restoreTerminal()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &origTerminal);
}

bool prepareProfileDir(const string& profile, string& dir)
{
    dir = "profiles/" + profile;

    error_code ec;
    filesystem::create_directories(dir, ec);
    if (ec)
    {
        return false;
    }

    filesystem::permissions(dir, filesystem::perms::owner_all, filesystem::perm_options::replace, ec);
    return !ec;
}

bool amInitiator(const Identity& me, const PeerId& peer)
{
    return memcmp(me.pk, peer.data(), 32) < 0;
}

bool startHandshake(int sock, const Identity& me, const PeerId& peer, Session& s)
{
    wipeSession(s);
    s.isInitiator = amInitiator(me, peer);
    makeKeyPair(s);

    unsigned char sig[crypto_sign_BYTES];
    if (!signHandshake(me, peer.data(), s.pk, sig))
    {
        return false;
    }
    vector<unsigned char> handshakePayload(s.pk, s.pk + crypto_kx_PUBLICKEYBYTES);  // [eph pk]
    handshakePayload.insert(handshakePayload.end(), sig, sig + crypto_sign_BYTES);  // [eph pk][sig]

    vector<unsigned char> body = buildBody(peer, handshakePayload);

    if(!sendFrame(sock, MsgType::HANDSHAKE, body))
    {
        return false;
    }

    return true;
}


void receiveMsg(int clientSocket, map<PeerId, Session>& sessions, const Identity& id)
{
    while (true)
    {
        MsgType type;
        string body{};
        if(!receiveFrame(clientSocket, type, body))
        {
            if (quitting) { break; }
            {
                lock_guard<mutex> lock(terminalMutex);
                cout << "\r\033[K";
                cerr << "Error receiving frame." << endl;
            }
            exit(0);
        }

        // branch by type here
        switch(type)
        {
            case MsgType::MESSAGE:
                {
                    lock_guard<mutex> lock(terminalMutex);
                    cout << "\r\033[K";

                    PeerId sender{};
                    vector<unsigned char> payload;
                    if (!parseBody(body, sender, payload))
                    {
                        cerr << "Malformed MESSAGE received." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }

                    // find taht sender's session
                    auto it = sessions.find(sender);
                    if (it == sessions.end() || !it->second.ready)
                    {
                        cerr << "Message from unknown or unsecured peer rejected." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }

                    // decrypt message here with sender's rx jey
                    vector<unsigned char> decryptedMessage{};
                    if(!decrypt(decryptedMessage, string(payload.begin(), payload.end()), it->second.rx))
                    {
                        cerr << "Failed to decrypt message." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }
                    string nameAndMessage(decryptedMessage.begin(), decryptedMessage.end());

                    size_t colonIdx = nameAndMessage.find(" : ");
                    if (colonIdx == string::npos)
                    {
                        cout << "\r\033[K";
                        cerr << "Client sender does not have a name" << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        
                        continue;
                    }
                    string name = nameAndMessage.substr(0, colonIdx);
                    string message = nameAndMessage.substr(colonIdx + 3);
                        
                    cout << "\033[94m" << name << "\033[0m : " << message << endl;
                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::HANDSHAKE:
                {
                    lock_guard<mutex> lock(terminalMutex);

                    cout << "\r\033[K";
        
                    // parse body here to extract sender
                    PeerId sender{};
                    vector<unsigned char> payload;
                    if (!parseBody(body, sender, payload))
                    {
                        cerr << "Malformed HANDSHAKE received." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }

                    // check payload size
                    if (payload.size() != HANDSHAKE_PAYLOAD_SIZE)
                    {
                        cerr << "Malformed payload received." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }

                    // split
                    const unsigned char* ephPk = payload.data();
                    const unsigned char* sig = payload.data() + crypto_kx_PUBLICKEYBYTES;

                    // verify
                    if (!verifyHandshake(sender.data(), id.pk, ephPk, sig))
                    {
                        cerr << "Handshake signature invalid. Rejected." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }

                    // look up sender. if no sesiion or no key pair, the peer started handshake, create session  with sender. then call start handshake
                    auto it = sessions.find(sender);
                    bool needReply = (it == sessions.end() || !it->second.hasKeyPair || it->second.ready);

                    Session& s = sessions[sender];

                    if (needReply)
                    {
                        if (!startHandshake(clientSocket, id, sender, s))
                        {
                            cerr << "Failed to send HANDSHAKE reply." << endl;
                            sessions.erase(sender);
                            cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                            continue;
                        }
                    }

                    if (!deriveSessionKeys(s, string(ephPk, ephPk + crypto_kx_PUBLICKEYBYTES)))
                    {
                        cout << "\r\033[K";
                        cerr << "Failed to derive session keys." << endl;
                        cerr << "HANDSHAKE rejected." << endl;
                        wipeSession(s);
                        sessions.erase(sender);
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }
                    s.ready = true;

                    cout << "Verified secure connection with " << idFingerPrint(sender.data()) << endl;

                    string fingerPrint = sessionFingerPrint(s);
                    cout << "Session fingerprint: " << fingerPrint << endl;

                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::ERROR_MSG:
                {
                    lock_guard<mutex> lock(terminalMutex);
                    cout << "\r\033[K";
                    cerr << body << endl;
                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::USER_LIST:
                {
                    lock_guard<mutex> lockTerm(terminalMutex);
                    lock_guard<mutex> lockUid(userIdsMutex);
                    if (!parseUserList(body, userIds))
                    {
                        cerr << "Failed to parse list of online users." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }
                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::USER_ONLINE:
                // convert body into peer id and push back to vector
                {
                    lock_guard<mutex> lock(userIdsMutex);
                    PeerId id{};
                    if (body.size() != PeerId{}.size())
                    {
                        // print error here
                        continue;
                    }

                    memcpy(id.data(), body.data(), PeerId{}.size());
                    userIds.insert(id);
                }
                continue;
            case MsgType::USER_OFFLINE:
                {
                    lock_guard<mutex> lockTerm(terminalMutex);
                    lock_guard<mutex> lockUid(userIdsMutex);
                    PeerId id{};
                    if (body.size() != PeerId{}.size())
                    {
                        // cerr
                        continue;
                    }
                    memcpy(id.data(), body.data(), PeerId{}.size());
                    userIds.erase(id);
                    
                    auto it = sessions.find(id);
                    if (it != sessions.end())
                    {
                        wipeSession(it->second);
                        sessions.erase(it);
                    }
                }
                continue;
            default:
                {
                    lock_guard<mutex> lock(terminalMutex);
                    cout << "\r\033[K";
                    cerr << "INVALID MESSAGE TYPE" << endl;
                }
                exit(1);
        }
    }
}

void sendMsg(int clientSocket, const string& name, map<PeerId, Session>& sessions, const Identity& id)
{
    {
        lock_guard<mutex> lock(terminalMutex);
        cout << "\r\033[K";
        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
    }

    char ch;

    const string chatCMD = "/chat ";

    vector<PeerId> userIdVector;
    optional<PeerId> currentPeer;

    while (read(STDIN_FILENO, &ch, 1) == 1)
    {
        if (quitting) { break; }
        {
            lock_guard<mutex> lock(terminalMutex);

            // enter or ctrl+c
            if (ch == '\n' || ch == '\r' || ch == 3)
            {
                if (inputBuffer == "q()" || ch == 3)
                {
                    cout << "\r\033[2K";
                    cout << "Exiting program..." << endl;
                    quitting = true;
                    shutdown(clientSocket, SHUT_RDWR);
                    break;
                }

                // display list of user ids when prompted
                if (inputBuffer == "/users")
                {
                    userIdVector.clear();
                    lock_guard<mutex> lock(userIdsMutex);
                    cout << "\r\033[2K";
                    // all print user ids to terminal here
                    int n = 1;
                    for (const auto& uid : userIds)
                    { 
                        userIdVector.push_back(uid);
                        cout << n << ") " << idFingerPrint(uid.data()) << endl;
                        n++;
                    }
                    inputBuffer.clear();
                    cout << "\033[92myou\033[0m > " << flush;
                    continue;
                }
                
                // when certain command with user id is prompted set the recipient id that
                if (inputBuffer.rfind(chatCMD, 0) == 0)
                {
                    string arg = inputBuffer.substr(chatCMD.size());

                    size_t n = 0;
                    try
                    {
                        n = stoul(arg);
                    }
                    catch(const exception&)
                    {
                        n = 0;
                    }
                    

                    if (n < 1 || n > userIdVector.size())
                    {
                        cout << "\r\033[K" << "No such user. Type /users to see the list." << endl;
                    }
                    else
                    {
                        PeerId selected = userIdVector[n - 1];
                        bool online;
                        {
                            lock_guard<mutex> lock(userIdsMutex);
                            online = userIds.contains(selected);
                        }
                        if (!online)
                        {
                            cout << "\r\033[K" << "That user is no longer online." << endl;
                        }
                        else
                        {
                            cout << "\r\033[K" << "Now chatting with " << idFingerPrint(selected.data()) << endl;
                            currentPeer = selected;

                            cout << "Establishing secure connection..." << endl;

                            auto it = sessions.find(selected);
                            if (it == sessions.end() || !it->second.hasKeyPair)
                            {
                                if (!startHandshake(clientSocket, id, selected, sessions[selected]))
                                {
                                    sessions.erase(selected);
                                }
                            }
                        }
                        
                    }

                    inputBuffer.clear();
                    cout << "\033[92myou\033[0m > " << flush;
                    continue;                    
                }


                if (inputBuffer.empty())
                {
                    continue; 
                }

                if (!currentPeer)
                {
                    cout << "\r\033[K";
                    cerr << "No chat selected. Type /users, then /chat <n>." << endl;
                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }

                auto it = sessions.find(*currentPeer);
                if (it == sessions.end() || !it->second.ready)
                {
                    cout << "\r\033[K";
                    cerr << "Connection not secured yet. Wait for the fingerprint." << endl;
                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }

                cout << "\r\033[2K";

                string message = name + " : " + inputBuffer; 

                vector<unsigned char> encrypted{};
                if (!encrypt(message, encrypted, it->second.tx))
                {
                    cerr << "Failed to encrypt" << endl;
                    cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }
                
                vector<unsigned char> frameBody = buildBody(*currentPeer, encrypted);
                if (frameBody.size() + 1 > MAX_MESSAGE_SIZE)
                {
                    cerr << "Invalid Message Length" << endl;
                    cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }              

                MsgType msg = MsgType::MESSAGE;
                if (!sendFrame(clientSocket, msg, frameBody))
                {
                    cerr << "Failed to send frame" << endl;
                    cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }

                // print sent message
                cout << "\r\033[K";
                cout << "\033[92myou\033[0m : " << inputBuffer << flush;
                inputBuffer.clear();

                // redraw prompt
                cout << "\n\033[92myou\033[0m > " << flush; 
                continue;
            }

            // backspace
            if (ch == 127 || ch == '\b')
            {
                if (!inputBuffer.empty())
                { 
                    inputBuffer.pop_back();
                }
            }
            // char
            else if (ch >= 32 && ch <= 126)
            {
                inputBuffer += ch;
            }

	        cout << "\r\033[K";
	        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
        }
    }
}


int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE signal to prevent abrupt termination

    if (sodium_init() < 0)
    {
        cerr << "Failed to initialize libsodium." << endl;
        return 1;
    }

    string profile = "default";
    if (argc > 3)
    {
        profile = argv[3];
    }

    if (!isValidName(profile))
    {
        cerr << "Invalid profile name. Use 1-32 letters, digits, '-' or '_'." << endl;
        return 1;
    }

    string profileDir;
    if (!prepareProfileDir(profile, profileDir))
    {
        cerr << "Failed to create profile folder: profiles/" << profile << endl;
        return 1;
    }

    cout << "Profile: " << profile << endl;

    Identity id;
    if (!loadOrCreateIdentity(profileDir + "/identity.key", id))
    {
        cerr << "Failed to load identity." << endl;
        return 1;
    }

    cout << "Your fingerprint: " << idFingerPrint(id.pk) << endl;

    // create client socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0)
    {
		perror("Failed to create client socket");
		return 1;
    }

    cout << "Client socket created" << endl;

    const char* serverIp = "127.0.0.1"; // default : this computer
    int portNum = 8080;                 // default : 8080
    if (argc > 1)
    {
        serverIp = argv[1];
    }

    if (argc > 2)
    {
        try
        {
            portNum = stoi(argv[2]);
        }
        catch (const exception& e)
        {
            cerr << "Invalid port: " << argv[2] << e.what() << endl;
            return 1;
        }
        if (portNum < 1 || portNum > 65535)
        {
            cerr << "Port must be between 1 and 65535" << endl;
            return 1;
        }
    }

    // define server address
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(portNum);

    if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) <= 0)
    {
        cerr << "Invalid server address:" << serverIp << endl;
        return 1;
    }

    // connect to server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
		cerr << "Failed to connect to server" << endl;
		return 1;
    }

    cout << "Connected to server" << endl;

    MsgType type;
    string body;

    if (!receiveFrame(clientSocket, type, body) || type != MsgType::CHALLENGE || body.size() != CHALLENGE_BYTES)
    {
        cerr << "Login failed: unexpected reply from server." << endl;
        return 1;
    }

    array<unsigned char, CHALLENGE_BYTES> challenge{};
    memcpy(challenge.data(), body.data(), challenge.size());
    
    // sign label and challenge
    unsigned char signature[crypto_sign_BYTES];
    if (!signLogin(id, challenge, signature))
    {
        cerr << "Login failed: Could not sign challenge." << endl;
        return 1;
    }

    vector<unsigned char> auth(id.pk, id.pk + crypto_sign_PUBLICKEYBYTES);
    auth.insert(auth.end(), signature, signature + crypto_sign_BYTES);

    if (!sendFrame(clientSocket, MsgType::AUTH, auth))
    {
        cerr << "Login failed: could not send AUTH." << endl;
        return 1;
    }

    // wait for AUTH_OK (or the server's error message)
    if (!receiveFrame(clientSocket, type, body))
    {
        cerr << "Login failed: server closed the connection." << endl;
        return 1;
    }
    if (type == MsgType::ERROR_MSG)
    {
        cerr << "Login failed: " << body << endl;
        return 1;
    }
    if (type != MsgType::AUTH_OK)
    {
        cerr << "Login failed: unexpected reply from server." << endl;
        return 1;
    }

    cout << "Logged in." << endl;

    cout << "Enter your chat name: ";
    string name;
    getline(cin, name);

    cout << endl;

    cout << "Type /users to see who's online..." << endl;

    // set terminal to raw mode
    tcgetattr(STDIN_FILENO, &origTerminal);
    atexit(restoreTerminal);
    termios modTerminal = origTerminal;
    modTerminal.c_lflag &= ~(ICANON | ECHO | ISIG);
    modTerminal.c_cc[VMIN] = 1;

    tcsetattr(STDIN_FILENO, TCSANOW, &modTerminal);

    map<PeerId, Session> sessions;

    // threads
    thread threadReceive(receiveMsg, clientSocket, ref(sessions), cref(id));
    thread threadSend(sendMsg, clientSocket, name, ref(sessions), cref(id));

    threadReceive.join();
    threadSend.join();

    sodium_memzero(id.sk, sizeof(id.sk));

    // restore terminal settings
    restoreTerminal();
    cout << "\nTerminal settings restored" << endl;

    // close client socket
    if (close(clientSocket) < 0)
    {
		perror("Failed to close client socket");
		return 1;
    }

    cout << "Client socket closed" << endl;
    
    return 0;
}



