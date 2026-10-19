#pragma once
#include <cstddef>

namespace study {
enum class MemoryPolicy { NoEviction, AllKeysLru };
constexpr std::size_t DEFAULT_MEMORY_LIMIT = 64 * 1024 * 1024;
struct Config {
    int port = 6380;
    std::size_t memory_limit = DEFAULT_MEMORY_LIMIT;
    MemoryPolicy memory_policy = MemoryPolicy::NoEviction;
};

Config parse_config(int argc, char **argv);
void validate_config(const Config &config);
} // namespace study
