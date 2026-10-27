# Atlas TCP protocol v1

All multibyte integers are **unsigned big endian**. Strings are byte strings, not
NUL-terminated; no native struct serialization. One request is processed at a time
per connection. Multiple frames may be pipelined; responses retain connection
order. Different connections have no predetermined ordering.

## Header (24 bytes)

| Offset | Width | Meaning |
|---|---|---|
| 0 | 4 | ASCII `ATLS` (`41 54 4c 53`) |
| 4 | 1 | Version, `1` |
| 5 | 1 | Kind: request `0`, response `1` |
| 6 | 2 | Request opcode or response status |
| 8 | 8 | Nonzero request ID, echoed in response |
| 16 | 4 | Key length |
| 20 | 4 | Value length |

The body is exactly key bytes followed by value bytes. Key length <= 256;
combined body <= 262144. Lengths are checked before body allocation. An invalid
header causes connection closure without a response. A valid header containing
an invalid command receives INVALID and the connection can continue.

## Operations

| Code | Name | Key | Value |
|---|---|---|---|
| 1 | PING | Empty | Empty |
| 2 | GET | 1..256 bytes | Empty |
| 3 | PUT | 1..256 bytes | 0..16384 bytes |
| 4 | DELETE | 1..256 bytes | Empty |
| 5 | BATCH | Empty | Batch payload below |
| 6 | STATS | Empty | Empty |

PUT replaces an existing value. DELETE of an absent key returns OK. GET of a
missing key returns NOT_FOUND. No range scan is defined in v1. Add one through a
reviewed contract change when D1 and D3 agree on its semantics.

Batch payload: uint32 mutation count (1..100), followed by each mutation:
uint16 opcode (PUT/DELETE only), uint32 key length, uint32 value length, key, value.
The entire encoded batch must fit 256 KiB. Each mutation obeys individual limits;
DELETE has no value. Trailing bytes, nested batches and read operations are invalid.
All mutations apply in listed order, atomically; repeated keys are allowed.
Validation of the whole batch precedes storage dispatch.

## Responses

Key length is always zero. Value is the GET data, `PONG`, STATS JSON, or a textual
error. Successful mutations have an empty body. Errors must not expose secrets.

| Code | Name | Suggested D3 HTTP mapping |
|---|---|---|
| 0 | OK | 200/204 |
| 1 | NOT_FOUND | 404 |
| 2 | INVALID | 400 |
| 3 | BUSY | 503 (gateway may provide Retry-After) |
| 4 | INTERNAL | 500 |
| 5 | NOT_IMPLEMENTED | 501 |
| 6 | RESOURCE_LIMIT | 507 or documented 503 |

The HTTP gateway may distinguish authentication, request size, and rate limits
before issuing a TCP command. A TCP timeout is not proof that a mutation failed.

## IDs, retries, and current limitations

IDs currently provide **correlation only**, not durable deduplication. A response
ID mismatch is a client protocol error and closes the connection. The client never
automatically retries. Use a unique nonzero ID per request in a long-lived gateway
client. The one-shot CLI defaults to ID 1, which is valid for correlation only.

Before D1 enables persistent idempotency, agree on global ID/session allocation,
result retention and payload-conflict handling; change the CLI defaults then.
This release does not advertise exactly-once execution. If a connection times out
after dispatch, a queued mutation can still execute; report outcome unknown.

The in-memory adapter returns after in-memory application only. D1's persistent
adapter must synchronize its WAL before returning success. Networking does not
upgrade volatile success into durable success.

## Example

PUT key `k`, value `v`, ID `0x0102030405060708`:

```text
41 54 4c 53 01 00 00 03 01 02 03 04 05 06 07 08
00 00 00 01 00 00 00 01 6b 76
```

Python header codec: `struct.Struct('!4sBBHQII')`. See `tests/integration.py` for
an independent executable wire client and adversarial tests.
