#pragma once
#include "atlas/protocol.hpp"
#include <map>

namespace atlas {
// Calls are serialized on the server's storage worker, never on the I/O thread.
// Developer 1 replaces this adapter; successful durable writes are their contract.
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual Result execute(const Command& command) = 0;
};
// Integration-only adapter: volatile, no WAL, no retry deduplication.
class MemoryStorage final : public IStorage {
public:
    explicit MemoryStorage(std::size_t limit = 32 * 1024 * 1024) : limit_(limit) {}
    Result execute(const Command& command) override;
private:
    std::map<std::string, std::string> data_;
    std::size_t bytes_ = 0;
    std::size_t limit_;
};
}
