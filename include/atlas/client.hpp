#pragma once
#include "atlas/protocol.hpp"
#include <chrono>
#include <memory>
#include <string>

namespace atlas {
// Blocking facade over deadline-bounded asynchronous I/O. Not thread-safe;
// use one Client per caller. On any transport error the connection is closed.
// No transparent retries: a timed-out mutation may already have executed.
class Client {
public:
    Client(std::string host = "127.0.0.1", std::uint16_t port = 7070,
           std::chrono::milliseconds timeout = std::chrono::milliseconds{3000});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Result request(const Frame& request);
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
