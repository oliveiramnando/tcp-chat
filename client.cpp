#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>

#include <termios.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>
#include <thread>

#include <mutex>

#define BUFFER_SIZE 8192

using namespace std;

string inputBuffer;
mutex terminalMutex;
termios origTerminal{};


void restoreTerminal()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &origTerminal);
}


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

void sendMsg(int clientSocket, const string& name)
{

	cout << "You : " << flush;
    char ch;

    while (read(STDIN_FILENO, &ch, 1) == 1)
    {
        {
            lock_guard<mutex> lock(terminalMutex);

            // enter or ctrl+c
            if (ch == '\n' || ch == '\r' || ch == 3)
            {
                if (inputBuffer == "q()" || ch == 3)
                {
                    cout << "\r\033[2K";
                    cout << "Exiting program..." << endl;
                    shutdown(clientSocket, SHUT_RDWR);
                    break;
                }

                if (inputBuffer.empty())
                {
                    cout << "\033[92myou\033[0m > " << flush;
                    continue;
                }

                cout << "\r\033[2K";

                string message = name + " : " + inputBuffer;

                if (::send(clientSocket, message.c_str(), message.size(), 0) < 0)
    	        {
    	            perror("Failed to send message");
    	        }

                cout << "\033[92myou\033[0m : " << inputBuffer << flush;
                inputBuffer.clear();

                cout << "\n\033[92myou\033[0m > " << flush; 
                continue;
            }

            // backspace
            if (ch == 127 || ch == '\b')
            {
                if (!inputBuffer.empty())
                { 
                    inputBuffer.pop_back();
                }
            }
            // char
            else if (ch >= 32 && ch <= 126)
            {
                inputBuffer += ch;
            }

	        cout << "\r\033[K";
	        cout << "\033[92myou\033[0m > " << inputBuffer << flush;

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

    cout << "Enter your chat name: ";
    string name;
    getline(cin, name);

    // set terminal to raw mode
    tcgetattr(STDIN_FILENO, &origTerminal);
    termios modTerminal = origTerminal;
    modTerminal.c_lflag &= ~(ICANON | ECHO | ISIG);
    modTerminal.c_cc[VMIN] = 1;

    tcsetattr(STDIN_FILENO, TCSANOW, &modTerminal);

    thread threadSend(sendMsg,clientSocket, name);
    thread threadReceive(receiveMsg, clientSocket);
    
    threadSend.join();
    threadReceive.join();
    
    // restore terminal settings
    restoreTerminal();
    cout << "\nTerminal settings restored" << endl;

    // close client socket
    if (close(clientSocket) < 0)
    {
		perror("Failed to close client socket");
		return 1;
    }

    cout << "Client socket closed" << endl;
    
    return 0;
}



