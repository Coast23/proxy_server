#include "io.hpp"
#include <ctime>
#include <mutex>
#include <cstdio>
#include <cstdarg>

static void set_blocking(socket_t fd){
#ifdef _WIN32
    u_long mode = 0;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags != -1) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

// Non-blocking connect with timeout. Returns 0 on success, -1 on failure/timeout.
static int connect_timeout(socket_t fd, const struct sockaddr *addr, socklen_t addrlen, int timeout_sec = 3){
    set_nonblocking(fd);
    int ret = connect(fd, addr, addrlen);
#ifdef _WIN32
    if(ret == SOCK_ERR){
        int e = WSAGetLastError();
        if(e != WSAEWOULDBLOCK) return -1;
#else
    if(ret == -1){
        if(errno != EINPROGRESS) return -1;
#endif
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = {timeout_sec, 0};
        ret = select((int)(fd + 1), nullptr, &wfds, nullptr, &tv);
        if(ret <= 0) return -1;
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
        if(err != 0) return -1;
    }
    set_blocking(fd);
    return 0;
}

static FILE *log_fp = stdout;
static std::mutex log_lock;

void log_init(FILE *fp){
    log_fp = fp;
}

static void log_write(const char *level, const char *fmt, va_list args){
    char vbuf[512];
    vsnprintf(vbuf, sizeof(vbuf), fmt, args);

    time_t now;
    time(&now);
    struct tm *tm_now = localtime(&now);
    char date[32];
    strftime(date, sizeof(date), "%m-%d %H:%M:%S", tm_now);

    std::lock_guard<std::mutex> lock(log_lock);
    fprintf(log_fp, "[%s][%s] %s\n", date, level, vbuf);
    fflush(log_fp);
}

void log_info(const char *fmt, ...){
    va_list args;
    va_start(args, fmt);
    log_write("INFO", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...){
    va_list args;
    va_start(args, fmt);
    log_write("ERROR", fmt, args);
    va_end(args);
}

// Read exactly n bytes, handling interrupts
long readn(socket_t fd, void *buf, size_t n){
    size_t left = n;
    auto *ptr = static_cast<char*>(buf);
    while(left > 0){
    #ifdef _WIN32
        int nread = recv(fd, ptr, (int)left, 0);
        if(nread == SOCK_ERR){
            int e = WSAGetLastError();
            if(e == WSAEINTR or e == WSAEWOULDBLOCK) continue;
            return -1;
        }
    #else
        ssize_t nread = recv(fd, ptr, left, 0);
        if(nread == -1){
            if(errno == EINTR or errno == EAGAIN) continue;
            return -1;
        }
    #endif
        if(nread == 0) return (long)(n - left); // connection closed
        left -= (size_t)nread;
        ptr += nread;
    }
    return (long)n;
}

// Write exactly n bytes, handling interrupts
long writen(socket_t fd, const void *buf, size_t n){
    size_t left = n;
    auto *ptr = static_cast<const char*>(buf);
    while(left > 0){
    #ifdef _WIN32
        int nwrite = send(fd, ptr, (int)left, 0);
        if(nwrite == SOCK_ERR){
            int e = WSAGetLastError();
            if(e == WSAEINTR or e == WSAEWOULDBLOCK) continue;
            return -1;
        }
    #else
        ssize_t nwrite = send(fd, ptr, left, MSG_NOSIGNAL);
        if(nwrite == -1){
            if(errno == EINTR or errno == EAGAIN) continue;
            return -1;
        }
    #endif
        left -= (size_t)nwrite;
        ptr += nwrite;
    }
    return (long)n;
}

// Bidirectional data forwarding between two sockets using select()
int socket_pipe(socket_t fd0, socket_t fd1){
    char buffer[BUFSIZE];

    while(true){
        fd_set rd_set;
        FD_ZERO(&rd_set);
        FD_SET(fd0, &rd_set);
        FD_SET(fd1, &rd_set);

        socket_t maxfd = (fd0 > fd1) ? fd0 : fd1;
        int ret = select((int)(maxfd + 1), &rd_set, nullptr, nullptr, nullptr);

        if(ret < 0){
        #ifdef _WIN32
            if(WSAGetLastError() == WSAEINTR) continue;
        #else
            if(errno == EINTR) continue;
        #endif
            return -1;
        }

        if(FD_ISSET(fd0, &rd_set)){
            int nread = recv(fd0, buffer, (int)BUFSIZE, 0);
            if(nread <= 0) return 0;
            if(writen(fd1, buffer, (size_t)nread) < 0) return -1;
        }

        if(FD_ISSET(fd1, &rd_set)){
            int nread = recv(fd1, buffer, (int)BUFSIZE, 0);
            if(nread <= 0) return 0;
            if(writen(fd0, buffer, (size_t)nread) < 0) return -1;
        }
    }
}

int set_nonblocking(socket_t fd){
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int set_tcp_nodelay(socket_t fd){
    int one = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
}

int set_recv_timeout(socket_t fd, int seconds){
#ifdef _WIN32
    DWORD timeout = (DWORD)(seconds * 1000);
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv = {seconds, 0};
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif
}

socket_t create_server_socket(const char *bind_addr, uint16_t port, int backlog){
    socket_t sock = socket(AF_INET6, SOCK_STREAM, 0);
    if(sock == INVALID_SOCK) {
        // IPv6 stack not available, fall back to IPv4
        log_info("AF_INET6 socket failed, trying AF_INET");
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock == INVALID_SOCK) {
            log_error("socket() failed");
            return INVALID_SOCK;
        }

        int optval = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));

        struct sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        if(bind_addr and bind_addr[0]) local.sin_addr.s_addr = inet_addr(bind_addr);
        else local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(port);

        if(bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0){
            log_error("bind() failed on port %u", port);
            io_close(sock);
            return INVALID_SOCK;
        }

        if(listen(sock, backlog) < 0){
            log_error("listen() failed");
            io_close(sock);
            return INVALID_SOCK;
        }

        set_tcp_nodelay(sock);
        log_info("Listening on 0.0.0.0:%u (IPv4 only)", port);
        return sock;
    }

    // Dual-stack: accept both IPv4 (mapped) and IPv6 connections
    int optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));

    // Disable IPV6_V6ONLY so IPv4 clients can connect via mapped addresses
    int v6only = 0;
    setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof(v6only));

    struct sockaddr_in6 local;
    memset(&local, 0, sizeof(local));
    local.sin6_family = AF_INET6;
    if(bind_addr and bind_addr[0]){
        // Try to parse as IPv6, fall back to IPv4-mapped
        if(inet_pton(AF_INET6, bind_addr, &local.sin6_addr) != 1)
            inet_pton(AF_INET, bind_addr, &local.sin6_addr);
    }
    else local.sin6_addr = in6addr_any;
    local.sin6_port = htons(port);

    if(bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0){
        log_error("bind() failed on port %u", port);
        io_close(sock);
        return INVALID_SOCK;
    }

    if(listen(sock, backlog) < 0){
        log_error("listen() failed");
        io_close(sock);
        return INVALID_SOCK;
    }

    set_tcp_nodelay(sock);
    log_info("Listening on [::]:%u (dual-stack)", port);
    return sock;
}

socket_t connect_remote_ipv4(const uint8_t ip[4], uint16_t port){
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd == INVALID_SOCK) return INVALID_SOCK;

    set_tcp_nodelay(fd);

    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    char addr_str[16];
    snprintf(addr_str, sizeof(addr_str), "%hhu.%hhu.%hhu.%hhu", ip[0], ip[1], ip[2], ip[3]);
    remote.sin_addr.s_addr = inet_addr(addr_str);
    remote.sin_port = htons(port);

    if(connect_timeout(fd, (struct sockaddr *)&remote, sizeof(remote)) < 0){
        log_error("connect() to %s:%u failed", addr_str, port);
        io_close(fd);
        return INVALID_SOCK;
    }
    return fd;
}

socket_t connect_remote_ipv6(const uint8_t ip[16], uint16_t port){
    socket_t fd = socket(AF_INET6, SOCK_STREAM, 0);
    if(fd == INVALID_SOCK) return INVALID_SOCK;

    set_tcp_nodelay(fd);

    struct sockaddr_in6 remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin6_family = AF_INET6;
    memcpy(&remote.sin6_addr, ip, 16);
    remote.sin6_port = htons(port);

    char addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, ip, addr_str, sizeof(addr_str));
    log_info("Connecting to [%s]:%u", addr_str, port);

    if(connect_timeout(fd, (struct sockaddr *)&remote, sizeof(remote)) < 0){
        log_error("connect() to [%s]:%u failed", addr_str, port);
        io_close(fd);
        return INVALID_SOCK;
    }
    return fd;
}

socket_t connect_remote(const char *host, uint16_t port){
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(host, port_str, &hints, &res);
    if(ret != 0){
    #ifdef _WIN32
        log_error("getaddrinfo(%s) failed: %d", host, ret);
    #else
        log_error("getaddrinfo(%s) failed: %s", host, gai_strerror(ret));
    #endif
        return INVALID_SOCK;
    }

    // Initiate non-blocking connects to all addresses in parallel
    socket_t fds[8];
    int count = 0;
    fd_set wfds;
    FD_ZERO(&wfds);
    socket_t maxfd = 0;

    for(struct addrinfo *r = res; r != nullptr and count < 8; r = r->ai_next){
        socket_t s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if(s == INVALID_SOCK) continue;

        set_nonblocking(s);
        set_tcp_nodelay(s);

        int cr = connect(s, r->ai_addr, (int)r->ai_addrlen);
        if(cr == 0){
            // Connected immediately — winner, skip the rest
            freeaddrinfo(res);
            for(int i = 0; i < count; ++i) io_close(fds[i]);
            set_blocking(s);
            return s;
        }
#ifdef _WIN32
        if(WSAGetLastError() != WSAEWOULDBLOCK){ io_close(s); continue; }
#else
        if(errno != EINPROGRESS){ io_close(s); continue; }
#endif

        fds[count++] = s;
        FD_SET(s, &wfds);
        if(s > maxfd) maxfd = s;
    }
    freeaddrinfo(res);

    if(count == 0) return INVALID_SOCK;

    // Wait for ANY socket to complete (2 second total deadline)
    struct timeval tv = {2, 0};
    select((int)(maxfd + 1), nullptr, &wfds, nullptr, &tv);

    // Pick the first successfully connected socket, close the rest
    socket_t result = INVALID_SOCK;
    for(int i = 0; i < count; ++i){
        if(result == INVALID_SOCK){
            int err = 0; socklen_t len = sizeof(err);
            getsockopt(fds[i], SOL_SOCKET, SO_ERROR, (char *)&err, &len);
            if(err == 0){
                result = fds[i];
                set_blocking(result);
                continue;
            }
        }
        io_close(fds[i]);
    }

    if(result == INVALID_SOCK)
        log_error("connect_remote(%s:%u) all attempts failed", host, port);
    return result;
}
