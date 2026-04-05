#pragma once

#include <string>
#include <string_view>

namespace protocol {

enum class RequestType {
    Get,
    Set,
    Del,
    Quit,
    Invalid,
};

enum class ResponseType {
    Ok,
    Value,
    NotFound,
    Error,
    Bye,
};

struct Request {
    RequestType type = RequestType::Invalid;
    std::string key;
    std::string value;
    std::string error;
};

struct Response {
    ResponseType type = ResponseType::Error;
    std::string payload;
};

Request parse_request(std::string_view line);
std::string serialize_response(const Response& response);

}  // namespace protocol
