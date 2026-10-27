#include "atlas/protocol.hpp"
#include "atlas/storage.hpp"
#include "test.hpp"
#include <random>

using namespace atlas;
Frame roundtrip(const Frame& frame) {
    auto bytes = encode_frame(frame);
    auto h = decode_header(std::span(bytes).first(header_size));
    return decode_frame(h, std::span(bytes).subspan(header_size));
}
int main() {
    return run_tests({
        {"golden byte order", [] {
            auto bytes = encode_frame(make_request(Op::put, 0x0102030405060708ULL, "k", "v"));
            const std::vector<std::uint8_t> expected{65,84,76,83,1,0,0,3,1,2,3,4,5,6,7,8,0,0,0,1,0,0,0,1,107,118};
            CHECK(bytes == expected);
        }},
        {"binary round trip", [] {
            auto f = roundtrip(make_request(Op::put, UINT64_MAX, std::string("a\0b", 3), std::string("\0\xffx", 3)));
            CHECK(f.request_id == UINT64_MAX); CHECK(f.key.size() == 3); CHECK(f.value == std::string("\0\xffx", 3));
        }},
        {"header rejects bad magic version kind id lengths", [] {
            auto good = encode_frame(make_request(Op::ping, 1));
            for (auto offset : {0, 4, 5}) { auto bad = good; bad[static_cast<std::size_t>(offset)] = 255; rejects([&] { decode_header(bad); }); }
            auto bad = good; bad[15] = 0; rejects([&] { decode_header(bad); });
            bad = good; bad[16] = 255; rejects([&] { decode_header(bad); });
            bad = good; bad[20] = 255; rejects([&] { decode_header(bad); });
            rejects([&] { decode_header(std::span(good).first(23)); });
        }},
        {"operation validation", [] {
            rejects([] { make_request(Op::get, 1); });
            rejects([] { make_request(Op::get, 1, "k", "v"); });
            rejects([] { make_request(Op::ping, 1, "k"); });
            rejects([] { make_request(static_cast<Op>(42), 1); });
            rejects([] { make_request(Op::put, 1, std::string(257, 'k'), "v"); });
            rejects([] { make_request(Op::put, 1, "k", std::string(max_value_size + 1, 'v')); });
            CHECK(roundtrip(make_request(Op::put, 1, "k", "")).value.empty());
        }},
        {"exact maximum sizes", [] {
            auto f = roundtrip(make_request(Op::put, 1, std::string(max_key_size, 'k'), std::string(max_value_size, 'v')));
            CHECK(decode_command(f).value.size() == max_value_size);
        }},
        {"response validation", [] {
            auto f = roundtrip(make_response(7, {Status::not_found, "missing"}));
            CHECK(f.kind == Kind::response); CHECK(f.code == 1); CHECK(f.request_id == 7);
            rejects([&] { decode_command(f); });
            f.code = 255; rejects([&] { encode_frame(f); });
        }},
        {"batch roundtrip", [] {
            auto c = decode_command(roundtrip(make_batch(5, {{Op::put, "a", "one"}, {Op::erase, "b", ""}})));
            CHECK(c.mutations.size() == 2); CHECK(c.mutations[0].value == "one"); CHECK(c.mutations[1].op == Op::erase);
        }},
        {"batch rejects truncation trailing data illegal ops and excess", [] {
            auto f = make_batch(5, {{Op::put, "a", "v"}});
            for (std::size_t i = 0; i < f.value.size(); ++i) { auto bad = f; bad.value.resize(i); rejects([&] { decode_command(bad); }); }
            f.value.push_back('x'); rejects([&] { decode_command(f); });
            rejects([] { make_batch(1, {}); });
            rejects([] { make_batch(1, {{Op::get, "a", ""}}); });
            rejects([] { make_batch(1, std::vector<Mutation>(101, {Op::put, "k", "v"})); });
            rejects([] { make_batch(1, std::vector<Mutation>(20, {Op::put, "k", std::string(max_value_size, 'v')})); });
        }},
        {"storage CRUD and deletion semantics", [] {
            MemoryStorage store;
            auto call = [&](Frame f) { return store.execute(decode_command(f)); };
            CHECK(call(make_request(Op::get, 1, "k")).status == Status::not_found);
            CHECK(call(make_request(Op::put, 2, "k", "v")).status == Status::ok);
            CHECK(call(make_request(Op::put, 3, "k", "new")).status == Status::ok);
            CHECK(call(make_request(Op::get, 4, "k")).value == "new");
            CHECK(call(make_request(Op::erase, 5, "k")).status == Status::ok);
            CHECK(call(make_request(Op::erase, 6, "k")).status == Status::ok);
            CHECK(call(make_request(Op::get, 7, "k")).status == Status::not_found);
        }},
        {"storage resource failure preserves entire prior batch state", [] {
            MemoryStorage store(6);
            store.execute(decode_command(make_request(Op::put, 1, "a", "old")));
            auto result = store.execute(decode_command(make_batch(2, {{Op::erase, "a", ""}, {Op::put, "b", "too-large"}})));
            CHECK(result.status == Status::resource_limit);
            CHECK(store.execute(decode_command(make_request(Op::get, 3, "a"))).value == "old");
            CHECK(store.execute(decode_command(make_request(Op::get, 4, "b"))).status == Status::not_found);
        }},
        {"seeded binary roundtrip corpus", [] {
            std::mt19937 rng(12345);
            for (int n = 0; n < 1000; ++n) {
                std::string value(rng() % 1000, '\0');
                for (auto& c : value) c = static_cast<char>(rng() & 255);
                CHECK(roundtrip(make_request(Op::put, 1, "key", value)).value == value);
            }
        }}
    });
}
