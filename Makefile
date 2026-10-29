CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wconversion -Wsign-conversion -pedantic

# Directories
BUILD_DIR = build

# Executables
SERVER = $(BUILD_DIR)/server
CLIENT = $(BUILD_DIR)/client

# Default: build both
all: $(SERVER) $(CLIENT)

# Build server
$(SERVER): server.cpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

# Build client
$(CLIENT): client.cpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

# Run server
server: $(SERVER)
	./$(SERVER)

# Run client
client: $(CLIENT)
	./$(CLIENT)

# Debug build
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: clean all

# Release build
release: CXXFLAGS += -O2 -DNDEBUG
release: clean all

# Clean compiled files
clean:
	rm -rf $(BUILD_DIR)

# Rebuild everything
rebuild: clean all

.PHONY: all server client debug release clean rebuild
