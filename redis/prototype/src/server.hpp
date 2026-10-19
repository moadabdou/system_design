#pragma once
#include "common.hpp"
#include "config.hpp"
#include "connection.hpp"
#include "store.hpp"
#include <csignal>
#include <unordered_map>

namespace study {
// One thread owns all sockets, input/output buffers, and the in-memory store.
class Server {
  public:
    explicit Server(Config config);
    ~Server();
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;
    void run(const volatile std::sig_atomic_t &running);

  private:
    // Socket lifecycle and event processing (server_network.cpp).
    void initialize_listener();
    void poll_network();
    void accept_connections();
    void add_connection(int fd);
    void close_connection(int fd);
    void update_interest(Connection &connection);
    void queue_reply(Connection &connection, const std::string &reply);
    void handle_socket_event(int fd, uint32_t events);
    bool read_available(Connection &connection);
    bool write_available(Connection &connection);
    void process_input(Connection &connection);
    void drain_buffered_commands();
    void finish_half_closed_input(Connection &connection);

    // Complete commands execute sequentially (server_commands.cpp).
    void dispatch_command(Connection &connection, const Arguments &arguments);

    Config config_;
    Store store_;
    int epoll_fd_ = -1;
    int listen_fd_ = -1;
    uint64_t next_connection_token_ = 1;
    std::unordered_map<int, Connection> connections_;
};
} // namespace study
