#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>

#include <vector>
#include <string>
#include <string_view>

#include "utils.h"
#include "hashtable.h"
#include "heap.h"

using namespace std;

const size_t k_max_msg = 32768;
const int MAX_EVENTS = 1024;

static HMap g_map;
static vector<HeapItem> g_heap;

struct Entry {
    struct HNode node;
    string key;
    string val;
    size_t heap_idx = -1; 
};

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

static bool entry_eq(HNode *lhs, HNode *rhs) {
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

// Timer management helpers
static void entry_del_timer(Entry *ent) {
    if (ent->heap_idx == (size_t)-1) return;
    size_t pos = ent->heap_idx;
    g_heap[pos] = g_heap.back();
    *g_heap[pos].ref = pos;
    g_heap.pop_back();
    if (pos < g_heap.size()) heap_update(g_heap.data(), pos, g_heap.size());
    ent->heap_idx = -1;
}

static void entry_set_timer(Entry *ent, uint64_t expire_at) {
    if (ent->heap_idx == (size_t)-1) {
        ent->heap_idx = g_heap.size();
        g_heap.push_back({expire_at, &ent->heap_idx});
        heap_up(g_heap.data(), ent->heap_idx);
    } else {
        g_heap[ent->heap_idx].val = expire_at;
        heap_update(g_heap.data(), ent->heap_idx, g_heap.size());
    }
}

typedef vector<uint8_t> Buffer;

struct Conn {
    int fd;
    char rbuf[k_max_msg];
    size_t rbuf_size = 0;
    
    Buffer outgoing;
    size_t outgoing_pos = 0;
};

// ============================================================================
// RESP SERIALIZATION
// ============================================================================
static void out_string(Buffer &out, const string &s) {
    out.insert(out.end(), s.begin(), s.end());
}

static void out_pong(Buffer &out) {
    out_string(out, "+PONG\r\n");
}

static void out_ok(Buffer &out) {
    out_string(out, "+OK\r\n");
}

static void out_nil(Buffer &out) {
    out_string(out, "$-1\r\n");
}

static void out_str(Buffer &out, const char *s, size_t size) {
    string res = "$" + std::to_string(size) + "\r\n" + string(s, size) + "\r\n";
    out_string(out, res);
}

static void out_int(Buffer &out, int64_t val) {
    string res = ":" + std::to_string(val) + "\r\n";
    out_string(out, res);
}

static void out_err(Buffer &out, const string &msg) {
    string res = "-ERR " + msg + "\r\n";
    out_string(out, res);
}

// ============================================================================
// NETWORK & COMMAND PARSING
// ============================================================================
static bool handle_read(Conn *conn) {
    while (true) {
        size_t available = sizeof(conn->rbuf) - conn->rbuf_size;
        if (available == 0) return false; // Buffer full

        ssize_t rv = read(conn->fd, conn->rbuf + conn->rbuf_size, available);
        if (rv < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            return false;
        }
        if (rv == 0) return false; // EOF
        conn->rbuf_size += (size_t)rv;
    }
}

static bool process_request(Conn *conn) {
    size_t processed = 0;
    
    while (conn->rbuf_size - processed > 0) {
        const char* data = conn->rbuf + processed;
        size_t size = conn->rbuf_size - processed;
        
        // Find first CRLF
        size_t crlf = (size_t)-1;
        for (size_t i = 0; i + 1 < size; ++i) {
            if (data[i] == '\r' && data[i+1] == '\n') {
                crlf = i; break;
            }
        }
        if (crlf == (size_t)-1) break; // Incomplete message, wait for more data

        vector<string> args;
        size_t cmd_len = 0;

        // Standard RESP Array (e.g. redis-cli / redis-benchmark)
        if (data[0] == '*') {
            long argc = 0;
            try { argc = std::stol(string(data + 1, crlf - 1)); } 
            catch (...) { return false; } // Malformed

            size_t pos = crlf + 2;
            bool complete = true;
            
            for (long i = 0; i < argc; ++i) {
                if (pos >= size || data[pos] != '$') { complete = false; break; }
                
                size_t next_crlf = (size_t)-1;
                for (size_t j = pos; j + 1 < size; ++j) {
                    if (data[j] == '\r' && data[j+1] == '\n') {
                        next_crlf = j; break;
                    }
                }
                if (next_crlf == (size_t)-1) { complete = false; break; }

                long len = 0;
                try { len = std::stol(string(data + pos + 1, next_crlf - pos - 1)); } 
                catch (...) { return false; }
                
                pos = next_crlf + 2;
                if (pos + len + 2 > size) { complete = false; break; }
                
                args.emplace_back(data + pos, len);
                
                // Verify trailing CRLF
                if (data[pos + len] != '\r' || data[pos + len + 1] != '\n') return false;
                pos += len + 2;
            }
            if (!complete) break; // Wait for more data
            cmd_len = pos;
        } 
        // Fallback: Inline commands (e.g. raw telnet: "SET key val\r\n")
        else {
            string line(data, crlf);
            size_t start = 0;
            while (start < line.size()) {
                while (start < line.size() && line[start] == ' ') start++;
                if (start == line.size()) break;
                size_t end = start;
                while (end < line.size() && line[end] != ' ') end++;
                args.push_back(line.substr(start, end - start));
                start = end;
            }
            cmd_len = crlf + 2;
        }

        // --- COMMAND EXECUTION ---
        if (!args.empty()) {
            string cmd = args[0];
            for(auto &c : cmd) c = toupper(c); // Case insensitive

            Entry dummy_key;
            if (args.size() >= 2) {
                dummy_key.key = args[1];
                dummy_key.node.hcode = str_hash((uint8_t *)dummy_key.key.data(), dummy_key.key.size());
            }

            if (cmd == "PING") {
                out_pong(conn->outgoing);
            }
            else if (cmd == "GET" && args.size() == 2) {
                HNode *node = hm_lookup(&g_map, &dummy_key.node, &entry_eq);
                if (node) {
                    const string &val = container_of(node, Entry, node)->val;
                    out_str(conn->outgoing, val.data(), val.size());
                } else out_nil(conn->outgoing);
            }
            else if (cmd == "SET" && args.size() >= 3) {
                HNode *node = hm_lookup(&g_map, &dummy_key.node, &entry_eq);
                if (node) {
                    Entry *ent = container_of(node, Entry, node);
                    ent->val = args[2];
                    entry_del_timer(ent); 
                } else {
                    Entry *ent = new Entry();
                    ent->key = dummy_key.key;
                    ent->node.hcode = dummy_key.node.hcode;
                    ent->val = args[2];
                    hm_insert(&g_map, &ent->node);
                }
                out_ok(conn->outgoing);
            }
            else if (cmd == "DEL" && args.size() == 2) {
                HNode *node = hm_pop(&g_map, &dummy_key.node, &entry_eq);
                if (node) {
                    Entry *ent = container_of(node, Entry, node);
                    entry_del_timer(ent); 
                    delete ent;
                    out_int(conn->outgoing, 1);
                } else out_int(conn->outgoing, 0);
            }
            else if (cmd == "EXPIRE" && args.size() == 3) {
                try {
                    int64_t ttl_sec = std::stoll(args[2]);
                    HNode *node = hm_lookup(&g_map, &dummy_key.node, &entry_eq);
                    if (node) {
                        Entry *ent = container_of(node, Entry, node);
                        uint64_t expire_at = get_monotonic_usec() + (uint64_t)ttl_sec * 1000000;
                        entry_set_timer(ent, expire_at);
                        out_int(conn->outgoing, 1);
                    } else out_int(conn->outgoing, 0);
                } catch (...) { out_err(conn->outgoing, "value is not an integer or out of range"); }
            }
            else if (cmd == "TTL" && args.size() == 2) {
                HNode *node = hm_lookup(&g_map, &dummy_key.node, &entry_eq);
                if (node) {
                    Entry *ent = container_of(node, Entry, node);
                    if (ent->heap_idx == (size_t)-1) out_int(conn->outgoing, -1);
                    else {
                        uint64_t now = get_monotonic_usec();
                        uint64_t expire_at = g_heap[ent->heap_idx].val;
                        if (expire_at < now) expire_at = now;
                        out_int(conn->outgoing, (int64_t)((expire_at - now) / 1000000));
                    }
                } else out_int(conn->outgoing, -2);
            }
            else if (cmd == "COMMAND") {
                // redis-cli sends this automatically on connect. Just say OK to silence it.
                out_ok(conn->outgoing); 
            }
            else {
                out_err(conn->outgoing, "unknown command '" + cmd + "'");
            }
        }
        processed += cmd_len;
    }
    
    if (processed > 0) {
        size_t remaining = conn->rbuf_size - processed;
        if (remaining > 0) {
            memmove(conn->rbuf, conn->rbuf + processed, remaining);
        }
        conn->rbuf_size = remaining;
    }
    return true;
}

static bool handle_write(Conn *conn) {
    while (conn->outgoing_pos < conn->outgoing.size()) {
        ssize_t rv = write(conn->fd, 
                           conn->outgoing.data() + conn->outgoing_pos, 
                           conn->outgoing.size() - conn->outgoing_pos);
        if (rv < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            return false;
        }
        conn->outgoing_pos += rv;
    }
    
    if (conn->outgoing_pos == conn->outgoing.size()) {
        conn->outgoing.clear();
        conn->outgoing_pos = 0;
    }
    return true;
}

static void process_timers() {
    uint64_t now = get_monotonic_usec();
    while (!g_heap.empty()) {
        HeapItem *top = &g_heap[0];
        if (top->val > now) break;

        Entry *ent = container_of(top->ref, Entry, heap_idx);
        HNode *node = hm_pop(&g_map, &ent->node, &entry_eq);
        assert(node == &ent->node);
        
        entry_del_timer(ent);
        delete ent;
    }
}

static void update_epoll(int epoll_fd, Conn *conn) {
    struct epoll_event ev{};
    ev.data.ptr = conn;
    ev.events = EPOLLIN;
    if (conn->outgoing.size() > 0) ev.events |= EPOLLOUT;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket()");
    
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    fd_set_nonblocking(fd);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) die("bind()");
    if (listen(fd, SOMAXCONN) < 0) die("listen()");

    printf("RESP Server listening on port 1234 using epoll...\n");

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) die("epoll_create1");

    struct epoll_event ev{}, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) die("epoll_ctl(listen)");

    while (true) {
        int timeout_ms = -1;
        if (!g_heap.empty()) {
            uint64_t now = get_monotonic_usec();
            uint64_t next = g_heap[0].val;
            timeout_ms = (next <= now) ? 0 : (int)((next - now) / 1000);
        }

        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout_ms);
        if (n_events < 0) {
            if (errno == EINTR) continue;
            die("epoll_wait()");
        }

        for (int i = 0; i < n_events; i++) {
            if (events[i].data.fd == fd) {
                while (true) {
                    struct sockaddr_in client_addr{};
                    socklen_t addrlen = sizeof(client_addr);
                    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
                    if (connfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        break;
                    }
                    int tcp_nodelay = 1;
                    setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));
                    fd_set_nonblocking(connfd);

                    Conn *conn = new Conn();
                    conn->fd = connfd;
                    
                    struct epoll_event conn_ev{};
                    conn_ev.events = EPOLLIN;
                    conn_ev.data.ptr = conn;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connfd, &conn_ev);
                }
            } else {
                Conn *conn = (Conn *)events[i].data.ptr;
                bool alive = true;

                if (events[i].events & EPOLLIN) {
                    if (!handle_read(conn) || !process_request(conn)) alive = false;
                }
                if (alive && (events[i].events & EPOLLOUT)) {
                    if (!handle_write(conn)) alive = false;
                }
                
                if (alive) {
                    update_epoll(epoll_fd, conn);
                } else {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
                    close(conn->fd);
                    delete conn;
                }
            }
        }
        process_timers(); 
    }
    close(fd);
    return 0;
}