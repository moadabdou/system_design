#include "config.hpp"
#include "server.hpp"
#include <csignal>
#include <exception>
#include <iostream>

namespace {
volatile std::sig_atomic_t running = 1;
void request_stop(int) {
    running = 0;
}
} // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        study::Server server(study::parse_config(argc, argv));
        server.run(running);
    } catch (const std::exception &error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
