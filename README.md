# Redis

A small Redis-like in-memory key-value server written in C++, following
[Build Your Own Redis](https://build-your-own.org/redis/).

**Serves ~1.2M ops/sec across 50 concurrent clients** (pipelined GET/SET,
single-threaded event loop, Apple M1). See [Benchmark](#benchmark).

## Build & run

```sh
make            # builds ./server and ./client
./server        # listens on 0.0.0.0:1234
```

In another terminal:

```sh
./client set greeting hello     # (nil)
./client get greeting           # (str) hello
./client zadd board 10 alice    # (int) 1
./client zquery board 0 "" 0 10 # (arr) len=2 ... (str) alice (dbl) 10 ...
```

Without make:

```sh
g++ -std=c++17 -Wall -Wextra -Wno-invalid-offsetof -O2 -g \
    server.cpp hashtable.cpp avl.cpp zset.cpp heap.cpp thread_pool.cpp -o server -pthread
g++ -std=c++17 -Wall -Wextra -O2 -g client.cpp -o client
```

## Tests

```sh
make test       # runs all three suites below
```

| Suite | What it covers |
|---|---|
| [tests/test_ds.cpp](tests/test_ds.cpp) | AVL tree (invariants checked after every insert/delete), rank/offset queries, heap, hashtable incl. rehashing; all checked against `std::multiset` |
| [tests/test_cmds.sh](tests/test_cmds.sh) | every command end-to-end through `./client`, error replies, TTL expiry, freeing a large zset in the thread pool |
| [tests/test_protocol.py](tests/test_protocol.py) | raw-socket edge cases: pipelining (up to 5000 requests in one write), a request sent 1 byte at a time, a 1MB value, truncated / trailing-garbage / oversized (>32MB) messages, and the 5s idle timeout closing idle connections but not active ones |

## Benchmark

[tests/bench.py](tests/bench.py) is a small load generator (like `redis-benchmark`):
N client processes, each sending batches of P pipelined requests and checking
every reply byte for byte.

```sh
./server &
./tests/bench.py --clients 1 10 50 --pipeline 1 16 --cmd get set
```

Results on an Apple M1 (8 cores), server built with `-O2`, client and server on
the same machine over loopback, 4–6s per run:

| cmd | clients | pipeline | ops/sec | p50 batch | p99 batch |
|---|---:|---:|---:|---:|---:|
| GET | 1 | 1 | 28K | 0.03ms | 0.06ms |
| GET | 10 | 1 | 96K | 0.10ms | 0.23ms |
| GET | 50 | 1 | 85K | 0.48ms | 1.4–1.9ms |
| GET | 1 | 16 | 418K | 0.04ms | 0.06ms |
| GET | 10 | 16 | 1.24M | 0.12ms | 0.25ms |
| GET | 50 | 16 | 1.17M | 0.65ms | 1.44ms |
| SET | 10 | 16 | 1.20M | 0.12ms | 0.26ms |
| SET | 50 | 16 | 1.00M | 0.73ms | 1.79ms |

How to read it:

- During every multi-client run the server used **95–98% of one core**, so
  these numbers are limited by the server, not by the Python load generator.
- Without pipelining, every request costs a `poll()` + `read()` + `write()`
  round of syscalls, which caps throughput at ~100K ops/sec. Pipelining
  amortizes those syscalls over 16 requests, giving ~12x more throughput.
- Going from 10 to 50 clients adds no throughput (one core is already
  saturated) and only raises latency, as expected for a single-threaded
  event loop. Note `poll()` also rescans every connection on each iteration;
  `epoll`/`kqueue` would avoid that.
- Numbers vary by roughly ±10% between runs. The load generator shares the
  8 cores with the server, so a separate client machine would likely score
  higher.

## Commands

Command names are case-insensitive.

| Command | Reply |
|---|---|
| `get key` | the string, or nil |
| `set key value` | nil (error if the key holds a sorted set) |
| `del key` | 1 if deleted, 0 if missing |
| `keys` | array of all keys |
| `pexpire key ms` | 1 if the key exists, 0 otherwise; a negative `ms` removes the TTL |
| `pttl key` | remaining ms, -1 if no TTL, -2 if no such key |
| `zadd zset score name` | 1 if added, 0 if an existing score was updated |
| `zrem zset name` | 1 if removed, 0 if missing |
| `zscore zset name` | the score, or nil |
| `zquery zset score name offset limit` | array of `name, score, ...` pairs |

`zquery` seeks to the first member `>= (score, name)`, skips `offset` members
(can be negative), and returns up to `limit` pairs. For example,
`zquery z -inf "" 0 10` returns the 10 lowest-ranked members.

## Protocol

Every message is `[len: u32][body]`, integers are little-endian.

- **Request body:** `[nstr: u32]` then `nstr` times `[len: u32][bytes]`.
- **Response body:** one tagged value:
  `NIL=0`, `ERR=1 [code][len][msg]`, `STR=2 [len][bytes]`, `INT=3 [i64]`,
  `DBL=4 [f64]`, `ARR=5 [n]` followed by `n` nested values.

See [protocol.h](protocol.h) for the tags and error codes.

## Design

| File | Book chapter | What it does |
|---|---|---|
| [server.cpp](server.cpp) | 3–7, 9, 12–14 | non-blocking sockets + `poll()` event loop, request parsing, pipelining, commands, response serialization |
| [client.cpp](client.cpp) | 3, 4, 9 | sends one command from argv and pretty-prints the reply |
| [hashtable.cpp](hashtable.cpp) | 8 | intrusive chaining hashtable with **progressive rehashing** (resizes a little per operation, no latency spikes) |
| [avl.cpp](avl.cpp) | 10 | intrusive AVL tree with subtree sizes for O(log n) rank/offset queries |
| [zset.cpp](zset.cpp) | 11 | sorted set = hashtable (by name) + AVL tree (by score, name) |
| [list.h](list.h) | 12 | doubly linked list for idle-connection timers (connections idle for 5s are closed) |
| [heap.cpp](heap.cpp) | 13 | min-heap of key expiration times for TTLs |
| [thread_pool.cpp](thread_pool.cpp) | 14 | worker threads that free large sorted sets (>1000 members) off the main thread |

## Differences from the book

- **SIGPIPE is ignored.** By default, writing to a socket whose peer has
  already closed raises `SIGPIPE`, which kills the process. A single client
  disconnecting before reading its reply could take the whole server down.
  The server ignores the signal and handles the `EPIPE` error from `write()`
  instead, closing just that connection.
- **`zquery`'s `limit` counts members, not output items.** In the book,
  `limit` is compared against the number of array elements, and each member
  produces two (name and score), so `limit 10` returns 5 members. Here
  `limit 10` returns 10 members.
- **Byte-order conversions say what they mean.** The port and address are
  converted with `htons`/`htonl` (host to network). Using `ntohs`/`ntohl`
  happens to produce the same bytes, since both just swap on a little-endian
  CPU, but it states the wrong direction.
- **Commands are case-insensitive**, like real Redis (`GET` = `get`).
- **Hangups are handled.** A `POLLHUP` with nothing left to read closes the
  connection, rather than leaving it in the poll set.
