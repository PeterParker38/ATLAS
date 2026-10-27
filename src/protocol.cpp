#include "atlas/protocol.hpp"
#include <utility>

namespace atlas {
namespace {
void append(std::vector<std::uint8_t>& out, std::uint64_t value, unsigned count) {
    for (unsigned i = count; i > 0; --i) out.push_back(static_cast<std::uint8_t>((value >> ((i - 1) * 8)) & 0xff));
}
std::uint64_t read(std::span<const std::uint8_t> bytes, std::size_t offset, unsigned count) {
    if (offset > bytes.size() || count > bytes.size() - offset) throw ProtocolError("truncated integer");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value = (value << 8) | bytes[offset + i];
    return value;
}
void validate(const Header& h) {
    if (h.kind != Kind::request && h.kind != Kind::response) throw ProtocolError("invalid frame kind");
    if (h.request_id == 0) throw ProtocolError("request ID must be nonzero");
    if (h.key_size > max_key_size || h.value_size > max_body_size ||
        static_cast<std::uint64_t>(h.key_size) + h.value_size > max_body_size) throw ProtocolError("frame exceeds limit");
    if (h.kind == Kind::response && (h.key_size != 0 || h.code > static_cast<std::uint16_t>(Status::resource_limit))) throw ProtocolError("invalid response");
}
void validate_mutation(const Mutation& m) {
    if (m.op != Op::put && m.op != Op::erase) throw ProtocolError("batch supports PUT and DELETE only");
    if (m.key.empty() || m.key.size() > max_key_size) throw ProtocolError("invalid key length");
    if (m.value.size() > max_value_size || (m.op == Op::erase && !m.value.empty())) throw ProtocolError("invalid mutation value");
}
}
Header decode_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != header_size) throw ProtocolError("incorrect header length");
    if (read(bytes, 0, 4) != 0x41544c53 || bytes[4] != 1) throw ProtocolError("bad magic or unsupported version");
    Header h{static_cast<Kind>(bytes[5]), static_cast<std::uint16_t>(read(bytes, 6, 2)), read(bytes, 8, 8),
             static_cast<std::uint32_t>(read(bytes, 16, 4)), static_cast<std::uint32_t>(read(bytes, 20, 4))};
    validate(h);
    return h;
}
Frame decode_frame(const Header& h, std::span<const std::uint8_t> body) {
    validate(h);
    if (body.size() != static_cast<std::size_t>(h.key_size) + h.value_size) throw ProtocolError("incorrect body length");
    Frame f{h.kind, h.code, h.request_id, {}, {}};
    if (h.key_size) f.key.assign(reinterpret_cast<const char*>(body.data()), h.key_size);
    if (h.value_size) f.value.assign(reinterpret_cast<const char*>(body.data() + h.key_size), h.value_size);
    return f;
}
std::vector<std::uint8_t> encode_frame(const Frame& f) {
    if (f.key.size() > max_key_size || f.value.size() > max_body_size) throw ProtocolError("frame exceeds limit");
    validate({f.kind, f.code, f.request_id, static_cast<std::uint32_t>(f.key.size()), static_cast<std::uint32_t>(f.value.size())});
    std::vector<std::uint8_t> out;
    out.reserve(header_size + f.key.size() + f.value.size());
    append(out, 0x41544c53, 4); append(out, 1, 1); append(out, static_cast<std::uint8_t>(f.kind), 1);
    append(out, f.code, 2); append(out, f.request_id, 8); append(out, f.key.size(), 4); append(out, f.value.size(), 4);
    out.insert(out.end(), f.key.begin(), f.key.end()); out.insert(out.end(), f.value.begin(), f.value.end());
    return out;
}
Command decode_command(const Frame& f) {
    if (f.kind != Kind::request || f.request_id == 0) throw ProtocolError("expected request");
    Command c{static_cast<Op>(f.code), f.request_id, f.key, f.value, {}};
    switch (c.op) {
    case Op::ping: case Op::stats:
        if (!f.key.empty() || !f.value.empty()) throw ProtocolError("command has no payload");
        break;
    case Op::get: case Op::erase:
        if (f.key.empty() || f.key.size() > max_key_size || !f.value.empty()) throw ProtocolError("invalid key or unexpected value");
        break;
    case Op::put:
        validate_mutation({Op::put, f.key, f.value});
        break;
    case Op::batch: {
        if (!f.key.empty() || f.value.size() > max_body_size) throw ProtocolError("invalid batch envelope");
        auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(f.value.data()), f.value.size());
        const auto count = read(bytes, 0, 4);
        if (count == 0 || count > max_batch_count) throw ProtocolError("invalid batch count");
        std::size_t offset = 4;
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto op = static_cast<Op>(read(bytes, offset, 2));
            const auto ks = read(bytes, offset + 2, 4), vs = read(bytes, offset + 6, 4);
            offset += 10;
            if (ks > max_key_size || vs > max_value_size || ks + vs > bytes.size() - offset) throw ProtocolError("invalid batch lengths");
            Mutation m{op, f.value.substr(offset, static_cast<std::size_t>(ks)), f.value.substr(offset + static_cast<std::size_t>(ks), static_cast<std::size_t>(vs))};
            validate_mutation(m); c.mutations.push_back(std::move(m)); offset += static_cast<std::size_t>(ks + vs);
        }
        if (offset != bytes.size()) throw ProtocolError("trailing batch bytes");
        break;
    }
    default: throw ProtocolError("unknown operation");
    }
    return c;
}
Frame make_request(Op op, std::uint64_t id, std::string key, std::string value) {
    Frame f{Kind::request, static_cast<std::uint16_t>(op), id, std::move(key), std::move(value)};
    (void)decode_command(f);
    return f;
}
Frame make_batch(std::uint64_t id, const std::vector<Mutation>& mutations) {
    if (mutations.empty() || mutations.size() > max_batch_count) throw ProtocolError("invalid batch count");
    std::vector<std::uint8_t> bytes;
    append(bytes, mutations.size(), 4);
    for (const auto& m : mutations) {
        validate_mutation(m);
        if (bytes.size() + 10 + m.key.size() + m.value.size() > max_body_size) throw ProtocolError("batch exceeds limit");
        append(bytes, static_cast<std::uint16_t>(m.op), 2); append(bytes, m.key.size(), 4); append(bytes, m.value.size(), 4);
        bytes.insert(bytes.end(), m.key.begin(), m.key.end()); bytes.insert(bytes.end(), m.value.begin(), m.value.end());
    }
    return make_request(Op::batch, id, {}, std::string(bytes.begin(), bytes.end()));
}
Frame make_response(std::uint64_t id, Result r) { return {Kind::response, static_cast<std::uint16_t>(r.status), id, {}, std::move(r.value)}; }
const char* status_name(Status s) {
    switch (s) {
    case Status::ok: return "OK"; case Status::not_found: return "NOT_FOUND";
    case Status::invalid: return "INVALID"; case Status::busy: return "BUSY";
    case Status::internal: return "INTERNAL"; case Status::not_implemented: return "NOT_IMPLEMENTED";
    case Status::resource_limit: return "RESOURCE_LIMIT";
    }
    return "UNKNOWN";
}
}
