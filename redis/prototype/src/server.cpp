#include "server.hpp"
#include <iostream>
#include <unistd.h>
#include <utility>

namespace study {
namespace {
constexpr std::size_t EXPIRATIONS_PER_PASS = 128;
}
Server::Server(Config config) : config_(std::move(config)) {
    initialize_listener();
}

Server::~Server() {
    for (const auto &[fd, connection] : connections_) {
        (void)connection;
        ::close(fd);
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
}

void Server::run(const volatile std::sig_atomic_t &running) {
    std::cout << "listening 127.0.0.1:" << config_.port << std::endl;
    while (running) {
        poll_network();
        drain_buffered_commands();
        store_.expire_due(EXPIRATIONS_PER_PASS);
    }
}
} // namespace study
