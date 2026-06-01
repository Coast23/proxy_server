#pragma once
#include "io.hpp"
#include <cstdint>

struct ProxyConfig {
    uint16_t port = 1080;
    int auth_type = 0;   // 0 = NOAUTH, 2 = USERPASS (SOCKS5)
    const char *username = "user";
    const char *password = "pass";
};

extern ProxyConfig g_config;

// Entry point for each accepted connection (called from thread pool)
void handle_session(socket_t client_fd);

// Auto-detect protocol and dispatch
void dispatch_protocol(socket_t client_fd, uint8_t first_byte);