# Local verification

Verified in Ubuntu 24.04 on WSL2 with GCC 13.3, CMake 3.28.3, Asio 1.28.1,
and Python 3.12.3. Tests contain 11 unit cases, 4 C++ transport cases and 16
Python black-box integration cases, grouped into three CTest entries.

## Checks

- Debug: all three CTest suites passed.
- Release: all three CTest suites passed.
- ASan + UBSan, with leak detection and halt-on-error: all three suites passed.
- Local CLI demo: PING, PUT, GET, BATCH, DELETE and STATS passed.
- Shell syntax and Python compilation checks passed.
- Docker Engine 29.1.3 / Compose 2.40.3: multi-stage image built successfully;
  Compose became healthy; PING, PUT, GET, BATCH and DELETE smoke checks passed.
  The host CLI also read data through the published loopback port. The runtime
  user was verified as UID/GID 10001 and the port as 127.0.0.1:7070. The test
  container/network were stopped and removed afterward; the local image remains.
- ThreadSanitizer: binaries built, but this WSL host's runtime failed with
  `FATAL: ThreadSanitizer: unexpected memory mapping`. This is **not a passing
  race-detector run**. The optional preset remains available for compatible hosts;
  the mandatory CI matrix uses Debug, Release and ASan/UBSan.

The transport suite deterministically stalls storage to test queue admission,
network responsiveness and client deadlines. The socket suite tests malformed
headers, oversized input, fragmentation, coalescing, binary payloads, 24 concurrent
clients, partial disconnects, maximum sessions, incomplete responses, wrong request
IDs and graceful shutdown. No database crash-durability or replication claim is
made by these tests: those modules are not part of this slice.

GitHub Actions runs on pushes and pull requests. The local results above do not
establish GitHub CI status; check the repository's Actions tab for remote runs.
No remote server deployment has been performed.
