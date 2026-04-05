#include "net/socket_utils.hpp"
#include "protocol/text_protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool send_request(int fd, const std::string& line) {
    if (!net::write_all(fd, line + "\n")) {
        std::cerr << "failed to send request\n";
        return false;
    }

    std::string response;
    if (!net::read_line(fd, response)) {
        std::cerr << "server closed the connection\n";
        return false;
    }

    std::cout << response << '\n';
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::uint16_t port = static_cast<std::uint16_t>(argc > 2 ? std::stoi(argv[2]) : 9090);

    int fd = net::create_client_socket(host, port);
    if (fd < 0) {
        std::cerr << "failed to connect to " << host << ':' << port << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "connected to " << host << ':' << port << '\n';
    std::cout << "type commands like: SET key value | GET key | DEL key | QUIT\n";

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line.empty()) {
            continue;
        }

        if (!send_request(fd, line)) {
            break;
        }

        const protocol::Request request = protocol::parse_request(line);
        if (request.type == protocol::RequestType::Quit) {
            break;
        }
    }

    net::close_socket(fd);
    return EXIT_SUCCESS;
}
