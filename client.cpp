#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>
#include <thread>

#include <mutex>

#define BUFFER_SIZE 8192

using namespace std;

mutex terminalMutex;

void receiveMsg(int clientSocket)
{
    while (true)
    {
	char buffer[BUFFER_SIZE] = {};
	ssize_t dataSize = recv(clientSocket, buffer, sizeof(buffer), 0);
	if (dataSize < 0)
	{
	    perror("Failed to receive message from server");
	    break;
	}

	if (dataSize == 0)
	{
	    cout << "Server Disconnected!" << endl;
	    break;
	}
	
	string raw(buffer, static_cast<size_t>(dataSize));

	size_t colonIdx = raw.find(" : ");
	string name = raw.substr(0, colonIdx);
	string message = raw.substr(colonIdx + 3);
	    
	{
	    lock_guard<mutex> lock(terminalMutex);
	    cout << "\r\033[K";
	    cout << "\033[94m" << name << "\033[0m : " << message << endl;
	    cout << "\033[92myou\033[0m : " << flush;
	}
    }

    close(clientSocket);
}

void sendMsg(int clientSocket)
{
    cout << "Enter your chat name" << endl;
    string name;
    getline(cin, name);

    string message;

    while (true)
    {
	// send data to server
	{
	    lock_guard<mutex> lock(terminalMutex);
	    cout << "\r\033[K";
	    cout << "\033[92myou\033[0m : " << flush;
	}

	getline(cin, message);

	message = name + " : " + message;

    	if (::send(clientSocket, message.c_str(), message.size(), 0) < 0)
    	{
    	    perror("Failed to send message");
    	}
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



