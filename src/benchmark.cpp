#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

using namespace std;

const int NUM_REQUESTS = 500000;
const int PIPELINE_SIZE = 50; // Optimization: Send batches to avoid network RTT bottlenecks

void die(const char *msg) { perror(msg); exit(EXIT_FAILURE); }

void send_batch(int fd, const vector<string>& cmds, int start_idx, int count) {
    vector<uint8_t> wbuf;
    for (int i = 0; i < count; i++) {
        uint32_t len = cmds[start_idx + i].size();
        vector<uint8_t> packet(4 + len);
        memcpy(packet.data(), &len, 4);
        memcpy(packet.data() + 4, cmds[start_idx + i].data(), len);
        wbuf.insert(wbuf.end(), packet.begin(), packet.end());
    }

    size_t total = 0;
    while (total < wbuf.size()) {
        ssize_t rv = write(fd, wbuf.data() + total, wbuf.size() - total);
        if (rv <= 0) die("write()");
        total += rv;
    }
}

void read_batch(int fd, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t len = 0;
        size_t total_read = 0;
        while (total_read < 4) {
            ssize_t rv = read(fd, (char*)&len + total_read, 4 - total_read);
            if (rv <= 0) die("read header");
            total_read += rv;
        }

        vector<uint8_t> buf(len);
        total_read = 0;
        while (total_read < len) {
            ssize_t rv = read(fd, buf.data() + total_read, len - total_read);
            if (rv <= 0) die("read payload");
            total_read += rv;
        }
    }
}

void run_benchmark(int fd, const vector<string>& cmds, const string& name) {
    cout << "Benchmarking " << name << "..." << endl;
    auto start = chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_REQUESTS; i += PIPELINE_SIZE) {
        int batch_size = min(PIPELINE_SIZE, NUM_REQUESTS - i);
        send_batch(fd, cmds, i, batch_size);
        read_batch(fd, batch_size);
    }

    auto end = chrono::high_resolution_clock::now();
    chrono::duration<double> diff = end - start;
    
    cout << name << " completed in: " << diff.count() << " seconds" << endl;
    cout << name << " OPS: " << (int)(NUM_REQUESTS / diff.count()) << "\n\n";
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket()");

    int tcp_nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        die("connect() - Make sure ./server is running!");
    }

    cout << "Pre-generating " << NUM_REQUESTS << " commands..." << endl;
    vector<string> set_cmds, get_cmds;
    set_cmds.reserve(NUM_REQUESTS);
    get_cmds.reserve(NUM_REQUESTS);

    for (int i = 0; i < NUM_REQUESTS; i++) {
        set_cmds.push_back("SET key_" + to_string(i) + " val_" + to_string(i));
        get_cmds.push_back("GET key_" + to_string(i));
    }

    run_benchmark(fd, set_cmds, "SET");
    run_benchmark(fd, get_cmds, "GET");

    close(fd);
    return 0;
}