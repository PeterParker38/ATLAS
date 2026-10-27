#include "atlas/client.hpp"
#include <asio.hpp>
#include <array>
#include <functional>
#include <utility>

namespace atlas {
using asio::ip::tcp;
struct Client::Impl {
    asio::io_context io;
    tcp::resolver resolver{io};
    tcp::socket socket{io};
    asio::steady_timer timer{io};
    std::string host;
    std::uint16_t port;
    std::chrono::milliseconds timeout;
    Impl(std::string h, std::uint16_t p, std::chrono::milliseconds t) : host(std::move(h)), port(p), timeout(t) {
        if (host.empty() || port == 0 || timeout.count() <= 0) throw std::invalid_argument("invalid client configuration");
    }
    void close() { asio::error_code ec; resolver.cancel(); socket.cancel(ec); socket.close(ec); }
    Result request(const Frame& frame) {
        (void)decode_command(frame);
        auto outgoing = encode_frame(frame);
        std::array<std::uint8_t, header_size> header_bytes{};
        std::vector<std::uint8_t> body;
        Header header{};
        Result result;
        std::string error;
        bool done = false;
        auto finish = [&](std::string reason = {}) {
            if (done) return;
            done = true; error = std::move(reason); timer.cancel();
            if (!error.empty()) close();
        };
        auto read_header = [&] {
            asio::async_read(socket, asio::buffer(header_bytes), [&](const asio::error_code& ec, std::size_t) {
                if (done) return;
                if (ec) return finish("read header: " + ec.message());
                try {
                    header = decode_header(header_bytes);
                    if (header.kind != Kind::response || header.request_id != frame.request_id) throw ProtocolError("response correlation mismatch");
                    body.resize(static_cast<std::size_t>(header.key_size) + header.value_size);
                } catch (const ProtocolError& e) { return finish(e.what()); }
                asio::async_read(socket, asio::buffer(body), [&](const asio::error_code& body_ec, std::size_t) {
                    if (done) return;
                    if (body_ec) return finish("read body: " + body_ec.message());
                    try {
                        auto response = decode_frame(header, body);
                        result = {static_cast<Status>(response.code), std::move(response.value)};
                        finish();
                    } catch (const ProtocolError& e) { finish(e.what()); }
                });
            });
        };
        auto write = [&] {
            asio::async_write(socket, asio::buffer(outgoing), [&](const asio::error_code& ec, std::size_t) {
                if (done) return;
                if (ec) return finish("write: " + ec.message());
                read_header();
            });
        };
        io.restart();
        timer.expires_after(timeout);
        timer.async_wait([&](const asio::error_code& ec) {
            if (!ec) finish("request deadline exceeded; mutation outcome may be unknown");
        });
        if (socket.is_open()) write();
        else resolver.async_resolve(host, std::to_string(port), [&](const asio::error_code& ec, tcp::resolver::results_type endpoints) {
            if (done) return;
            if (ec) return finish("resolve: " + ec.message());
            asio::async_connect(socket, endpoints, [&](const asio::error_code& connect_ec, const tcp::endpoint&) {
                if (done) return;
                if (connect_ec) return finish("connect: " + connect_ec.message());
                asio::error_code ignored; socket.set_option(tcp::no_delay(true), ignored);
                write();
            });
        });
        // Drain cancelled callbacks before locals go out of scope.
        io.run();
        if (!error.empty()) throw std::runtime_error(error);
        return result;
    }
};
Client::Client(std::string host, std::uint16_t port, std::chrono::milliseconds timeout)
    : impl_(std::make_unique<Impl>(std::move(host), port, timeout)) {}
Client::~Client() = default;
Result Client::request(const Frame& frame) { return impl_->request(frame); }
void Client::close() { impl_->close(); }
}
