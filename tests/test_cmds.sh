#!/usr/bin/env bash
# end-to-end tests: starts the server, runs commands through the client and
# compares the printed output with the expected output
set -u
cd "$(dirname "$0")/.."

./server 2>/tmp/redis_test_server.log &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null' EXIT
sleep 0.3
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "server failed to start (is port 1234 already in use?):"
    cat /tmp/redis_test_server.log
    exit 1
fi

FAILED=0
PASSED=0

# usage: t "<expected output>" cmd args...
t() {
    local expected="$1"
    shift
    local actual
    actual="$(./client "$@")"
    if [[ "$actual" == "$expected" ]]; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
        echo "FAIL: client $*"
        echo "  expected: $(echo "$expected" | tr '\n' '|')"
        echo "  actual:   $(echo "$actual" | tr '\n' '|')"
    fi
}

# --- strings ---
t "(nil)"            get k
t "(nil)"            set k v
t "(str) v"          get k
t "(nil)"            set k v2
t "(str) v2"         GET k
t "(int) 1"          del k
t "(int) 0"          del k
t "(nil)"            get k
t "(nil)"            set "" "empty key works"
t "(str) empty key works" get ""
t "(int) 1"          del ""
t "(err) 1 unknown command." foo
t "(err) 1 unknown command." get

# --- keys ---
t "(nil)"            set a 1
t "(arr) len=1
(str) a
(arr) end"           keys
t "(int) 1"          del a
t "(arr) len=0
(arr) end"           keys

# --- sorted sets ---
t "(nil)"            zscore asdf n1
t "(arr) len=0
(arr) end"           zquery xxx 1 asdf 1 10
t "(int) 1"          zadd zset 1 n1
t "(int) 1"          zadd zset 2 n2
t "(int) 0"          zadd zset 1.1 n1
t "(dbl) 1.1"        zscore zset n1
t "(arr) len=4
(str) n1
(dbl) 1.1
(str) n2
(dbl) 2
(arr) end"           zquery zset 1 "" 0 10
t "(arr) len=2
(str) n2
(dbl) 2
(arr) end"           zquery zset 1.1 "" 1 10
t "(arr) len=0
(arr) end"           zquery zset 1.1 "" 2 10
t "(arr) len=2
(str) n1
(dbl) 1.1
(arr) end"           zquery zset 2 "" -1 1
t "(int) 0"          zrem zset adsf
t "(int) 1"          zrem zset n1
t "(arr) len=2
(str) n2
(dbl) 2
(arr) end"           zquery zset 1 "" 0 10
t "(err) 4 expect float" zadd zset abc n3
t "(nil)"            set str v
t "(err) 3 expect zset"  zadd str 1 n1
t "(err) 3 expect zset"  zscore str n1
t "(err) 3 not a string value" get zset

# --- TTL ---
t "(int) -2"         pttl nokey
t "(nil)"            set tk v
t "(int) -1"         pttl tk
t "(int) 1"          pexpire tk 300
ttl="$(./client pttl tk)"
if [[ "$ttl" =~ ^\(int\)\ ([0-9]+)$ ]] && (( BASH_REMATCH[1] > 0 && BASH_REMATCH[1] <= 300 )); then
    PASSED=$((PASSED + 1))
else
    FAILED=$((FAILED + 1)); echo "FAIL: pttl tk -> $ttl"
fi
t "(int) 1"          pexpire tk -1
t "(int) -1"         pttl tk
t "(int) 1"          pexpire tk 200
sleep 0.5
t "(nil)"            get tk
t "(int) -2"         pttl tk
t "(int) 0"          pexpire nokey 100

# --- large zset, deleted in the thread pool ---
for i in $(seq 1 1100); do ./client zadd big "$i" "m$i" >/dev/null; done
t "(dbl) 1100"       zscore big m1100
t "(int) 1"          del big
t "(nil)"            zscore big m1
t "(int) 1"          zadd big 1 again    # the server is still alive and well

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "FAIL: server died"; FAILED=$((FAILED + 1))
fi

echo "passed: $PASSED, failed: $FAILED"
[[ $FAILED -eq 0 ]]
