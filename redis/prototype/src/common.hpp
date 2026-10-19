#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace study {
using Arguments = std::vector<std::string>;
constexpr std::size_t MAX_KEY = 4096;
constexpr std::size_t MAX_VALUE = 1024 * 1024;
constexpr std::size_t MAX_WIRE = 8 * 1024 * 1024;

int64_t parse_integer(std::string_view text);
} // namespace study
