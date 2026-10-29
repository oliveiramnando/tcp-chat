#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENT 5
#define BUFFER_SIZE 1024

using namespace std;

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
    if (listen(serverSocket, 5) < 0)
    {
	perror("Failed to listen for incoming connections");
	return 1;
    }

    int clientSocket = accept(serverSocket, nullptr, nullptr);
    if (clientSocket < 0)
    {
	perror("Failed to accept client socket");
	return 1;
    }

    cout << "Accepted client connection" << endl;

    char buffer[BUFFER_SIZE] = {0};
    if (recv(clientSocket, buffer, sizeof(buffer), 0) < 0)
    {
	perror("Failed to recive data from client socket");
	return 1;
    }

    cout << "message from client " << buffer << endl;

    close(serverSocket);

    cout << "Server socket closed" << endl;

    return 0;
}





