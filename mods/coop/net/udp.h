// Minimal non-blocking UDP socket (Winsock on Windows, BSD sockets elsewhere).
#pragma once

#include <cstdint>
#include <string>

namespace net {

struct Endpoint {
    uint32_t ip = 0;   // network byte order
    uint16_t port = 0; // host byte order
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Endpoint& o) const { return !(*this == o); }
    std::string str() const;
    std::string masked() const; // for logs and reports (net/ipmask.h): never the full public address
    static bool parse(const std::string& s, uint16_t default_port, Endpoint& out);
    // err: why not ("the port must be a number from 1 to 65535", "cannot resolve <host>")
    static bool parse(const std::string& s, uint16_t default_port, Endpoint& out, std::string& err);
    static std::string clean(const std::string& s); // trimmed, the first address of a list
    // without a name lookup (never waits): false with *name set = a host name, out.port is set;
    // resolve_name looks it up (blocking: a slow name server holds the caller, BUG-0204)
    static bool parse_numeric(const std::string& s, uint16_t default_port, Endpoint& out, std::string& err,
                              std::string* name);
    static bool resolve_name(const std::string& host, uint32_t& ip, std::string& err);
};

// the name lookup behind resolve_name (getaddrinfo, IPv4); tests put a slow fake here
using LookupFn = bool (*)(const std::string& host, uint32_t& ip);
extern LookupFn g_lookup;

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(uint16_t port, std::string* err = nullptr); // 0 = ephemeral
    void close();
    bool is_open() const { return fd_ != -1; }
    bool send_to(const Endpoint& to, const void* data, size_t n);
    // returns bytes received, 0 if nothing pending, -1 on error
    int recv_from(Endpoint& from, void* buf, size_t cap);
    uint16_t local_port() const;
    bool enable_broadcast(); // allow sending to 255.255.255.255 / subnet broadcast

private:
    intptr_t fd_ = -1;
};

bool net_init(); // WSAStartup on Windows
uint32_t now_ms();

} // namespace net
