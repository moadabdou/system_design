#include "common.hpp"
#include <charconv>
#include <stdexcept>

namespace study {
int64_t parse_integer(std::string_view text) {
    int64_t value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        throw std::runtime_error("invalid integer");
    }
    return value;
}
} // namespace study
