#pragma once

namespace study {
struct Config {
    int port = 6380;
};

Config parse_config(int argc, char **argv);
void validate_config(const Config &config);
} // namespace study
