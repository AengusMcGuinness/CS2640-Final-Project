#include "net/socket_utils.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {
namespace {

bool set_reuseaddr(int fd) {
    int enabled = 1;
    return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == 0;
}

}  // namespace

bool set_non_blocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }

    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int create_server_socket(std::uint16_t port, int backlog) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    if (!set_reuseaddr(fd)) {
        close_socket(fd);
        return -1;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_socket(fd);
        return -1;
    }

    if (::listen(fd, backlog) < 0) {
        close_socket(fd);
        return -1;
    }

    if (!set_non_blocking(fd)) {
        close_socket(fd);
        return -1;
    }

    return fd;
}

int create_client_socket(const std::string& host, std::uint16_t port) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const std::string port_string = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_string.c_str(), &hints, &result) != 0) {
        return -1;
    }

    int client_fd = -1;
    for (addrinfo* current = result; current != nullptr; current = current->ai_next) {
        client_fd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (client_fd < 0) {
            continue;
        }

        if (::connect(client_fd, current->ai_addr, current->ai_addrlen) == 0) {
            break;
        }

        close_socket(client_fd);
        client_fd = -1;
    }

    ::freeaddrinfo(result);
    return client_fd;
}

bool read_line(int fd, std::string& line) {
    line.clear();

    char ch = '\0';
    while (true) {
        const ssize_t bytes_read = ::recv(fd, &ch, 1, 0);
        if (bytes_read == 0) {
            return !line.empty();
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (ch == '\n') {
            return true;
        }
        if (ch != '\r') {
            line.push_back(ch);
        }
    }
}

bool write_all(int fd, std::string_view data) {
    std::size_t total_written = 0;
    while (total_written < data.size()) {
        const ssize_t bytes_written = ::send(
            fd,
            data.data() + total_written,
            data.size() - total_written,
            0
        );

        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        total_written += static_cast<std::size_t>(bytes_written);
    }

    return true;
}

void close_socket(int fd) {
    if (fd >= 0) {
        ::close(fd);
    }
}

}  // namespace net
