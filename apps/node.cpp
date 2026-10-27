#include "atlas/server.hpp"
#include <charconv>
#include <csignal>
#include <iostream>
#include <string_view>

namespace {
std::size_t number(std::string_view value, std::size_t maximum) {
    std::size_t n = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
    if (ec != std::errc{} || end != value.data() + value.size() || n > maximum) throw std::invalid_argument("invalid numeric option");
    return n;
}
}
int main(int argc, char** argv) {
    try {
        atlas::ServerConfig config;
        bool memory_demo = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--help") {
                std::cout << "atlas-node --memory-demo [--bind IP] [--port 7070] [--max-connections 128] [--max-pending 128] [--timeout-ms 5000]\n"
                          << "Volatile integration adapter only. No WAL, authentication, or replication. Bind private interfaces only.\n";
                return 0;
            }
            if (arg == "--memory-demo") { memory_demo = true; continue; }
            if (i + 1 >= argc) throw std::invalid_argument("missing option value");
            const std::string_view value = argv[++i];
            if (arg == "--bind") config.bind_address = value;
            else if (arg == "--port") config.port = static_cast<std::uint16_t>(number(value, 65535));
            else if (arg == "--max-connections") config.max_connections = number(value, 4096);
            else if (arg == "--max-pending") config.max_pending = number(value, 4096);
            else if (arg == "--timeout-ms") config.request_timeout = std::chrono::milliseconds(number(value, 600000));
            else throw std::invalid_argument("unknown option: " + std::string(arg));
        }
        if (!memory_demo) throw std::invalid_argument("pass --memory-demo to acknowledge volatile storage; persistent adapter is Developer 1's integration point");
        asio::io_context io;
        atlas::Server server(io, std::make_shared<atlas::MemoryStorage>(), config);
        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](const asio::error_code& ec, int) { if (!ec) { server.stop(); io.stop(); } });
        std::cout << "ATLAS_READY " << config.bind_address << ':' << server.port() << " storage=memory-demo durability=none\n" << std::flush;
        io.run();
        return 0;
    } catch (const std::exception& e) { std::cerr << "atlas-node: " << e.what() << '\n'; return 1; }
}
