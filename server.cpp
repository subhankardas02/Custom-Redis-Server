#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <assert.h>

#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>

using namespace std;

const size_t k_max_msg = 4096;
const int MAX_CONNECTIONS = 100;

static unordered_map<string, string> g_map;

// --- STEP 1: ADD TLV TAGS & BUFFER ALIAS ---
typedef vector<uint8_t> Buffer;

enum {
    TAG_NIL = 0,    // nil
    TAG_ERR = 1,    // error code + msg
    TAG_STR = 2,    // string
    TAG_INT = 3,    // int64
    TAG_ARR = 5,    // array
};

void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void fd_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) die("fcntl(F_GETFL)");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) die("fcntl(F_SETFL)");
}

// --- STEP 1: UPDATE CONN STRUCT ---
struct Conn {
    int fd;
    char rbuf[4 + k_max_msg];
    size_t rbuf_size = 0;
    
    // We replaced the fixed char array with a dynamic vector for binary data
    Buffer outgoing; 
};

// --- STEP 1: ADD SERIALIZATION HELPER FUNCTIONS ---
static void buf_append_u8(Buffer &buf, uint8_t data) {
    buf.push_back(data);
}
static void buf_append_u32(Buffer &buf, uint32_t data) {
    uint8_t bytes[4];
    memcpy(bytes, &data, 4);
    buf.insert(buf.end(), bytes, bytes + 4);
}
static void buf_append_i64(Buffer &buf, int64_t data) {
    uint8_t bytes[8];
    memcpy(bytes, &data, 8);
    buf.insert(buf.end(), bytes, bytes + 8);
}

static void out_nil(Buffer &out) {
    buf_append_u8(out, TAG_NIL);
}
static void out_str(Buffer &out, const char *s, size_t size) {
    buf_append_u8(out, TAG_STR);
    buf_append_u32(out, (uint32_t)size);
    out.insert(out.end(), (const uint8_t*)s, (const uint8_t*)s + size);
}
static void out_int(Buffer &out, int64_t val) {
    buf_append_u8(out, TAG_INT);
    buf_append_i64(out, val);
}
static void out_err(Buffer &out, int32_t code, const string &msg) {
    buf_append_u8(out, TAG_ERR);
    buf_append_u32(out, (uint32_t)code);
    buf_append_u32(out, (uint32_t)msg.size());
    out.insert(out.end(), msg.begin(), msg.end());
}


static bool handle_read(Conn *conn) {
    while (true) {
        size_t available = sizeof(conn->rbuf) - conn->rbuf_size;
        if (available == 0) return true;

        ssize_t rv = read(conn->fd, conn->rbuf + conn->rbuf_size, available);
        if (rv < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            perror("read()");
            return false;
        }
        if (rv == 0) return false;

        conn->rbuf_size += (size_t)rv;
    }
}

// --- STEP 1: UPDATE PROCESS REQUEST ---
static bool process_request(Conn *conn) {
    while (conn->rbuf_size >= 4) {
        uint32_t len = 0;
        memcpy(&len, conn->rbuf, 4);

        if (len > k_max_msg) {
            msg("message too long");
            return false;
        }

        size_t total_size = 4 + (size_t)len;
        if (conn->rbuf_size < total_size) break; 

        string_view req(conn->rbuf + 4, len);
        vector<string_view> args;
        size_t pos = 0;
        
        while (pos < req.size()) {
            while (pos < req.size() && req[pos] == ' ') pos++;
            if (pos == req.size()) break;
            size_t start = pos;
            while (pos < req.size() && req[pos] != ' ') pos++;
            args.emplace_back(req.data() + start, pos - start);
        }

        // 1. Reserve 4 bytes for the response header length
        size_t header_pos = conn->outgoing.size();
        buf_append_u32(conn->outgoing, 0); 

        // 2. Generate the binary response using TLV Helpers
        if (args.empty()) {
            out_err(conn->outgoing, 1, "ERR empty command");
        }
        else if (args[0] == "PING" || args[0] == "ping") {
            out_str(conn->outgoing, "PONG", 4);
        }
        else if ((args[0] == "GET" || args[0] == "get") && args.size() == 2) {
            auto it = g_map.find(string(args[1]));
            if (it != g_map.end()) {
                out_str(conn->outgoing, it->second.data(), it->second.size());
            } else {
                out_nil(conn->outgoing);
            }
        }
        else if ((args[0] == "SET" || args[0] == "set") && args.size() >= 3) {
            const char* val_start = args[2].data();
            const char* val_end = args.back().data() + args.back().size();
            g_map[string(args[1])] = string(val_start, val_end - val_start);
            out_nil(conn->outgoing); // Standard SET returns nil in this system
        }
        else if ((args[0] == "DEL" || args[0] == "del") && args.size() == 2) {
            size_t erased = g_map.erase(string(args[1]));
            out_int(conn->outgoing, erased);
        }
        else {
            out_err(conn->outgoing, 1, "ERR unknown command or invalid arguments");
        }

        // 3. Backfill the actual payload length into the reserved 4 bytes
        uint32_t reply_len = (uint32_t)(conn->outgoing.size() - header_pos - 4);
        memcpy(&conn->outgoing[header_pos], &reply_len, 4);

        size_t remaining = conn->rbuf_size - total_size;
        if (remaining > 0) {
            memmove(conn->rbuf, conn->rbuf + total_size, remaining);
        }
        conn->rbuf_size = remaining;
    }

    return true;
}

// --- STEP 1: UPDATE HANDLE WRITE ---
static bool handle_write(Conn *conn) {
    // Write out whatever is in the dynamic outgoing vector
    while (!conn->outgoing.empty()) {
        ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());

        if (rv < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            perror("write()");
            return false;
        }

        // Erase the bytes we just sent from the front of the vector
        conn->outgoing.erase(conn->outgoing.begin(), conn->outgoing.begin() + rv);
    }
    return true;
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket()");

    int val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) die("setsockopt()");

    fd_set_nonblocking(fd);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) die("bind()");
    if (listen(fd, SOMAXCONN) < 0) die("listen()");

    printf("Server listening on port 1234...\n");

    Conn *connections[MAX_CONNECTIONS]{};
    struct pollfd pollfds[MAX_CONNECTIONS + 1];

    while (true) {
        int nfds = 1;
        pollfds[0].fd = fd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (connections[i] == nullptr) continue;

            pollfds[nfds].fd = connections[i]->fd;
            pollfds[nfds].events = POLLIN;

            // Updated check: Poll for OUT if there is data in the vector
            if (!connections[i]->outgoing.empty()) {
                pollfds[nfds].events |= POLLOUT;
            }

            pollfds[nfds].revents = 0;
            nfds++;
        }

        int poll_rv = poll(pollfds, nfds, -1);
        if (poll_rv < 0) {
            if (errno == EINTR) continue;
            die("poll()");
        }

        if (pollfds[0].revents & POLLIN) {
            while (true) {
                struct sockaddr_in client_addr{};
                socklen_t addrlen = sizeof(client_addr);
                int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);

                if (connfd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (errno == EINTR) continue;
                    perror("accept()");
                    break;
                }

                int tcp_nodelay = 1;
                setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));

                fd_set_nonblocking(connfd);

                bool added = false;
                for (int i = 0; i < MAX_CONNECTIONS; i++) {
                    if (connections[i] == nullptr) {
                        Conn *conn = new Conn();
                        conn->fd = connfd;
                        connections[i] = conn;
                        added = true;
                        break;
                    }
                }

                if (!added) {
                    msg("Too many clients");
                    close(connfd);
                }
            }
        }

        int poll_index = 1;
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            Conn *conn = connections[i];
            if (conn == nullptr) continue;

            short revents = pollfds[poll_index].revents;
            poll_index++;
            bool alive = true;

            if (revents & (POLLERR | POLLHUP | POLLNVAL)) alive = false;

            if (alive && (revents & POLLIN)) {
                if (!handle_read(conn) || !process_request(conn)) alive = false;
            }

            if (alive && (revents & POLLOUT)) {
                if (!handle_write(conn)) alive = false;
            }

            if (!alive) {
                close(conn->fd);
                delete conn;
                connections[i] = nullptr;
            }
        }
    }

    close(fd);
    return 0;
}