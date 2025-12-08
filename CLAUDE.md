# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (using devcontainer or after installing dependencies)
cmake -GNinja -S . -B build

# Build
cmake --build build

# Run tests
cmake --build build --target test
```

For manual setup outside devcontainer:
```bash
apt-get install libnuma-dev libyaml-dev libssl-dev
export CC=/usr/bin/clang-15
export CXX=/usr/bin/clang++-15
```

The project uses vcpkg for dependencies (nghttp2, protobuf). The devcontainer auto-configures the vcpkg toolchain file.

## Architecture

libechidna is a C++20 networking library under the `coypu` namespace with these core components:

- **Event Management** (`event_mgr.hpp`): Template-based epoll event loop (`EventManager<LogTrait>`) supporting read/write/close callbacks per file descriptor
- **Buffer System** (`buf.hpp`): BipBuf circular buffer implementation for efficient I/O
- **TCP/UDP** (`tcp.hpp`, `udp.hpp`): Socket helpers for IPv4/IPv6, non-blocking sockets, and Unix domain sockets
- **TLS** (`openssl_mgr.hpp`): `OpenSSLManager<LogTrait>` for non-blocking SSL connections
- **WebSocket** (`websocket.hpp`): RFC 6455 implementation with HTTP upgrade handling
- **HTTP/2** (`http2.hpp`): nghttp2-based HTTP/2 with protobuf stream integration
- **Storage** (`store.hpp`): Memory-mapped log buffer (`LogWriteBuf`) with page-based allocation

## Code Patterns

- Template parameters named `LogTrait` expect a logger interface
- Most classes are header-only templates in `include/echidna/`
- Implementations in `src/` are minimal, with business logic in headers
- Tests use GoogleTest, fetched via CMake FetchContent

## Test Structure

Tests are in `tests/testcases-*.cpp` files, organized by component:
- `testcases.cpp` - main test suite
- `testcases-store.cpp` - storage tests
- `testcases-config.cpp`, `testcases-file.cpp`, etc. - component-specific tests
- `test-ssl.cpp` - separate SSL test executable (`echidnassltest`)

Note: io_uring tests (`testcases-uring.cpp`) are expected to fail in the devcontainer.
