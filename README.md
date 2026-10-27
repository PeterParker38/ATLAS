# Atlas - Developer 2, week 1

C++20 TCP transport, shared client, protocol contract, build/test infrastructure,
and local container deployment for the Atlas key-value engine.

**This is the networking foundation, not the complete database.** The included
`MemoryStorage` adapter is explicitly volatile. It provides working GET, PUT,
DELETE and atomic write batches for integration. There is no WAL, persistence,
replication, leader election, HTTP API, authentication, or frontend in this slice.
Developer 1 supplies the persistent engine; Developer 3 supplies the HTTP gateway
and web UI. Never expose the unauthenticated TCP port to the internet.

## Linux / WSL quick start

Ubuntu 24.04 is the tested development target. From a Linux shell in this checkout:

```sh
sudo apt-get update
sudo apt-get install -y g++ cmake ninja-build libasio-dev python3
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
```

On Windows, open Ubuntu/WSL, then:

```sh
cd '/mnt/c/Users/Henil Shah/OneDrive/Documents/Atlas'
```

For faster builds on WSL, a future clone under the Linux home directory avoids
mounted Windows filesystem overhead. The checked-in source works in either place.

Run the server in one terminal:

```sh
./build/debug/atlas-node --memory-demo
```

Use the client in another:

```sh
./build/debug/atlas-cli ping
./build/debug/atlas-cli put greeting 'hello Atlas'
./build/debug/atlas-cli get greeting
./build/debug/atlas-cli batch put a one put b two delete greeting
./build/debug/atlas-cli stats
./build/debug/atlas-cli delete a
```

`get` writes exact value bytes with **no extra newline**, suitable for piping to a
file. `put-file KEY PATH` sends up to 16 KiB of binary data. Use `--help` for options.
For a self-contained demonstration, run `bash scripts/demo.sh` after building.

## Delivered features

- Explicit 24-byte, network-byte-order header and versioned protocol.
- Correct handling of fragmented/coalesced frames and binary keys/values.
- 256-byte keys, 16 KiB values, 100-operation / 256 KiB batches.
- Asynchronous Asio TCP server, loopback binding by default.
- One serialized storage worker; network loop remains responsive during slow I/O.
- Bounded active sessions and outstanding storage commands; BUSY on queue saturation.
- Per-request/idle deadline; graceful SIGINT/SIGTERM handling.
- Persistent C++ client with correlation checks and operation deadline.
- CLI for CRUD, batch, ping, binary files and actual transport counters.
- Atomic in-memory integration adapter with a logical data limit.
- Unit, adversarial socket, concurrent client and overload tests.
- Debug, Release, ASan/UBSan and optional TSan CMake presets.
- GitHub Actions and non-root, loopback-only Docker Compose configuration.

## Tests and tools

```sh
bash scripts/check.sh debug
bash scripts/check.sh release
bash scripts/check.sh asan
# Optional: requires a host compatible with ThreadSanitizer's address mapping.
bash scripts/check.sh tsan
```

The test runner reports failures even in Release builds (tests do not rely on
`assert`). Python integration tests use only the standard library. No external
test framework download is required. Compiler/tooling dependencies come from the
Ubuntu package repository. Asio 1.28.1 is the tested system package version.

## Local Docker deployment

Install Docker Engine + Compose v2 or use Docker Desktop's WSL integration, then:

```sh
docker compose up --build --detach --wait
docker compose exec node atlas-cli put hello world
docker compose exec node atlas-cli get hello
docker compose logs
docker compose down
```

Host port 7070 binds only to `127.0.0.1`. Set `ATLAS_PORT=7071` if needed.
The container runs without root/capabilities and has a read-only root filesystem.
There is intentionally no persistent volume until a persistent adapter is added.
Restarting the server or container loses all demo data. No cloud server is needed.
On a fresh Linux Docker Engine installation, prefix Docker commands with `sudo`
if your account cannot access its socket; Docker Desktop may not require this.

## Integration handoff

- [Protocol and wire examples](docs/protocol.md)
- [D1/D3 interfaces and ownership](docs/integration.md)
- [Architecture, limits and shutdown semantics](docs/architecture.md)
- [Week-1 checklist and learning notes](docs/week1.md)
- [Local verification results](docs/verification.md)

Targets: `atlas_protocol`, `atlas_memory_storage`, `atlas_transport`, `atlas-node`,
`atlas-cli`. Link `atlas_transport` for the C++ gateway client; do not duplicate
the frame codec. Storage depends only on the protocol/domain types, not Asio.

## GitHub

Remote: https://github.com/PeterParker38/ATLAS

The workflow runs after these files are committed and pushed. It builds Debug,
Release and ASan/UBSan, runs all tests, then builds and smoke-tests the container.
It does not publish images or deploy to any remote server.
