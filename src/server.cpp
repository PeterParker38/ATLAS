#include "atlas/server.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace atlas {
using asio::ip::tcp;
struct Server::Impl : std::enable_shared_from_this<Server::Impl> {
    struct Session;
    asio::io_context& io;
    std::shared_ptr<IStorage> storage;
    ServerConfig config;
    tcp::acceptor acceptor;
    asio::thread_pool worker{1};
    std::unordered_map<std::uint64_t, std::shared_ptr<Session>> sessions;
    std::uint64_t next_session = 0;
    std::size_t pending = 0;
    std::uint64_t requests = 0, errors = 0, rejected_connections = 0, timeouts = 0;
    std::uint64_t completed = 0, latency_us_sum = 0;
    bool stopping = false;
    Impl(asio::io_context& context, std::shared_ptr<IStorage> store, ServerConfig cfg)
        : io(context), storage(std::move(store)), config(std::move(cfg)), acceptor(io) {
        if (!storage || config.max_connections == 0 || config.max_pending == 0 || config.request_timeout.count() <= 0)
            throw std::invalid_argument("invalid server configuration");
        const tcp::endpoint endpoint(asio::ip::make_address(config.bind_address), config.port);
        acceptor.open(endpoint.protocol());
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen(asio::socket_base::max_listen_connections);
    }
    void accept();
    void dispatch(const std::shared_ptr<Session>& session, Frame frame);
    void stop();
    std::string stats() const {
        std::ostringstream out;
        out << "{\"role\":\"standalone\",\"storage\":\"adapter\",\"connections\":" << sessions.size()
            << ",\"pending\":" << pending << ",\"requests\":" << requests
            << ",\"errors\":" << errors << ",\"rejected_connections\":" << rejected_connections
            << ",\"timeouts\":" << timeouts << ",\"storage_completed\":" << completed
            << ",\"storage_latency_us_sum\":" << latency_us_sum << "}";
        return out.str();
    }
};

struct Server::Impl::Session : std::enable_shared_from_this<Session> {
    std::weak_ptr<Impl> owner;
    std::uint64_t id;
    tcp::socket socket;
    asio::steady_timer timer;
    std::array<std::uint8_t, header_size> head{};
    std::vector<std::uint8_t> body, output;
    Header current{};
    bool closed = false;
    Session(std::shared_ptr<Impl> parent, std::uint64_t sid, tcp::socket sock)
        : owner(parent), id(sid), socket(std::move(sock)), timer(socket.get_executor()) {}
    void close() {
        if (closed) return;
        closed = true;
        timer.cancel();
        asio::error_code ec;
        socket.cancel(ec); socket.shutdown(tcp::socket::shutdown_both, ec); socket.close(ec);
        if (auto p = owner.lock()) p->sessions.erase(id);
    }
    void start() {
        if (closed) return;
        auto p = owner.lock();
        if (!p || p->stopping) return close();
        timer.expires_after(p->config.request_timeout);
        timer.async_wait([self = shared_from_this()](const asio::error_code& ec) {
            if (!ec) {
                if (auto parent = self->owner.lock()) ++parent->timeouts;
                self->close();
            }
        });
        asio::async_read(socket, asio::buffer(head), [self = shared_from_this()](const asio::error_code& ec, std::size_t) {
            if (ec || self->closed) return self->close();
            try {
                self->current = decode_header(self->head);
                if (self->current.kind != Kind::request) throw ProtocolError("client sent response frame");
                self->body.resize(static_cast<std::size_t>(self->current.key_size) + self->current.value_size);
            } catch (const ProtocolError&) {
                if (auto p2 = self->owner.lock()) ++p2->errors;
                return self->close(); // Do not trust malformed headers enough to allocate or respond.
            }
            self->read_body();
        });
    }
    void read_body() {
        asio::async_read(socket, asio::buffer(body), [self = shared_from_this()](const asio::error_code& ec, std::size_t) {
            if (ec || self->closed) return self->close();
            if (auto p = self->owner.lock()) p->dispatch(self, decode_frame(self->current, self->body));
            else self->close();
        });
    }
    void reply(std::uint64_t request_id, Result result) {
        if (closed) return;
        if (result.status != Status::ok) { if (auto p = owner.lock()) ++p->errors; }
        try { output = encode_frame(make_response(request_id, std::move(result))); }
        catch (const ProtocolError&) {
            output = encode_frame(make_response(request_id, {Status::internal, "invalid storage response"}));
        }
        asio::async_write(socket, asio::buffer(output), [self = shared_from_this()](const asio::error_code& ec, std::size_t) {
            if (ec || self->closed) return self->close();
            self->timer.cancel();
            self->start(); // One request in flight per session; kernel buffers bound pipelining.
        });
    }
};

void Server::Impl::accept() {
    if (stopping) return;
    acceptor.async_accept([self = shared_from_this()](const asio::error_code& ec, tcp::socket socket) {
        if (self->stopping) return;
        if (!ec) {
            if (self->sessions.size() >= self->config.max_connections) {
                ++self->rejected_connections;
                asio::error_code ignored; socket.close(ignored);
            } else {
                asio::error_code ignored; socket.set_option(tcp::no_delay(true), ignored);
                auto session = std::make_shared<Session>(self, ++self->next_session, std::move(socket));
                self->sessions.emplace(session->id, session);
                session->start();
            }
        }
        if (ec && ec != asio::error::operation_aborted) {
            // Stop on resource exhaustion instead of busy-looping failed accepts.
            std::cerr << "accept failed: " << ec.message() << '\n';
            self->stop(); self->io.stop(); return;
        }
        self->accept();
    });
}
void Server::Impl::dispatch(const std::shared_ptr<Session>& session, Frame frame) {
    ++requests;
    Command command{};
    try { command = decode_command(frame); }
    catch (const ProtocolError& e) { return session->reply(frame.request_id, {Status::invalid, e.what()}); }
    if (command.op == Op::ping) return session->reply(command.request_id, {Status::ok, "PONG"});
    if (command.op == Op::stats) return session->reply(command.request_id, {Status::ok, stats()});
    if (pending >= config.max_pending) return session->reply(command.request_id, {Status::busy, "storage queue is full"});
    ++pending;
    const auto started = std::chrono::steady_clock::now();
    asio::post(worker, [self = shared_from_this(), session, command = std::move(command), started] {
        Result result;
        try { result = self->storage->execute(command); }
        catch (const std::exception&) { result = {Status::internal, "storage operation failed"}; }
        catch (...) { result = {Status::internal, "storage operation failed"}; }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
        asio::post(self->io, [self, session, id = command.request_id, result = std::move(result), elapsed]() mutable {
            --self->pending; ++self->completed;
            self->latency_us_sum += static_cast<std::uint64_t>(elapsed);
            if (!self->stopping) session->reply(id, std::move(result));
        });
    });
}
void Server::Impl::stop() {
    if (stopping) return;
    stopping = true;
    asio::error_code ec; acceptor.cancel(ec); acceptor.close(ec);
    const auto copy = sessions;
    for (const auto& [id, session] : copy) { (void)id; session->close(); }
}
Server::Server(asio::io_context& io, std::shared_ptr<IStorage> storage, ServerConfig config)
    : impl_(std::make_shared<Impl>(io, std::move(storage), std::move(config))) { impl_->accept(); }
Server::~Server() {
    impl_->stop();
    impl_->worker.join();
    // Drain completions before Impl releases its pool. The io_context must outlive Server.
    impl_->io.restart();
    while (impl_->io.poll() != 0) {}
}
std::uint16_t Server::port() const { return impl_->acceptor.local_endpoint().port(); }
void Server::stop() { impl_->stop(); }
}
