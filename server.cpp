#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

using namespace std;
void die(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void do_something(int connfd){
    char rbuf[64]={};
    ssize_t n=read(connfd, rbuf, sizeof(rbuf)-1);
    if(n<0){
        perror("read() failed");
        return;
    }
    cout<<"client says: "<<endl;
    cout<<rbuf<<endl;
    char wbuf[]="world";
    ssize_t w_len=write(connfd, wbuf, strlen(wbuf));
    if (w_len < 0) {
        perror("write() failed");
    }
}


int main(){
    // int fd=socket();
    // connect(fd, addr);

    // struct MyString{
    //     char* data;
    //     size_t length;
    //     size_t capacity;

    // };
    
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
    
    // struct sockaddr_in {
    //     uint16_t sin_family; // address family (AF_INET)
    //     uint16_t sin_port;   // port in big endian
    //     struct in_addr sin_addr; // IPv4 address
    // };
    // struct in_addr{
    //     uint32_t s_addr; // IPv4 address in big endian
    // };
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

        do_something(connfd);
        close(connfd);

    }


    

}