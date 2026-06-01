#include "io.hpp"
#include "session.hpp"
#include "thread_pool.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <signal.h>
#endif

static std::atomic<bool> g_running{true};

static void signal_handler(int){
    g_running = false;
}

static void setup_signals(){
#ifdef _WIN32
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
#endif
}

static void print_usage(const char *app){
    printf("USAGE: %s [-h] [-p PORT] [-a AUTHTYPE] [-u USERNAME] [-P PASSWORD] [-t THREADS] [-b BIND_ADDR]\n", app);
    printf("  -p PORT       Listen port (default: 1080)\n");
    printf("  -a AUTHTYPE   SOCKS5 auth: 0=NOAUTH, 2=USERPASS (default: 0)\n");
    printf("  -u USERNAME   Auth username (default: user)\n");
    printf("  -P PASSWORD   Auth password (default: pass)\n");
    printf("  -t THREADS    Worker thread count (default: CPU cores)\n");
    printf("  -b BIND_ADDR  Bind address (default: 0.0.0.0)\n");
    printf("  -h            Show this help\n");
    exit(0);
}

signed main(const int argc, const char *argv[]){
    // Defaults
    const char *bind_addr = "0.0.0.0";
    size_t threads = 0; // auto

    // Parse args
    for(int i = 1; i < argc; ++i){
        if(strcmp(argv[i], "-h") == 0) print_usage(argv[0]);
        else if(strcmp(argv[i], "-p") == 0 and i + 1 < argc)
            g_config.port = (uint16_t)(atoi(argv[++i]) & 0xffff);
        else if(strcmp(argv[i], "-a") == 0 and i + 1 < argc)
            g_config.auth_type = atoi(argv[++i]);
        else if(strcmp(argv[i], "-u") == 0 and i + 1 < argc)
            g_config.username = argv[++i];
        else if(strcmp(argv[i], "-P") == 0 and i + 1 < argc)
            g_config.password = argv[++i];
        else if(strcmp(argv[i], "-t") == 0 and i + 1 < argc)
            threads = (size_t)atoi(argv[++i]);
        else if(strcmp(argv[i], "-b") == 0 and i + 1 < argc)
            bind_addr = argv[++i];
    }

#ifdef _WIN32
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0){
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    log_info("Starting proxy server on %s:%u", bind_addr, g_config.port);
    log_info("SOCKS5 auth: %s", g_config.auth_type == 0 ? "NOAUTH" : "USERPASS");
    if(g_config.auth_type == 2){
        log_info("Credentials: %s / %s", g_config.username, g_config.password);
    }

    socket_t listen_fd = create_server_socket(bind_addr, g_config.port, 128);
    if(listen_fd == INVALID_SOCK){
        fprintf(stderr, "Failed to create server socket\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    setup_signals();

    ThreadPool pool(threads);
    log_info("Thread pool: %zu workers", pool.size());
    log_info("Listening on %s:%u ...", bind_addr, g_config.port);

    struct sockaddr_storage remote;
    socklen_t remotelen = sizeof(remote);

    while(g_running){
        memset(&remote, 0, sizeof(remote));
        remotelen = sizeof(remote);
        socket_t client_fd = accept(listen_fd, (struct sockaddr *)&remote, &remotelen);
        if(client_fd == INVALID_SOCK){
#ifdef _WIN32
            if(!g_running) break;
            int e = WSAGetLastError();
            if(e == WSAEINTR) continue;
#else
            if(errno == EINTR) continue;
#endif
            log_error("accept() failed");
            continue;
        }

        char client_ip[INET6_ADDRSTRLEN];
        uint16_t client_port = 0;
        if(remote.ss_family == AF_INET6){
            auto *addr6 = (struct sockaddr_in6 *)&remote;
            inet_ntop(AF_INET6, &addr6->sin6_addr, client_ip, sizeof(client_ip));
            client_port = ntohs(addr6->sin6_port);
        }
        else{
            auto *addr4 = (struct sockaddr_in *)&remote;
            inet_ntop(AF_INET, &addr4->sin_addr, client_ip, sizeof(client_ip));
            client_port = ntohs(addr4->sin_port);
        }
        log_info("Connection from %s:%u", client_ip, client_port);

        pool.enqueue([client_fd](){handle_session(client_fd);});
    }

    log_info("Shutting down...");
    io_close(listen_fd);

#ifdef _WIN32
    WSACleanup();
#endif

    log_info("Server stopped.");
    return 0;
}
