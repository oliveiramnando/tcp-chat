#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>

#include <termios.h>
#include <csignal>
#include <filesystem>

#include <sodium.h>

#include "net.hpp"
#include "crypto.hpp"

using namespace std;

string inputBuffer;
mutex terminalMutex;
termios origTerminal{};
atomic<bool> quitting = false;


void restoreTerminal()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &origTerminal);
}


void receiveMsg(int clientSocket, const unsigned char* key)
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
                break;
            case MsgType::PEER_JOINED:
                 // close socket and clean up 
                {
                    lock_guard<mutex> lock(terminalMutex);
                    cout << "\n\r\033[K";
                    cout << body << endl;
                    cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::PEER_LEFT:
                // close socket and clean up 
                {
                    lock_guard<mutex> lock(terminalMutex);
                    cout << "\n\r\033[K";
                    cerr << body << endl;
                    cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
                }
                continue;
            case MsgType::ERROR_MSG:
                {
                    lock_guard<mutex> lock(terminalMutex);
                    cout << "\n\r\033[K";
                    cerr << body << endl;
                }
                //exit program and clean up here
                exit(1);
        }

        // decrypt message here
        vector<unsigned char> decryptedMessage{};
        if(!decrypt(decryptedMessage, body, key))
        {
            {
                lock_guard<mutex> lock(terminalMutex);
                cout << "\r\033[K";
                cerr << "Failed to decrypt message." << endl;
                cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
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
                cout << "\n\033[92myou\033[0m > " << inputBuffer << flush;
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

void sendMsg(int clientSocket, const string& name, const unsigned char* key)
{

	cout << "\033[92myou\033[0m > " << flush;
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

                if (inputBuffer.empty())
                {
                    cout << "\033[92myou\033[0m > " << flush;
                    continue;
                }

                cout << "\r\033[2K";

                string message = name + " : " + inputBuffer; 

                vector<unsigned char> encrypted{};
                if (!encrypt(message, encrypted, key))
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


int main()
{
    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE signal to prevent abrupt termination

    if (sodium_init() < 0)
    {
        cerr << "Failed to initialize libsodium" << endl;
        return 1;
    }

    // generate and load key
    string keyFile = "chat.key";
    if (!filesystem::exists(keyFile))
    {
        bool generatedKey = keyGen();
        if (!generatedKey)
        {
            cerr << "Failed to generate key" << endl;
            exit(1);
        }
    }

    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    if (!loadKey(keyFile, key))
    {
        cerr << "Failed to load key from file: " << keyFile << endl;
        return 1;
    }

    // create client socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0)
    {
		perror("Failed to create client socket");
		return 1;
    }

    cout << "Client socket created" << endl;

    // define server address
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0)
    {
        perror("Invalid server address");
        return 1;
    }

    // connect to server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
		perror("Failed to connect to server");
		return 1;
    }

    cout << "Connected to server" << endl;

    cout << "Enter your chat name: ";
    string name;
    getline(cin, name);

    cout << endl;

    // set terminal to raw mode
    tcgetattr(STDIN_FILENO, &origTerminal);
    atexit(restoreTerminal);
    termios modTerminal = origTerminal;
    modTerminal.c_lflag &= ~(ICANON | ECHO | ISIG);
    modTerminal.c_cc[VMIN] = 1;

    tcsetattr(STDIN_FILENO, TCSANOW, &modTerminal);

    // threads
    thread threadSend(sendMsg, clientSocket, name, key);
    thread threadReceive(receiveMsg, clientSocket, key);

    threadSend.join();
    threadReceive.join();
    
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



