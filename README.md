# TCP Chat

A lightweight, terminal-based chat application built in C++ using TCP sockets. It follows a client-server architecture and supports real-time messaging between multiple connected clients.

This project is a hands-on exploration of socket programming, multithreading, TCP message framing, and terminal input handling.

Features

* Multi-client chat: The server accepts multiple connections and broadcasts messages to other connected clients.
* Concurrent communication: Clients send and receive messages on separate threads.
* TCP message framing: Each message uses a 4-byte length prefix so it can be read correctly over a byte-stream connection.
* Reliable send/receive helpers: Shared networking functions handle partial socket reads and writes.
* Terminal interface: Custom input handling and ANSI colors keep incoming messages and the typing prompt readable.
* Connection handling: Clients can disconnect without stopping the server or other conversations.

Tech Stack

* Language: C++20
* Networking: POSIX TCP sockets
* Concurrency: std::thread and std::mutex
* Terminal handling: termios and ANSI escape sequences
* Build: GNU Make and g++

Getting Started

Prerequisites

* A Unix-like environment (Linux or macOS)
* A C++20-compatible compiler (g++)
* make

Installation

Clone the repository and compile the server and client:

git clone https://github.com/oliveiramnando/tcp-chat.git
cd tcp-chat
make

Running the Chat

Start the server in one terminal:

./server

In a second terminal, start a client:

./client

Enter a chat name when prompted, then start typing messages. Open additional terminals and run ./client to connect more participants.

Type q() and press Enter to exit a client.

By default, the client connects to 127.0.0.1:8080. The server listens on port 8080. To connect from another machine, update the server IP address in client.cpp and rebuild.

How It Works

1. The server opens a TCP socket and listens for incoming connections.
2. Each accepted connection is handled by its own thread.
3. Clients send messages in the format name : message, preceded by a 4-byte network-order length field.
4. The server reads a complete message and forwards it to the other connected clients.
5. Each client has a receiving thread that displays incoming messages while the sending thread handles terminal input.

The current message payload limit is 8,192 bytes, including the sender name and separator.

Project Structure

tcp-chat/
├── client.cpp         # Client connection, terminal UI, send/receive threads
├── server.cpp         # TCP server, client threads, message broadcasting
├── net.cpp            # Socket read/write helper implementations
├── net.hpp            # Networking declarations and message size limit
├── terminal_test.cpp  # Terminal input experiments
├── Makefile           # Build targets for client and server
├── LICENSE            # MIT license
└── README.md

Roadmap

* Add authenticated message encryption and a key-distribution mechanism.
* Support private conversations or separate chat rooms.
* Make server address and port configurable.
* Improve client lifecycle handling and terminal UX.

Security Note

The version currently published on main sends messages in plaintext. It is a learning project and should not be used for sensitive communication over untrusted networks. Encryption is being developed separately and is not yet part of the published source.

License

Licensed under the MIT License.