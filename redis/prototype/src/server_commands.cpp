#include "protocol.hpp"
#include "server.hpp"
#include <stdexcept>

namespace study {
void Server::dispatch_command(Connection &connection, const Arguments &arguments) {
    const auto &operation = arguments[0];
    if (operation == "PING" && arguments.size() == 1) {
        queue_reply(connection, "+PONG\r\n");
        return;
    }
    if (operation != "GET" && operation != "SET" && operation != "DEL") {
        throw std::runtime_error("unknown command");
    }
    const std::size_t expected_arguments = operation == "SET" ? 3 : 2;
    if (arguments.size() != expected_arguments && !(operation == "SET" && arguments.size() == 5)) {
        throw std::runtime_error("wrong arity");
    }

    const auto &key = arguments[1];
    if (key.size() > MAX_KEY) {
        throw std::runtime_error("key exceeds 4096 bytes");
    }
    if (operation == "GET") {
        const auto *value = store_.get(key);
        queue_reply(connection, value == nullptr ? "$-1\r\n" : encode_bulk(*value));
    } else if (operation == "DEL") {
        queue_reply(connection, store_.erase(key) ? ":1\r\n" : ":0\r\n");
    } else {
        const auto &value = arguments[2];
        if (value.size() > MAX_VALUE) {
            throw std::runtime_error("value exceeds 1 MiB");
        }
        std::optional<std::chrono::milliseconds> ttl;
        if (arguments.size() == 5) {
            if (arguments[3] != "PX") {
                throw std::runtime_error("unsupported SET option; expected PX");
            }
            ttl = std::chrono::milliseconds{parse_integer(arguments[4])};
        }
        store_.set(key, value, ttl);
        queue_reply(connection, "+OK\r\n");
    }
}
} // namespace study
