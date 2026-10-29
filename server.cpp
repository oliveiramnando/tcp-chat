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
    ::bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    cout << "Socket bound to address" << endl;

    // listen for incoming connections
    listen(serverSocket, 5);

    int clientSocket = accept(serverSocket, nullptr, nullptr);

    char buffer[BUFFER_SIZE] = {0};
    recv(clientSocket, buffer, sizeof(buffer), 0);

    cout << "message from client " << buffer << endl;

    close(serverSocket);

    return 0;
}





