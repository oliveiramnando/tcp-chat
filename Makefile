CXX      = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -g -Isrc
LDLIBS   = -lsodium

# Code shared by the client, the server and the tests
COMMON_SRCS = src/net.cpp src/crypto.cpp
COMMON_HDRS = src/net.hpp src/crypto.hpp

TEST_FLAGS = $(CXXFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all
TEST_SRCS  = $(wildcard tests/test_*.cpp)

all: server client

server: src/server.cpp $(COMMON_SRCS) $(COMMON_HDRS)
	$(CXX) $(CXXFLAGS) src/server.cpp $(COMMON_SRCS) -o server $(LDLIBS)

client: src/client.cpp src/util.cpp src/util.hpp $(COMMON_SRCS) $(COMMON_HDRS)
	$(CXX) $(CXXFLAGS) src/client.cpp src/util.cpp $(COMMON_SRCS) -o client $(LDLIBS)

run_tests: $(TEST_SRCS) src/util.cpp src/util.hpp $(COMMON_SRCS) $(COMMON_HDRS)
	$(CXX) $(TEST_FLAGS) $(TEST_SRCS) src/util.cpp $(COMMON_SRCS) -o run_tests $(LDLIBS)

test: run_tests
	./run_tests

clean:
	rm -f server client run_tests

.PHONY: all clean test
