#include "atlas/client.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>

namespace {
std::uint64_t number(std::string_view text, std::uint64_t max) {
    std::uint64_t result = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (ec != std::errc{} || end != text.data() + text.size() || result == 0 || result > max) throw std::invalid_argument("invalid numeric option");
    return result;
}
}
int main(int argc, char** argv) {
    try {
        std::string host = "127.0.0.1";
        std::uint16_t port = 7070;
        std::uint64_t id = 1;
        std::chrono::milliseconds timeout{3000};
        int i = 1;
        for (; i < argc && std::string_view(argv[i]).starts_with("--"); ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help") {
                std::cout << "atlas-cli [--host HOST] [--port PORT] [--timeout-ms MS] [--request-id ID] ping|stats|get KEY|put KEY VALUE|put-file KEY FILE|delete KEY|batch (put KEY VALUE|delete KEY)...\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::invalid_argument("missing option value");
            std::string_view value = argv[++i];
            if (arg == "--host") host = value;
            else if (arg == "--port") port = static_cast<std::uint16_t>(number(value, 65535));
            else if (arg == "--timeout-ms") timeout = std::chrono::milliseconds(number(value, 600000));
            else if (arg == "--request-id") id = number(value, UINT64_MAX);
            else throw std::invalid_argument("unknown option");
        }
        if (i >= argc) throw std::invalid_argument("missing command; use --help");
        const std::string_view op = argv[i++];
        atlas::Frame frame;
        if (op == "ping" || op == "stats") {
            if (i != argc) throw std::invalid_argument("unexpected arguments");
            frame = atlas::make_request(op == "ping" ? atlas::Op::ping : atlas::Op::stats, id);
        } else if (op == "get" || op == "delete") {
            if (argc - i != 1) throw std::invalid_argument("expected one key");
            frame = atlas::make_request(op == "get" ? atlas::Op::get : atlas::Op::erase, id, argv[i]);
        } else if (op == "put" || op == "put-file") {
            if (argc - i != 2) throw std::invalid_argument("expected key and value/file");
            std::string value;
            if (op == "put-file") {
                std::ifstream file(argv[i + 1], std::ios::binary);
                if (!file) throw std::invalid_argument("cannot open value file");
                value.resize(atlas::max_value_size + 1);
                file.read(value.data(), static_cast<std::streamsize>(value.size()));
                value.resize(static_cast<std::size_t>(file.gcount()));
                if (file.bad()) throw std::runtime_error("cannot read value file");
            } else value = argv[i + 1];
            frame = atlas::make_request(atlas::Op::put, id, argv[i], std::move(value));
        } else if (op == "batch") {
            std::vector<atlas::Mutation> mutations;
            while (i < argc) {
                const std::string_view action = argv[i++];
                if (action == "put" && argc - i >= 2) { mutations.push_back({atlas::Op::put, argv[i], argv[i + 1]}); i += 2; }
                else if (action == "delete" && i < argc) mutations.push_back({atlas::Op::erase, argv[i++], {}});
                else throw std::invalid_argument("batch expects put KEY VALUE or delete KEY");
            }
            frame = atlas::make_batch(id, mutations);
        } else throw std::invalid_argument("unknown command");
        atlas::Client client(host, port, timeout);
        auto result = client.request(frame);
        if (result.status != atlas::Status::ok) {
            std::cerr << atlas::status_name(result.status) << ": " << result.value << '\n';
            return result.status == atlas::Status::not_found ? 2 : 3;
        }
        if (op == "get") std::cout.write(result.value.data(), static_cast<std::streamsize>(result.value.size()));
        else std::cout << (result.value.empty() ? "OK" : result.value) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "atlas-cli: " << e.what() << '\n'; return 1; }
}
