#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <chrono>
#include <vector>

using namespace std;

const size_t k_max_msg = 4096;

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) return -1;
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) return -1;
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static bool send_cmd(int fd, const char *text) {
    uint32_t len = (uint32_t)strlen(text);
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(wbuf + 4, text, len);

    if (write_all(fd, wbuf, 4 + len) != 0) return false;

    char rbuf[4 + k_max_msg];
    if (read_full(fd, rbuf, 4) != 0) return false;
    uint32_t res_len = 0;
    memcpy(&res_len, rbuf, 4);

    if (read_full(fd, rbuf + 4, res_len) != 0) return false;
    return true;
}

int main() {
    int total_requests = 100000;
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        cerr << "Connection failed!" << endl;
        return 1;
    }

    cout << "Benchmarking " << total_requests << " SET requests..." << endl;

    auto start = chrono::high_resolution_clock::now();

    for (int i = 0; i < total_requests; i++) {
        if (!send_cmd(fd, "SET bench_key bench_val")) {
            cerr << "Request failed at iteration " << i << endl;
            break;
        }
    }

    auto end = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = end - start;

    double rps = total_requests / elapsed.count();
    double avg_latency_ms = (elapsed.count() * 1000.0) / total_requests;

    cout << "\n--- Benchmark Results ---" << endl;
    cout << "Total Time:       " << elapsed.count() << " seconds" << endl;
    cout << "Throughput:       " << (int)rps << " Requests/Sec (RPS)" << endl;
    cout << "Avg Latency:      " << avg_latency_ms << " ms per request" << endl;

    close(fd);
    return 0;
}