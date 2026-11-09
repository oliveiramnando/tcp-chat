# TCP Chat

A terminal-based, end-to-end encrypted TCP chat application written in **C++20**. Two clients communicate through a multithreaded relay server using a custom framed protocol, an ephemeral **X25519** key exchange, and **XChaCha20-Poly1305** authenticated encryption via [libsodium](https://doc.libsodium.org/).

The server only relays frames: it never holds the session keys and cannot read chat content. Each new conversation uses fresh keys, and both clients display a **session fingerprint** that users can compare out of band to detect a man-in-the-middle.

> **Project status:** Work in progress, built for learning systems programming and applied cryptography. It has not been audited and is **not suitable for protecting sensitive communication**. See [Security Considerations](#security-considerations).

## Features

- **End-to-end encryption:** XChaCha20-Poly1305 authenticated encryption with a fresh random 24-byte nonce for every message.
- **Ephemeral key exchange:** A new key pair per session, with directional `rx`/`tx` session keys derived using libsodium's `crypto_kx` API. Secret keys are wiped from memory as soon as the session keys are derived.
- **Session fingerprints:** After the handshake, each client prints a hash of both public keys. Matching fingerprints on both sides show that no one replaced the keys in transit.
- **Ciphertext-only relay:** The server forwards only `HANDSHAKE` and `MESSAGE` frames between clients. Clients cannot forge server control messages; a client that sends any other frame type is disconnected.
- **Input validation:** Frame lengths, message types, roles, public-key sizes and ciphertext sizes are checked before use. Malformed or unauthenticated data is rejected without crashing the client.
- **Two-person rooms:** A thread-per-client server that admits **two clients**; a third receives an error and is disconnected.
- **Connection lifecycle handling:** Join/leave notifications from the server, session keys wiped when a peer leaves, and a fresh handshake when a new peer joins.
- **Terminal chat interface:** Colored messages, live line editing, prompt redraw when messages arrive, and terminal settings restored on every exit path.
- **Configurable host and port:** Set from the command line, defaulting to `127.0.0.1:8080`.

## Tech Stack

| Component | Technology |
| --- | --- |
| Language | C++20 |
| Transport | POSIX TCP sockets |
| Concurrency | `std::thread`, `std::mutex`, `std::atomic` |
| Cryptography | libsodium (`crypto_kx`, XChaCha20-Poly1305, BLAKE2b via `crypto_generichash`) |
| Terminal | `termios`, ANSI escape sequences |
| Build | GNU Make, `g++` |

## Getting Started

### Prerequisites

- **Linux or macOS** (uses POSIX sockets and terminal APIs; on Windows, use WSL)
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

This produces two executables: `server` and `client`. Only the client links against libsodium; the server never performs any cryptography.

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

```text
./server [port]
./client [server-ip] [port]
```

| Program | Argument | Default |
| --- | --- | --- |
| `server` | `port` | `8080` |
| `client` | `server-ip` | `127.0.0.1` |
| `client` | `port` | `8080` |

**On one computer**, start the server, then a client in each of two other terminals:

```bash
./server
./client
./client
```

**Across computers**, pass the server machine's address:

```bash
./server 9000                  # on the server machine
./client 192.168.1.20 9000     # on each client machine
```

Then:

1. Enter a chat name in each client.
2. When both clients are connected, they exchange public keys automatically. Wait for **`Secure connection established.`**
3. **Compare the fingerprint** printed below that line with your peer, in person, by phone or through another trusted channel. If the two fingerprints do not match exactly, someone may be intercepting the connection; do not continue.
4. Type a message and press Enter to send. To leave, type **`q()`** and press Enter, or press **Ctrl+C**.

**Networking notes:**

- The server listens on **all interfaces**, so other machines can reach it, provided the firewall allows the port.
- When the server runs inside **WSL**, other machines can't reach it by default. Enable WSL's mirrored networking mode, or run the server on another machine.
- On **macOS**, only `127.0.0.1` is a working loopback address by default. Other `127.x.x.x` addresses that work on Linux will fail there.

## How It Works

```text
Client A                    Relay Server                    Client B
   |                             |                              |
   |-------- TCP connect ------->|<-------- TCP connect --------|
   |<-- PEER_JOINED (initiator) -|- PEER_JOINED (responder) --->|
   |                             |                              |
   |--- HANDSHAKE (public key) ->|--- HANDSHAKE (public key) -->|
   |<-- HANDSHAKE (public key) --|<-- HANDSHAKE (public key) ---|
   |                             |                              |
   |  derive rx/tx, wipe secret  |  derive rx/tx, wipe secret   |
   |  print fingerprint          |  print fingerprint           |
   |                             |                              |
   |--- MESSAGE (ciphertext) --->|--- MESSAGE (ciphertext) ---->|
   |<-- MESSAGE (ciphertext) ----|<-- MESSAGE (ciphertext) -----|
```

1. When a second client connects, the server sends `PEER_JOINED` to both, assigning the **initiator** role to the client that connected first and the **responder** role to the other. `crypto_kx` requires the two sides to take different roles.
2. Each client wipes any previous session, generates a fresh key pair with `crypto_kx_keypair()`, and sends its public key in a `HANDSHAKE` frame.
3. On receiving the peer's key, each client checks its size, derives a **receive (`rx`)** and a **transmit (`tx`)** key, and wipes its secret key. One side's `tx` equals the other side's `rx`.
4. Each client hashes both public keys, in a fixed initiator-then-responder order, with BLAKE2b and prints the result as the session fingerprint.
5. Messages (`name : message`) are encrypted with `tx` and a new random nonce, relayed by the server, and decrypted by the recipient with `rx`. Anything that fails authentication is rejected.
6. When a peer disconnects, the server sends `PEER_LEFT`; the remaining client wipes its session keys and waits for a new peer and a new handshake.

### TCP Frame Format

Each frame is encoded as:

```text
+----------------------+-------------------+-------------------------+
| Length (4 bytes)     | Type (1 byte)     | Body (variable length)  |
+----------------------+-------------------+-------------------------+
```

- **Length:** Unsigned 32-bit integer in network byte order, counting the type byte and body (not the length prefix itself). Frames shorter than 1 byte or longer than the maximum are rejected and the connection is closed.
- **Type:** One of the values below.
- **Limit:** The maximum frame length is **8,192 bytes**, including the type byte. Encryption adds 40 bytes per message, which reduces the maximum plaintext size.

| Type | Value | Sent by | Body |
| --- | --- | --- | --- |
| `MESSAGE` | 1 | client (relayed) | `[24-byte nonce][ciphertext][16-byte tag]` |
| `HANDSHAKE` | 2 | client (relayed) | 32-byte X25519 public key |
| `PEER_JOINED` | 3 | server only | 1 byte: `1` = initiator, `0` = responder |
| `PEER_LEFT` | 4 | server only | notice text |
| `ERROR_MSG` | 5 | server only | error text (for example, room full) |

The `readNBytes()` and `sendAll()` helpers handle reads and writes that transfer fewer bytes than requested, since TCP does not preserve message boundaries. `sendFrame()` and `receiveFrame()` build and parse frames, and are shared by the client and server so both always use the same format.

### Encryption

`crypto.cpp` uses **XChaCha20-Poly1305** with a random **24-byte nonce** per message. The encrypted body is structured as:

```text
[24-byte nonce][ciphertext][16-byte authentication tag]
```

The tag lets the recipient reject any message that was altered, truncated or encrypted with a different key. Because each direction has its own key, a message cannot be reflected back to its sender. No key files are needed: all keys are generated at runtime and exist only for the length of a session.

## Project Structure

```text
tcp-chat/
├── client.cpp          # Connection, handshake handling, terminal UI, send/receive threads
├── server.cpp          # Two-client relay, role assignment, connection lifecycle
├── crypto.cpp          # Key pairs, session key derivation, fingerprints, AEAD helpers
├── crypto.hpp          # Session structure and cryptography declarations
├── net.cpp             # Framing and complete socket read/write helpers
├── net.hpp             # Message types, roles, limits, networking declarations
├── terminal_test.cpp   # Terminal input experiments
├── Makefile            # Build and clean targets
├── .gitignore
├── LICENSE             # MIT license
└── README.md
```

## Security Considerations

This is an experimental application, **not an audited or production-ready secure messenger**.

- **Fingerprint checks are manual:** The key exchange does not authenticate who sent each public key. A malicious server or active attacker could substitute its own keys; this is detected **only if both users compare their fingerprints** through a separate channel.
- **No long-term identity:** Fingerprints change every session, so they must be compared every time. There are no persistent identity keys and no "trust on first use."
- **No replay protection:** A captured valid message could be re-sent, and messages could be dropped or reordered, without detection.
- **Message type not bound to ciphertext:** The frame type is not yet included as authenticated additional data.
- **Display names are not authenticated:** The chat name is chosen by the sender and only travels inside the encrypted message.
- **Metadata is visible to the server:** It sees when clients connect, message sizes and timing, though not message content.
- **One conversation at a time:** The server supports a single two-person session and keeps no history.

Do not rely on this project to protect sensitive information.

## Roadmap

- [x] Client-to-client key exchange with ephemeral keys
- [x] Session fingerprints for detecting man-in-the-middle attacks
- [x] Server relays only client frame types; control messages come only from the server
- [x] Make server address and port configurable
- [x] Improve disconnect, terminal and error handling
- [ ] Bind the frame type to each ciphertext as authenticated additional data
- [ ] Add message counters for replay and reordering detection
- [ ] Long-term identity keys with trust on first use, so peers are verified once
- [ ] Automated protocol and malformed-input tests
- [ ] Support multiple independent conversations and group chat
- [ ] Replace thread-per-client with `poll()`, and add load and latency benchmarks

## License

Licensed under the [MIT License](LICENSE).
