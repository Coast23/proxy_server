#include "session.hpp"
#include <cstdlib>
#include <cstring>

ProxyConfig g_config;

enum Socks5Auth : uint8_t {
    SOCKS5_NOAUTH   = 0x00,
    SOCKS5_USERPASS = 0x02,
    SOCKS5_NOMETHOD = 0xff,
};

enum Socks5Cmd : uint8_t {
    SOCKS5_CONNECT = 0x01,
};

enum Socks5Atyp : uint8_t {
    SOCKS5_IPV4   = 0x01,
    SOCKS5_IPV6   = 0x04,
    SOCKS5_DOMAIN = 0x03,
};

enum Socks5Rep : uint8_t {
    SOCKS5_REP_OK             = 0x00,
    SOCKS5_REP_GEN_FAIL       = 0x01,
    SOCKS5_REP_CONN_REFUSED   = 0x05,
    SOCKS5_REP_CMD_UNSUPPORTED = 0x07,
    SOCKS5_REP_ATYP_UNSUPPORTED = 0x08,
};

static void socks5_send_reply(socket_t fd, uint8_t rep, uint8_t atyp,
                               const void *addr, uint8_t addr_len, uint16_t port){
    uint8_t response[4] = {0x05, rep, 0x00, atyp};
    writen(fd, response,4);
    writen(fd, addr, addr_len);
    uint16_t netport = htons(port);
    writen(fd, &netport, 2);
}

static void socks5_send_reply_fail(socket_t fd, uint8_t rep){
    uint8_t ipv4[4] = {0, 0, 0, 0};
    socks5_send_reply(fd, rep, SOCKS5_IPV4, ipv4, 4, 0);
}

static int socks5_auth_userpass(socket_t fd){
    uint8_t answer[2] = {0x05, SOCKS5_USERPASS};
    writen(fd, answer, 2);

    uint8_t auth_ver;
    if(readn(fd, &auth_ver, 1) <= 0) return -1;

    uint8_t ulen;
    if(readn(fd, &ulen, 1) <= 0) return -1;
    char username[256];
    if(readn(fd, username, ulen) <= 0) return -1;
    username[ulen] = '\0';

    uint8_t plen;
    if(readn(fd, &plen, 1) <= 0) return -1;
    char password[256];
    if(readn(fd, password, plen) <= 0) return -1;
    password[plen] = '\0';

    log_info("SOCKS5 auth: user=%s", username);

    uint8_t resp[2];
    if(strcmp(g_config.username, username) == 0 and
        strcmp(g_config.password, password) == 0){
        resp[0] = 0x01; resp[1] = 0x00;
        writen(fd, resp, 2);
        return 0;
    }
    else{
        resp[0] = 0x01; resp[1] = 0xff;
        writen(fd, resp, 2);
        return -1;
    }
}

static int socks5_auth(socket_t fd, int nmethods){
    // Read methods list
    uint8_t methods[256];
    if(readn(fd, methods, (size_t)nmethods) <= 0) return -1;

    bool supported = false;
    for(int i = 0; i < nmethods; ++i){
        if(methods[i] == (uint8_t)g_config.auth_type){
            supported = true;
            break;
        }
    }

    if(!supported){
        uint8_t answer[2] = {0x05, SOCKS5_NOMETHOD};
        writen(fd, answer, 2);
        return -1;
    }

    switch(g_config.auth_type){
    case SOCKS5_NOAUTH: {
        uint8_t answer[2] = {0x05, SOCKS5_NOAUTH};
        writen(fd, answer, 2);
        return 0;
    }
    case SOCKS5_USERPASS:
        return socks5_auth_userpass(fd);
    default:
        return -1;
    }
}

static int handle_socks5(socket_t client_fd){
    // Read request header (4 bytes: VER CMD RSV ATYP)
    uint8_t req[4];
    if(readn(client_fd, req, 4) <= 0) return -1;

    if(req[1] != SOCKS5_CONNECT){
        log_info("SOCKS5 unsupported command: 0x%02x", req[1]);
        socks5_send_reply_fail(client_fd, SOCKS5_REP_CMD_UNSUPPORTED);
        return -1;
    }

    if(req[3] == SOCKS5_IPV4){
        // IPv4 address
        uint8_t ip[4];
        if(readn(client_fd, ip, 4) <= 0) return -1;
        uint16_t port_net;
        if(readn(client_fd, &port_net, 2) <= 0) return -1;
        uint16_t port = ntohs(port_net);

        log_info("SOCKS5 CONNECT -> %hhu.%hhu.%hhu.%hhu:%u",
                 ip[0], ip[1], ip[2], ip[3], port);

        socket_t remote = connect_remote_ipv4(ip, port);
        if(remote == INVALID_SOCK){
            socks5_send_reply_fail(client_fd, SOCKS5_REP_CONN_REFUSED);
            return -1;
        }

        socks5_send_reply(client_fd, SOCKS5_REP_OK, SOCKS5_IPV4, ip, 4, port);
        socket_pipe(client_fd, remote);
        io_close(remote);
        return 0;

    }
    else if(req[3] == SOCKS5_IPV6){
        // IPv6 address
        uint8_t ip[16];
        if(readn(client_fd, ip, 16) <= 0) return -1;
        uint16_t port_net;
        if(readn(client_fd, &port_net, 2) <= 0) return -1;
        uint16_t port = ntohs(port_net);

        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, ip, ip_str, sizeof(ip_str));
        log_info("SOCKS5 CONNECT -> [%s]:%u", ip_str, port);

        socket_t remote = connect_remote_ipv6(ip, port);
        if(remote == INVALID_SOCK){
            socks5_send_reply_fail(client_fd, SOCKS5_REP_CONN_REFUSED);
            return -1;
        }

        socks5_send_reply(client_fd, SOCKS5_REP_OK, SOCKS5_IPV6, ip, 16, port);
        socket_pipe(client_fd, remote);
        io_close(remote);
        return 0;

    }
    else if(req[3] == SOCKS5_DOMAIN){
        // Domain name
        uint8_t dlen;
        if(readn(client_fd, &dlen, 1) <= 0) return -1;
        char domain[256];
        if(readn(client_fd, domain, dlen) <= 0) return -1;
        domain[dlen] = '\0';
        uint16_t port_net;
        if(readn(client_fd, &port_net, 2) <= 0) return -1;
        uint16_t port = ntohs(port_net);

        log_info("SOCKS5 CONNECT -> %s:%u", domain, port);

        socket_t remote = connect_remote(domain, port);
        if(remote == INVALID_SOCK){
            socks5_send_reply_fail(client_fd, SOCKS5_REP_CONN_REFUSED);
            return -1;
        }

        socks5_send_reply(client_fd, SOCKS5_REP_OK, SOCKS5_DOMAIN,
                          domain, dlen, port);
        socket_pipe(client_fd, remote);
        io_close(remote);
        return 0;
    }
    else{
        log_info("SOCKS5 unsupported atyp: 0x%02x", req[3]);
        socks5_send_reply_fail(client_fd, SOCKS5_REP_ATYP_UNSUPPORTED);
        return -1;
    }
}

static bool socks4_is_4a(const uint8_t ip[4]){
    return ip[0] == 0 and ip[1] == 0 and ip[2] == 0 and ip[3] != 0;
}

static ssize_t socks4_read_nstring(socket_t fd, char *buf, size_t size){
    size_t i = 0;
    while(i < size){
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if(n <= 0) break;
        buf[i++] = c;
        if(c == '\0') break;
    }
    return (ssize_t)i;
}

static void socks4_send_response(socket_t fd, uint8_t status){
    uint8_t resp[8] = {0x00, status, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    writen(fd, resp, 8);
}

static int handle_socks4(socket_t client_fd){
    uint16_t port_net;
    if(readn(client_fd, &port_net, 2) <= 0) return -1;
    uint16_t port = ntohs(port_net);

    uint8_t ip[4];
    if(readn(client_fd, ip, 4) <= 0) return -1;

    // Read userid (null-terminated)
    char userid[256];
    socks4_read_nstring(client_fd, userid, sizeof(userid));

    socket_t remote;
    if(socks4_is_4a(ip)){
        // SOCKS4A: domain name follows userid
        char domain[256];
        socks4_read_nstring(client_fd, domain, sizeof(domain));
        log_info("SOCKS4a CONNECT -> %s:%u (user=%s)", domain, port, userid);
        remote = connect_remote(domain, port);
    }
    else{
        log_info("SOCKS4 CONNECT -> %hhu.%hhu.%hhu.%hhu:%u (user=%s)",
                 ip[0], ip[1], ip[2], ip[3], port, userid);
        remote = connect_remote_ipv4(ip, port);
    }

    if(remote == INVALID_SOCK){
        socks4_send_response(client_fd, 0x5b); // rejected
        return -1;
    }

    socks4_send_response(client_fd, 0x5a); // granted
    socket_pipe(client_fd, remote);
    io_close(remote);
    return 0;
}

static char *http_read_line(socket_t fd, char *buf, size_t size){
    size_t i = 0;
    while(i < size - 1){
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if(n <= 0) return nullptr;
        buf[i++] = c;
        if(c == '\n') break;
    }
    buf[i] = '\0';
    // Trim trailing \r\n
    while(i > 0 and (buf[i-1] == '\r' or buf[i-1] == '\n')) buf[--i] = '\0';
    return buf;
}

// Parse "CONNECT host:port HTTP/1.1" or "GET http://host/path HTTP/1.1"
static int http_parse_request(const char *line, char *host, size_t host_sz,
                               uint16_t *port, bool *is_connect){
    // "CONNECT host:port HTTP/1.x"
    if(strncmp(line, "CONNECT ", 8) == 0){
        *is_connect = true;
        const char *p = line + 8;
        const char *colon = strrchr(p, ':');
        if(!colon) return -1;

        size_t host_len = (size_t)(colon - p);
        if(host_len >= host_sz) return -1;
        memcpy(host, p, host_len);
        host[host_len] = '\0';

        *port = (uint16_t)atoi(colon + 1);
        return 0;
    }

    // Regular HTTP request: "METHOD http://host[:port]/path HTTP/1.x"
    *is_connect = false;
    const char *proto = strstr(line, "http://");
    if(!proto) return -1;

    const char *p = proto + 7; // skip "http://"
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    const char *host_end;
    if(slash and colon and colon < slash){
        // host:port/path
        host_end = colon;
        *port = (uint16_t)atoi(colon + 1);
    }
    else if(colon and !slash){
        host_end = colon;
        *port = (uint16_t)atoi(colon + 1);
    }
    else if(slash){
        host_end = slash;
        *port = 80;
    }
    else{
        host_end = p + strlen(p);
        *port = 80;
    }

    size_t host_len = (size_t)(host_end - p);
    if(host_len >= host_sz) return -1;
    memcpy(host, p, host_len);
    host[host_len] = '\0';
    return 0;
}

static int handle_http(socket_t client_fd, char first_byte){
    // Build first line from the already-read byte + rest
    char line[8192];
    line[0] = first_byte;
    size_t pos = 1;

    // Read the rest of the first line
    while(pos < sizeof(line) - 1){
        char c;
        ssize_t n = recv(client_fd, &c, 1, 0);
        if(n <= 0) return -1;
        line[pos++] = c;
        if(c == '\n') break;
    }
    line[pos] = '\0';
    // Trim \r\n
    while(pos > 0 and (line[pos-1] == '\r' or line[pos-1] == '\n')) line[--pos] = '\0';

    log_info("HTTP request: %s", line);

    char host[256];
    uint16_t port;
    bool is_connect;
    if(http_parse_request(line, host, sizeof(host), &port, &is_connect) < 0){
        const char *resp = "HTTP/1.1 400 Bad Request\r\n\r\n";
        writen(client_fd, resp, strlen(resp));
        return -1;
    }

    if(is_connect){
        // CONNECT tunnel: consume remaining headers up to empty line
        char hdr_line[4096];
        while(http_read_line(client_fd, hdr_line, sizeof(hdr_line))){
            if(hdr_line[0] == '\0') break;
        }
        log_info("HTTP CONNECT -> %s:%u", host, port);
        socket_t remote = connect_remote(host, port);
        if(remote == INVALID_SOCK){
            const char *resp = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
            writen(client_fd, resp, strlen(resp));
            return -1;
        }

        const char *resp = "HTTP/1.1 200 Connection Established\r\n\r\n";
        writen(client_fd, resp, strlen(resp));
        socket_pipe(client_fd, remote);
        io_close(remote);
        return 0;
    }
    else{
        // Regular HTTP forward: read remaining headers, connect, forward request
        // Skip all headers until empty line
        char hdr_line[4096];
        while(http_read_line(client_fd, hdr_line, sizeof(hdr_line))){
            if(hdr_line[0] == '\0') break; // empty line = end of headers
        }

        log_info("HTTP forward -> %s:%u", host, port);
        socket_t remote = connect_remote(host, port);
        if(remote == INVALID_SOCK){
            const char *resp = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
            writen(client_fd, resp, strlen(resp));
            return -1;
        }

        // Reconstruct and forward the request (strip http://host from path)
        char fwd[8192];
        const char *method_end = strchr(line, ' ');
        if(!method_end){ io_close(remote); return -1;}
        const char *url_start = method_end + 1;
        const char *http = strstr(url_start, "http://");
        if(http){
            const char *path = strchr(http + 7, '/');
            if(!path) path = "/";
            size_t method_len = (size_t)(method_end - line);
            memcpy(fwd, line, method_len);
            fwd[method_len] = ' ';
            strcpy(fwd + method_len + 1, path);
            strcat(fwd, " HTTP/1.0\r\n");
        }
        else snprintf(fwd, sizeof(fwd), "%s\r\n", line);
        writen(remote, fwd, strlen(fwd));

        // Add basic forwarding headers
        char hdr[256];
        snprintf(hdr, sizeof(hdr), "Host: %s\r\n", host);
        writen(remote, hdr, strlen(hdr));
        const char *end_hdr = "Connection: close\r\n\r\n";
        writen(remote, end_hdr, strlen(end_hdr));

        // Forward response
        socket_pipe(client_fd, remote);
        io_close(remote);
        return 0;
    }
}

static bool is_http_start(uint8_t byte){
    // HTTP methods: GET, POST, CONNECT, PUT, DELETE, HEAD, OPTIONS, PATCH, TRACE
    return (byte >= 'A' and byte <= 'Z');
}

void dispatch_protocol(socket_t client_fd, uint8_t first_byte){
    if(first_byte == 0x05){
        // SOCKS5: first byte = version, second byte = nmethods
        uint8_t nmethods;
        if(readn(client_fd, &nmethods, 1) <= 0) return;
        if(socks5_auth(client_fd, nmethods) < 0) return;
        handle_socks5(client_fd);
    }
    else if(first_byte == 0x04){
        // SOCKS4: read CD (command code), must be CONNECT (0x01)
        uint8_t cd;
        if(readn(client_fd, &cd, 1) <= 0) return;
        if(cd != 0x01){
            log_info("SOCKS4 unsupported command: 0x%02x", cd);
            socks4_send_response(client_fd, 0x5b);
            return;
        }
        handle_socks4(client_fd);
    }
    else if(is_http_start(first_byte)) handle_http(client_fd, (char)first_byte);
    else log_info("Unknown protocol, first byte: 0x%02x", first_byte);
}

void handle_session(socket_t client_fd){
    set_tcp_nodelay(client_fd);

    // Read first byte to determine protocol (consumes it)
    char first_char;
    ssize_t n = recv(client_fd, &first_char, 1, 0);
    if(n <= 0){
        io_close(client_fd);
        return;
    }

    dispatch_protocol(client_fd, (uint8_t)first_char);
    io_close(client_fd);
}
