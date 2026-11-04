CXX      = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -g
LDLIBS   = -lsodium

all: server client

server: server.cpp net.cpp net.hpp
	$(CXX) $(CXXFLAGS) server.cpp net.cpp -o server

client: client.cpp net.cpp net.hpp
	$(CXX) $(CXXFLAGS) client.cpp net.cpp -o client 

clean:
	rm -f server client 

.PHONY: all clean