#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
    constexpr int SOCK_ERR = SOCKET_ERROR;
    #define io_close closesocket
    using ssize_t = SSIZE_T;
    // socklen_t is defined in ws2tcpip.h on Vista+
    #ifndef socklen_t
    using socklen_t = int;
    #endif
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/tcp.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <cerrno>
    #include <csignal>
    using socket_t = int;
    constexpr socket_t INVALID_SOCK = -1;
    constexpr int SOCK_ERR = -1;
    #define io_close close
#endif

constexpr size_t BUFSIZE = 65536;

// Logging
void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_init(FILE *fp);

// I/O: returns bytes read/written, -1 on error, 0 on EOF (read)
long readn(socket_t fd, void *buf, size_t n);
long writen(socket_t fd, const void *buf, size_t n);
int socket_pipe(socket_t fd0, socket_t fd1);

// Socket helpers
int set_nonblocking(socket_t fd);
int set_tcp_nodelay(socket_t fd);
socket_t create_server_socket(const char *bind_addr, uint16_t port, int backlog = 128);

// DNS / connect to remote
socket_t connect_remote(const char *host, uint16_t port);
socket_t connect_remote_ipv4(const uint8_t ip[4], uint16_t port);
socket_t connect_remote_ipv6(const uint8_t ip[16], uint16_t port);
