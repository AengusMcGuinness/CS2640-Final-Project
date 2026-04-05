#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace net {

int create_server_socket(std::uint16_t port, int backlog = 16);
int create_client_socket(const std::string& host, std::uint16_t port);
bool read_line(int fd, std::string& line);
bool write_all(int fd, std::string_view data);
void close_socket(int fd);

}  // namespace net
