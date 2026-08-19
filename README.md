# Custom Redis Server in C++

A Redis-inspired in-memory key-value store built from scratch in **C++** using low-level Linux networking APIs.

The project focuses on understanding how a real network server works internally, including **TCP socket programming, non-blocking I/O, event-driven architecture, request buffering, partial reads/writes, request pipelining, and in-memory data storage**.

The server supports multiple simultaneous TCP clients using `poll()` instead of creating a separate thread for every connection.

---

## Features

* TCP server built using POSIX socket APIs
* Non-blocking sockets using `fcntl()` and `O_NONBLOCK`
* Event-driven I/O using `poll()`
* Multiple simultaneous client connections
* Support for up to 100 connected clients
* Length-prefixed request/response protocol
* Per-client read and write buffers
* Handles partial TCP reads and writes
* Handles `EINTR`, `EAGAIN`, and `EWOULDBLOCK`
* Request pipelining support
* In-memory key-value database using `std::unordered_map`
* Redis-style commands
* Configurable maximum message size
* Client disconnect and socket error handling

---

## Supported Commands

### `PING`

Checks whether the server is responding.

```text
PING
```

Response:

```text
PONG
```

---

### `SET`

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

### `GET`

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

### `DEL`

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

## Architecture

The server follows an event-driven architecture based on `poll()`.

```text
                         +------------------+
                         | Listening Socket |
                         +--------+---------+
                                  |
                               accept()
                                  |
                                  v
                         +------------------+
                         |    poll()        |
                         | Event Loop       |
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

The server does not block waiting for a particular client.

Instead, `poll()` monitors all active sockets and tells the server which sockets are ready for reading or writing.

---

## How a Request Is Processed

A typical request follows this path:

```text
Client
  |
  | TCP
  v
Non-blocking Socket
  |
  v
Read Buffer
  |
  v
Message Framing
  |
  v
Command Parser
  |
  v
Key-Value Store
  |
  v
Response Buffer
  |
  v
Non-blocking write()
  |
  v
Client
```

For example:

```text
SET name Subhankar
```

is received by the server, parsed into:

```text
Command = SET
Key     = name
Value   = Subhankar
```

and stored in:

```cpp
unordered_map<string, string>
```

---

# Custom Network Protocol

TCP is a byte stream and does not preserve application-level message boundaries.

Therefore, this project uses a simple **length-prefixed protocol**.

Each request contains:

```text
+----------------------+----------------------+
| 4-byte message size | Message body         |
+----------------------+----------------------+
```

For example:

```text
[4-byte length][SET name Subhankar]
```

The server first determines the message length and then waits until the complete request has been received.

Responses use the same format:

```text
[4-byte response length][response body]
```

This allows the server to correctly handle:

* Partial messages
* Multiple messages in one `read()`
* TCP packet fragmentation
* Request pipelining

---

# Non-Blocking I/O

The server sets sockets to non-blocking mode using:

```cpp
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

This means a socket operation does not block the entire server while waiting for data.

For example, when there is currently no data available:

```text
read()
  |
  +---- EAGAIN / EWOULDBLOCK
             |
             v
        Return to poll()
```

The server can then continue monitoring other clients.

---

# Read Buffer

Each client maintains its own read buffer:

```cpp
char rbuf[4 + k_max_msg];
```

Incoming bytes are appended to this buffer.

The server only processes a request after the complete length-prefixed message has arrived.

This is important because one `read()` call may receive:

```text
[SET nam
```

while a later call receives:

```text
e Subhankar]
```

The server combines these bytes in the read buffer before processing the command.

---

# Write Buffer

The server also maintains a per-client write buffer:

```cpp
char wbuf[4 + k_max_msg];
```

A call to `write()` is not guaranteed to send the entire response.

For example:

```text
Response:
[100 bytes]

write()
   |
   +---- sends 40 bytes
   |
   +---- 60 bytes remain
```

The server tracks the number of bytes already sent using:

```cpp
size_t wbuf_sent;
```

When the socket becomes writable again, the remaining data is sent.

---

# Request Pipelining

The server can process multiple complete requests stored in the same read buffer.

For example:

```text
SET name Subhankar
GET name
PING
```

may arrive together.

The server processes each complete request sequentially:

```text
Request 1 → SET
Request 2 → GET
Request 3 → PING
```

After processing a request, remaining data is moved within the buffer using `memmove()`.

---

# In-Memory Database

The database is implemented using:

```cpp
static unordered_map<string, string> g_map;
```

Example:

```text
Key         Value
-------------------------
name        Subhankar
language    C++
city        Kolkata
```

A command such as:

```text
SET language C++
```

stores:

```cpp
g_map["language"] = "C++";
```

A `GET` operation performs a lookup using:

```cpp
g_map.find(key);
```

Since the database is entirely memory-based, all stored data is lost when the server process terminates.

---

# Performance Benchmark

A local benchmark was performed using **100,000 `SET` requests**.

### Benchmark Result

| Metric          |                  Result |
| --------------- | ----------------------: |
| Requests        |                 100,000 |
| Operation       |                   `SET` |
| Total Time      |         14.6186 seconds |
| Throughput      |  **6,840 requests/sec** |
| Average Latency | **0.146186 ms/request** |

### Benchmark Output

```text
Benchmarking 100000 SET requests...

--- Benchmark Results ---
Total Time:       14.6186 seconds
Throughput:       6840 Requests/Sec (RPS)
Avg Latency:      0.146186 ms per request
```

This benchmark demonstrates the current performance of the server for sequential local `SET` request/response operations.

> **Note:** This is a local development benchmark and should not be directly compared with production Redis performance. The benchmark is intended to measure the performance of this implementation and provide a baseline for future optimizations.

---

# Project Structure

```text
Custom-Redis/
│
├── server.cpp
├── client.cpp
├── benchmark.cpp
├── README.md
└── .gitignore
```

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

# Compilation

Clone the repository and enter the project directory:

```bash
git clone <your-repository-url>
cd Custom-Redis
```

Compile the server:

```bash
g++ -std=c++17 -Wall -Wextra -O2 server.cpp -o server
```

Compile the client:

```bash
g++ -std=c++17 -Wall -Wextra -O2 client.cpp -o client
```

Compile the benchmark:

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

Now enter commands interactively.

Example:

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
DEL name

server says: (integer) 1
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

Example output:

```text
Benchmarking 100000 SET requests...

--- Benchmark Results ---
Total Time:       14.6186 seconds
Throughput:       6840 Requests/Sec (RPS)
Avg Latency:      0.146186 ms per request
```

For meaningful comparisons, run benchmarks under the same machine and system conditions.

---

# Multiple Clients

The server is designed to handle multiple simultaneous connections using `poll()`.

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

All clients connect to the same server and share the same in-memory key-value store.

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

# Important System Calls

The project uses several Linux/POSIX system calls and APIs.

### `socket()`

Creates the TCP socket.

```cpp
socket(AF_INET, SOCK_STREAM, 0);
```

### `bind()`

Associates the socket with port `1234`.

### `listen()`

Places the server socket into listening mode.

### `accept()`

Accepts incoming client connections.

### `fcntl()`

Configures sockets for non-blocking I/O.

### `poll()`

Monitors multiple sockets for I/O events.

### `read()`

Reads incoming bytes from clients.

### `write()`

Sends response bytes to clients.

### `close()`

Closes sockets when clients disconnect.

---

# Error Handling

The server handles common errors associated with non-blocking sockets.

### `EAGAIN` / `EWOULDBLOCK`

Indicates that an operation would block because the socket is not currently ready.

The server waits for the next appropriate `poll()` event.

### `EINTR`

Indicates that a system call was interrupted by a signal.

The server retries the operation.

### `POLLHUP`

Indicates that a client has disconnected.

The server closes and removes the corresponding connection.

---

# Current Limitations

This project implements a small subset of Redis functionality and is primarily intended for learning and experimentation.

Current limitations include:

* No persistence
* Data is lost when the server stops
* Maximum message size is 4096 bytes
* Maximum of 100 client connections
* No authentication
* No Redis RESP protocol
* No replication
* No transactions
* No TTL/expiration
* Limited command set
* Single-threaded event loop
* No production-level memory management or eviction policy

---

# Future Improvements

Possible future improvements include:

* Implement the Redis RESP protocol
* Add more Redis commands
* Add `EXPIRE` and TTL support
* Add persistent storage
* Implement an append-only log
* Add snapshot-based persistence
* Improve command parsing
* Add `epoll()` support
* Add connection timeouts
* Add automated unit and integration tests
* Add concurrent/multi-client benchmarking
* Add request pipelining benchmarks
* Add performance metrics
* Add graceful server shutdown
* Add configuration support

---

# Learning Outcomes

This project provided practical experience with:

* TCP/IP networking
* Client-server architecture
* Linux socket programming
* Non-blocking I/O
* Event-driven programming
* `poll()` and I/O multiplexing
* TCP stream behavior
* Partial reads and writes
* Read/write buffering
* Request pipelining
* Length-prefixed protocols
* Error handling with `errno`
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
BSD/POSIX Sockets
poll()
fcntl()
Non-blocking I/O
std::unordered_map
```

---

# Project Goal

The goal of this project is to understand the core concepts behind an in-memory networked data store by implementing the server, networking layer, protocol framing, buffering, command processing, and client communication from scratch rather than relying on an existing Redis server implementation.

This project is a learning-focused Redis-inspired server and is **not intended to be a drop-in replacement for Redis**.

---

## License

This project is intended for educational and learning purposes.
