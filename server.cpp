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

#include "net.hpp"

#define MAX_CLIENTS 2

using namespace std;

mutex clientsMutex;
mutex coutMutex;

void clientHandler(int clientSocket, vector<int>& clients)
{
	{
		lock_guard<mutex> lock(coutMutex);
		cout << "Accepted client connection" << endl;
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
		erase(clients, clientSocket);
	}
	// send peer left to remaing clients here
    MsgType peerLeft = PEER_LEFT;
    string peerLeftMsg = "PEER LEFT.";
    vector<unsigned char> plMsg(peerLeftMsg.begin(), peerLeftMsg.end());
    for (int cli: clients)
	{
		if (!sendFrame(cli, peerLeft, plMsg))
		{
			cerr << "Failed to send message to client" << endl;
		}
	}

	close(clientSocket);
	cout << "Client disconnected" << endl;
}

int main()
{
	signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE signal to prevent abrupt termination
    // create server socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0)
    {
		perror("Failed to create server socket");
		return 1;
    }

    cout << "Server socket created!" << endl;

    // define server address
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
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

    vector<int> clients;

    while (true)
    {
		// accept client connection
		int clientSocket = accept(serverSocket, nullptr, nullptr);
		if (clientSocket < 0)
		{
			perror("Failed to accept client socket");
			continue;
		}
		
		// add client to vector, let thread handle that client
        if (clients.size() >= MAX_CLIENTS)
        {
            lock_guard<mutex> lock(coutMutex);

            MsgType errorType = ERROR_MSG;
            string errorMessage = "MAX CLIENTS HAVE BEEN REACHED.";

            cerr << errorMessage << endl;

            vector<unsigned char> errorMsg(errorMessage.begin(), errorMessage.end()); 
            if (!sendFrame(clientSocket, errorType, errorMsg))
            {
                cerr << "Could not send frame to client" << endl;
                break;
            }
            continue;
        }

        {
			lock_guard<mutex> lock(clientsMutex);
			clients.push_back(clientSocket);
		}

        // if size of clients vector is > 0, notify all clients that peer joined
        if (clients.size() >= 0)
        {
            lock_guard<mutex> lock(coutMutex);

            MsgType peerJoined = PEER_JOINED;
            string peerJoinedMsg = "PEER JOINED.";

            cerr << peerJoinedMsg << endl;

            vector<unsigned char> pjMsg(peerJoinedMsg.begin(), peerJoinedMsg.end()); 
            for (int cli: clients)
	        {
	        	if (!sendFrame(cli, peerJoined, pjMsg))
	        	{
	        		cerr << "Failed to send message to client" << endl;
	        	}
	        }
        }

		thread cli(clientHandler, clientSocket, ref(clients));
		cli.detach();
    }

    // close socket
    close(serverSocket);

    cout << "Server socket closed" << endl;

    return 0;
}

