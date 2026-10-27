#pragma once
#include "atlas/storage.hpp"
#include <asio.hpp>
#include <chrono>
#include <memory>
#include <string>

namespace atlas {
struct ServerConfig {
    std::string bind_address = "127.0.0.1";
    std::uint16_t port = 7070;
    std::size_t max_connections = 128;
    std::size_t max_pending = 128;
    std::chrono::milliseconds request_timeout{5000};
};
// Run the supplied io_context on ONE thread. Storage calls use one owned worker.
// stop() must be called on the I/O thread (or before/after io.run()).
class Server {
public:
    Server(asio::io_context& io, std::shared_ptr<IStorage> storage, ServerConfig config = {});
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    std::uint16_t port() const;
    void stop();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
