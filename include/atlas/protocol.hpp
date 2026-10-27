#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace atlas {
inline constexpr std::size_t header_size = 24;
inline constexpr std::size_t max_key_size = 256;
inline constexpr std::size_t max_value_size = 16 * 1024;
inline constexpr std::size_t max_body_size = 256 * 1024;
inline constexpr std::size_t max_batch_count = 100;
enum class Kind : std::uint8_t { request = 0, response = 1 };
enum class Op : std::uint16_t { ping = 1, get = 2, put = 3, erase = 4, batch = 5, stats = 6 };
enum class Status : std::uint16_t { ok = 0, not_found = 1, invalid = 2, busy = 3, internal = 4, not_implemented = 5, resource_limit = 6 };
struct Header {
    Kind kind;
    std::uint16_t code;
    std::uint64_t request_id;
    std::uint32_t key_size;
    std::uint32_t value_size;
};
struct Frame {
    Kind kind = Kind::request;
    std::uint16_t code = 0;
    std::uint64_t request_id = 0;
    std::string key;
    std::string value;
};
struct Mutation { Op op; std::string key; std::string value; };
struct Command {
    Op op;
    std::uint64_t request_id;
    std::string key;
    std::string value;
    std::vector<Mutation> mutations;
};
struct Result { Status status = Status::ok; std::string value; };
class ProtocolError : public std::runtime_error { using std::runtime_error::runtime_error; };
Header decode_header(std::span<const std::uint8_t> bytes);
Frame decode_frame(const Header& header, std::span<const std::uint8_t> body);
std::vector<std::uint8_t> encode_frame(const Frame& frame);
Command decode_command(const Frame& frame);
Frame make_request(Op op, std::uint64_t id, std::string key = {}, std::string value = {});
Frame make_batch(std::uint64_t id, const std::vector<Mutation>& mutations);
Frame make_response(std::uint64_t id, Result result);
const char* status_name(Status status);
}
