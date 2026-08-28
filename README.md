# Custom Redis Server

A lightweight **Redis-compatible in-memory key-value server written from scratch in C++17**.

This project was built to understand how an in-memory database such as Redis works internally, including networking, request parsing, hash tables, TTL/expiration management, and event-driven I/O.

> **Note:** This is an educational Redis-like implementation, not a replacement for production Redis.

---

## Features

### 1. In-Memory Key-Value Storage
- Stores keys and values directly in memory.
- Supports string keys and string values.
- Fast lookup, insertion, and deletion using a custom hash table.

### 2. Custom Hash Table
The project implements its own hash table instead of using `std::unordered_map`.

It includes:
- Custom hash function based on the FNV-1a style algorithm.
- Separate chaining for collision handling.
- Key lookup and deletion.
- Dynamic table resizing.
- Incremental rehashing to avoid doing the entire resize operation at once.

The hash table uses two tables during resizing:
- `ht1` — the new/current table.
- `ht2` — the old table being gradually migrated.

A limited amount of rehashing work is performed during normal operations.

### 3. TTL and Key Expiration
The server supports key expiration using:

```text
EXPIRE key seconds
TTL key
```

A custom **min-heap** is used to efficiently track the next key that should expire.

This allows the server to:
- Add expiration timers.
- Update existing timers.
- Remove timers when keys are deleted or overwritten.
- Automatically delete expired keys.

### 4. Event-Driven Networking
The server uses Linux:

- TCP sockets
- Non-blocking sockets
- `epoll`
- `EPOLLIN`
- `EPOLLOUT`
- `TCP_NODELAY`

Instead of creating a thread for every client, the server uses an event-driven architecture to handle multiple connections.

### 5. RESP Protocol Support
The server implements basic Redis Serialization Protocol (RESP) request parsing and response generation.

It supports both:
- Standard RESP array commands used by `redis-cli` and `redis-benchmark`.
- Simple inline commands for testing with tools such as `telnet`/manual TCP clients.

Example RESP request:

```text
*2\r\n
$3\r\n
GET\r\n
$3\r\n
foo\r\n
```

### 6. Non-Blocking I/O
Client sockets are configured as non-blocking.

The server correctly handles common conditions such as:

```text
EAGAIN
EWOULDBLOCK
EINTR
EOF
```

This allows the event loop to continue processing other clients instead of blocking on a single connection.

---

## Supported Commands

| Command | Description |
|---|---|
| `PING` | Test whether the server is responding |
| `SET key value` | Store a value |
| `GET key` | Retrieve a value |
| `DEL key` | Delete a key |
| `EXPIRE key seconds` | Set a TTL on a key |
| `TTL key` | Get remaining TTL |
| `COMMAND` | Basic compatibility response for clients such as `redis-cli` |

### Examples

```text
SET name Subhankar
GET name
DEL name
```

TTL example:

```text
SET session abc123
EXPIRE session 60
TTL session
```

---

## Project Structure

```text
Custom-Redis/
│
├── src/
│   ├── server.cpp
│   ├── hashtable.cpp
│   ├── hashtable.h
│   ├── heap.cpp
│   ├── heap.h
│   └── utils.h
│
├── Makefile
│
├── obj/
│   └── generated object files
│
├── server
├── client
└── benchmark
```

### File Responsibilities

#### `server.cpp`
Main Redis-like server implementation.

Handles:
- TCP server setup
- Client connections
- `epoll` event loop
- Request parsing
- RESP serialization
- Redis commands
- TTL processing
- Client read/write operations

#### `hashtable.h / hashtable.cpp`
Custom hash table implementation.

Provides:

```cpp
hm_lookup()
hm_insert()
hm_pop()
```

Also implements incremental hash table resizing.

#### `heap.h / heap.cpp`
Custom binary min-heap implementation used for key expiration.

Provides:

```cpp
heap_up()
heap_down()
heap_update()
```

#### `utils.h`
Utility functions for:
- Monotonic time in microseconds.
- Error handling.
- Setting sockets to non-blocking mode.

#### `Makefile`
Automates compilation of the project using `g++`.

---

# Build Instructions

## Requirements

Linux or WSL Ubuntu is recommended.

Install the compiler and build tools:

```bash
sudo apt update
sudo apt install g++ make
```

The project uses:

```text
C++17
Linux sockets
epoll
```

---

## Compile

From the project root:

```bash
make
```

This builds:

```text
server
client
benchmark
```

Object files are placed inside:

```text
obj/
```

---

## Run the Server

Start the server with:

```bash
./server
```

The server listens on:

```text
0.0.0.0:1234
```

You should see:

```text
RESP Server listening on port 1234 using epoll...
```

---

# Testing With redis-cli

If `redis-cli` is installed, connect to the custom server:

```bash
redis-cli -p 1234
```

Then test:

```text
127.0.0.1:1234> PING
PONG

127.0.0.1:1234> SET name Subhankar
OK

127.0.0.1:1234> GET name
"Subhankar"

127.0.0.1:1234> DEL name
(integer) 1
```

### Test TTL

```text
127.0.0.1:1234> SET user 123
OK

127.0.0.1:1234> EXPIRE user 10
(integer) 1

127.0.0.1:1234> TTL user
(integer) 10
```

After the TTL expires:

```text
127.0.0.1:1234> GET user
(nil)
```

---

# Benchmark

The server was benchmarked using the standard Redis benchmarking tool:

```bash
redis-benchmark -p 1234 -t set,get -n 100000 -c 50 -q
```

### Benchmark Configuration

| Parameter | Value |
|---|---:|
| Operations | 100,000 |
| Concurrent clients | 50 |
| Commands | SET, GET |
| Server port | 1234 |
| Benchmark tool | `redis-benchmark` |

### Results

```text
WARNING: Could not fetch server CONFIG
SET: 92165.90 requests per second, p50=0.239 msec
GET: 118203.30 requests per second, p50=0.199 msec
```

### Summary

| Operation | Throughput | p50 Latency |
|---|---:|---:|
| SET | **92,165.90 req/s** | **0.239 ms** |
| GET | **118,203.30 req/s** | **0.199 ms** |

The `CONFIG` warning is expected because this custom server does not implement Redis's `CONFIG` command. The benchmark still successfully executed the `SET` and `GET` tests.

> Benchmark numbers depend on CPU, OS, WSL configuration, compiler optimization, background processes, and other system conditions. They should be treated as results from this particular test environment rather than a direct comparison with production Redis.

---

# Makefile Commands

Build everything:

```bash
make
```

Clean compiled files:

```bash
make clean
```

Build again after cleaning:

```bash
make clean
make
```

The Makefile compiles with:

```text
-O3
-Wall
-std=c++17
```

`-O3` enables aggressive compiler optimizations, while `-Wall` enables common compiler warnings.

---

# Architecture

The high-level architecture is:

```text
                    Client
                      |
                      | TCP
                      v
              +---------------+
              |  Non-blocking  |
              |     Socket     |
              +-------+-------+
                      |
                      v
              +---------------+
              |     epoll      |
              |  Event Loop    |
              +-------+-------+
                      |
             +--------+--------+
             |                 |
             v                 v
       Request Parser     Response Buffer
             |
             v
      Command Execution
             |
       +-----+------+
       |            |
       v            v
   Hash Table    Min Heap
       |            |
       |            |
       v            v
   Key/Value     Expiration
     Store         Timers
```

---

# Data Structures

## Hash Table

```text
Hash(key)
   |
   v
Bucket
   |
   +---- Entry
   |
   +---- Entry
   |
   +---- Entry
```

Collisions are handled using linked lists through `HNode::next`.

During resizing:

```text
          HMap
        /      \
      ht1      ht2
      |         |
   new table   old table
                 |
                 v
          incremental migration
```

This avoids moving every key in a single expensive operation.

## Min-Heap

Expiration times are stored in a binary min-heap:

```text
             earliest
                |
        +-------+-------+
        |               |
       ...             ...
```

The smallest expiration timestamp remains at the root, allowing the server to efficiently determine which key should expire next.

---

# Design Highlights

This project focuses on implementing important systems concepts rather than relying entirely on high-level library abstractions.

### Concepts implemented

- TCP networking
- Non-blocking sockets
- Linux `epoll`
- Event-driven server architecture
- RESP protocol parsing
- RESP response serialization
- Custom hash table
- Hash collision handling
- Incremental rehashing
- Binary min-heap
- TTL management
- Monotonic clocks
- Dynamic memory management
- Makefile-based C++ builds

---

# Limitations

This project intentionally implements only a subset of Redis functionality.

It currently does **not** aim to provide:

- Persistence/RDB
- AOF
- Replication
- Redis Cluster
- Authentication
- Pub/Sub
- Transactions
- Lists/Sets/Sorted Sets
- Lua scripting
- Full Redis command compatibility
- Production-grade memory management
- Full `CONFIG` command support

The goal is to understand how the core pieces of an in-memory key-value server work.

---

# Future Improvements

Possible improvements include:

- [ ] Implement more Redis commands
- [ ] Add `MGET` / `MSET`
- [ ] Add multiple data types
- [ ] Add persistence using RDB
- [ ] Add AOF logging
- [ ] Add authentication
- [ ] Improve protocol parsing
- [ ] Add pipelining support
- [ ] Add connection limits
- [ ] Add memory usage statistics
- [ ] Add automated tests
- [ ] Add more detailed benchmarking
- [ ] Add graceful server shutdown
- [ ] Improve error handling and malformed-request handling

---

# Why I Built This

I built this project to learn how an in-memory database and network server work internally instead of only using existing database/server libraries.

The project gave me hands-on experience with:

- Systems programming in C++
- Network programming
- Linux I/O
- Data structures
- Event-driven architecture
- Memory management
- Hash tables and rehashing
- Heaps and timer management
- Protocol design
- Performance benchmarking

---

# Tech Stack

- **Language:** C++17
- **Networking:** Linux TCP Sockets
- **I/O Multiplexing:** `epoll`
- **Data Structures:** Custom Hash Table + Binary Min-Heap
- **Protocol:** RESP
- **Build:** Make + g++
- **Testing/Benchmarking:** `redis-cli`, `redis-benchmark`
- **Environment:** Linux / WSL Ubuntu

---

## License

This project is intended for educational and learning purposes.
