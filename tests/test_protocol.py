#!/usr/bin/env python3
"""Protocol-level tests that the one-shot ./client can't do: pipelining,
requests split across many writes, malformed input, and the idle timeout.

Starts its own ./server (run from anywhere, the repo root is found
relative to this file). Pass --no-spawn to test an already running server.
"""
import os
import socket
import struct
import subprocess
import sys
import time

HOST, PORT = '127.0.0.1', 1234
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TAG_NIL, TAG_ERR, TAG_STR, TAG_INT, TAG_DBL, TAG_ARR = range(6)
K_MAX_MSG = 32 << 20
IDLE_TIMEOUT_S = 5


# ======================= protocol helpers =======================

def req(*args):
    """Encode one request: [len][nstr][len][str]..."""
    parts = [a.encode() if isinstance(a, str) else a for a in args]
    body = struct.pack('<I', len(parts))
    body += b''.join(struct.pack('<I', len(p)) + p for p in parts)
    return struct.pack('<I', len(body)) + body


def read_n(s, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError('server closed the connection')
        buf += chunk
    return bytes(buf)


class Err(tuple):
    """An error reply: (code, message)."""


def decode(buf, i=0):
    """Decode one TLV value starting at buf[i], returns (value, next_i)."""
    tag = buf[i]
    i += 1
    if tag == TAG_NIL:
        return None, i
    if tag == TAG_ERR:
        code, n = struct.unpack_from('<II', buf, i)
        i += 8
        return Err((code, buf[i:i + n].decode())), i + n
    if tag == TAG_STR:
        (n,) = struct.unpack_from('<I', buf, i)
        i += 4
        return buf[i:i + n], i + n
    if tag == TAG_INT:
        return struct.unpack_from('<q', buf, i)[0], i + 8
    if tag == TAG_DBL:
        return struct.unpack_from('<d', buf, i)[0], i + 8
    if tag == TAG_ARR:
        (n,) = struct.unpack_from('<I', buf, i)
        i += 4
        items = []
        for _ in range(n):
            v, i = decode(buf, i)
            items.append(v)
        return items, i
    raise ValueError('bad tag %d' % tag)


def read_resp(s):
    (n,) = struct.unpack('<I', read_n(s, 4))
    body = read_n(s, n)
    value, end = decode(body)
    assert end == len(body), 'trailing bytes in response'
    return value


def connect():
    s = socket.create_connection((HOST, PORT), timeout=10)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def call(s, *args):
    s.sendall(req(*args))
    return read_resp(s)


def assert_closed_by_server(s):
    try:
        data = s.recv(16)
    except ConnectionResetError:
        return
    assert data == b'', 'expected the server to close the connection, got %r' % data


# ======================= tests =======================

def test_pipelining():
    """Several requests in a single write, replies come back in order."""
    s = connect()
    s.sendall(req('set', 'p', '1') + req('get', 'p') + req('del', 'p') + req('get', 'p'))
    assert read_resp(s) is None
    assert read_resp(s) == b'1'
    assert read_resp(s) == 1
    assert read_resp(s) is None
    s.close()


def test_deep_pipeline():
    """5000 pipelined requests (~150KB, more than one read() worth)."""
    s = connect()
    n = 5000
    s.sendall(b''.join(req('zadd', 'z', str(i), 'm%d' % i) for i in range(n)))
    assert [read_resp(s) for _ in range(n)] == [1] * n
    r = call(s, 'zquery', 'z', str(n - 10), '', '0', '100')
    assert r == [x for i in range(n - 10, n) for x in (b'm%d' % i, float(i))], r
    assert call(s, 'del', 'z') == 1
    s.close()


def test_byte_by_byte():
    """A request split into 1-byte writes is reassembled by the server."""
    s = connect()
    for b in req('set', 'split', 'value'):
        s.sendall(bytes([b]))
        time.sleep(0.001)
    assert read_resp(s) is None
    assert call(s, 'get', 'split') == b'value'
    s.close()


def test_large_value():
    """A 1MB value round-trips (needs many read()/write() calls)."""
    s = connect()
    big = os.urandom(1 << 20)
    assert call(s, 'set', 'big', big) is None
    assert call(s, 'get', 'big') == big
    assert call(s, 'del', 'big') == 1
    s.close()


def test_empty_command():
    """nstr = 0 is a valid message, but not a valid command."""
    s = connect()
    body = struct.pack('<I', 0)
    s.sendall(struct.pack('<I', len(body)) + body)
    assert read_resp(s) == Err((1, 'empty command'))
    s.close()


def test_malformed_request():
    """nstr says 5 strings but the body is truncated -> connection closed."""
    s = connect()
    s.sendall(struct.pack('<I', 8) + struct.pack('<I', 5) + b'xxxx')
    assert_closed_by_server(s)


def test_trailing_garbage():
    """Extra bytes after the last string -> connection closed."""
    s = connect()
    body = struct.pack('<I', 1) + struct.pack('<I', 4) + b'keys' + b'junk'
    s.sendall(struct.pack('<I', len(body)) + body)
    assert_closed_by_server(s)


def test_too_long():
    """A length header above 32MB is rejected without buffering it."""
    s = connect()
    s.sendall(struct.pack('<I', K_MAX_MSG + 1))
    assert_closed_by_server(s)


def test_idle_timeout():
    """An idle connection is dropped after ~5s, active ones are not."""
    idle = connect()
    active = connect()
    t0 = time.time()
    # keep `active` busy while `idle` stays silent
    while time.time() - t0 < IDLE_TIMEOUT_S - 1:
        assert call(active, 'get', 'nothing') is None
        time.sleep(0.5)
    assert_closed_by_server(idle)
    dt = time.time() - t0
    assert IDLE_TIMEOUT_S - 0.5 < dt < IDLE_TIMEOUT_S + 2, 'closed after %.1fs' % dt
    assert call(active, 'get', 'nothing') is None   # still alive
    active.close()


def test_server_still_healthy():
    """After all the abuse above, a fresh connection still works."""
    s = connect()
    assert call(s, 'set', 'ok', 'yes') is None
    assert call(s, 'get', 'ok') == b'yes'
    s.close()


TESTS = [
    test_pipelining,
    test_deep_pipeline,
    test_byte_by_byte,
    test_large_value,
    test_empty_command,
    test_malformed_request,
    test_trailing_garbage,
    test_too_long,
    test_idle_timeout,
    test_server_still_healthy,
]


# ======================= runner =======================

def start_server():
    proc = subprocess.Popen([os.path.join(ROOT, 'server')],
                            stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    deadline = time.time() + 3
    while time.time() < deadline:
        if proc.poll() is not None:
            sys.exit('server exited at startup (is port %d already in use?)' % PORT)
        try:
            socket.create_connection((HOST, PORT), timeout=0.2).close()
            return proc
        except OSError:
            time.sleep(0.05)
    proc.kill()
    sys.exit('server did not start listening on port %d' % PORT)


def main():
    proc = None if '--no-spawn' in sys.argv else start_server()
    failed = 0
    try:
        for t in TESTS:
            t0 = time.time()
            try:
                t()
                print('ok    %-28s %.2fs' % (t.__name__, time.time() - t0))
            except Exception as e:
                failed += 1
                print('FAIL  %-28s %s: %s' % (t.__name__, type(e).__name__, e))
        if proc and proc.poll() is not None:
            failed += 1
            print('FAIL  server died')
    finally:
        if proc:
            proc.terminate()
            proc.wait()
    print('passed: %d, failed: %d' % (len(TESTS) - failed, failed))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
