#include "protocol/text_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace protocol {
namespace {

std::string trim_left(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    return std::string(text.substr(start));
}

std::string trim_right(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    std::size_t end = text.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(0, end));
}

std::string trim(std::string_view text) {
    return trim_right(trim_left(text));
}

std::string lower_copy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

Request invalid_request(std::string message) {
    Request request;
    request.type = RequestType::Invalid;
    request.error = std::move(message);
    return request;
}

}  // namespace

Request parse_request(std::string_view line) {
    const std::string cleaned = trim(line);
    if (cleaned.empty()) {
        return invalid_request("empty request");
    }

    const std::size_t first_space = cleaned.find(' ');
    const std::string command = lower_copy(cleaned.substr(0, first_space));
    const std::string remainder =
        first_space == std::string::npos ? std::string() : trim_left(cleaned.substr(first_space + 1));

    Request request;

    if (command == "get") {
        if (remainder.empty()) {
            return invalid_request("GET requires a key");
        }
        if (remainder.find(' ') != std::string::npos) {
            return invalid_request("GET accepts exactly one key");
        }
        request.type = RequestType::Get;
        request.key = remainder;
        return request;
    }

    if (command == "del" || command == "delete") {
        if (remainder.empty()) {
            return invalid_request("DEL requires a key");
        }
        if (remainder.find(' ') != std::string::npos) {
            return invalid_request("DEL accepts exactly one key");
        }
        request.type = RequestType::Del;
        request.key = remainder;
        return request;
    }

    if (command == "set") {
        const std::size_t key_end = remainder.find(' ');
        if (key_end == std::string::npos) {
            return invalid_request("SET requires a key and a value");
        }

        request.type = RequestType::Set;
        request.key = remainder.substr(0, key_end);
        request.value = trim_left(remainder.substr(key_end + 1));

        if (request.key.empty()) {
            return invalid_request("SET requires a non-empty key");
        }
        if (request.value.empty()) {
            return invalid_request("SET requires a non-empty value");
        }
        return request;
    }

    if (command == "quit" || command == "exit") {
        request.type = RequestType::Quit;
        return request;
    }

    return invalid_request("unknown command: " + command);
}

std::string serialize_response(const Response& response) {
    switch (response.type) {
    case ResponseType::Ok:
        return "OK\n";
    case ResponseType::Value:
        return "VALUE " + response.payload + "\n";
    case ResponseType::NotFound:
        return "NOT_FOUND\n";
    case ResponseType::Error:
        return "ERROR " + response.payload + "\n";
    case ResponseType::Bye:
        return "BYE\n";
    }

    return "ERROR invalid response\n";
}

}  // namespace protocol
