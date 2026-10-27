# Week-1 transport architecture

```text
C++ client / CLI / future HTTP gateway
  -> TCP framed request
  -> single Asio event loop: accept, validate header, read body, validate command
  -> bounded outstanding command count
  -> one storage worker: IStorage::execute
  -> event-loop completion: encode and write response
```

The event loop owns sessions, timers and metrics. The worker only accesses the
storage adapter and posts results back. No shared map or metrics mutation occurs
across threads. Do not start multiple `io_context::run()` threads without first
adding strands or equivalent synchronization around all shared network state.

One in-flight request per session bounds output buffering and preserves order.
At most 128 sessions and 128 outstanding storage commands are admitted by default.
The outstanding bound includes the currently executing command. An overloaded
storage queue returns BUSY. Excess connections are closed before reading a frame.
PING/STATS bypass storage so health observations remain responsive during slow I/O.
The Docker PING health check establishes transport liveness only; it does not
certify storage readiness, available capacity, durability or cluster health.

The default 5-second server deadline begins before each header and covers header,
body, queue wait, storage and response transmission. It also acts as an idle timeout.
Slow-drip traffic cannot reset it indefinitely. It closes the socket but does not
cancel already dispatched mutations. Clients must handle ambiguous outcomes.

SIGINT/SIGTERM closes acceptance and sessions. Destruction joins the worker,
drains queued callbacks, and releases state. A persistent adapter must bound its
operations; a permanently blocked storage call prevents graceful process exit.
Do not forcibly terminate a thread writing a WAL.

Error handling distinguishes framing errors (close), command errors (INVALID),
overload (BUSY), capacity limits (RESOURCE_LIMIT), storage errors (sanitized INTERNAL),
and transport exceptions. Client connections are closed on timeout, I/O failure
or malformed/correlation-mismatched responses. There are no automatic retries.

## Explicitly absent

- WAL, crash durability, SSTables and compaction.
- Replication, membership, elections and failover.
- HTTP, TLS, authentication and public deployment.
- Persistent idempotency, scans and percentile histograms.

The Docker image is a local integration environment, not an internet-ready service.
Using multiple instances would produce independent databases, not a cluster.
