# Custom Redis Server in C++

A Redis-inspired in-memory key-value store built from scratch in **C++** using TCP sockets, non-blocking I/O, `poll()`, custom request/response framing, buffering, and an event-driven architecture.

# Custom Redis Server in C++

A Redis-inspired in-memory key-value store built from scratch in **C++** using TCP sockets, non-blocking I/O, `poll()`, custom request/response framing, buffering, and an event-driven architecture.

## ⚡ Benchmark

The server was benchmarked locally with **100,000 `SET` requests**.

| Metric          |                 Result |
| --------------- | ---------------------: |
| Total Requests  |                100,000 |
| Operation       |                  `SET` |
| Total Time      |    **13.1557 seconds** |
| Throughput      | **7,601 requests/sec** |
| Average Latency | **0.131557 ms/request** |

### Benchmark Output

```text
Benchmarking 100000 SET requests...

--- Benchmark Results ---
Total Time:       13.1557 seconds
Throughput:       7601 Requests/Sec (RPS)
Avg Latency:      0.131557 ms per request
```

> **Note:** This is a local development benchmark measuring sequential `SET` request/response operations. It is intended as a performance baseline for this implementation and should not be directly compared with production Redis benchmarks.

---

# Overview

This project is a lightweight Redis-like key-value server implemented from scratch to explore how network servers and in-memory data stores work internally.

Instead of using a networking framework, the server directly uses Linux/POSIX APIs such as:

* `socket()`
* `bind()`
* `listen()`
* `accept()`
* `read()`
* `write()`
* `fcntl()`
* `poll()`

The server uses a **single event-driven loop** to handle multiple TCP clients without creating a separate thread for every connection.

---

# Features

* TCP client-server communication
* Non-blocking sockets using `fcntl()` and `O_NONBLOCK`
* Event-driven I/O using `poll()`
* Multiple simultaneous client connections
* Support for up to 100 clients
* Custom length-prefixed request/response protocol
* Per-client read buffers
* Per-client write buffers
* Partial read handling
* Partial write handling
* `EINTR` handling
* `EAGAIN` / `EWOULDBLOCK` handling
* Request pipelining
* In-memory key-value storage using `std::unordered_map`
* Redis-style `SET`, `GET`, `DEL`, and `PING` commands
* Client disconnect and socket error handling
* Maximum message size of 4096 bytes

---

# Architecture

The server follows an event-driven architecture using `poll()`.

```text
                         +------------------+
                         | Listening Socket |
                         +--------+---------+
                                  |
                               accept()
                                  |
                                  v
                         +------------------+
                         |     poll()       |
                         |    Event Loop    |
                         +--------+---------+
                                  |
              +-------------------+-------------------+
              |                   |                   |
              v                   v                   v
         +---------+         +---------+         +---------+
         | Client 1|         | Client 2|         | Client N|
         +---------+         +---------+         +---------+
              |                   |                   |
           POLLIN              POLLIN              POLLIN
           POLLOUT             POLLOUT             POLLOUT
```

The server continuously waits for socket events using `poll()`.

When a client has data available:

```text
POLLIN
  ↓
read()
  ↓
read buffer
  ↓
parse request
  ↓
execute command
  ↓
write buffer
  ↓
POLLOUT
  ↓
write()
```

This allows one server process to manage multiple client connections.

---

# Supported Commands

## PING

Checks whether the server is responding.

```text
PING
```

Response:

```text
PONG
```

---

## SET

Stores a value associated with a key.

```text
SET name Subhankar
```

Response:

```text
OK
```

Values containing spaces are also supported:

```text
SET message Hello World
```

Response:

```text
OK
```

---

## GET

Retrieves the value associated with a key.

```text
GET name
```

Response:

```text
Subhankar
```

If the key does not exist:

```text
GET unknown
```

Response:

```text
(nil)
```

---

## DEL

Deletes a key.

```text
DEL name
```

If the key existed:

```text
(integer) 1
```

If the key did not exist:

```text
(integer) 0
```

---

# Example Session

```text
Enter command
PING

server says: PONG

Enter command
SET name Subhankar

server says: OK

Enter command
GET name

server says: Subhankar

Enter command
SET language C++

server says: OK

Enter command
GET language

server says: C++

Enter command
DEL language

server says: (integer) 1

Enter command
GET language

server says: (nil)
```

---

# Custom Protocol

TCP provides a byte stream rather than individual messages.

Therefore, this project uses a simple **length-prefixed protocol**.

Every request is structured as:

```text
+----------------------+----------------------+
| 4-byte message size | Message body         |
+----------------------+----------------------+
```

For example:

```text
[4-byte length][SET name Subhankar]
```

The server reads the length first and then waits until the complete request has been received.

Responses use the same format:

```text
+------------------------+----------------------+
| 4-byte response length | Response body        |
+------------------------+----------------------+
```

This approach allows the server to correctly handle TCP stream behavior.

---

# Handling Partial Reads

A TCP `read()` call does not guarantee that an entire application message will arrive at once.

For example, the client may send:

```text
SET name Subhankar
```

but the server could receive:

```text
SET nam
```

followed by:

```text
e Subhankar
```

The server therefore maintains a per-client read buffer:

```cpp
char rbuf[4 + k_max_msg];
```

Incoming bytes are accumulated until a complete length-prefixed request is available.

---

# Handling Partial Writes

The same problem can occur when sending responses.

A single `write()` call may send only part of a response.

The server therefore maintains a per-client write buffer:

```cpp
char wbuf[4 + k_max_msg];
```

and tracks how many bytes have already been sent:

```cpp
size_t wbuf_sent;
```

If the socket cannot currently accept more data, the server waits for `POLLOUT` and continues writing later.

---

# Request Pipelining

The server can process multiple requests that arrive in the same buffer.

For example:

```text
SET name Subhankar
GET name
PING
```

The server can process them sequentially:

```text
Request 1 → SET
Request 2 → GET
Request 3 → PING
```

The remaining unprocessed bytes stay in the client's read buffer until they form another complete request.

---

# Non-Blocking I/O

Client and server sockets are configured as non-blocking using:

```cpp
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

This prevents a single socket operation from blocking the entire event loop.

If there is currently no data available, `read()` can return:

```text
EAGAIN
```

or:

```text
EWOULDBLOCK
```

The server then returns to `poll()` and waits for the socket to become ready again.

---

# In-Memory Database

The key-value store is implemented using:

```cpp
static unordered_map<string, string> g_map;
```

For example:

```text
Key          Value
-------------------------
name         Subhankar
language     C++
city         Kolkata
```

When the client executes:

```text
SET language C++
```

the server stores:

```cpp
g_map["language"] = "C++";
```

A `GET` command searches the map and returns the corresponding value.

Because the database exists only in memory, **all data is lost when the server stops**.

---

# Project Structure

The recommended repository structure is:

```text
Custom-Redis-Server/
│
├── .gitignore
├── README.md
├── Makefile
│
├── server.cpp
├── client.cpp
└── benchmark.cpp
```

Compiled executables such as `server`, `client`, and `benchmark` should not be committed to GitHub. They can be generated locally using `make`.

---

# Requirements

The project uses Linux/POSIX networking APIs.

### Required

* Linux
* GCC/G++
* C++17 or newer
* POSIX socket APIs

The project can also be run using **WSL** on Windows.

---

# Build

Clone the repository:

```bash
git clone <your-repository-url>
cd Custom-Redis-Server
```

If a `Makefile` is included:

```bash
make
```

This builds:

```text
server
client
benchmark
```

You can also compile manually:

### Server

```bash
g++ -std=c++17 -Wall -Wextra -O2 server.cpp -o server
```

### Client

```bash
g++ -std=c++17 -Wall -Wextra -O2 client.cpp -o client
```

### Benchmark

```bash
g++ -std=c++17 -Wall -Wextra -O2 benchmark.cpp -o benchmark
```

---

# Running the Server

Start the server:

```bash
./server
```

Expected output:

```text
Server listening on port 1234...
```

The server listens on:

```text
127.0.0.1:1234
```

---

# Running the Client

Open another terminal and run:

```bash
./client
```

You should see:

```text
Enter command
```

You can now enter commands such as:

```text
PING
SET name Subhankar
GET name
DEL name
```

---

# Running the Benchmark

Start the server first:

```bash
./server
```

Then open another terminal and run:

```bash
./benchmark
```

Example:

```text
Benchmarking 100000 SET requests...

--- Benchmark Results ---
Total Time:       14.6186 seconds
Throughput:       6840 Requests/Sec (RPS)
Avg Latency:      0.146186 ms per request
```

For meaningful performance comparisons, benchmarks should be run under consistent hardware and system conditions.

---

# Multiple Clients

The server supports multiple simultaneous TCP clients.

You can test this by opening multiple terminals.

### Terminal 1

```bash
./server
```

### Terminal 2

```bash
./client
```

### Terminal 3

```bash
./client
```

### Terminal 4

```bash
./client
```

All clients communicate with the same server and therefore share the same in-memory database.

For example:

```text
Client 1:
SET username Alice

Client 2:
GET username

Response:
Alice
```

---

# Important APIs Used

| API        | Purpose                          |
| ---------- | -------------------------------- |
| `socket()` | Creates a TCP socket             |
| `bind()`   | Assigns IP address and port      |
| `listen()` | Starts listening for connections |
| `accept()` | Accepts a client connection      |
| `fcntl()`  | Enables non-blocking I/O         |
| `poll()`   | Monitors multiple sockets        |
| `read()`   | Receives data                    |
| `write()`  | Sends data                       |
| `close()`  | Closes a socket                  |

---

# Error Handling

The server handles common errors related to non-blocking sockets.

### `EAGAIN` / `EWOULDBLOCK`

Indicates that the socket is not currently ready for the requested operation.

The server waits for the appropriate `poll()` event instead of treating this as a fatal error.

### `EINTR`

Indicates that a system call was interrupted by a signal.

The server retries the operation.

### `POLLHUP`

Indicates that the client has disconnected.

The server closes and removes the corresponding connection.

---

# Current Limitations

This project implements a small subset of Redis functionality and is primarily intended for learning and experimentation.

Current limitations:

* Data is stored only in memory
* Data is lost when the server stops
* Maximum message size is 4096 bytes
* Maximum of 100 simultaneous connections
* No authentication
* No Redis RESP protocol
* No persistence
* No replication
* No transactions
* No TTL/expiration
* Limited command set
* Single-threaded event loop

---

# Future Improvements

Possible improvements include:

* Implement the Redis RESP protocol
* Add more Redis commands
* Add `EXPIRE` and TTL support
* Add persistent storage
* Implement append-only logging
* Add snapshot-based persistence
* Improve command parsing
* Add `epoll()` support
* Add connection timeouts
* Add automated tests
* Add concurrent multi-client benchmarks
* Add pipelining benchmarks
* Add performance metrics
* Add graceful server shutdown
* Add configuration support

---

# What I Learned

Building this project provided practical experience with:

* TCP/IP networking
* Client-server architecture
* Linux socket programming
* Non-blocking I/O
* Event-driven programming
* `poll()` and I/O multiplexing
* TCP byte-stream behavior
* Partial reads and writes
* Read/write buffering
* Request pipelining
* Length-prefixed protocols
* `errno`-based error handling
* In-memory data structures
* C++ system programming
* Linux system calls
* Network server design
* Performance benchmarking

---

# Technologies

```text
C++
C++17
Linux / POSIX
TCP/IP
POSIX Sockets
poll()
fcntl()
Non-blocking I/O
std::unordered_map
```

---

# Project Goal

The goal of this project is to understand the core concepts behind an in-memory networked data store by implementing the networking layer, event loop, protocol framing, buffering, command processing, client communication, and key-value storage from scratch.

This project is **Redis-inspired and educational** and is not intended to be a drop-in replacement for Redis.

---

## License

This project is intended for educational and learning purposes.
