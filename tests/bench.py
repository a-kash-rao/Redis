#!/usr/bin/env python3
"""Load generator, in the spirit of redis-benchmark.

Spawns N client processes. Each one opens a connection and, until the time
is up, sends a batch of P pipelined requests and waits for all P replies.
Every reply is checked byte for byte.

    ./tests/bench.py --clients 50 --pipeline 16 --cmd get

The server must already be running (./server).
"""
import argparse
import multiprocessing as mp
import socket
import struct
import time

HOST, PORT = '127.0.0.1', 1234
NUM_KEYS = 100      # per client
VALUE = 'v' * 16


def req(*args):
    parts = [a.encode() for a in args]
    body = struct.pack('<I', len(parts))
    body += b''.join(struct.pack('<I', len(p)) + p for p in parts)
    return struct.pack('<I', len(body)) + body


def resp(body):
    return struct.pack('<I', len(body)) + body


NIL_REPLY = resp(b'\x00')
STR_REPLY = resp(b'\x02' + struct.pack('<I', len(VALUE)) + VALUE.encode())


def recv_exact(s, buf):
    view = memoryview(buf)
    while view:
        n = s.recv_into(view)
        if n == 0:
            raise EOFError('server closed the connection')
        view = view[n:]


def worker(idx, cmd, pipeline, duration, barrier, results):
    s = socket.create_connection((HOST, PORT))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    keys = ['bench:%d:%d' % (idx, j) for j in range(NUM_KEYS)]

    # populate the keys so GETs hit
    s.sendall(b''.join(req('set', k, VALUE) for k in keys))
    buf = bytearray(len(NIL_REPLY) * NUM_KEYS)
    recv_exact(s, buf)

    # one pre-encoded batch, reused every round
    if cmd == 'set':
        batch = b''.join(req('set', keys[j % NUM_KEYS], VALUE) for j in range(pipeline))
        expected = NIL_REPLY * pipeline
    else:
        batch = b''.join(req('get', keys[j % NUM_KEYS]) for j in range(pipeline))
        expected = STR_REPLY * pipeline
    buf = bytearray(len(expected))

    barrier.wait()
    ops = 0
    lats = []
    t_start = time.perf_counter()
    deadline = t_start + duration
    while True:
        t0 = time.perf_counter()
        if t0 >= deadline:
            break
        s.sendall(batch)
        recv_exact(s, buf)
        lats.append(time.perf_counter() - t0)
        if buf != expected:
            raise AssertionError('unexpected reply')
        ops += pipeline
    elapsed = time.perf_counter() - t_start
    s.close()
    results.put((ops, elapsed, lats))


def run(cmd, clients, pipeline, duration):
    barrier = mp.Barrier(clients + 1)
    results = mp.Queue()
    procs = [mp.Process(target=worker, args=(i, cmd, pipeline, duration, barrier, results))
             for i in range(clients)]
    for p in procs:
        p.start()
    barrier.wait()      # every client is connected and ready
    out = [results.get() for _ in procs]
    for p in procs:
        p.join()

    ops_per_sec = sum(ops / elapsed for ops, elapsed, _ in out)
    lats = sorted(l for _, _, ls in out for l in ls)
    p50 = lats[len(lats) // 2] * 1e3
    p99 = lats[min(len(lats) - 1, int(len(lats) * 0.99))] * 1e3
    return ops_per_sec, p50, p99


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--clients', '-c', type=int, nargs='+', default=[50],
                    help='concurrent connections (several values = a sweep)')
    ap.add_argument('--pipeline', '-P', type=int, nargs='+', default=[1],
                    help='requests per batch (several values = a sweep)')
    ap.add_argument('--cmd', nargs='+', choices=['get', 'set'], default=['set', 'get'])
    ap.add_argument('--duration', '-d', type=float, default=5.0, help='seconds per run')
    args = ap.parse_args()

    print('%-4s %8s %9s %12s %12s %12s' %
          ('cmd', 'clients', 'pipeline', 'ops/sec', 'p50 batch', 'p99 batch'))
    for cmd in args.cmd:
        for clients in args.clients:
            for pipeline in args.pipeline:
                ops, p50, p99 = run(cmd, clients, pipeline, args.duration)
                print('%-4s %8d %9d %12s %10.2fms %10.2fms' %
                      (cmd, clients, pipeline, '{:,.0f}'.format(ops), p50, p99), flush=True)


if __name__ == '__main__':
    main()
