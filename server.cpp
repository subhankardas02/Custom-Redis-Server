#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <vector>

using namespace std;


const size_t k_max_msg=4096;
const size_t MAX_CONNECTIONS=1024;

static void die(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
static void msg(const char *msg){
    fprintf(stderr, "%s\n", msg);
}

static void fd_set_nonblocking(int fd){
    int flags=fcntl(fd, F_GETFL, 0);
    if(flags<0){
        die("fcntl(F_GETFL)");
    }
    if(fcntl(fd, F_SETFL, flags | O_NONBLOCK)<0){
        die("fcntl(F_SETFL)");
    }
}
// Connection State
struct Conn{
    int fd;
    char rbuf[4+k_max_msg];
    size_t rbuf_size=0;
    char wbuf[4+k_max_msg];
    size_t wbuf_size=0;
    size_t wbuf_sent=0;
}



// static int32_t read_full(int fd, char *buf, size_t n){

//     while(n>0){
//         ssize_t rv=read(fd, buf, n);
//         if(rv<=0){
//             if(rv<0 && errno==EINTR) continue; // Retry if interrupted by signal
//             return -1;  
//         }
//         assert((size_t)rv<=n);
//         n=n-(size_t)rv;
//         buf=buf+rv;
//     }
//     return 0;

// }

// static int32_t write_all(int fd, const char *buf, size_t n){
//     while(n>0){
//         ssize_t rv=write(fd, buf, n);
//         if(rv<=0){
//             if(rv<0 && errno==EINTR) continue; // Retry if interrupted by signal
//             return -1;
//         }
//         assert((size_t)rv <= n);
//         n=n-(size_t)rv;
//         buf=buf+rv;
//     }
//     return 0;

// }



static int32_t one_request(int connfd){
    char rbuf[4+k_max_msg];
    errno=0;
    int32_t err=read_full(connfd, rbuf, 4);
    if(err){
        msg(errno==0 ? "EOF" : "read() error");
        return err;
    }
    
    uint32_t len=0;
    memcpy(&len, rbuf, 4);
    if(len>k_max_msg){
        msg("too long");
        return -1;
    }

    err=read_full(connfd, &rbuf[4], len);
    if(err){
        msg("read() error");
        return err;
    }
    // do something
    printf("client says: %.*s\n", len, &rbuf[4]);

    // reply using the same protocol
    const char reply[]="Your message was received";
    char wbuf[4+sizeof(reply)];
    len=(uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4+len);


}


int main(){
    
    int fd=socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0){
        die("socket()");
    }

    int val=1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
     

    // Make listening socket non-blocking
    fd_set_nonblocking(fd);



    // Bind to an address
    struct sockaddr_in addr={}; // it's holding an IPv4 address
    addr.sin_family=AF_INET;
    addr.sin_port=htons(1234); // Port
    addr.sin_addr.s_addr=htonl(0); // wildcard IP address
    int rv=bind(fd, (const struct sockaddr *)&addr, sizeof(addr));

    if(rv){
        die("bind()");
    }
    rv=listen(fd, SOMAXCONN);
    if(rv){
        die("listen()");
    }
    printf("Server listening on port 1234...\n");

    Conn* connections[MAX_CONNECTIONS] = {nullptr};  // Array to hold active connections

    struct pollfd pollfds[MAX_CONNECTIONS + 1]; // +1 for the listening socket

    while(true){

        int nfds = 1; // Start with the listening socket
        
        pollfds[0].fd = fd;
        pollfds[0].events = POLLIN; // We want to know when we can accept a new connection
        pollfds[0].revents = 0;

        for(int i=0; i<Max_CONNECTIONS; i++){
            if(connections[i]==nullptr) continue;

            pollfds[nfds].fd = connections[i]->fd;
            pollfds[nfds].events = POLLIN; // We want to know when we can read from the connection

            if(connections[i]->wbuf_size > connections[i]->wbuf_sent){
                pollfds[nfds].events |= POLLOUT; // We also want to know when we can write to the connection
            }
            pollfds[nfds].revents = 0;
            nfds++;

        }

        int poll_rv=poll(pollfds, nfds, -1);
        if(poll_rv<0){
            if(errno==EINTR) continue; // Retry if interrupted by signal
            die("poll()");
        }

        if(pollfds[0].revents & POLLIN){
            while(true){
                struct sockaddr_in client_addr{};
                socklen_t addrlen=sizeof(client_addr);
                int connfd=accept(fd, (struct sockaddr *)&client_addr, &addrlen);
                if(connfd<0){
                    if(errno==EINTR) continue; // Retry if interrupted by signal
                    if(errno==EAGAIN || errno==EWOULDBLOCK) break; // No more connections to accept
                    perror("accept()");
                    break;
                }

                // make client socket non-blocking
                fd_set_nonblocking(connfd);
                printf("Accepted new connection: fd=%d\n", connfd);

                // Find a free slot for the new connection
                bool added=false;
                for(int i=0; i<MAX_CONNECTIONS; i++){
                    if(connections[i]==nullptr){
                        Conn* conn=new Conn();
                        conn->fd=connfd;
                        connections[i]=conn;
                        added=true;
                        break;
                    }
                }
                if(!added){
                    msg("Too many connections, closing new connection");
                    close(connfd);
                }

            }
        }

        // Handle existing connections
        for(int i=0; i<MAX_CONNECTIONS; i++){

            Conn* conn=connections[i];
            if(conn==nullptr) continue;
            short revents=pollfds[i+1].revents; // +1 because pollfds[0] is the listening socket

        }



    }









    // //  Accept connections
    // while(true){
    //     struct sockaddr_in client_addr={};
    //     socklen_t addrlen=sizeof(client_addr);
    //     int connfd=accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    //     if(connfd<0){
    //         continue; // if error occurs, just continue to the next iteration of the loop
    //     }

    //     // do_something(connfd);
    //     while(true){
    //         int32_t err=one_request(connfd);
    //         if(err){
    //             break;
    //         }
    //     }

    //     close(connfd);

    }


    // return 0;

}