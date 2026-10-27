# Teammate integration contract

## Developer 1: storage

Implement `atlas::IStorage::execute(const Command&) -> Result` in a new library.
The server calls it serially on a dedicated worker. It does not call storage from
the network event loop. You can introduce internal reader/compaction threads later,
provided the public operation semantics remain unchanged.

`Command` carries the operation, request ID, binary key/value, and decoded batch
mutations. PING/STATS are transport operations and do not reach storage. The transport
validates the complete command before dispatch. Keep storage validation if it is
also callable directly outside this transport.

Replace `std::make_shared<atlas::MemoryStorage>()` in `apps/node.cpp` with your
persistent implementation. Add a real data-directory option, startup recovery,
directory locking, and accurate startup capability reporting. Do not keep the
`--memory-demo` label for a persistent implementation.

Required contracts for the persistent adapter:

- GET returns bytes or NOT_FOUND; DELETE of absent key returns OK.
- PUT/DELETE/batch success only after required WAL synchronization.
- Batch is atomic both for visibility and recovery.
- Handle disk failure and uncertain WAL tail without continuing unsafe writes.
- Complete bounded operations; shutdown drains accepted storage commands and
  cannot safely cancel an arbitrary storage function hanging forever.
- Add fault injection and recovery tests before advertising persistence.
- Agree on request-ID deduplication separately; v1 currently only correlates IDs.

The demo adapter copies its map per mutation/batch for simple exception-safe
publication. It is deliberately not a performance baseline or a substitute for
your storage engine. Its 32 MiB limit counts logical key/value bytes, not total
process memory; map overhead, queued frames and copies consume extra memory.

## Developer 3: HTTP gateway

Link `atlas_transport`. Use one `atlas::Client` per serialized gateway worker.
The client is not thread-safe and its blocking facade must not run on Drogon's
event-loop thread. Use a bounded gateway worker pool; reuse connections within
each worker and assign request IDs. Do not share a Client across workers.

```cpp
atlas::Client client("127.0.0.1", 7070, std::chrono::milliseconds{3000});
auto result = client.request(atlas::make_request(atlas::Op::get, 42, "greeting"));
```

Map status codes per `docs/protocol.md`. Catch transport exceptions and return
unavailable/deadline errors. Do not silently retry mutations. The operation deadline
covers resolution, connection, write and response. System DNS resolver cancellation
can take longer to drain on some platforms; use configured numeric internal addresses
when strict deadline behavior is needed.

HTTP validation/authentication, HTTPS, CORS and rate limits belong to the gateway.
Never let browsers talk to the unauthenticated TCP service. No HTTP gateway or web
frontend is supplied in Developer 2's week-1 slice.

STATS returns actual transport counters. Fields include connections, pending,
requests, errors, rejected_connections, timeouts, storage_completed, and
storage_latency_us_sum. The sum includes queue wait plus storage work and is not
network or browser latency. It does not provide P95/P99. Counters reset on restart.
No WAL or replication metrics exist yet; hide those UI panels until implemented.

## Ownership and merge order

- D2: protocol, transport, client, CLI, CMake/CI/container packaging.
- D1: persistent storage, WAL, recovery, storage correctness tests.
- D3: gateway, frontend, API contract, browser tests.
- Joint review: command types, status codes, durability, retry semantics.

First merge protocol/interface changes; then adapters; then application wiring.
Keep changes additive and run the shared integration suite after each merge.
