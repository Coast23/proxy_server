# Proxy Server

基于 C++17 的高性能多协议代理服务器，支持 SOCKS4 / SOCKS4a / SOCKS5 / HTTP，具备自动协议检测与 IPv4/IPv6 双栈能力。

[:us: English](README.md)

## 特性

- **SOCKS5** — CONNECT 命令，IPv4 / IPv6 / 域名三种地址类型，NOAUTH 和 USERPASS 两种认证方式
- **SOCKS4 / SOCKS4a** — CONNECT 命令，IPv4 直连 + 域名解析（4a 模式自动检测）
- **HTTP** — CONNECT 隧道（用于 HTTPS）+ 基础 HTTP 转发（绝对 URL 请求）
- **双栈 IPv4/IPv6** — 单 `AF_INET6` 套接字 + `IPV6_V6ONLY=0` 同时接受 IPv4 和 IPv6 连接，IPv6 栈不可用时自动回退到 IPv4
- **自动协议检测** — 根据客户端握手首字节识别 SOCKS4 / SOCKS5 / HTTP
- **每连接一线程** — 每个连接获得独立线程，零排队
- **握手前过滤** — 浏览器空探针在主线程中 1 秒内关闭，不创建工作线程
- **并行 DNS 连接** — 所有解析地址同时发起非阻塞连接，2 秒截止
- **跨平台** — Windows (Winsock2) 与 Linux (POSIX sockets)，通过 `_WIN32` 条件编译

## 快速开始

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
./proxy_server
```

默认监听 `0.0.0.0:1080`（双栈，SOCKS5 无认证）。

## 命令行参数

```
proxy_server [-h] [-p PORT] [-a AUTHTYPE] [-u USERNAME] [-P PASSWORD] [-b BIND_ADDR]

  -p PORT       监听端口（默认: 1080）
  -a AUTHTYPE   SOCKS5 认证方式: 0 = 无认证, 2 = 用户名密码（默认: 0）
  -u USERNAME   认证用户名（默认: user）
  -P PASSWORD   认证密码（默认: pass）
  -b BIND_ADDR  绑定地址（默认: 0.0.0.0，双栈通过 :: 实现）
  -h            显示帮助信息
```

### 使用示例

```bash
./proxy_server                                         # 默认启动
./proxy_server -p 1080 -a 2 -u admin -P secret123      # 启用用户名密码认证
./proxy_server -b 127.0.0.1                            # 仅监听本地
```

## 客户端配置

### curl

```bash
# SOCKS5
curl --proxy socks5://127.0.0.1:1080 http://example.com
curl --proxy socks5://127.0.0.1:1080 https://example.com

# SOCKS5 带认证
curl --proxy socks5://admin:secret123@127.0.0.1:1080 http://example.com

# HTTP 代理
curl --proxy http://127.0.0.1:1080 https://example.com

# IPv6 目标（socks5h 表示域名由代理端解析）
curl --proxy socks5h://127.0.0.1:1080 http://ipv6.google.com
```

### Firefox

`设置 → 常规 → 网络设置 → 手动代理配置`
- SOCKS 主机: `127.0.0.1`，端口: `1080`，选择 SOCKS5

### Windows 系统代理

`设置 → 网络和 Internet → 代理 → 手动设置代理`
- 地址: `127.0.0.1`，端口: `1080`（仅支持 HTTP 转发，推荐使用浏览器插件以支持 SOCKS）

## 协议支持

| 协议      | 命令                | 地址类型             | 认证方式           |
|-----------|---------------------|----------------------|-------------------|
| SOCKS4    | CONNECT             | IPv4                 | —                 |
| SOCKS4a   | CONNECT             | 域名                 | —                 |
| SOCKS5    | CONNECT             | IPv4, IPv6, 域名     | NOAUTH, USERPASS  |
| HTTP      | CONNECT, GET/POST/… | 域名                 | —                 |

HTTPS 无需额外实现 — 代理仅做 TCP 层隧道转发，TLS 握手在客户端与目标服务器之间端到端完成。

## 架构

```
main.cpp           入口、命令行解析、信号处理、accept 循环
io.hpp / io.cpp    跨平台套接字 I/O、日志、DNS 解析、并行连接
session.cpp        协议检测 + SOCKS4/4a/5/HTTP 处理
thread_pool.hpp    头文件线程池（保留为工具组件，main 不再使用）
```

```
客户端 ──TCP──> accept()
                   │
                   ├─ select(fd, 1s) ── 超时 ──> close（浏览器探针）
                   │
                   └─ 数据就绪 ──> std::thread::detach()
                                       │
                                 协议检测（首字节）
                                       │
                          ┌────────────┼────────────┐
                          │            │            │
                       SOCKS4       SOCKS5        HTTP
                          │            │            │
                          └────────────┼────────────┘
                                       │
                              并行连接（2s 截止）
                                       │
                                  socket_pipe()
                                （双向数据转发）
```

## 构建依赖

- CMake ≥ 3.12
- C++17 编译器（g++ / clang++ / MSVC）

## 许可证

MIT
