#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

using namespace std;

static bool send_all(int fd, const string &data) {
    size_t sent = 0;

    while (sent < data.size()) {
        ssize_t n = write(fd, data.data() + sent, data.size() - sent);

        if (n < 0) {
            perror("write()");
            return false;
        }

        if (n == 0) {
            return false;
        }

        sent += n;
    }

    return true;
}

static bool read_line(int fd, string &line) {
    line.clear();

    char c;
    while (true) {
        ssize_t n = read(fd, &c, 1);

        if (n < 0) {
            perror("read()");
            return false;
        }

        if (n == 0) {
            return false;
        }

        line.push_back(c);

        if (line.size() >= 2 &&
            line[line.size() - 2] == '\r' &&
            line[line.size() - 1] == '\n') {
            line.resize(line.size() - 2);
            return true;
        }
    }
}

static bool read_exact(int fd, char *buf, size_t len) {
    size_t total = 0;

    while (total < len) {
        ssize_t n = read(fd, buf + total, len - total);

        if (n < 0) {
            perror("read()");
            return false;
        }

        if (n == 0) {
            return false;
        }

        total += n;
    }

    return true;
}

static bool read_resp(int fd) {
    char type;

    if (!read_exact(fd, &type, 1)) {
        return false;
    }

    // Simple String
    if (type == '+') {
        string line;

        if (!read_line(fd, line)) {
            return false;
        }

        cout << line << '\n';
        return true;
    }

    // Error
    if (type == '-') {
        string line;

        if (!read_line(fd, line)) {
            return false;
        }

        cout << "(error) " << line << '\n';
        return true;
    }

    // Integer
    if (type == ':') {
        string line;

        if (!read_line(fd, line)) {
            return false;
        }

        cout << "(integer) " << line << '\n';
        return true;
    }

    // Bulk String
    if (type == '$') {
        string line;

        if (!read_line(fd, line)) {
            return false;
        }

        long long len = stoll(line);

        // Null bulk string
        if (len == -1) {
            cout << "(nil)\n";
            return true;
        }

        string value(len, '\0');

        if (!read_exact(fd, value.data(), len)) {
            return false;
        }

        // Read trailing CRLF
        char crlf[2];

        if (!read_exact(fd, crlf, 2)) {
            return false;
        }

        cout << '"' << value << '"' << '\n';
        return true;
    }

    // Array
    if (type == '*') {
        string line;

        if (!read_line(fd, line)) {
            return false;
        }

        long long count = stoll(line);

        if (count == -1) {
            cout << "(nil)\n";
            return true;
        }

        for (long long i = 0; i < count; ++i) {
            if (!read_resp(fd)) {
                return false;
            }
        }

        return true;
    }

    cerr << "Unknown RESP type: " << type << '\n';
    return false;
}

// Convert:
//
// SET key value
//
// into:
//
// *3\r\n
// $3\r\nSET\r\n
// $3\r\nkey\r\n
// $5\r\nvalue\r\n
//
static string make_resp(const vector<string> &args) {
    string out;

    out += "*" + to_string(args.size()) + "\r\n";

    for (const string &arg : args) {
        out += "$" + to_string(arg.size()) + "\r\n";
        out += arg;
        out += "\r\n";
    }

    return out;
}

// Simple command-line parser.
// Splits input on spaces.
static vector<string> parse_command(const string &cmd) {
    vector<string> args;

    size_t start = 0;

    while (start < cmd.size()) {

        // Skip spaces
        while (start < cmd.size() && cmd[start] == ' ') {
            start++;
        }

        if (start >= cmd.size()) {
            break;
        }

        size_t end = start;

        while (end < cmd.size() && cmd[end] != ' ') {
            end++;
        }

        args.push_back(cmd.substr(start, end - start));

        start = end;
    }

    return args;
}

int main() {

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        perror("socket()");
        return 1;
    }

    sockaddr_in addr{};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect()");
        close(fd);
        return 1;
    }

    cout << "Connected to Custom Redis at 127.0.0.1:1234\n";

    while (true) {

        cout << "Enter command: ";
        cout.flush();

        string cmd;

        if (!getline(cin, cmd)) {
            break;
        }

        if (cmd.empty()) {
            continue;
        }

        vector<string> args = parse_command(cmd);

        if (args.empty()) {
            continue;
        }

        string request = make_resp(args);

        if (!send_all(fd, request)) {
            break;
        }

        if (!read_resp(fd)) {
            cerr << "Failed to read server response.\n";
            break;
        }
    }

    close(fd);

    return 0;
}