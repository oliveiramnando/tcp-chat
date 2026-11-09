#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>

#include <vector>
#include <thread>
#include <mutex>
#include <csignal>

#include <sodium.h>

#include <map>

#include "net.hpp"
#include "crypto.hpp"

#define MAX_CLIENTS 2

using namespace std;
using ClientMap = map<PeerId, int>;     // logged in userid -> socket; instead of clients vector

mutex clientsMutex;
mutex coutMutex;

void clientHandler(int clientSocket, ClientMap& clients)
{
    // send challenge 
    array<unsigned char, CHALLENGE_BYTES> challenge{};
    randombytes_buf(challenge.data(), challenge.size());

    vector<unsigned char> challengePayload(challenge.begin(), challenge.end());
    if (!sendFrame(clientSocket, MsgType::CHALLENGE, challengePayload))
    {
        lock_guard<mutex> lock(coutMutex);
        cerr << "Failed to send challenge to client" << endl;
        close(clientSocket);
        return;
    }

    // receive auth and verify against login_label + challenge
    constexpr size_t AUTH_BODY_BYTES = crypto_sign_PUBLICKEYBYTES + crypto_sign_BYTES;  // 32+ 64 bytes

    MsgType type;
    string authBody;
    if(!receiveFrame(clientSocket, type, authBody) || type != MsgType::AUTH || authBody.size() != AUTH_BODY_BYTES)
    {
        {
            lock_guard<mutex> lock(coutMutex);
            cout << "\r\033[K";
            cerr << "Client disconnected or Error receiving frame." << endl;
        }
        // reject client 
        return;
    }

    // split into pk and sig
    PeerId clientId{};
    unsigned char signature[crypto_sign_BYTES];
    memcpy(clientId.data(), authBody.data(), clientId.size());
    memcpy(signature, authBody.data() + clientId.size(), crypto_sign_BYTES);

    // rebuild what client should've sent
    vector<unsigned char> message = buildLoginMessage(challenge);

    // verify
    if (crypto_sign_verify_detached(signature, message.data(), message.size(), clientId.data()) != 0)
    {
        // reject 
        return;
    }

    {
        lock_guard<mutex> lock(clientsMutex);

        if (clients.count(clientId) > 0)
        {
            // reject
            return;
        }
        if (clients.size() >= MAX_CLIENTS)
        {
            // reject
            return;
        }

        if (!sendFrame(clientSocket, MsgType::AUTH_OK, {}))
        {
            close(clientSocket);
            return;
        }

        clients[clientId] = clientSocket;

        if (clients.size() == MAX_CLIENTS)
        {
            for (const auto& [otherId, otherSocket] : clients)
            {
                if (otherId == clientId) { continue; }
                sendFrame(otherSocket,  MsgType::PEER_JOINED, {static_cast<unsigned char>(Role::INITIATOR)});
                sendFrame(clientSocket, MsgType::PEER_JOINED, {static_cast<unsigned char>(Role::RESPONDER)});
            }
        }
    }

    {
		lock_guard<mutex> lock(coutMutex);
		cout << "Logged in: " << idFingerPrint(clientId.data()) << endl;
	}

    while (true)
    {
		MsgType type;
        string encrypted{};
        if(!receiveFrame(clientSocket, type, encrypted))
        {
            {
                lock_guard<mutex> lock(coutMutex);
                cout << "\r\033[K";
                cerr << "Client disconnected or Error receiving frame." << endl;
            }
            break;
        }

        if ((type != MsgType::MESSAGE) && (type !=  MsgType::HANDSHAKE))
        {
            break;
        }

        //  convert encryptedMessage into uvector<Char>
        const vector<unsigned char> encryptedMessage(encrypted.begin(), encrypted.end()); 

		// print bytes relayted
		cout << "[Relayed " << encryptedMessage.size() << " bytes]" << endl; 

		{
			lock_guard<mutex> lock(clientsMutex);
			for (int cli: clients)
			{
				if (cli == clientSocket)
				{
					continue;
				}

			
				if (!sendFrame(cli, type, encryptedMessage))
				{
					cerr << "Failed to send message to client" << endl;
				}
			}
		}
    }

	{
		lock_guard<mutex> lock(clientsMutex);
		clients.erase(clientId);

        // send peer left to remaining clients
        MsgType peerLeft = MsgType::PEER_LEFT;
        string peerLeftMsg = "PEER LEFT.";
        vector<unsigned char> plMsg(peerLeftMsg.begin(), peerLeftMsg.end());
        for (int cli: clients)
	    {
	    	if (!sendFrame(cli, peerLeft, plMsg))
	    	{
	    		cerr << "Failed to send message to client" << endl;
	    	}
	    }
	}

	close(clientSocket);
	cout << "Client disconnected" << endl;
}

int main(int argc, char* argv[])
{
	signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE signal to prevent abrupt termination

    if (sodium_init() < 0)
    {
        cerr << "Failed to initialize libsodium" << endl;
        return 1;
    }
    // create server socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0)
    {
		perror("Failed to create server socket");
		return 1;
    }

    cout << "Server socket created!" << endl;

    int portNum = 8080;                 // default : 8080
    if (argc > 1)
    {
        try
        {
            portNum = stoi(argv[1]);
        }
        catch (const exception& e)
        {
            cerr << "Invalid port: " << argv[1] << e.what() << endl;
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
    serverAddr.sin_addr.s_addr = INADDR_ANY; 	// works only on server, where it means "listen on all my address"
	int yes = 1;
	if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
	{
		perror("Failed to set socket option");
		return 1;
	}
    
    // bind socket to address
    if (::bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
		perror("Failed to bind socket to server address");
		return 1;
    }

    cout << "Socket bound to address" << endl;

    // listen for incoming connections
    if (listen(serverSocket, 2) < 0)
    {
		perror("Failed to listen for incoming connections");
		return 1;
    }

    // vector<int> clients;
    ClientMap clients;

    while (true)
    {	
		int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket < 0)
        {
            perror("Accept");
            continue;
        }

		thread cli(clientHandler, clientSocket, ref(clients));
		cli.detach();
    }

    // close socket
    close(serverSocket);

    cout << "Server socket closed" << endl;

    return 0;
}

