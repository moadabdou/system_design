#pragma once
#include <cstdint>
#include <string>

namespace study {
// All connection state is owned and updated by the event-loop thread.
struct Connection {
    int fd = -1;
    uint64_t token = 0;
    std::string input_buffer;
    std::string output_buffer;
    std::size_t output_offset = 0;
    bool close_after_write = false;
    bool peer_closed_write = false;

    bool has_pending_output() const {
        return output_offset < output_buffer.size();
    }
    void clear_output() {
        output_buffer.clear();
        output_offset = 0;
    }
};
} // namespace study
