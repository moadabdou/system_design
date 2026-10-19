#include "protocol.hpp"
#include <stdexcept>

namespace study {

std::string encode_bulk(std::string_view value) {
    return "$" + std::to_string(value.size()) + "\r\n" + std::string(value) + "\r\n";
}

std::string encode_array(const Arguments &arguments) {
    std::string response = "*" + std::to_string(arguments.size()) + "\r\n";
    for (const auto &argument : arguments) {
        response += encode_bulk(argument);
    }
    return response;
}

size_t parse_command(std::string_view input, Arguments &arguments, size_t limit) {
    arguments.clear();
    if (input.empty()) {
        return 0;
    }
    if (input[0] != '*') {
        throw std::runtime_error("expected RESP array of bulk strings");
    }
    auto line_end = input.find("\r\n");
    if (line_end == std::string_view::npos) {
        if (input.size() > 32) {
            throw std::runtime_error("invalid array header");
        }
        return 0;
    }
    int64_t argument_count = parse_integer(input.substr(1, line_end - 1));
    if (argument_count < 1 || argument_count > 4000000) {
        throw std::runtime_error("invalid array length");
    }
    size_t cursor = line_end + 2;
    for (int64_t i = 0; i < argument_count; ++i) {
        if (cursor == input.size()) {
            return 0;
        }
        if (input[cursor] != '$') {
            throw std::runtime_error("expected bulk string");
        }
        line_end = input.find("\r\n", cursor);
        if (line_end == std::string_view::npos) {
            if (input.size() - cursor > 32) {
                throw std::runtime_error("invalid bulk header");
            }
            return 0;
        }
        int64_t length = parse_integer(input.substr(cursor + 1, line_end - cursor - 1));
        if (length < 0 || static_cast<uint64_t>(length) > limit) {
            throw std::runtime_error("bulk length exceeds limit");
        }
        cursor = line_end + 2;
        size_t byte_count = static_cast<size_t>(length);
        if (cursor > limit || byte_count > limit - cursor || limit - cursor - byte_count < 2) {
            throw std::runtime_error("command exceeds limit");
        }
        if (input.size() - cursor < byte_count + 2) {
            return 0;
        }
        if (input.substr(cursor + byte_count, 2) != "\r\n") {
            throw std::runtime_error("invalid bulk ending");
        }
        arguments.emplace_back(input.substr(cursor, byte_count));
        cursor += byte_count + 2;
    }
    return cursor;
}

} // namespace study
