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

    
    

}