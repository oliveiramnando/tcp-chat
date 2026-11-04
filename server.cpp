#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

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
		char lengthBytes[4] = {};
		if (!readNBytes(clientSocket, lengthBytes, sizeof(uint32_t)))
		{
			lock_guard<mutex> lock(coutMutex);
			cout << "Failed to read message length!" << endl;
			break;
		}

		uint32_t messageLength;
        memcpy(&messageLength, lengthBytes, sizeof(messageLength));
        messageLength = ntohl(messageLength); 

        if (messageLength == 0 || messageLength > MAX_MESSAGE_SIZE)
        {
            cerr << "Invalid message length received from client" << endl;
            break;
        }
    
        string nameAndMessage(messageLength, '\0');
        if (!readNBytes(clientSocket, nameAndMessage.data(), messageLength))
        {
            cerr << "Failed to read message from client" << endl;
            break;
        }

		uint32_t messageLengthNetworkOrder = htonl(static_cast<uint32_t>(nameAndMessage.size()));
		vector<char> sendBuffer(sizeof(messageLengthNetworkOrder) + nameAndMessage.size());

		memcpy(sendBuffer.data(), &messageLengthNetworkOrder, sizeof(messageLengthNetworkOrder));
		memcpy(sendBuffer.data() + sizeof(messageLengthNetworkOrder), nameAndMessage.c_str(), nameAndMessage.size());

		// print bytes relayted
		cout << "[Relayed " << messageLength << " bytes]" << endl; 

		{
			lock_guard<mutex> lock(clientsMutex);
			for (int cli: clients)
			{
				if (cli == clientSocket)
				{
					continue;
				}

			
				if (!sendAll(cli, sendBuffer.data(), sendBuffer.size()))
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
    if (listen(serverSocket, MAX_CLIENTS) < 0)
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
		{
			lock_guard<mutex> lock(clientsMutex);
			clients.push_back(clientSocket);
		}
		thread cli(clientHandler, clientSocket, ref(clients));

		cli.detach();
    }

    // close socket
    close(serverSocket);

    cout << "Server socket closed" << endl;

    return 0;
}
