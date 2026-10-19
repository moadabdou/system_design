#include "config.hpp"
#include "common.hpp"
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
        if (option != "--port") {
            throw std::runtime_error("unknown option " + option);
        }
        const auto port = parse_integer(argv[i + 1]);
        // Check before narrowing to int. No components exist during parsing.
        if (port < 1 || port > 65535) {
            throw std::runtime_error("invalid port");
        }
        config.port = static_cast<int>(port);
    }
    validate_config(config);
    return config;
}

void validate_config(const Config &config) {
    if (config.port < 1 || config.port > 65535) {
        throw std::runtime_error("invalid port");
    }
}

} // namespace study
