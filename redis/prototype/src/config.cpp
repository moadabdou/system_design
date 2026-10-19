#include "config.hpp"
#include "common.hpp"
#include <charconv>
#include <stdexcept>
#include <string>

namespace study {

Config parse_config(int argc, char **argv) {
    Config config;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            throw std::runtime_error("options require values");
        }
        const std::string option = argv[i];
        const std::string value = argv[i + 1];
        if (option == "--memory") {
            std::size_t bytes = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), bytes);
            if (value.empty() || error != std::errc{} || end != value.data() + value.size() ||
                bytes == 0) {
                throw std::runtime_error("invalid memory limit");
            }
            config.memory_limit = bytes;
        } else if (option == "--policy") {
            if (value == "noeviction") {
                config.memory_policy = MemoryPolicy::NoEviction;
            } else if (value == "allkeys-lru") {
                config.memory_policy = MemoryPolicy::AllKeysLru;
            } else {
                throw std::runtime_error("invalid memory policy");
            }
        } else if (option == "--port") {
            const auto port = parse_integer(value);
            // Check before narrowing to int. No components exist during parsing.
            if (port < 1 || port > 65535) {
                throw std::runtime_error("invalid port");
            }
            config.port = static_cast<int>(port);
        } else {
            throw std::runtime_error("unknown option " + option);
        }
    }
    validate_config(config);
    return config;
}

void validate_config(const Config &config) {
    if (config.port < 1 || config.port > 65535) {
        throw std::runtime_error("invalid port");
    }
    if (config.memory_limit == 0) {
        throw std::runtime_error("invalid memory limit");
    }
    if (config.memory_policy != MemoryPolicy::NoEviction &&
        config.memory_policy != MemoryPolicy::AllKeysLru) {
        throw std::runtime_error("invalid memory policy");
    }
}

} // namespace study
