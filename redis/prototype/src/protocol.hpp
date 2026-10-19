#pragma once
#include "common.hpp"

namespace study {
std::string encode_bulk(std::string_view value);
std::string encode_array(const Arguments &arguments);
// Returns consumed bytes, or zero if the command is incomplete.
// Malformed or oversized input throws before any command is executed.
std::size_t parse_command(std::string_view input, Arguments &arguments,
                          std::size_t limit = MAX_WIRE);
} // namespace study
