#include "atlas/storage.hpp"

namespace atlas {
Result MemoryStorage::execute(const Command& c) {
    if (c.op == Op::get) {
        const auto it = data_.find(c.key);
        return it == data_.end() ? Result{Status::not_found, "key not found"} : Result{Status::ok, it->second};
    }
    if (c.op == Op::put || c.op == Op::erase || c.op == Op::batch) {
        // Copy-on-apply gives the demo adapter atomic batches and exception safety.
        // This intentionally favors clarity over performance; not the production engine.
        auto next = data_;
        auto size = bytes_;
        auto apply = [&](const Mutation& m) {
            const auto it = next.find(m.key);
            if (it != next.end()) { size -= it->first.size() + it->second.size(); next.erase(it); }
            if (m.op == Op::put) { next.emplace(m.key, m.value); size += m.key.size() + m.value.size(); }
        };
        if (c.op == Op::batch) { for (const auto& m : c.mutations) apply(m); }
        else apply({c.op, c.key, c.value});
        if (size > limit_) return {Status::resource_limit, "demo storage capacity exceeded"};
        data_.swap(next); bytes_ = size;
        return {};
    }
    return {Status::not_implemented, "storage operation not implemented"};
}
}
