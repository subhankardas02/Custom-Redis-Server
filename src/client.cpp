#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <iostream>

using namespace std;

enum {
    TAG_NIL = 0, TAG_ERR = 1, TAG_STR = 2, TAG_INT = 3, TAG_ARR = 5,
};

static void print_response(const uint8_t *buf, size_t len) {
    if (len < 1) return;
    uint8_t tag = buf[0];
    switch (tag) {
        case TAG_NIL: printf("(nil)\n"); break;
        case TAG_ERR: {
            if (len < 1 + 8) return;
            uint32_t code = 0, msg_len = 0;
            memcpy(&code, buf + 1, 4);
            memcpy(&msg_len, buf + 5, 4);
            string err_msg((const char *)buf + 9, msg_len);
            printf("(error %u) %s\n", code, err_msg.c_str());
            break;
        }
        case TAG_STR: {
            if (len < 1 + 4) return;
            uint32_t str_len = 0;
            memcpy(&str_len, buf + 1, 4);
            string str((const char *)buf + 5, str_len);
            printf("\"%s\"\n", str.c_str());
            break;
        }
        case TAG_INT: {
            if (len < 1 + 8) return;
            int64_t val = 0;
            memcpy(&val, buf + 1, 8);
            printf("(integer) %ld\n", val);
            break;
        }
        default: printf("Unknown tag %u\n", tag); break;
    }
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket()"); return 1; }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect()"); return 1;
    }

    while (true) {
        printf("Enter command: ");
        string cmd;
        if (!getline(cin, cmd) || cmd.empty()) break;

        uint32_t len = cmd.size();
        vector<uint8_t> wbuf(4 + len);
        memcpy(wbuf.data(), &len, 4);
        memcpy(wbuf.data() + 4, cmd.data(), len);

        if (write(fd, wbuf.data(), wbuf.size()) < 0) {
            perror("write()"); break;
        }

        uint32_t rlen = 0;
        if (read(fd, &rlen, 4) <= 0) break;

        vector<uint8_t> rbuf(rlen);
        size_t total_read = 0;
        while (total_read < rlen) {
            ssize_t rv = read(fd, rbuf.data() + total_read, rlen - total_read);
            if (rv <= 0) break;
            total_read += rv;
        }
        printf("server says: ");
        print_response(rbuf.data(), rlen);
    }
    close(fd);
    return 0;
}