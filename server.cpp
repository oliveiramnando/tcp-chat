#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <vector>
#include <thread>

#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024

using namespace std;

void clientHandler(int clientSocket, vector<int>& clients)
{
    cout << "Accepted client connection" << endl;
    
    while (true)
    {
	char buffer[BUFFER_SIZE] = {};

	ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
	if (bytesReceived < 0)
	{
	    perror("Failed to recive data from client socket");
	}

	if (bytesReceived == 0)
	{
	    cout << "Client disconnected!" << endl;
	    break;
	}

	cout.write(buffer, bytesReceived);	// writes specific number of bytes to standard output
	cout << endl;

	for (int cli: clients)
	{
	    if (cli == clientSocket)
	    {
		continue;
	    }
	    
	    ssize_t bytesSent = send(cli, buffer, static_cast<size_t>(bytesReceived), 0); 
	    if (bytesSent < 0)
	    {
		perror("Could not send message to client");
	    }
	}
    }
    close(clientSocket);
}

int main()
{
    // create server socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket < 0)
    {
	perror("Failed to create server socket");
	return 1;
    }

    cout << "Server socket created!" << endl;

    // define server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY; 	// works only on server, where it means "listen on all my address"
    
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
	    return 1;
	}
	
	// add client to vector, let thread handle that client
	clients.push_back(clientSocket);
	thread cli(clientHandler, clientSocket, ref(clients));

	cli.detach();
    }

    // close socket
    close(serverSocket);

    cout << "Server socket closed" << endl;

    return 0;
}





