#include "protocol.hpp"
#include "server.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace study {

namespace {
constexpr std::size_t MAX_CONNECTIONS = 256;
constexpr int ACCEPTS_PER_TURN = 32;
constexpr int READS_PER_TURN = 16;
constexpr int COMMANDS_PER_PASS = 32;
constexpr std::size_t READ_BUFFER_BYTES = 16384;
constexpr std::size_t WRITE_BYTES_PER_TURN = 256 * 1024;
} // namespace

void Server::initialize_listener() {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (epoll_fd_ < 0 || listen_fd_ < 0) {
        throw std::runtime_error("cannot create sockets/epoll");
    }
    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(config_.port));
    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        ::listen(listen_fd_, 128) != 0) {
        throw std::runtime_error("cannot bind/listen: " + std::string(strerror(errno)));
    }
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = 0;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) != 0) {
        throw std::runtime_error("cannot register listener");
    }
}

void Server::poll_network() {
    std::array<epoll_event, 128> events{};
    int count = ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), 10);
    if (count < 0 && errno != EINTR) {
        throw std::runtime_error("epoll_wait failed");
    }

    for (int i = 0; i < count; ++i) {
        if (events[i].data.u64 == 0) {
            accept_connections();
            continue;
        }
        // The token includes a generation: an old event must not reach a new
        // connection that happens to reuse the same descriptor.
        int fd = static_cast<int>(events[i].data.u64 & 0xffffffffU);
        auto connection = connections_.find(fd);
        if (connection != connections_.end() && connection->second.token == events[i].data.u64) {
            handle_socket_event(fd, events[i].events);
        }
    }
}

void Server::accept_connections() {
    for (int accepted = 0; accepted < ACCEPTS_PER_TURN; ++accepted) {
        int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            break;
        }
        if (connections_.size() >= MAX_CONNECTIONS) {
            ::close(fd);
            continue;
        }
        add_connection(fd);
    }
}

bool Server::read_available(Connection &connection) {
    std::array<char, READ_BUFFER_BYTES> buffer{};
    // A ready socket must not monopolize the thread. Cap bytes as well as commands.
    for (int attempt = 0; attempt < READS_PER_TURN; ++attempt) {
        ssize_t received = ::recv(connection.fd, buffer.data(), buffer.size(), 0);
        if (received > 0) {
            connection.input_buffer.append(buffer.data(), static_cast<std::size_t>(received));
            if (connection.input_buffer.size() > MAX_WIRE) {
                connection.close_after_write = true;
                connection.clear_output();
                break;
            }
        } else if (received == 0) {
            connection.peer_closed_write = true;
            break;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            return false;
        }
    }
    return true;
}

bool Server::write_available(Connection &connection) {
    std::size_t budget = WRITE_BYTES_PER_TURN;
    while (connection.has_pending_output() && budget > 0) {
        const auto remaining = connection.output_buffer.size() - connection.output_offset;
        ssize_t written =
            ::send(connection.fd, connection.output_buffer.data() + connection.output_offset,
                   std::min(budget, remaining), MSG_NOSIGNAL);
        if (written > 0) {
            connection.output_offset += static_cast<std::size_t>(written);
            budget -= static_cast<std::size_t>(written);
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            return false;
        }
    }
    if (!connection.has_pending_output()) {
        connection.clear_output();
    }
    return true;
}

void Server::handle_socket_event(int fd, uint32_t events) {
    auto &connection = connections_.at(fd);
    if (events & EPOLLERR) {
        close_connection(fd);
        return;
    }
    if ((events & EPOLLIN) && !read_available(connection)) {
        close_connection(fd);
        return;
    }
    if (connection.peer_closed_write && connection.input_buffer.empty()) {
        connection.close_after_write = true;
    }
    if ((events & EPOLLOUT) && !write_available(connection)) {
        close_connection(fd);
        return;
    }
    const bool finished_reply = connection.close_after_write && !connection.has_pending_output();
    const bool hung_up = (events & EPOLLHUP) && connection.output_buffer.empty();
    if (finished_reply || hung_up) {
        close_connection(fd);
    } else {
        update_interest(connection);
    }
}

void Server::finish_half_closed_input(Connection &connection) {
    if (!connection.peer_closed_write || connection.close_after_write) {
        return;
    }
    Arguments trailing;
    bool incomplete = connection.input_buffer.empty();
    try {
        if (!incomplete) {
            incomplete = parse_command(connection.input_buffer, trailing) == 0;
        }
    } catch (const std::exception &) {
        incomplete = true;
    }
    if (incomplete) {
        connection.input_buffer.clear();
        connection.close_after_write = true;
    }
}

void Server::drain_buffered_commands() {
    // The only command-processing pass: each connection gets at most
    // COMMANDS_PER_PASS commands per event-loop iteration, even without a new
    // read-ready notification for commands already buffered in memory.
    std::vector<int> pending;
    for (const auto &[fd, connection] : connections_) {
        if (!connection.input_buffer.empty() || connection.close_after_write) {
            pending.push_back(fd);
        }
    }
    for (int fd : pending) {
        if (!connections_.contains(fd)) {
            continue;
        }
        auto &connection = connections_.at(fd);
        if (!connection.close_after_write) {
            process_input(connection);
            finish_half_closed_input(connection);
        }
        if (connection.close_after_write && !connection.has_pending_output()) {
            close_connection(fd);
        }
    }
}

void Server::update_interest(Connection &connection) {
    epoll_event event{};
    event.events = connection.peer_closed_write ? 0U : (EPOLLRDHUP | EPOLLIN);
    if (connection.has_pending_output()) {
        event.events |= EPOLLOUT;
    }
    event.data.u64 = connection.token;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection.fd, &event) != 0) {
        throw std::runtime_error("epoll update failed");
    }
}

void Server::queue_reply(Connection &connection, const std::string &reply) {
    // TODO(memory): Add a server-wide input/output buffer budget. Per-connection
    // limits alone can still consume substantial memory across 256 clients.
    if (connection.close_after_write) {
        return;
    }
    if (connection.output_offset) {
        connection.output_buffer.erase(0, connection.output_offset);
        connection.output_offset = 0;
    }
    if (reply.size() > MAX_WIRE || connection.output_buffer.size() > MAX_WIRE - reply.size()) {
        connection.close_after_write = true;
        connection.output_buffer.clear();
        return;
    }
    connection.output_buffer += reply;
    update_interest(connection);
}

void Server::close_connection(int fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    connections_.erase(fd);
}

void Server::add_connection(int fd) {
    if (connections_.size() >= MAX_CONNECTIONS) {
        ::close(fd);
        throw std::runtime_error("connection limit reached");
    }
    uint64_t token = (next_connection_token_++ << 32) | static_cast<uint32_t>(fd);
    Connection connection;
    connection.fd = fd;
    connection.token = token;
    connections_.emplace(fd, std::move(connection));
    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.u64 = token;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) != 0) {
        ::close(fd);
        connections_.erase(fd);
        throw std::runtime_error("epoll add failed");
    }
}

void Server::process_input(Connection &connection) {
    for (int processed = 0; processed < COMMANDS_PER_PASS && !connection.input_buffer.empty() &&
                            !connection.close_after_write;
         ++processed) {
        Arguments arguments;
        std::size_t consumed_bytes;
        try {
            consumed_bytes = parse_command(connection.input_buffer, arguments);
        } catch (const std::exception &error) {
            queue_reply(connection, "-ERR " + std::string(error.what()) + "\r\n");
            connection.close_after_write = true;
            connection.input_buffer.clear();
            return;
        }
        if (consumed_bytes == 0) {
            return;
        }
        connection.input_buffer.erase(0, consumed_bytes);
        try {
            dispatch_command(connection, arguments);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (const MemoryError &error) {
            queue_reply(connection, "-OOM " + std::string(error.what()) + "\r\n");
        } catch (const std::exception &error) {
            queue_reply(connection, "-ERR " + std::string(error.what()) + "\r\n");
        }
    }
}

} // namespace study
