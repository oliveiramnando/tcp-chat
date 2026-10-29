#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>
#include <thread>

#define BUFFER_SIZE 1024

using namespace std;

void receiveMsg(int clientSocket)
{
    while (true)
    {
	char buffer[BUFFER_SIZE] = {};
	ssize_t dataSize = recv(clientSocket, buffer, sizeof(buffer), 0);
	if (dataSize < 0)
	{
	    perror("Failed to receive message from server");
	}
	
	string raw(buffer, static_cast<size_t>(dataSize));
	cout << raw << endl;
    }
}

void sendMsg(int clientSocket)
{
    while (true)
    {
	// send data to server
    	string message;
	cout << "Message : " << endl;
	getline(cin, message);

    	if (::send(clientSocket, message.c_str(), sizeof(message), 0) < 0)
    	{
    	    perror("Failed to send message");
    	}

    	cout << "Message sent to server" << endl;
    }
}

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

    thread threadSend(sendMsg,clientSocket);
    thread threadReceive(receiveMsg, clientSocket);
    
    threadSend.join();
    threadReceive.join();
    

    // close client socket
    if (close(clientSocket) < 0)
    {
	perror("Failed to close client socket");
	return 1;
    }

    cout << "Client socket closed" << endl;
    
    return 0;
}



