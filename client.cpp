#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>

using namespace std;

void die(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static int32_t read_full(int fd, char *buf, size_t n){

    while(n>0){
        ssize_t rv=read(fd, buf, n);
        if(rv<=0){
            if(rv<0 && errno==EINTR) continue; // Retry if interrupted by signal
            return -1;  
        }
        assert((size_t)rv<=n);
        n=n-(size_t)rv;
        buf=buf+rv;

    }
    return 0;

}

static int32_t write_all(int fd, const char *buf, size_t n){
    while(n>0){
        ssize_t rv=write(fd, buf, n);
        if(rv<=0){
            if(rv<0 && errno==EINTR) continue; // Retry if interrupted by signal
            return -1;
        }
        assert((size_t)rv<=n);
        n=n-(size_t)rv;
        buf=buf+rv;

    }
    return 0;

}

const size_t k_max_msg=4096;

static size_t send_req(int fd, const char *text){

    uint32_t len=(uint32_t)strlen(text);
    if(len>k_max_msg) return -1;

    char wbuf[4+k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    if(int32_t err=write_all(fd, wbuf, 4+len)){
        return err;
    }

    char rbuf[4+k_max_msg];
    errno = 0;
    if (int32_t err = read_full(fd, rbuf, 4)) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    uint32_t res_len=0;
    memcpy(&res_len, rbuf, 4);
    if(res_len>k_max_msg){
        msg("too long");
        return -1;
    }

    if(int32_t err=read_full(fd, &rbuf[4], res_len)){
        msg("read() error");
        return err;
    }
    // Print what the server sent back
    printf("server says: %.*s\n", res_len, &rbuf[4]);
    return 0;

}

int main(){
    
    int fd=socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0){
        die("socket()");
    }

    struct sockaddr_in addr={}; // it's holding an IPv4
    addr.sin_family=AF_INET;
    addr.sin_port=htons(1234); // Port
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK );
    int rv=connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if(rv<0){
        die("connect()");
    }
    // while(true){
    //     string s;
    //     cin>>s;
    //     send_req(fd, s.c_str());
    // }
    send_req(fd, "hello");
    send_req(fd, "world");
    send_req(fd, "this is a custom redis client!");
    
    close(fd);
    return 0;



}