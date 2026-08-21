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

void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static void fd_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) die("fcntl(F_GETFL)");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) die("fcntl(F_SETFL)");
}

struct Conn
{
    int fd;
    char rbuf[4 + k_max_msg];
    size_t rbuf_size = 0;
    char wbuf[4 + k_max_msg];
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
};

static bool handle_read(Conn *conn)
{
    while (true)
    {
        size_t available = sizeof(conn->rbuf) - conn->rbuf_size;

        if (available == 0) return true;

        ssize_t rv = read(
            conn->fd,
            conn->rbuf + conn->rbuf_size,
            available);

        if (rv < 0)
        {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            perror("read()");
            return false;
        }

        if (rv == 0) return false; // Client disconnected

        conn->rbuf_size += (size_t)rv;
        // Removed printf for incoming bytes
    }
}

static bool process_request(Conn *conn)
{
    while (conn->rbuf_size >= 4)
    {
        uint32_t len = 0;
        memcpy(&len, conn->rbuf, 4);

        if (len > k_max_msg)
        {
            msg("message too long");
            return false;
        }

        size_t total_size = 4 + (size_t)len;
        if (conn->rbuf_size < total_size) break; 

        // Fast zero-copy parsing using string_view instead of stringstream
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

        string reply;
        if (args.empty())
        {
            reply = "ERR empty command";
        }
        else if (args[0] == "PING" || args[0] == "ping")
        {
            reply = "PONG";
        }
        else if ((args[0] == "GET" || args[0] == "get") && args.size() == 2)
        {
            auto it = g_map.find(string(args[1]));
            reply = (it != g_map.end()) ? it->second : "(nil)";
        }
        else if ((args[0] == "SET" || args[0] == "set") && args.size() >= 3)
        {
            const char* val_start = args[2].data();
            const char* val_end = args.back().data() + args.back().size();
            g_map[string(args[1])] = string(val_start, val_end - val_start);
            reply = "OK";
        }
        else if ((args[0] == "DEL" || args[0] == "del") && args.size() == 2)
        {
            size_t erased = g_map.erase(string(args[1]));
            reply = erased ? "(integer) 1" : "(integer) 0";
        }
        else
        {
            reply = "ERR unknown command or invalid arguments";
        }

        // Removed printf output per request

        uint32_t reply_len = (uint32_t)reply.length();
        if (conn->wbuf_size + 4 + reply_len > sizeof(conn->wbuf))
        {
            msg("wbuf overflow");
            return false;
        }

        memcpy(conn->wbuf + conn->wbuf_size, &reply_len, 4);
        memcpy(conn->wbuf + conn->wbuf_size + 4, reply.data(), reply_len);
        conn->wbuf_size += 4 + reply_len;

        size_t remaining = conn->rbuf_size - total_size;
        if (remaining > 0)
        {
            memmove(conn->rbuf, conn->rbuf + total_size, remaining);
        }
        conn->rbuf_size = remaining;
    }

    return true;
}

static bool handle_write(Conn *conn)
{
    while (conn->wbuf_sent < conn->wbuf_size)
    {
        ssize_t rv = write(
            conn->fd,
            conn->wbuf + conn->wbuf_sent,
            conn->wbuf_size - conn->wbuf_sent);

        if (rv < 0)
        {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            perror("write()");
            return false;
        }

        conn->wbuf_sent += (size_t)rv;
    }

    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    return true;
}

int main()
{
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

    while (true)
    {
        int nfds = 1;
        pollfds[0].fd = fd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        for (int i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (connections[i] == nullptr) continue;

            pollfds[nfds].fd = connections[i]->fd;
            pollfds[nfds].events = POLLIN;

            if (connections[i]->wbuf_sent < connections[i]->wbuf_size)
            {
                pollfds[nfds].events |= POLLOUT;
            }

            pollfds[nfds].revents = 0;
            nfds++;
        }

        int poll_rv = poll(pollfds, nfds, -1);
        if (poll_rv < 0)
        {
            if (errno == EINTR) continue;
            die("poll()");
        }

        if (pollfds[0].revents & POLLIN)
        {
            while (true)
            {
                struct sockaddr_in client_addr{};
                socklen_t addrlen = sizeof(client_addr);
                int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);

                if (connfd < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (errno == EINTR) continue;
                    perror("accept()");
                    break;
                }

                // Disable Nagle's Algorithm on client socket
                int tcp_nodelay = 1;
                setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));

                fd_set_nonblocking(connfd);

                bool added = false;
                for (int i = 0; i < MAX_CONNECTIONS; i++)
                {
                    if (connections[i] == nullptr)
                    {
                        Conn *conn = new Conn();
                        conn->fd = connfd;
                        connections[i] = conn;
                        added = true;
                        break;
                    }
                }

                if (!added)
                {
                    msg("Too many clients");
                    close(connfd);
                }
            }
        }

        int poll_index = 1;
        for (int i = 0; i < MAX_CONNECTIONS; i++)
        {
            Conn *conn = connections[i];
            if (conn == nullptr) continue;

            short revents = pollfds[poll_index].revents;
            poll_index++;
            bool alive = true;

            if (revents & (POLLERR | POLLHUP | POLLNVAL)) alive = false;

            if (alive && (revents & POLLIN))
            {
                if (!handle_read(conn) || !process_request(conn)) alive = false;
            }

            if (alive && (revents & POLLOUT))
            {
                if (!handle_write(conn)) alive = false;
            }

            if (!alive)
            {
                close(conn->fd);
                delete conn;
                connections[i] = nullptr;
            }
        }
    }

    close(fd);
    return 0;
}