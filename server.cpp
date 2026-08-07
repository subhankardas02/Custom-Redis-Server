#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cassert> // Required for assert()
#include <cerrno>  // Required for errno

using namespace std;

void die(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
static void msg(const char *msg){
    fprintf(stderr, "%s\n", msg);
}


static int32_t read_full(int fd, char *buf, size_t n){

    while(n>0){
        ssize_t rv=read(fd, buf, n);
        if(rv<=0) return -1;
        assert((size_t)rv<=n);
        n=n-(size_t)rv;
        buf=buf+rv;
    }
    return 0;

}

static int32_t write_all(int fd, const char *buf, size_t n){
    while(n>0){
        ssize_t rv=write(fd, buf, n);
        if(rv<=0) return -1;
        assert((size_t)rv <= n);
        n=n-(size_t)rv;
        buf=buf+rv;
    }
    return 0;

}


const size_t k_max_msg=4096;

static int32_t one_request(int connfd){
    char rbuf[4+k_max_msg];
    errno=0;
    int32_t err=read_full(connfd, rbuf, 4);
    if(err){
        msg(errno==0 ? "EFO" : "read() error");
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
    const char reply[]="world";
    char wbuf[4+sizeof(reply)];
    len=(uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4+len);


}


int main(){
    
    int fd=socket(AF_INET, SOCK_STREAM, 0);
    int val=1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // Bind to an address
    struct sockaddr_in addr={}; // it's holding an IPv4
    addr.sin_family=AF_INET;
    addr.sin_port=htons(1234); // Port
    addr.sin_addr.s_addr=htonl(0); // wildcard IP address
    int rv=bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if(rv) {
        die("bind()");
    }
    rv=listen(fd, SOMAXCONN);
    if(rv){
        die("listen()");
    }

    //  Accept connections
    while(true){
        struct sockaddr_in client_addr={};
        socklen_t addrlen=sizeof(client_addr);
        int connfd=accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if(connfd<0){
            continue; // if error occurs, just continue to the next iteration of the loop
        }

        // do_something(connfd);
        while(true){
            int32_t err=one_request(connfd);
            if(err){
                break;
            }
        }

        close(connfd);

    }


    return 0;

}