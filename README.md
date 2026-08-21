# Custom Redis Server in C++

A Redis-inspired in-memory key-value server built from scratch in **C++17** using Linux/POSIX networking APIs.

The project focuses on understanding how a networked in-memory data store works internally, including TCP sockets, non-blocking I/O, `poll()`, request/response framing, buffering, event-driven connection handling, and command processing.

## Benchmark

The current implementation was benchmarked locally with **100,000 sequential `SET` request/response operations** over a single TCP connection.

| Metric | Result |
|---|---:|
| Total Requests | 100,000 |
| Operation | `SET` |
| Total Time | **8.2699 seconds** |
| Throughput | **12,092 requests/sec** |
| Average Latency | **0.082699 ms/request** |

### Benchmark Output

```text
Benchmarking 100000 SET requests...

--- Benchmark Results ---
Total Time:       8.2699 seconds
Throughput:       12092 Requests/Sec (RPS)
Avg Latency:      0.082699 ms per request
```

> **Benchmark note:** This is a local development benchmark using one client connection and sequential request/response operations. Results depend on CPU, operating system, compiler, optimization level, system load, and networking configuration. It should be treated as a performance baseline for this implementation rather than a direct comparison with production Redis.

---

## Features

- TCP client-server communication
- Linux/POSIX socket programming
- Non-blocking sockets using `fcntl()` and `O_NONBLOCK`
- Event-driven I/O using `poll()`
- Multiple simultaneous TCP connections
- Up to 100 active client connections
- Custom 4-byte length-prefixed request/response protocol
- Per-client read buffers
- Per-client write buffers
- Partial read handling
- Partial write handling
- `EINTR` handling
- `EAGAIN` / `EWOULDBLOCK` handling
- Multiple requests processed from a single read buffer
- In-memory storage using `std::unordered_map`
- Redis-style `SET`, `GET`, `DEL`, and `PING` commands
- TCP_NODELAY enabled for accepted client connections
- Client disconnect and socket error handling
- Maximum message size of 4096 bytes

---

## Architecture

The server uses a **single-threaded event loop** with `poll()` to monitor the listening socket and connected clients.

```text
                    +----------------------+
                    |   Listening Socket   |
                    |      Port 1234       |
                    +----------+-----------+
                               |
                            accept()
                               |
                               v
                    +----------------------+
                    |      poll() loop     |
                    |   Event-driven I/O   |
                    +----------+-----------+
                               |
             +-----------------+-----------------+
             |                 |                 |
             v                 v                 v
        +---------+       +---------+       +---------+
        | Client 1|       | Client 2|       | Client N|
        +---------+       +---------+       +---------+
             |                 |                 |
          POLLIN            POLLIN            POLLIN
          POLLOUT           POLLOUT           POLLOUT
```

For a readable client:

```text
POLLIN
  |
  v
read()
  |
  v
per-client read buffer
  |
  v
parse length-prefixed request
  |
  v
execute command
  |
  v
append response to write buffer
  |
  v
POLLOUT
  |
  v
write()
```

This lets one server process manage multiple connections without creating one thread per client.

---

## Supported Commands

### PING

Checks whether the server is responding.

```text
PING
```

Response:

```text
PONG
```

### SET

Stores a value associated with a key.

```text
SET name Subhankar
```

Response:

```text
OK
```

Values containing spaces are supported:

```text
SET message Hello World
```

The server stores the value as:

```text
Hello World
```

### GET

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

### DEL

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

## Example Client Session

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

## Custom Request/Response Protocol

TCP provides a continuous byte stream. It does not preserve application-level message boundaries.

To define message boundaries, this project uses a simple **length-prefixed protocol**.

Each request is encoded as:

```text
+----------------------+----------------------+
| 4-byte message size  |     Message body     |
+----------------------+----------------------+
```

For example:

```text
[4-byte length][SET name Subhankar]
```

The response uses the same structure:

```text
+----------------------+----------------------+
| 4-byte response size |    Response body     |
+----------------------+----------------------+
```

The server first determines the expected message size and then waits until the complete message is available in the connection's read buffer.

---

## Handling Partial Reads

A single TCP `read()` call is not guaranteed to return an entire application message.

For example, a request could arrive in multiple pieces:

```text
SET nam
```

followed by:

```text
e Subhankar
```

The server therefore maintains a separate read buffer for each connection:

```cpp
char rbuf[4 + k_max_msg];
```

Received bytes are accumulated until a complete length-prefixed request is available.

The same mechanism also allows multiple complete requests to be present in the buffer at the same time.

---

## Handling Partial Writes

A `write()` call may also send only part of a response.

Each connection therefore maintains a write buffer:

```cpp
char wbuf[4 + k_max_msg];
```

and tracks the number of bytes already sent:

```cpp
size_t wbuf_sent = 0;
```

If the socket cannot currently accept more data, the server waits for `POLLOUT` and continues writing when the socket becomes writable.

---

## Multiple Requests in One Read

The server processes all complete requests currently available in a client's read buffer.

For example:

```text
SET name Subhankar
GET name
PING
```

can be processed as:

```text
Request 1 -> SET
Request 2 -> GET
Request 3 -> PING
```

Any incomplete request remains in the connection's read buffer until more bytes arrive.

This is an important distinction from the benchmark's request pattern: **the benchmark currently waits for each response before sending the next request, so it is not a pipelined benchmark.**

---

## Non-Blocking I/O

The server configures its sockets as non-blocking:

```cpp
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

When no data is currently available, a non-blocking `read()` can return:

```text
EAGAIN
```

or:

```text
EWOULDBLOCK
```

These are not treated as fatal errors. The server returns to `poll()` and waits for the socket to become ready again.

This prevents a slow or idle client from blocking the entire event loop.

---

## Event Handling with `poll()`

The server monitors:

- `POLLIN` for readable sockets
- `POLLOUT` for sockets with pending response data
- `POLLERR` for socket errors
- `POLLHUP` for disconnected clients
- `POLLNVAL` for invalid file descriptors

The listening socket is also monitored for new connections.

---

## In-Memory Data Store

The key-value database is implemented using:

```cpp
static unordered_map<string, string> g_map;
```

Conceptually:

```text
Key          Value
-------------------------
name         Subhankar
language     C++
city         Kolkata
```

A command such as:

```text
SET language C++
```

stores the corresponding key and value in the hash map.

Because the database is memory-only, **all data is lost when the server process stops**.

---

## Connection Management

The server maintains an array of connection objects:

```cpp
Conn *connections[MAX_CONNECTIONS]{};
```

Each active connection stores:

- Socket file descriptor
- Read buffer
- Read buffer size
- Write buffer
- Write buffer size
- Number of response bytes already sent

The current implementation supports up to:

```text
100 simultaneous connections
```

When a client disconnects or an unrecoverable socket error occurs, the server closes the socket, frees the connection object, and releases the connection slot.

---

## TCP_NODELAY

The server enables `TCP_NODELAY` on accepted client sockets:

```cpp
int tcp_nodelay = 1;
setsockopt(
    connfd,
    IPPROTO_TCP,
    TCP_NODELAY,
    &tcp_nodelay,
    sizeof(tcp_nodelay)
);
```

The benchmark client also enables `TCP_NODELAY`.

This disables Nagle's algorithm for the benchmark connection and is useful when measuring small request/response messages where reducing packet coalescing can affect latency.

---

## Project Structure

```text
Custom-Redis-Server/
│
├── README.md
├── Makefile
├── server.cpp
├── client.cpp
└── benchmark.cpp
```

Build artifacts such as `server`, `client`, and `benchmark` should not be committed to GitHub. Add them to `.gitignore`.

Recommended `.gitignore`:

```gitignore
server
client
benchmark
*.o
```

---

## Requirements

- Linux
- GCC / G++
- C++17 or newer
- POSIX socket APIs

The project can also be built and run using **WSL on Windows**.

---

## Build

Clone the repository:

```bash
git clone https://github.com/subhankardas02/Custom-Redis-Server.git
cd Custom-Redis-Server
```

### Using Make

If the repository contains a `Makefile`:

```bash
make
```

This should build:

```text
server
client
benchmark
```

### Manual Compilation

Server:

```bash
g++ -std=c++17 -Wall -Wextra -O2 server.cpp -o server
```

Client:

```bash
g++ -std=c++17 -Wall -Wextra -O2 client.cpp -o client
```

Benchmark:

```bash
g++ -std=c++17 -Wall -Wextra -O2 benchmark.cpp -o benchmark
```

---

## Running the Server

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

## Running the Client

Open another terminal:

```bash
./client
```

Enter commands interactively:

```text
PING
SET name Subhankar
GET name
DEL name
```

---

## Running the Benchmark

Start the server first:

```bash
./server
```

Then open another terminal:

```bash
./benchmark
```

Current benchmark configuration:

```text
Requests:       100,000
Command:        SET bench_key bench_val
Connection:     1 TCP connection
Pattern:        Sequential request -> response
Location:       localhost
```

Current result:

```text
Benchmarking 100000 SET requests...

--- Benchmark Results ---
Total Time:       8.2699 seconds
Throughput:       12092 Requests/Sec (RPS)
Avg Latency:      0.082699 ms per request
```

For a meaningful comparison between different versions of the server, keep the following conditions consistent:

- Same machine
- Same compiler and optimization flags
- Same request count
- Same command
- Same client behavior
- Same background workload
- Same server configuration

---

## Multiple Clients

The server can handle multiple TCP clients through its `poll()` event loop.

For example:

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

All clients communicate with the same server process and therefore access the same in-memory key-value store.

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

## Important POSIX APIs

| API | Purpose |
|---|---|
| `socket()` | Creates a TCP socket |
| `setsockopt()` | Configures socket options |
| `bind()` | Assigns an address and port |
| `listen()` | Places the socket into listening mode |
| `accept()` | Accepts incoming connections |
| `fcntl()` | Enables non-blocking I/O |
| `poll()` | Monitors multiple file descriptors |
| `read()` | Receives bytes from a socket |
| `write()` | Sends bytes to a socket |
| `close()` | Closes a socket |

---

## Error Handling

### `EAGAIN` / `EWOULDBLOCK`

Indicates that a non-blocking socket is not currently ready for the requested operation.

The server waits for the next relevant `poll()` event instead of treating this as a fatal error.

### `EINTR`

Indicates that a system call was interrupted by a signal.

The server retries operations such as `read()`, `write()`, and `accept()` where appropriate.

### `POLLHUP`

Indicates that the peer has disconnected.

The server closes the socket and removes the connection.

---

## Current Limitations

This project implements a small Redis-like command set and is primarily intended for learning and systems programming practice.

Current limitations include:

- In-memory storage only
- Data is lost when the server stops
- Maximum message size of 4096 bytes
- Maximum of 100 simultaneous connections
- No authentication
- No Redis RESP protocol
- No persistence
- No replication
- No transactions
- No TTL / expiration
- Limited command set
- Single-threaded event loop
- No production-grade monitoring
- Benchmark currently uses one sequential client

---

## Future Improvements

Possible next steps:

- Implement the Redis RESP protocol
- Add more Redis commands
- Add `EXPIRE` and TTL support
- Add persistence
- Implement append-only logging
- Add snapshot-based persistence
- Improve command parsing
- Replace `poll()` with `epoll()` for Linux scalability experiments
- Add connection timeouts
- Add automated unit and integration tests
- Add concurrent multi-client benchmarks
- Add true pipelined benchmark tests
- Add latency percentiles such as p50, p95, and p99
- Add graceful server shutdown
- Add configuration support
- Improve response-buffer management
- Add a more complete protocol validation layer

---

## What I Learned

Building this project provided practical experience with:

- TCP/IP networking
- Client-server architecture
- Linux socket programming
- Non-blocking I/O
- Event-driven programming
- I/O multiplexing with `poll()`
- TCP byte-stream behavior
- Partial reads and partial writes
- Read/write buffering
- Length-prefixed protocols
- Request processing
- `errno`-based error handling
- In-memory data structures
- C++ systems programming
- Linux system calls
- Network server design
- Performance benchmarking

---

## Technologies

```text
C++
C++17
Linux / POSIX
TCP/IP
POSIX Sockets
poll()
fcntl()
Non-blocking I/O
TCP_NODELAY
std::unordered_map
```

---

## Project Goal

The goal of this project is to understand the core components of an in-memory networked data store by implementing the networking layer, event loop, protocol framing, buffering, command processing, client communication, and key-value storage from scratch.

This project is **Redis-inspired and educational**. It is not intended to be a drop-in replacement for Redis.

---

## License

This project is intended for educational and learning purposes.
