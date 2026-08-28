# Custom Redis Server

A lightweight **Redis-compatible in-memory key-value server written from scratch in C++17**.

This project was built to understand how an in-memory database such as Redis works internally, including networking, request parsing, hash tables, TTL/expiration management, and event-driven I/O.

> **Note:** This is an educational Redis-like implementation, not a replacement for production Redis.

---

## Features

### In-Memory Key-Value Storage
- Stores keys and values directly in memory.
- Supports string keys and string values.
- Uses a custom hash table for fast lookup, insertion, and deletion.

### Custom Hash Table
The project implements its own hash table instead of using `std::unordered_map`.

It includes:
- Custom hash function.
- Separate chaining for collision handling.
- Key lookup and deletion.
- Dynamic table resizing.
- Incremental rehashing.

During resizing, two hash tables are used:

- `ht1` — new/current table.
- `ht2` — old table being gradually migrated.

A limited amount of rehashing work is performed during normal operations rather than moving all entries at once.

### TTL and Key Expiration
The server supports key expiration using:

```text
EXPIRE key seconds
TTL key
```

A custom **binary min-heap** is used to track key expiration times.

The server can:
- Add expiration timers.
- Update existing timers.
- Remove timers when keys are deleted or overwritten.
- Automatically delete expired keys.

### Event-Driven Networking
The server uses Linux:

- TCP sockets
- Non-blocking sockets
- `epoll`
- `EPOLLIN`
- `EPOLLOUT`
- `TCP_NODELAY`

Instead of creating a thread for every client, the server uses an event-driven architecture to handle multiple connections.

### RESP Protocol Support
The server implements basic Redis Serialization Protocol (RESP) request parsing and response generation.

It supports:
- RESP array commands used by `redis-cli` and `redis-benchmark`.
- Inline commands for simple manual testing.

### Non-Blocking I/O
Client sockets are configured as non-blocking.

The server handles common conditions such as:

```text
EAGAIN
EWOULDBLOCK
EINTR
EOF
```

This allows the event loop to continue processing other clients without blocking on a single connection.

---

## Supported Commands

| Command | Description |
|---|---|
| `PING` | Check whether the server is responding |
| `SET key value` | Store a value |
| `GET key` | Retrieve a value |
| `DEL key` | Delete a key |
| `EXPIRE key seconds` | Set a TTL on a key |
| `TTL key` | Get remaining TTL |
| `COMMAND` | Basic compatibility response for clients such as `redis-cli` |

### Example

```text
SET name Subhankar
GET name
DEL name
```

### TTL Example

```text
SET session abc123
EXPIRE session 60
TTL session
```

After the TTL expires:

```text
GET session
(nil)
```

---

# Project Structure

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

It also implements incremental hash table resizing.

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
Automates compilation using `g++`.

---

# Build Instructions

## Requirements

The project is developed and tested in a **Linux/WSL Ubuntu environment**.

Required tools:

```text
g++
make
redis-cli
redis-benchmark
```

The project uses:

```text
C++17
Linux TCP sockets
Linux epoll
```

On Ubuntu/WSL, install the required build tools with:

```bash
sudo apt update
sudo apt install g++ make redis-tools
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

Object files are generated inside:

```text
obj/
```

---

# Run the Server

Start the server:

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

Keep the server running in one terminal.

---

# Test With redis-cli

Open another terminal and run:

```bash
redis-cli -p 1234
```

Then:

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

After the key expires:

```text
127.0.0.1:1234> GET user
(nil)
```

---

# Benchmark

The server was benchmarked using `redis-benchmark`.

## Benchmark Environment

| Property | Value |
|---|---|
| Environment | Linux / WSL Ubuntu |
| Language | C++17 |
| Compiler | `g++` |
| Compiler Optimization | `-O3` |
| Build System | GNU Make |
| Networking | Linux TCP sockets + `epoll` |
| Benchmark Tool | `redis-benchmark` |
| Server Port | `1234` |
| Requests | `100,000` |
| Benchmark Mode | Quiet (`-q`) |
| Commands | `SET`, `GET`, `PING` |

The benchmark was executed with:

```bash
redis-benchmark -p 1234 -t set,get,ping -n 100000 -q
```

### Benchmark Results

```text
WARNING: Could not fetch server CONFIG
PING_INLINE: 126742.72 requests per second, p50=0.207 msec
PING_MBULK: 126582.27 requests per second, p50=0.207 msec
SET: 122549.02 requests per second, p50=0.207 msec
GET: 129701.68 requests per second, p50=0.199 msec
```

### Results Summary

| Operation | Throughput | p50 Latency |
|---|---:|---:|
| PING_INLINE | **126,742.72 req/s** | **0.207 ms** |
| PING_MBULK | **126,582.27 req/s** | **0.207 ms** |
| SET | **122,549.02 req/s** | **0.207 ms** |
| GET | **129,701.68 req/s** | **0.199 ms** |

### Benchmark Notes

The benchmark used:

- **100,000 total requests**
- `SET`, `GET`, and `PING` commands
- The server running locally on port `1234`
- Optimized C++17 compilation with `-O3`
- Linux/WSL Ubuntu
- `redis-benchmark` in quiet mode

The `CONFIG` warning appears because the custom server does not implement Redis's `CONFIG` command. It does **not** prevent the `SET`, `GET`, or `PING` benchmark tests from running.

> Benchmark results depend on CPU, operating system, WSL configuration, background processes, compiler version, and other system conditions. These numbers represent the result from this development environment and should not be treated as a direct comparison with production Redis.

---

# Architecture

```text
                         Client
                           |
                           | TCP
                           v
                  +-------------------+
                  | Non-blocking Socket|
                  +---------+---------+
                            |
                            v
                  +-------------------+
                  |       epoll       |
                  |    Event Loop      |
                  +---------+---------+
                            |
                   +--------+--------+
                   |                 |
                   v                 v
             Request Parser    Response Buffer
                   |
                   v
             Command Handler
                   |
             +-----+------+
             |            |
             v            v
        Hash Table     Min Heap
             |            |
             v            v
        Key / Value    Expiration
           Store         Timers
```

---

# Data Structures

## Custom Hash Table

Keys are hashed and mapped to buckets:

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

Collisions are handled using linked-list chaining through `HNode::next`.

### Incremental Rehashing

During resizing:

```text
                HMap
              /      \
            ht1      ht2
             |        |
        new table   old table
                       |
                       v
              incremental migration
```

Instead of migrating every key in one operation, entries are gradually moved from `ht2` to `ht1`.

This reduces the chance of a large pause during hash table resizing.

---

## Min-Heap for TTL

Expiration timestamps are stored in a binary min-heap:

```text
                earliest
                   |
           +-------+-------+
           |               |
          ...             ...
```

The smallest expiration timestamp stays at the root, allowing the server to efficiently find the next key that needs to expire.

---

# Makefile

The project uses a Makefile to automate compilation.

Build:

```bash
make
```

Clean:

```bash
make clean
```

Rebuild:

```bash
make clean
make
```

The project is compiled using:

```text
-O3
-Wall
-std=c++17
```

Where:

- `-O3` enables compiler optimizations.
- `-Wall` enables common compiler warnings.
- `-std=c++17` enables C++17 features.

---

# Design Highlights

This project focuses on implementing important systems concepts instead of relying entirely on high-level abstractions.

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
- Performance benchmarking

---

# Limitations

This project currently implements only a subset of Redis functionality.

It does not currently aim to provide:

- Persistence / RDB
- AOF
- Replication
- Redis Cluster
- Authentication
- Pub/Sub
- Transactions
- Lists / Sets / Sorted Sets
- Lua scripting
- Full Redis command compatibility
- Production-grade memory management
- Full `CONFIG` command support

The goal is to understand how the core components of an in-memory key-value server work.

---

# Future Improvements

- [ ] Implement more Redis commands
- [ ] Add `MGET` / `MSET`
- [ ] Add multiple Redis data types
- [ ] Add persistence using RDB
- [ ] Add AOF logging
- [ ] Add authentication
- [ ] Improve RESP protocol parsing
- [ ] Add pipelining support
- [ ] Add connection limits
- [ ] Add memory usage statistics
- [ ] Add automated unit/integration tests
- [ ] Add more detailed benchmarking
- [ ] Add graceful server shutdown
- [ ] Improve malformed-request handling
- [ ] Add benchmark comparisons across different client counts

---

# Why I Built This

I built this project to understand how an in-memory database and network server work internally rather than only using existing database/server libraries.

The project provided hands-on experience with:

- Systems programming in C++
- Network programming
- Linux I/O
- Data structures
- Event-driven architecture
- Memory management
- Hash tables and incremental rehashing
- Binary heaps and timer management
- Protocol parsing
- Performance benchmarking

---

# Tech Stack

- **Language:** C++17
- **Networking:** Linux TCP Sockets
- **I/O Multiplexing:** `epoll`
- **Data Structures:** Custom Hash Table + Binary Min-Heap
- **Protocol:** RESP
- **Build System:** GNU Make
- **Compiler:** g++
- **Testing:** `redis-cli`
- **Benchmarking:** `redis-benchmark`
- **Environment:** Linux / WSL Ubuntu

---

## License

This project is intended for educational and learning purposes.
