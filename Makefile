CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-invalid-offsetof -O2 -g
LDFLAGS = -pthread

SERVER_SRCS = server.cpp hashtable.cpp avl.cpp zset.cpp heap.cpp thread_pool.cpp
HEADERS = $(wildcard *.h)

all: server client

server: $(SERVER_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SERVER_SRCS) -o $@ $(LDFLAGS)

client: client.cpp protocol.h
	$(CXX) $(CXXFLAGS) client.cpp -o $@

tests/test_ds: tests/test_ds.cpp hashtable.cpp avl.cpp heap.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) tests/test_ds.cpp hashtable.cpp avl.cpp heap.cpp -o $@

test: all tests/test_ds
	./tests/test_ds
	./tests/test_cmds.sh
	python3 tests/test_protocol.py

# needs a running ./server
bench: all
	python3 tests/bench.py --clients 1 10 50 --pipeline 1 16 --cmd get set

clean:
	rm -rf server client tests/test_ds *.dSYM tests/*.dSYM

.PHONY: all test bench clean
