# TCP Chat

A terminal-based, encrypted TCP chat application written in **C++20**. Two clients communicate through a multithreaded relay server using a custom framed protocol and **XChaCha20-Poly1305** authenticated encryption via [libsodium](https://doc.libsodium.org/).

The server forwards encrypted messages without decrypting them. Clients establish directional session keys by exchanging public keys through the relay, so a shared key file is no longer required.

> **Project status:** Work in progress and intended for learning systems programming. Key exchange is **not authenticated**, so the application is **not suitable for sensitive communication over untrusted networks**.

## Features

- **Encrypted messaging:** XChaCha20-Poly1305 authenticated encryption with a fresh random 24-byte nonce for every message.
- **Client-to-client key exchange:** Ephemeral public/private key pairs and directional session keys derived with libsodium's `crypto_kx` API.
- **Ciphertext-only relay:** The server routes handshake frames and encrypted message frames without holding client session keys or reading chat content.
- **Concurrent connections:** A thread-per-client server with a current limit of **two connected clients**.
- **Framed TCP protocol:** A 4-byte length prefix and 1-byte message type distinguish messages and connection events.
- **Connection lifecycle handling:** Join/leave notifications, session reset when a peer disconnects, and a new handshake when a peer joins.
- **Terminal chat interface:** Colored incoming messages, editable input, prompt redraw, and restoration of terminal settings on exit.

## Tech Stack

| Component | Technology |
| --- | --- |
| Language | C++20 |
| Transport | POSIX TCP sockets |
| Concurrency | `std::thread`, `std::mutex`, `std::atomic` |
| Cryptography | libsodium (`crypto_kx`, XChaCha20-Poly1305) |
| Terminal | `termios`, ANSI escape sequences |
| Build | GNU Make, `g++` |

## Getting Started

### Prerequisites

- **Linux or macOS** (uses POSIX sockets and terminal APIs)
- A C++20-compatible `g++` and `make`
- **libsodium** and its development headers

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential libsodium-dev
```

On macOS with Homebrew:

```bash
brew install libsodium
```

### Build

```bash
git clone https://github.com/oliveiramnando/tcp-chat.git
cd tcp-chat
make
```

This produces two executables: `server` and `client`.

If the compiler or linker cannot find a Homebrew-installed libsodium on macOS:

```bash
make CXXFLAGS="-std=c++20 -Wall -Wextra -g -I$(brew --prefix libsodium)/include" \
     LDLIBS="-L$(brew --prefix libsodium)/lib -lsodium"
```

To remove the binaries:

```bash
make clean
```

### Run

Start the relay server in one terminal:

```bash
./server
```

Then start a client in each of two other terminals:

```bash
./client
```

Enter a chat name in each client. Once both clients connect, the server assigns handshake roles; clients exchange public keys and derive their session keys. Wait until **`Secure connection established.`** appears before sending messages.

Type a message and press Enter to send. To leave, type **`q()`** and press Enter.

**Network defaults:** The server listens on TCP port **8080** on all interfaces. The client connects to **`127.0.0.1:8080`**. For use across computers, change the IP address in `client.cpp` to the server's reachable address and rebuild. Command-line host/port options are not implemented yet.

**Current capacity:** The server intentionally permits only **two connected clients**. A third client receives an error.

## How It Works

```text
Client A                    Relay Server                    Client B
   |                             |                              |
   |-------- TCP connect ------->|<-------- TCP connect -------|
   |<------- PEER_JOINED --------|-------- PEER_JOINED -------->|
   |                             |                              |
   |--- HANDSHAKE (public key) ->|--- HANDSHAKE (public key) --->|
   |<-- HANDSHAKE (public key) --|<-- HANDSHAKE (public key) ----|
   |                             |                              |
   |    derive session keys      |      derive session keys     |
   |                             |                              |
   |--- MESSAGE (ciphertext) --->|--- MESSAGE (ciphertext) ---->|
   |                             |                              |
   |<-- MESSAGE (ciphertext) ----|<-- MESSAGE (ciphertext) -----|
```

1. The server accepts two TCP connections and tells each client which key-exchange role to use.
2. Each client generates a temporary key pair using `crypto_kx_keypair()` and sends its public key through the server.
3. Each endpoint uses its private key and the other endpoint's public key to derive **receive (`rx`)** and **transmit (`tx`)** keys.
4. A client encrypts `name : message` with its `tx` key and a new random nonce.
5. The server relays the encrypted frame; the recipient decrypts it using its `rx` key and displays the message.
6. When a peer leaves, the remaining client clears its session and waits for a new peer and handshake.

The server **does not possess the client-derived session keys**. However, because clients do not authenticate the exchanged public keys, a malicious relay or active network attacker could impersonate a peer. See [Security Considerations](#security-considerations).

### TCP Frame Format

Each frame is encoded as:

```text
+----------------------+-------------------+-------------------------+
| Length (4 bytes)     | Type (1 byte)     | Body (variable length)  |
+----------------------+-------------------+-------------------------+
```

- **Length:** Unsigned 32-bit integer in network byte order, counting the type byte and body (not the length prefix).
- **Type:** `MESSAGE`, `HANDSHAKE`, `PEER_JOINED`, `PEER_LEFT`, or `ERROR_MSG`.
- **Body:** Depends on the message type. For `MESSAGE`, it contains the nonce followed by encrypted message data and its authentication tag.
- **Limit:** The current maximum frame length is **8,192 bytes**, including the type byte. Encryption overhead further reduces the maximum plaintext size.

The `readNBytes()` and `sendAll()` helpers handle reads and writes that transfer fewer bytes than requested, since TCP does not preserve application message boundaries.

### Encryption

`crypto.cpp` uses **XChaCha20-Poly1305** with a random **24-byte nonce** per message. The encrypted body is structured as:

```text
[24-byte nonce][ciphertext][16-byte authentication tag]
```

The tag allows the recipient to reject ciphertext that fails authentication. Clients derive new session keys when a new peer connection is established; there is no persistent shared `chat.key` file in this implementation.

## Project Structure

```text
tcp-chat/
├── client.cpp          # Connection logic, handshake, encryption, terminal UI
├── server.cpp          # Two-client TCP relay and connection lifecycle
├── crypto.cpp          # Session key generation, derivation, AEAD helpers
├── crypto.hpp          # Session structure and cryptography declarations
├── net.cpp             # Framing and complete socket read/write helpers
├── net.hpp             # Protocol types, limits, networking declarations
├── terminal_test.cpp   # Terminal input experiments
├── Makefile            # Build and clean targets
├── .gitignore
├── LICENSE             # MIT license
└── README.md
```

## Security Considerations

This is an experimental application, **not an audited or production-ready secure messenger**.

- **Unauthenticated key exchange:** `crypto_kx` establishes shared secrets, but the application does not verify public-key ownership. It does not currently resist an active man-in-the-middle attack.
- **No replay protection:** A previously captured valid encrypted message may be resent without being detected as a replay.
- **No authenticated user identities:** The display name is user-provided and is not tied to a verified identity.
- **Concurrent session state:** The send and receive threads access shared session data; stronger synchronization is needed around key lifecycle transitions.
- **No private conversations or message history:** The application currently supports one two-person session and does not persist chat history.

Do not rely on this project to protect sensitive information.

## Roadmap

- [ ] Authenticate peers and verify exchanged public keys
- [ ] Harden session state management and synchronization
- [ ] Add replay detection and protocol-level tests
- [ ] Support multiple independent peer conversations and targeted routing
- [ ] Make server address and port configurable
- [ ] Add load testing and connection/latency benchmarks
- [ ] Improve disconnect, terminal, and error handling

## License

Licensed under the [MIT License](LICENSE).
