#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;


int main(){
    // int fd=socket();
    // connect(fd, addr);

    struct MyString{
        char* data;
        size_t length;
        size_t capacity;

    };
    
    

}