CXX      = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -g
LDLIBS   = -lsodium

all: server client

server: server.cpp net.cpp net.hpp crypto.cpp crypto.hpp
	$(CXX) $(CXXFLAGS) server.cpp net.cpp crypto.cpp -o server $(LDLIBS)

client: client.cpp net.cpp net.hpp crypto.cpp crypto.hpp
	$(CXX) $(CXXFLAGS) client.cpp net.cpp crypto.cpp -o client $(LDLIBS)

clean:
	rm -f server client 

.PHONY: all clean