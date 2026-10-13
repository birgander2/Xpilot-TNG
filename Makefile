# Top-level Makefile forwarding to CMake build system

BUILD_DIR ?= build
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

.PHONY: all build clean server client install run-server run-client help

all build: $(BUILD_DIR)/CMakeCache.txt
	@cmake --build $(BUILD_DIR) -j$(JOBS)
	@ln -sf $(BUILD_DIR)/xpilot-tng-server xpilot-tng-server
	@ln -sf $(BUILD_DIR)/xpilot-tng-client xpilot-tng-client

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	@cmake -B $(BUILD_DIR)

server: $(BUILD_DIR)/CMakeCache.txt
	@cmake --build $(BUILD_DIR) --target xpilot-tng-server -j$(JOBS)
	@ln -sf $(BUILD_DIR)/xpilot-tng-server xpilot-tng-server

client: $(BUILD_DIR)/CMakeCache.txt
	@cmake --build $(BUILD_DIR) --target xpilot-tng-client -j$(JOBS)
	@ln -sf $(BUILD_DIR)/xpilot-tng-client xpilot-tng-client

clean:
	@rm -rf $(BUILD_DIR) xpilot-tng-server xpilot-tng-client

install:
	@cmake --install $(BUILD_DIR)

run-server: server
	@./$(BUILD_DIR)/xpilot-tng-server -map lib/maps/ndh.xp2 -port 15345

run-client: client
	@./$(BUILD_DIR)/xpilot-tng-client localhost -port 15345 -name Maverick -join

help:
	@echo "Available targets:"
	@echo "  make              - Build server and client (via CMake)"
	@echo "  make server       - Build only the server"
	@echo "  make client       - Build only the client"
	@echo "  make clean        - Remove build directory"
	@echo "  make install      - Install binaries and assets"
	@echo "  make run-server   - Start local game server"
	@echo "  make run-client   - Connect client to local game server"
