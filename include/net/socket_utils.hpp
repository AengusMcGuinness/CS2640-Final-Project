#pragma once

// `cstdint` gives us the fixed-width integer type used for ports.
#include <cstdint>
// `string` is used for host names and buffer storage.
#include <string>
// `string_view` lets the write helper avoid unnecessary copies.
#include <string_view>

// Group socket helpers in a dedicated namespace.
namespace net {

// Create, bind, and listen on a TCP server socket.
int create_server_socket(std::uint16_t port, int backlog = 16);
// Resolve and connect a TCP client socket.
int create_client_socket(const std::string& host, std::uint16_t port);
// Put a file descriptor into nonblocking mode.
bool set_non_blocking(int fd);
// Read a newline-delimited line from a socket.
bool read_line(int fd, std::string& line);
// Write the full buffer to the socket, retrying until complete.
bool write_all(int fd, std::string_view data);
// Close a socket if it is valid.
void close_socket(int fd);

// End the namespace.
}  // namespace net
