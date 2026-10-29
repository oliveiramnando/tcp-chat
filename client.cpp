#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

int main()
{
    // create client socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0)
    {
	perror("Failed to create client socket");
	return 1;
    }

    cout << "Client socket created" << endl;

    // define server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // connect to server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
	perror("Failed to connect to server");
	return 1;
    }

    cout << "Connected to server" << endl;

    // send data to server
    const char* message = "Hello, server. from client";
    if (send(clientSocket, message, strlen(message), 0) < 0)
    {
	perror("Failed to send message");
	return 1;
    }

    cout << "Message sent to server" << endl;

    // close client socket
    if (close(clientSocket) < 0)
    {
	perror("Failed to close client socket");
	return 1;
    }

    cout << "Client socket closed" << endl;
    
    return 0;
}



