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
    char msg[]="hello";
    ssize_t w_len=write(fd, msg, strlen(msg));
    if (w_len < 0) {
        die("write");
    }
    char rbuf[64]={};
    ssize_t n=read(fd, rbuf, sizeof(rbuf)-1);
    if(n<0){
        die("read()");
    }
    cout<<"server says: "<<rbuf<<endl;
    close(fd);


}