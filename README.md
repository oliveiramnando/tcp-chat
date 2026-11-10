# TCP Chat

A terminal-based, end-to-end encrypted TCP chat application written in **C++20**. Clients log in to a multithreaded relay server with a long-term **Ed25519** identity key, see who is online, and open private one-to-one conversations. Each conversation uses its own ephemeral **X25519** key exchange and **XChaCha20-Poly1305** authenticated encryption via [libsodium](https://doc.libsodium.org/).

The server authenticates users and routes frames, but it never holds conversation keys and cannot read chat content. Every conversation gets fresh keys, and both sides display a **session fingerprint** that users can compare out of band to detect a man-in-the-middle.

> **Project status:** Work in progress, built for learning systems programming and applied cryptography. It has not been audited and is **not suitable for protecting sensitive communication**. See [Security Considerations](#security-considerations).

## Features

- **Persistent identity:** Each client creates an Ed25519 identity key pair on first run, saves it to `identity.key` with owner-only permissions, and reuses it afterwards. A user's ID is their 32-byte public key, shown as a short fingerprint.
- **Challenge-response login:** The server sends a random 32-byte challenge, and the client proves it owns its identity by signing it. No passwords are involved, and duplicate logins with the same identity are rejected.
- **Multiple users and private conversations:** Any number of clients can be online. Use `/users` to list them and `/chat n` to talk to one; each peer gets its own independent encrypted session.
- **Live presence:** The server sends the list of online users at login, then notifies clients as users come online or go offline.
- **End-to-end encryption:** XChaCha20-Poly1305 authenticated encryption with a fresh random 24-byte nonce for every message.
- **Ephemeral key exchange per conversation:** A new key pair for every session, with directional `rx`/`tx` keys derived using libsodium's `crypto_kx` API. Ephemeral secret keys are wiped as soon as session keys are derived, and session keys are wiped when the peer goes offline.
- **Session fingerprints:** After each handshake, both clients print a hash of the two ephemeral public keys. Matching fingerprints show that no one replaced the keys in transit.
- **Addressed, relay-only routing:** The server forwards only `HANDSHAKE` and `MESSAGE` frames, and only to the addressed recipient. It stamps every relayed frame with the authenticated sender's ID, so clients cannot impersonate each other. A client that sends any other frame type is disconnected.
- **Input validation:** Frame lengths, message types, IDs, public-key sizes, signatures and ciphertext sizes are checked before use. Malformed or unauthenticated data is rejected without crashing.
- **Terminal chat interface:** Colored messages, live line editing, prompt redraw when messages arrive, and terminal settings restored on every exit path.
- **Configurable host and port:** Set from the command line, defaulting to `127.0.0.1:8080`.

## Tech Stack

| Component | Technology |
| --- | --- |
| Language | C++20 |
| Transport | POSIX TCP sockets |
| Concurrency | `std::thread`, `std::mutex`, `std::atomic` |
| Cryptography | libsodium (Ed25519 `crypto_sign`, X25519 `crypto_kx`, XChaCha20-Poly1305, BLAKE2b via `crypto_generichash`) |
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

This produces two executables: `server` and `client`. Both link against libsodium: the client for all of its cryptography, and the server only to verify login signatures.

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

**On one computer**, start the server, then run each client **from its own directory**, because every client reads and writes `identity.key` in its current directory:

```bash
./server

mkdir -p alice && cd alice && ../client     # second terminal
mkdir -p bob && cd bob && ../client         # third terminal
```

**Across computers**, pass the server machine's address:

```bash
./server 9000                  # on the server machine
./client 192.168.1.20 9000     # on each client machine
```

Then:

1. On startup, the client prints **`Your fingerprint:`**. This is your identity and stays the same as long as you keep `identity.key`.
2. After **`Logged in.`**, enter a chat name.
3. Type **`/users`** to see a numbered list of who is online, then **`/chat n`** to open a conversation with user `n`. The key exchange runs automatically; wait for **`Secure connection established.`**
4. **Compare the session fingerprint** printed below that line with your peer, in person, by phone or through another trusted channel. If the two fingerprints do not match exactly, someone may be intercepting the connection; do not continue.
5. Type a message and press Enter to send it to the selected user. Messages from anyone you have a session with appear as they arrive.

| Command | Action |
| --- | --- |
| `/users` | List online users with their identity fingerprints |
| `/chat n` | Start or switch to a conversation with user `n` from the last `/users` list |
| `q()` or **Ctrl+C** | Leave the chat |

> Keep `identity.key` private and out of version control. Anyone with this file can log in as you. Deleting it creates a new identity on the next run.

**Networking notes:**

- The server listens on **all interfaces**, so other machines can reach it, provided the firewall allows the port.
- When the server runs inside **WSL**, other machines can't reach it by default. Enable WSL's mirrored networking mode, or run the server on another machine.
- On **macOS**, only `127.0.0.1` is a working loopback address by default. Other `127.x.x.x` addresses that work on Linux will fail there.

## How It Works

```text
Client A                    Relay Server                    Client B
   |                             |                              |
   |<--------- CHALLENGE --------|-------- CHALLENGE ---------->|
   |--- AUTH (id pk, signature)->|<-(id pk, signature) AUTH ----|
   |<---------- AUTH_OK ---------|---------- AUTH_OK ---------->|
   |<--- USER_LIST / USER_ONLINE-|-- USER_LIST / USER_ONLINE -->|
   |                             |                              |
   |  /chat B                    |                              |
   |-- HANDSHAKE [B][A's eph pk]>|-- HANDSHAKE [A][A's eph pk]->|
   |<-HANDSHAKE [A][B's eph pk]--|<-HANDSHAKE [A][B's eph pk]---|
   |                             |                              |
   |  derive rx/tx, wipe secret  |  derive rx/tx, wipe secret   |
   |  print fingerprint          |  print fingerprint           |
   |                             |                              |
   |--- MESSAGE [B][ciphertext]->|--- MESSAGE [A][ciphertext]-->|
```

### Login

1. When a client connects, the server sends a `CHALLENGE` of 32 random bytes.
2. The client signs `"tcpchat-login-v1" || challenge` with its Ed25519 identity key and replies with `AUTH`: its 32-byte public key followed by the 64-byte signature. The label keeps a login signature from being reusable in any other context.
3. The server verifies the signature. If it is valid and that identity is not already online, the server sends `AUTH_OK`, registers the client, sends it a `USER_LIST`, and sends `USER_ONLINE` to everyone else. Otherwise it sends `ERROR_MSG` and closes the connection.
4. When a client disconnects, the server removes it and sends `USER_OFFLINE` to everyone else.

### Conversations

1. The client keeps a separate session for each peer, indexed by the peer's ID.
2. `crypto_kx` requires the two sides to take different roles. Each client decides without asking the server: the side whose identity public key compares lower (byte by byte) is the **initiator**, and the other is the **responder**. Both sides always reach the same answer.
3. `/chat n` generates a fresh ephemeral key pair for that peer and sends its public key in a `HANDSHAKE` addressed to the peer.
4. A client receiving a `HANDSHAKE` from someone it has not started a handshake with creates a session and replies with its own public key. If it already has a completed session with that peer, it treats the frame as a re-key and replies with a new key pair.
5. On receiving the peer's key, each client checks its size, derives a **receive (`rx`)** and **transmit (`tx`)** key, and wipes its ephemeral secret key. One side's `tx` equals the other side's `rx`.
6. Each client hashes both ephemeral public keys, in a fixed initiator-then-responder order, with BLAKE2b and prints the result as the session fingerprint.
7. Messages (`name : message`) are encrypted with that peer's `tx` and a new random nonce, sent addressed to the peer, and decrypted by the recipient with its `rx` for the sender. Messages from peers without a completed session, or that fail authentication, are rejected.
8. When a peer goes offline, the client wipes and removes that peer's session.

### TCP Frame Format

Each frame is encoded as:

```text
+----------------------+-------------------+-------------------------+
| Length (4 bytes)     | Type (1 byte)     | Body (variable length)  |
+----------------------+-------------------+-------------------------+
```

- **Length:** Unsigned 32-bit integer in network byte order, counting the type byte and body (not the length prefix itself). Frames shorter than 1 byte or longer than the maximum are rejected and the connection is closed.
- **Type:** One of the values below.
- **Limit:** The maximum frame length is **8,192 bytes**, including the type byte. The 32-byte peer ID and 40 bytes of encryption overhead leave room for about 8,100 bytes of plaintext per message, including the sender's name.

Relayed frames carry an **addressed body**: `[32-byte peer ID][payload]`. From client to server, the ID is the **recipient**; the server replaces it with the authenticated **sender's** ID before forwarding.

| Type | Value | Sent by | Body |
| --- | --- | --- | --- |
| `MESSAGE` | 1 | client (relayed) | `[peer ID][24-byte nonce][ciphertext][16-byte tag]` |
| `HANDSHAKE` | 2 | client (relayed) | `[peer ID][32-byte X25519 ephemeral public key]` |
| — | 3, 4 | — | Reserved (formerly `PEER_JOINED`/`PEER_LEFT`, no longer used) |
| `ERROR_MSG` | 5 | server only | Error text, or the 32-byte ID of a recipient who is not online |
| `CHALLENGE` | 6 | server only | 32 random bytes |
| `AUTH` | 7 | client only | `[32-byte Ed25519 public key][64-byte signature]` |
| `AUTH_OK` | 8 | server only | Empty |
| `USER_LIST` | 9 | server only | Concatenated 32-byte IDs of the other online users |
| `USER_ONLINE` | 10 | server only | 32-byte ID |
| `USER_OFFLINE` | 11 | server only | 32-byte ID |

The `readNBytes()` and `sendAll()` helpers handle reads and writes that transfer fewer bytes than requested, since TCP does not preserve message boundaries. `sendFrame()`, `receiveFrame()`, `buildBody()` and `parseBody()` build and parse frames, and are shared by the client and server so both always use the same format.

### Encryption

`crypto.cpp` uses **XChaCha20-Poly1305** with a random **24-byte nonce** per message. The encrypted payload is structured as:

```text
[24-byte nonce][ciphertext][16-byte authentication tag]
```

The tag lets the recipient reject any message that was altered, truncated or encrypted with a different key. Because each direction of each conversation has its own key, a message cannot be reflected back to its sender or replayed into a different conversation. The only key stored on disk is the long-term identity key; all conversation keys exist only in memory for the length of a session.

## Project Structure

```text
tcp-chat/
├── client.cpp          # Login, per-peer sessions, /users and /chat, terminal UI, send/receive threads
├── server.cpp          # Login verification, user registry, presence updates, addressed routing
├── crypto.cpp          # Identity keys, login signing, key pairs, session keys, fingerprints, AEAD helpers
├── crypto.hpp          # Identity and Session structures, cryptography declarations
├── net.cpp             # Framing, addressed bodies, user lists, complete socket read/write helpers
├── net.hpp             # Message types, peer IDs, limits, networking declarations
├── terminal_test.cpp   # Terminal input experiments
├── Makefile            # Build and clean targets
├── .gitignore
├── LICENSE             # MIT license
└── README.md
```

## Security Considerations

This is an experimental application, **not an audited or production-ready secure messenger**.

- **Handshakes are not yet signed:** Identity keys prove who you are to the server at login, but ephemeral handshake keys are not signed with them. A malicious server could substitute its own keys in a conversation; this is detected **only if both users compare session fingerprints** through a separate channel. Any online user can also send a `HANDSHAKE` that forces a re-key of an existing session.
- **No saved contacts:** Identity fingerprints are shown but not remembered, so there is no "trust on first use" warning if a known user's identity changes.
- **Identity key stored unencrypted:** `identity.key` is protected only by file permissions (`0600`).
- **No replay protection:** A captured valid message could be re-sent, and messages could be dropped or reordered, without detection.
- **Message type not bound to ciphertext:** The frame type is not yet included as authenticated additional data.
- **Display names are not authenticated:** The chat name is chosen by the sender and only travels inside the encrypted message. The sender's ID, stamped by the server, is what actually identifies them.
- **Metadata is visible to the server:** It sees who is online, who talks to whom, and message sizes and timing, though not message content.
- **No offline delivery or history:** Messages to users who are not online are not stored, and no chat history is kept.

Do not rely on this project to protect sensitive information.

## Roadmap

- [x] Client-to-client key exchange with ephemeral keys
- [x] Session fingerprints for detecting man-in-the-middle attacks
- [x] Server relays only client frame types; control messages come only from the server
- [x] Make server address and port configurable
- [x] Improve disconnect, terminal and error handling
- [x] Long-term Ed25519 identity keys
- [x] Challenge-response login to the server
- [x] Addressed frames, multi-user routing and online presence
- [x] Multiple independent one-to-one conversations (`/users`, `/chat n`)
- [ ] Sign handshake keys with identity keys so peers are authenticated automatically
- [ ] Contacts file with trust on first use, so peers are verified once and remembered
- [ ] Dedicated "user not found" frame type instead of `ERROR_MSG`
- [ ] More chat commands (current conversation, contact names, sender labels)
- [ ] Bind the frame type to each ciphertext as authenticated additional data
- [ ] Add message counters for replay and reordering detection
- [ ] Automated protocol and malformed-input tests
- [ ] Group chat
- [ ] Replace thread-per-client with `poll()`, and add load and latency benchmarks

## License

Licensed under the [MIT License](LICENSE).
