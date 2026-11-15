// What a Session sends and receives through: the real UDP socket by default, a simulated lossy
// network in the tests (tests/fuzz_decoders.cpp, docs/04-netcode.md#netcode-fuzzing).
#pragma once

#include <cstdint>
#include <string>

#include "udp.h"

namespace net {

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool open(uint16_t port, std::string* err) = 0; // 0 = ephemeral
    virtual void close() = 0;
    virtual bool send_to(const Endpoint& to, const void* data, size_t n) = 0;
    // bytes received, 0 if nothing pending, -1 on error, -2 (Session::kRecvSkipped) when one datagram was dropped
    // (too big for `cap`, BUG-1007): more may be pending
    virtual int recv_from(Endpoint& from, void* buf, size_t cap) = 0;
    virtual uint16_t local_port() const = 0;
};

class UdpTransport : public Transport {
public:
    bool open(uint16_t port, std::string* err) override { return sock_.open(port, err); }
    void close() override { sock_.close(); }
    bool send_to(const Endpoint& to, const void* data, size_t n) override { return sock_.send_to(to, data, n); }
    int recv_from(Endpoint& from, void* buf, size_t cap) override { return sock_.recv_from(from, buf, cap); }
    uint16_t local_port() const override { return sock_.local_port(); }

private:
    UdpSocket sock_;
};

} // namespace net
