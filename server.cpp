#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <assert.h>

// Added for the Key-Value store and string parsing
#include <unordered_map>
#include <sstream>
#include <vector>
#include <string>

using namespace std;

const size_t k_max_msg = 4096;
const int MAX_CONNECTIONS = 100;

// Our in-memory database
static unordered_map<string, string> g_map;

// Error handling

void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

// Make socket non-blocking
static void fd_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
    {
        die("fcntl(F_GETFL)");
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        die("fcntl(F_SETFL)");
    }
}

// Connection state

struct Conn
{
    int fd;
    char rbuf[4 + k_max_msg];
    size_t rbuf_size = 0;
    char wbuf[4 + k_max_msg];
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
};

// Read data from client
static bool handle_read(Conn *conn)
{
    while (true)
    {
        size_t available = sizeof(conn->rbuf) - conn->rbuf_size;

        // Buffer is full
        if (available == 0)
        {
            return true;
        }

        // Read from socket
        ssize_t rv = read(
            conn->fd,
            conn->rbuf + conn->rbuf_size,
            available);

        // Error
        if (rv < 0)
        {
            // Signal interrupted the system call
            if (errno == EINTR)
            {
                continue;
            }

            // No more data available right now
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return true;
            }

            perror("read()");
            return false;
        }

        // Client closed connection
        if (rv == 0)
        {
            printf("Client disconnected\n");
            return false;
        }

        // Successfully received data
        conn->rbuf_size += (size_t)rv;

        printf("Received %zd bytes\n", rv);
    }
}

// Process requests (Updated to handle KV commands & pipelining)
static bool process_request(Conn *conn)
{
    // Process all complete messages buffered in rbuf
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
        
        // Complete request hasn't arrived yet
        if (conn->rbuf_size < total_size)
        {
            break; 
        }

        // Extract request payload
        string req(conn->rbuf + 4, len);

        // Tokenize command arguments
        stringstream ss(req);
        vector<string> args;
        string token;
        while (ss >> token)
        {
            args.push_back(token);
        }

        // Command Execution
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
            auto it = g_map.find(args[1]);
            reply = (it != g_map.end()) ? it->second : "(nil)";
        }
        else if ((args[0] == "SET" || args[0] == "set") && args.size() >= 3)
        {
            // Join everything after the key as the value (in case value has spaces)
            string value = args[2];
            for (size_t i = 3; i < args.size(); ++i) {
                value += " " + args[i];
            }
            g_map[args[1]] = value;
            reply = "OK";
        }
        else if ((args[0] == "DEL" || args[0] == "del") && args.size() == 2)
        {
            size_t erased = g_map.erase(args[1]);
            reply = erased ? "(integer) 1" : "(integer) 0";
        }
        else
        {
            reply = "ERR unknown command or invalid arguments";
        }

        printf("Client says: %s | Server replies: %s\n", req.c_str(), reply.c_str());

        // Buffer response into wbuf
        uint32_t reply_len = (uint32_t)reply.length();
        
        if (conn->wbuf_size + 4 + reply_len > sizeof(conn->wbuf))
        {
            msg("wbuf overflow");
            return false;
        }

        // Write response length
        memcpy(conn->wbuf + conn->wbuf_size, &reply_len, 4);
        
        // Write response body
        memcpy(conn->wbuf + conn->wbuf_size + 4, reply.data(), reply_len);
        
        conn->wbuf_size += 4 + reply_len;

        // Remove processed request from rbuf
        size_t remaining = conn->rbuf_size - total_size;
        if (remaining > 0)
        {
            memmove(conn->rbuf, conn->rbuf + total_size, remaining);
        }
        conn->rbuf_size = remaining;
    }

    return true;
}

// Write response to client
static bool handle_write(Conn *conn)
{
    while (conn->wbuf_sent < conn->wbuf_size)
    {
        ssize_t rv = write(
            conn->fd,
            conn->wbuf + conn->wbuf_sent,
            conn->wbuf_size - conn->wbuf_sent);

        // Error
        if (rv < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            // Socket isn't ready for writing
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return true;
            }

            perror("write()");
            return false;
        }

        // Update number of bytes sent
        conn->wbuf_sent += (size_t)rv;
    }

    // Entire response has been sent
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;

    return true;
}

int main()
{
    // Create listening socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        die("socket()");
    }

    int val = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
    {
        die("setsockopt()");
    }

    // Make listening socket non-blocking
    fd_set_nonblocking(fd);

    // Bind
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));

    if (rv < 0)
    {
        die("bind()");
    }

    // Listen
    rv = listen(fd, SOMAXCONN);

    if (rv < 0)
    {
        die("listen()");
    }

    printf("Server listening on port 1234...\n");

    Conn *connections[MAX_CONNECTIONS]{};
    struct pollfd pollfds[MAX_CONNECTIONS + 1];

    // Main event loop
    while (true)
    {
        int nfds = 1;

        // Listening socket
        pollfds[0].fd = fd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        // Add connected clients
        for (int i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (connections[i] == nullptr)
            {
                continue;
            }

            pollfds[nfds].fd = connections[i]->fd;
            
            // Always watch for incoming data
            pollfds[nfds].events = POLLIN;

            if (connections[i]->wbuf_sent < connections[i]->wbuf_size)
            {
                pollfds[nfds].events |= POLLOUT;
            }

            pollfds[nfds].revents = 0;
            nfds++;
        }

        // Wait for an event
        int poll_rv = poll(pollfds, nfds, -1);

        if (poll_rv < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            die("poll()");
        }

        // Accept new clients
        if (pollfds[0].revents & POLLIN)
        {
            while (true)
            {
                struct sockaddr_in client_addr{};
                socklen_t addrlen = sizeof(client_addr);

                int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);

                // No more connections waiting
                if (connfd < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        break;
                    }
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    perror("accept()");
                    break;
                }

                // Make client socket non-blocking
                fd_set_nonblocking(connfd);

                printf("New client connected: fd=%d\n", connfd);

                // Find empty connection slot
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

                // Server is full
                if (!added)
                {
                    msg("Too many clients");
                    close(connfd);
                }
            }
        }

        // Handle clients
        int poll_index = 1;

        for (int i = 0; i < MAX_CONNECTIONS; i++)
        {
            Conn *conn = connections[i];

            if (conn == nullptr)
            {
                continue;
            }

            // Get events for this client
            short revents = pollfds[poll_index].revents;
            poll_index++;
            bool alive = true;

            // Socket error / hangup
            if (revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                alive = false;
            }

            // Readable
            if (alive && (revents & POLLIN))
            {
                if (!handle_read(conn))
                {
                    alive = false;
                }
                else
                {
                    // If we just read data, try to process requests
                    if (!process_request(conn))
                    {
                        alive = false;
                    }
                }
            }

            // Writable
            if (alive && (revents & POLLOUT))
            {
                if (!handle_write(conn))
                {
                    alive = false;
                }
            }

            // Close connection
            if (!alive)
            {
                printf("Closing client fd=%d\n", conn->fd);
                close(conn->fd);
                delete conn;
                connections[i] = nullptr;
            }
        }
    }

    close(fd);
    return 0;
}