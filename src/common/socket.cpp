#include "socket.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

int connect_to_daemon(const std::string& path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool write_line(int fd, const std::string& payload) {
    std::string line = payload + "\n";
    ssize_t total = static_cast<ssize_t>(line.size());
    ssize_t sent  = 0;
    while (sent < total) {
        ssize_t n = write(fd, line.c_str() + sent, static_cast<size_t>(total - sent));
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool read_line(int fd, std::string& out) {
    out.clear();
    char ch;
    while (true) {
        ssize_t n = read(fd, &ch, 1);
        if (n <= 0) return false;
        if (ch == '\n') return true;
        out += ch;
    }
}

std::optional<std::string> send_command(int fd, const std::string& payload) {
    if (!write_line(fd, payload)) return std::nullopt;
    std::string response;
    if (!read_line(fd, response)) return std::nullopt;
    return response;
}
