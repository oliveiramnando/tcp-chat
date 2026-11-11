CXX      = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -g
LDLIBS   = -lsodium

TEST_FLAGS = $(CXXFLAGS) -fsanitize=address,undefined -I.
TEST_SRCS  = $(wildcard tests/test_*.cpp)

all: server client

server: server.cpp net.cpp net.hpp crypto.cpp crypto.hpp
	$(CXX) $(CXXFLAGS) server.cpp net.cpp crypto.cpp -o server $(LDLIBS)

client: client.cpp net.cpp net.hpp crypto.cpp crypto.hpp util.cpp util.hpp
	$(CXX) $(CXXFLAGS) client.cpp net.cpp crypto.cpp util.cpp -o client $(LDLIBS)

run_tests: $(TEST_SRCS) net.cpp net.hpp crypto.cpp crypto.hpp util.cpp util.hpp
	$(CXX) $(TEST_FLAGS) $(TEST_SRCS) net.cpp crypto.cpp util.cpp -o run_tests $(LDLIBS)

test: run_tests
	./run_tests

clean:
	rm -f server client run_tests

.PHONY: all clean test
