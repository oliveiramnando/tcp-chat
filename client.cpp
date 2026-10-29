#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


int main()
{
    // create client socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    // define server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // connect to server
    connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    // send data to server
    const char* message = "Hello, server. from client";
    send(clientSocket, message, strlen(message), 0);

    // close client socket
    close(clientSocket);
    
    return 0;
}



