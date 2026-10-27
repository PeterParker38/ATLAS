#include "atlas/client.hpp"
#include "atlas/server.hpp"
#include "test.hpp"
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

using namespace atlas;
using namespace std::chrono_literals;
class BlockingStorage : public IStorage {
    std::mutex mutex;
    std::condition_variable cv;
    bool released = false;
public:
    std::promise<void> entered;
    Result execute(const Command&) override {
        entered.set_value();
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return released; });
        return {Status::ok, "done"};
    }
    void release() { std::lock_guard lock(mutex); released = true; cv.notify_all(); }
};
struct RunningServer {
    asio::io_context io;
    std::shared_ptr<IStorage> storage;
    Server server;
    std::thread thread;
    explicit RunningServer(std::shared_ptr<IStorage> s, std::size_t pending = 1)
        : storage(std::move(s)), server(io, storage, {"127.0.0.1", 0, 32, pending, 5000ms}), thread([this] { io.run(); }) {}
    ~RunningServer() {
        if (auto blocking = std::dynamic_pointer_cast<BlockingStorage>(storage)) blocking->release();
        asio::post(io, [this] { server.stop(); io.stop(); });
        thread.join();
    }
};
int main() {
    return run_tests({
        {"persistent C++ client and binary values", [] {
            RunningServer s(std::make_shared<MemoryStorage>());
            Client client("127.0.0.1", s.server.port());
            CHECK(client.request(make_request(Op::ping, 1)).value == "PONG");
            CHECK(client.request(make_request(Op::put, 2, "k", std::string("\0x", 2))).status == Status::ok);
            CHECK(client.request(make_request(Op::get, 3, "k")).value == std::string("\0x", 2));
            client.close();
            CHECK(client.request(make_request(Op::get, 4, "k")).value == std::string("\0x", 2));
        }},
        {"slow storage does not block ping and queue is bounded", [] {
            auto storage = std::make_shared<BlockingStorage>();
            auto entered = storage->entered.get_future();
            RunningServer s(storage);
            auto first = std::async(std::launch::async, [&] { Client c("127.0.0.1", s.server.port()); return c.request(make_request(Op::put, 1, "a", "b")); });
            CHECK(entered.wait_for(2s) == std::future_status::ready);
            Client other("127.0.0.1", s.server.port(), 1000ms);
            CHECK(other.request(make_request(Op::ping, 2)).value == "PONG");
            CHECK(other.request(make_request(Op::put, 3, "c", "d")).status == Status::busy);
            storage->release();
            CHECK(first.get().status == Status::ok);
        }},
        {"client deadline closes connection without retrying mutation", [] {
            auto storage = std::make_shared<BlockingStorage>();
            RunningServer s(storage);
            Client c("127.0.0.1", s.server.port(), 100ms);
            const auto start = std::chrono::steady_clock::now();
            rejects([&] { c.request(make_request(Op::put, 1, "a", "b")); });
            CHECK(std::chrono::steady_clock::now() - start < 2s);
            storage->release();
            CHECK(c.request(make_request(Op::ping, 2)).status == Status::ok);
        }},
        {"storage exceptions become internal errors and session survives", [] {
            struct Throwing : IStorage { Result execute(const Command&) override { throw std::runtime_error("private detail"); } };
            RunningServer s(std::make_shared<Throwing>());
            Client c("127.0.0.1", s.server.port());
            auto result = c.request(make_request(Op::get, 1, "a"));
            CHECK(result.status == Status::internal); CHECK(result.value.find("private") == std::string::npos);
            CHECK(c.request(make_request(Op::ping, 2)).value == "PONG");
        }}
    });
}
