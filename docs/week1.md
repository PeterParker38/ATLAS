# Developer 2 week-1 handoff

## Delivered work

| Planned responsibility | Implementation |
|---|---|
| TCP framing/server | Asio async server, bounded input, request ordering and deadlines |
| Protocol/shared client | Versioned binary codec, persistent C++ client, CLI, wire spec |
| Build/CI | CMake targets/presets, GitHub Actions, independent Python wire tests |
| Initial deployment | Non-root Docker image, loopback-only Compose and local demo script |
| Tests | Codec limits, batches, concurrency, partial/coalesced frames, overload, timeouts |
| Teammate integration | IStorage seam and gateway client handoff |

The original team's week-1 persistent browser flow additionally requires D1's
WAL and D3's HTTP/frontend. Those are not silently counted as completed here.
The user requested local-only operation: no hosted backend or frontend is created.

## Suggested review session (2 hours)

1. 20 minutes: build and run the CLI demo.
2. 25 minutes: explain header validation and fragmented TCP reads.
3. 25 minutes: trace a request through worker dispatch and its timeout.
4. 25 minutes: review IStorage and response semantics with D1/D3.
5. 25 minutes: run fault/overload tests and assign week-2 replication tasks.

## Focused learning (2 hours)

- 30 minutes: TCP is a stream; review `async_read` and full-frame lengths.
- 30 minutes: shared_ptr callback lifetime and cancellation; read Session.
- 30 minutes: worker queue/backpressure; read dispatch and the blocking-storage test.
- 30 minutes: client deadlines, correlation and ambiguous mutation outcomes.

References:
- Asio tutorial: https://think-async.com/Asio/asio-1.30.2/doc/asio/tutorial/tutdaytime3.html
- CMake Threads target: https://cmake.org/cmake/help/latest/module/FindThreads.html
- GitHub Actions: https://docs.github.com/en/actions

## Before week 2

- D1 agrees on durable operation ordering and what execute() success means.
- D3 runs a real request through Client from a bounded gateway worker.
- Team reviews the protocol and chooses stable mutation-ID semantics.
- Only then design replication messages with persisted sequence positions.

No simulated follower, election or fake cluster metrics are present in this release.
