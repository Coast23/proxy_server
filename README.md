# Proxy Server

A high-performance, multi-protocol proxy server written in C++17. Supports SOCKS4, SOCKS4a, SOCKS5, and HTTP with automatic protocol detection and dual-stack IPv4/IPv6.

[:cn: 中文文档](README_CN.md)

## Features

- **SOCKS5** — CONNECT command, IPv4 / IPv6 / Domain address types, NOAUTH and USERPASS authentication
- **SOCKS4 / SOCKS4a** — CONNECT command, IPv4 direct + domain name resolution (4a mode auto-detected)
- **HTTP** — CONNECT tunneling (HTTPS) + basic HTTP forwarding for absolute-URL requests
- **Dual-stack IPv4/IPv6** — single `AF_INET6` socket with `IPV6_V6ONLY=0`, auto fallback to IPv4
- **Auto protocol detection** — identifies SOCKS4 / SOCKS5 / HTTP from the first byte of the handshake
- **Thread pool** — bounded workers via `std::thread`, defaults to `hardware_concurrency`
- **Cross-platform** — Windows (Winsock2) and Linux (POSIX sockets)

## Quick Start

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
./proxy_server
```

The server listens on `0.0.0.0:1080` (dual-stack, SOCKS5 NOAUTH) by default.

## Command Line

```
proxy_server [-h] [-p PORT] [-a AUTHTYPE] [-u USERNAME] [-P PASSWORD] [-t THREADS] [-b BIND_ADDR]

  -p PORT       Listen port (default: 1080)
  -a AUTHTYPE   SOCKS5 auth: 0 = NOAUTH, 2 = USERPASS (default: 0)
  -u USERNAME   Auth username (default: user)
  -P PASSWORD   Auth password (default: pass)
  -t THREADS    Worker threads (default: CPU cores)
  -b BIND_ADDR  Bind address (default: ::)
  -h            Show help
```

### Examples

```bash
./proxy_server                                          # defaults
./proxy_server -p 1080 -a 2 -u admin -P secret123      # SOCKS5 with auth
./proxy_server -t 8 -b 127.0.0.1                        # custom threads + loopback
```

## Client Configuration

Just set up the proxy as you like. Here are some examples.

### curl

```bash
curl --proxy socks5://127.0.0.1:1080 http://example.com
curl --proxy socks5://127.0.0.1:1080 https://example.com
curl --proxy socks5://admin:secret123@127.0.0.1:1080 http://example.com
curl --proxy http://127.0.0.1:1080 https://example.com
curl --proxy socks5h://127.0.0.1:1080 http://ipv6.google.com  # resolve on proxy
```

### Firefox

`Settings → General → Network Settings → Manual proxy configuration`
- SOCKS Host: `127.0.0.1`, Port: `1080`, SOCKS5

### Windows System Proxy

`Settings → Network & Internet → Proxy → Manual`
- Address: `127.0.0.1`, Port: `1080` (HTTP forwarding only)

## Protocol Support

| Protocol  | Command  | Address Types       | Authentication    |
|-----------|----------|---------------------|-------------------|
| SOCKS4    | CONNECT  | IPv4                | —                 |
| SOCKS4a   | CONNECT  | Domain              | —                 |
| SOCKS5    | CONNECT  | IPv4, IPv6, Domain  | NOAUTH, USERPASS  |
| HTTP      | CONNECT, GET/POST/… | Domain          | —                 |

HTTPS works transparently — the proxy tunnels the TLS stream without inspecting it.

## Architecture

```
main.cpp           Entry point, CLI, signal handling, accept loop
io.hpp / io.cpp    Cross-platform socket I/O, logging, DNS
session.cpp        Protocol detection + SOCKS4/4a/5/HTTP handlers
thread_pool.hpp    Header-only bounded thread pool
```

```
Client ──TCP──> accept() ──enqueue──> ThreadPool worker
                                           │
                                     protocol detection (first byte)
                                           │
                              ┌────────────┼────────────┐
                              │            │            │
                           SOCKS4       SOCKS5        HTTP
                              │            │            │
                              └────────────┼────────────┘
                                           │
                                     connect remote
                                           │
                                     socket_pipe()
                                   (bidirectional forwarding)
```

## Build Requirements

- CMake ≥ 3.12
- C++17 compiler (g++ / clang++ / MSVC)

## License

MIT
