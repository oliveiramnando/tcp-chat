#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cstdint>
#include <set>

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

#include "net.hpp"
#include "crypto.hpp"

using namespace std;

string inputBuffer;
mutex terminalMutex;
mutex userIdsMutex;
termios origTerminal{};
atomic<bool> quitting = false;
atomic<bool> peerConnected = false;

set<PeerId> userIds;

void restoreTerminal()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &origTerminal);
}


void receiveMsg(int clientSocket, Session& session)
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
                break;
            case MsgType::HANDSHAKE:
                {
                    lock_guard<mutex> lock(terminalMutex);

                    cout << "\r\033[K";
                    if (session.ready)
                    {
                        cerr << "Session is already ready." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }

                    cout << "HANDSHAKE RECEIVED" << endl;

                    if (!deriveSessionKeys(session, body))
                    {
                        cout << "\r\033[K";
                        cerr << "Failed to derive session keys." << endl;
                        cerr << "HANDSHAKE rejected." << endl;
                        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                        continue;
                    }

                    cout << "Secure connection established." << endl;

                    string fingerPrint = sessionFingerPrint(session);
                    cout << "Session fingerprint: " << fingerPrint << endl;

                    session.ready = true;

                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::PEER_JOINED:
                {
                    lock_guard<mutex> lock(terminalMutex);

                    if (body.size() != sizeof(uint8_t))
                    {
                        cout << "\r\033[K";
                        cerr << "Invalid payload on PEER_JOIN from server." << endl;
                        exit(1);
                    }

                    uint8_t roleByte = static_cast<uint8_t>(body[0]);
                    if (roleByte != static_cast<uint8_t>(Role::INITIATOR) && roleByte != static_cast<uint8_t>(Role::RESPONDER) ) 
                    {
                        cout << "\r\033[K";
                        cerr << "Invalid role on PEER_JOIN from server." << endl;
                        exit(1);
                    }

                    wipeSession(session);

                    session.isInitiator = (roleByte == static_cast<uint8_t>(Role::INITIATOR));
                    peerConnected = true;

                    cout << "\r\033[K";
                    cout << "PEER JOINED." << endl;

                    cout << "Establishing secure connection..." << endl;

                    // start the handshake
                    makeKeyPair(session);
                    
                    MsgType handshake = MsgType::HANDSHAKE;
                    vector<unsigned char> publicKey(session.pk, session.pk + crypto_kx_PUBLICKEYBYTES);
                    if(!sendFrame(clientSocket, handshake, publicKey))
                    {
                        cerr << "Failed to send frame." << endl;
                    }

                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::PEER_LEFT:
                {
                    lock_guard<mutex> lock(terminalMutex);
                    cout << "\r\033[K";
                    cerr << body << endl;
                    cout << "WAITING FOR PEER..." << endl;
                    peerConnected = false;
                    wipeSession(session);
                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::ERROR_MSG:
                {
                    lock_guard<mutex> lock(terminalMutex);
                    cout << "\r\033[K";
                    cerr << body << endl;
                }
                exit(1);
            case MsgType::USER_LIST:
                {
                    lock_guard<mutex> lock(userIdsMutex);
                    if (!parseUserList(body, userIds))
                    {
                        break;
                    }
                }
                continue;
            case MsgType::USER_ONLINE:
                // convert body into peer id and push back to vector
                {
                    lock_guard<mutex> lock(userIdsMutex);
                    PeerId id{};
                    memcpy(id.data(), body.data(), PeerId{}.size());
                    userIds.insert(id);
                }
                continue;
            case MsgType::USER_OFFLINE:
                {
                    lock_guard<mutex> lock(userIdsMutex);
                    PeerId id{};
                    memcpy(id.data(), body.data(), PeerId{}.size());
                    userIds.erase(id);
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

        if (!session.ready)
        {
            lock_guard<mutex> lock(terminalMutex);
            cout << "\r\033[2K";
            cerr << "Connection not secured." << endl;
            cout << "\033[92myou\033[0m > " << inputBuffer << flush;
            continue;
        }

        // decrypt message here
        vector<unsigned char> decryptedMessage{};
        if(!decrypt(decryptedMessage, body, session.rx))
        {
            {
                lock_guard<mutex> lock(terminalMutex);
                cout << "\r\033[K";
                cerr << "Failed to decrypt message." << endl;
                cout << "\033[92myou\033[0m > " << inputBuffer << flush;
            }
            continue;
        }
        string nameAndMessage(decryptedMessage.begin(), decryptedMessage.end());

		size_t colonIdx = nameAndMessage.find(" : ");
        if (colonIdx == string::npos)
        {
            {
                lock_guard<mutex> lock(terminalMutex);
                cout << "\r\033[K";
                cerr << "Client sender does not have a name" << endl;
                cout << "\033[92myou\033[0m > " << inputBuffer << flush;
            }
            continue;
        }
		string name = nameAndMessage.substr(0, colonIdx);
		string message = nameAndMessage.substr(colonIdx + 3);
			
		{
			lock_guard<mutex> lock(terminalMutex);
			cout << "\r\033[K";
			cout << "\033[94m" << name << "\033[0m : " << message << endl;
			cout << "\033[92myou\033[0m > " << inputBuffer << flush;
		}
    }
}

void sendMsg(int clientSocket, const string& name, Session& session)
{
    {
        lock_guard<mutex> lock(terminalMutex);
        cout << "\r\033[K";
        cout << "\033[92myou\033[0m > " << inputBuffer << flush;
    }

    char ch;

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

                if (!peerConnected)
                {
                    cout << "\r\033[K";
                    cout << "No one is here..." << endl;
                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }

                if (!session.ready)
                {
                    cout << "\r\033[2K";
                    cerr << "Connection not secured." << endl;
                    cout << "\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }

                if (inputBuffer.empty())
                {
                    continue; 
                }

                cout << "\r\033[2K";

                string message = name + " : " + inputBuffer; 

                vector<unsigned char> encrypted{};
                if (!encrypt(message, encrypted, session.tx))
                {
                    cerr << "Failed to encrypt" << endl;
                    cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }

                if (encrypted.size() > MAX_MESSAGE_SIZE)
                {
                    cerr << "Invalid Message Length" << endl;
                    cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
                    continue;
                }              

                MsgType msg = MsgType::MESSAGE;
                if (!sendFrame(clientSocket, msg, encrypted))
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

    Identity id;
    if (!loadOrCreateIdentity("identity.key", id))
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

    cout << "WAITING FOR PEER.." << endl;

    // set terminal to raw mode
    tcgetattr(STDIN_FILENO, &origTerminal);
    atexit(restoreTerminal);
    termios modTerminal = origTerminal;
    modTerminal.c_lflag &= ~(ICANON | ECHO | ISIG);
    modTerminal.c_cc[VMIN] = 1;

    tcsetattr(STDIN_FILENO, TCSANOW, &modTerminal);

    // create session
    Session session;

    // threads
    thread threadReceive(receiveMsg, clientSocket, ref(session));
    thread threadSend(sendMsg, clientSocket, name, ref(session));

    threadReceive.join();
    threadSend.join();

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



