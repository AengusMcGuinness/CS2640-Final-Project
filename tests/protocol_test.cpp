#include "protocol/text_protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "test failed: " << message << '\n';
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    using protocol::RequestType;
    using protocol::Response;
    using protocol::ResponseType;

    {
        const auto request = protocol::parse_request("GET alpha");
        if (int rc = expect(request.type == RequestType::Get, "GET should parse"); rc != 0) {
            return rc;
        }
        if (int rc = expect(request.key == "alpha", "GET should capture key"); rc != 0) {
            return rc;
        }
    }

    {
        const auto request = protocol::parse_request("  set beta value  ");
        if (int rc = expect(request.type == RequestType::Set, "SET should parse case-insensitively"); rc != 0) {
            return rc;
        }
        if (int rc = expect(request.key == "beta", "SET should capture key"); rc != 0) {
            return rc;
        }
        if (int rc = expect(request.value == "value", "SET should capture value"); rc != 0) {
            return rc;
        }
    }

    {
        const auto request = protocol::parse_request("DEL gamma");
        if (int rc = expect(request.type == RequestType::Del, "DEL should parse"); rc != 0) {
            return rc;
        }
    }

    {
        const auto request = protocol::parse_request("quit");
        if (int rc = expect(request.type == RequestType::Quit, "QUIT should parse"); rc != 0) {
            return rc;
        }
    }

    {
        const auto request = protocol::parse_request("GET a b");
        if (int rc = expect(request.type == RequestType::Invalid, "invalid GET should be rejected"); rc != 0) {
            return rc;
        }
    }

    {
        const auto request = protocol::parse_request("SET only-key");
        if (int rc = expect(request.type == RequestType::Invalid, "invalid SET should be rejected"); rc != 0) {
            return rc;
        }
    }

    {
        if (int rc = expect(protocol::serialize_response({ResponseType::Ok, {}}) == "OK\n", "serialize OK"); rc != 0) {
            return rc;
        }
        if (int rc = expect(protocol::serialize_response({ResponseType::Value, "abc"}) == "VALUE abc\n", "serialize VALUE"); rc != 0) {
            return rc;
        }
        if (int rc = expect(protocol::serialize_response({ResponseType::NotFound, {}}) == "NOT_FOUND\n", "serialize NOT_FOUND"); rc != 0) {
            return rc;
        }
        if (int rc = expect(protocol::serialize_response({ResponseType::Error, "bad"}) == "ERROR bad\n", "serialize ERROR"); rc != 0) {
            return rc;
        }
        if (int rc = expect(protocol::serialize_response({ResponseType::Bye, {}}) == "BYE\n", "serialize BYE"); rc != 0) {
            return rc;
        }
    }

    std::cout << "protocol_test passed\n";
    return EXIT_SUCCESS;
}
