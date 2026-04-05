#include "kvstore/kv_store.hpp"
#include "net/socket_utils.hpp"
#include "protocol/text_protocol.hpp"

#include <cstdlib>
#include <cerrno>
#include <exception>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>

namespace {

protocol::Response handle_request(kvstore::KeyValueStore& store, const protocol::Request& request) {
    using protocol::Response;
    using protocol::ResponseType;
    using protocol::RequestType;

    switch (request.type) {
    case RequestType::Get: {
        auto value = store.get(request.key);
        if (!value) {
            return {ResponseType::NotFound, {}};
        }
        return {ResponseType::Value, *value};
    }
    case RequestType::Set:
        store.set(request.key, request.value);
        return {ResponseType::Ok, {}};
    case RequestType::Del:
        return store.erase(request.key) ? Response{ResponseType::Ok, {}} : Response{ResponseType::NotFound, {}};
    case RequestType::Quit:
        return {ResponseType::Bye, {}};
    case RequestType::Invalid:
        return {ResponseType::Error, request.error};
    }

    return {ResponseType::Error, "unhandled request"};
}

void serve_client(int client_fd, kvstore::KeyValueStore& store) {
    std::string line;
    while (net::read_line(client_fd, line)) {
        const protocol::Request request = protocol::parse_request(line);
        const protocol::Response response = handle_request(store, request);
        if (!net::write_all(client_fd, protocol::serialize_response(response))) {
            break;
        }
        if (request.type == protocol::RequestType::Quit) {
            break;
        }
    }

    net::close_socket(client_fd);
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::uint16_t port = static_cast<std::uint16_t>(argc > 1 ? std::stoi(argv[1]) : 9090);

    int server_fd = net::create_server_socket(port);
    if (server_fd < 0) {
        std::cerr << "failed to start server on port " << port << ": " << std::strerror(errno) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "kv_server listening on port " << port << '\n';

    kvstore::KeyValueStore store;

    while (true) {
        sockaddr_storage client_addr {};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            std::cerr << "accept failed\n";
            continue;
        }

        std::thread([client_fd, &store]() {
            serve_client(client_fd, store);
        }).detach();
    }

    net::close_socket(server_fd);
    return EXIT_SUCCESS;
}
