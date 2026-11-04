# TCP Chat

A lightweight, terminal-based chat application written in **C++20**. It uses TCP sockets and multithreading to let multiple clients exchange messages through a central server, with **XChaCha20-Poly1305 authenticated encryption** provided by libsodium.

The project explores low-level networking, concurrent programming, message framing, and client-side encryption.

## Features

- **Multi-client messaging:** A TCP server broadcasts each message to other connected clients.
- **Client-side encryption:** Messages are encrypted and authenticated with XChaCha20-Poly1305 before being sent. The server forwards encrypted data without decrypting it.
- **Fresh nonce per message:** libsodium generates a random nonce for each encrypted message.
- **Shared-key setup:** Clients create a local `chat.key` automatically if one is missing and load it on startup.
- **Concurrent communication:** Separate send and receive threads keep the chat responsive.
- **TCP message framing:** A 4-byte length prefix identifies each complete encrypted message.
- **Terminal interface:** Colored messages, an editable input prompt, and terminal restoration on exit.

## Tech Stack

- **Language:** C++20
- **Networking:** POSIX TCP sockets
- **Concurrency:** `std::thread`, `std::mutex`, `std::atomic`
- **Cryptography:** libsodium (XChaCha20-Poly1305)
- **Terminal handling:** `termios` and ANSI escape sequences
- **Build:** GNU Make and `g++`

## Getting Started

### Prerequisites

- Linux or macOS (the current implementation uses POSIX APIs)
- A C++20-compatible compiler and `make`
- [libsodium](https://doc.libsodium.org/installation)

On **Ubuntu/Debian**:

```bash
sudo apt update
sudo apt install build-essential libsodium-dev
```

On **macOS** with Homebrew:

```bash
brew install libsodium
```

### Build

```bash
git clone https://github.com/oliveiramnando/tcp-chat.git
cd tcp-chat
make
```

This builds two executables: `server` and `client`.

If the macOS linker cannot find libsodium installed by Homebrew, try:

```bash
make CXXFLAGS="-std=c++20 -Wall -Wextra -g -I$(brew --prefix libsodium)/include" \
     LDLIBS="-L$(brew --prefix libsodium)/lib -lsodium"
```

### Run

Start the server in one terminal:

```bash
./server
```

Open another terminal in the same project directory and run:

```bash
./client
```

Enter a chat name when prompted. Run `./client` in additional terminals to join the conversation. Type **`q()`** and press Enter to disconnect.

By default, the server listens on **port 8080** on all network interfaces; the client connects to **`127.0.0.1:8080`**. To connect from another computer, change the IP address in `client.cpp` to the server's reachable address and rebuild.

## Encryption and Key Setup

The client calls libsodium to generate a **256-bit symmetric key** and saves it as `chat.key` if the file does not already exist. The file is ignored by Git.

**Every client in a conversation must have a copy of the same `chat.key`.** When clients run from the same directory, they naturally load that file. When running from different directories or computers, securely copy the existing `chat.key` to each client's working directory *before starting the client*. Otherwise, clients generate different keys and cannot decrypt each other's messages.

Keep `chat.key` private. Never commit it, post it publicly, or send it over an untrusted channel.

### Message Flow

1. A client constructs a message in the form `name : message`.
2. The client encrypts it with XChaCha20-Poly1305 using a fresh random nonce.
3. The client sends a **4-byte network-order length prefix**, followed by the nonce, ciphertext, and authentication tag.
4. The server validates the frame length and relays the encrypted payload to the other connected clients. It does **not** decrypt messages.
5. Recipients use their copy of the shared key to verify, decrypt, and display each message.

```text
Client A                   Server                   Client B
   |  encrypt                 |                         |
   |--- encrypted message --->|                         |
   |                          |--- encrypted message -->|
   |                          |                    decrypt & display
```

The maximum **encrypted payload** is currently **8,192 bytes**. The nonce and authentication tag consume some of that limit, so the available plaintext message size is smaller.

## Project Structure

```text
tcp-chat/
├── client.cpp        # TCP client, chat input/output, send/receive threads
├── server.cpp        # TCP server and encrypted-message broadcasting
├── crypto.cpp        # Key generation/loading and authenticated encryption
├── crypto.hpp        # Cryptography declarations
├── net.cpp           # Helpers for complete socket reads and writes
├── net.hpp           # Networking declarations and message size limit
├── terminal_test.cpp # Terminal input experiments
├── Makefile          # Build targets
├── .gitignore        # Excludes chat.key and local files
├── LICENSE           # MIT license
└── README.md
```

## Current Limitations

This is a learning project, **not a production-ready secure messenger**. In particular:

- Clients must distribute and manage a **shared secret key manually**; there is no secure key exchange or per-user key management yet.
- Anyone with the shared key can decrypt messages, and chat names are not authenticated as identities.
- The protocol does not currently provide replay protection, private conversations, or separate chat rooms.
- Server addresses and ports are hardcoded, and the terminal interface is designed for POSIX terminals.

## Roadmap

- Secure key exchange and stronger participant identity management
- Private conversations and separate chat rooms
- Configurable host and port settings
- Additional connection handling and terminal UI improvements

## License

This project is licensed under the [MIT License](LICENSE).