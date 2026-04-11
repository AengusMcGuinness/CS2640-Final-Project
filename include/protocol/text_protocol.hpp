#pragma once

// `string` is used for parsed keys, values, and error text.
#include <string>
// `string_view` lets the parser inspect input without copying first.
#include <string_view>

// Group protocol-related types and functions in their own namespace.
namespace protocol {

// The supported request kinds for the text protocol.
enum class RequestType {
    // Fetch the value for a key.
    Get,
    // Insert or overwrite a key/value pair.
    Set,
    // Delete a key.
    Del,
    // Close the session cleanly.
    Quit,
    // Flag malformed input.
    Invalid,
};

// The supported response kinds emitted by the server.
enum class ResponseType {
    // Operation succeeded without returning a value.
    Ok,
    // Operation succeeded and returned a payload.
    Value,
    // The requested key was not found.
    NotFound,
    // The request was malformed or otherwise invalid.
    Error,
    // The client asked to terminate the session.
    Bye,
};

// Structured representation of a parsed request line.
struct Request {
    // The command kind that was parsed.
    RequestType type = RequestType::Invalid;
    // The key argument, if the command uses one.
    std::string key;
    // The value argument, if the command uses one.
    std::string value;
    // Human-readable error text for malformed requests.
    std::string error;
};

// Structured representation of a server response.
struct Response {
    // The response kind to serialize.
    ResponseType type = ResponseType::Error;
    // Optional response payload, such as a value or error message.
    std::string payload;
};

// Parse one newline-delimited request into the structured form above.
Request parse_request(std::string_view line);
// Serialize a structured response back into the wire format.
std::string serialize_response(const Response& response);

// End the protocol namespace.
}  // namespace protocol
