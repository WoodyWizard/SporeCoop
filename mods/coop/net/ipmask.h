// An address for the logs and reports (players attach them to public GitHub issues): loopback stays
// as it is, a LAN / VPN address keeps its network class, a public address becomes a short hash that
// tells two players apart but not where they are. The full address stays where the code needs it.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace net {

// BUG-0776: may a LAN discovery query from `ip` (network byte order) be answered? Loopback, the private ranges
// (10/8, 172.16/12, 192.168/16), link-local 169.254/16, the carrier / VPN range 100.64/10 (Tailscale), or a subnet
// of one of this machine's adapters, given by its broadcast address (a virtual LAN on public numbers: Radmin VPN
// 26/8, Hamachi 25/8). The subnet of a broadcast address b is taken as the addresses that equal b once the trailing
// one bits of b are set (at most one bit wider than the real mask, when the network part ends in a one).
inline bool local_source(uint32_t ip, const std::vector<uint32_t>& broadcasts)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&ip);
    if (b[0] == 127 || b[0] == 10 || (b[0] == 192 && b[1] == 168) || (b[0] == 172 && b[1] >= 16 && b[1] < 32) ||
        (b[0] == 169 && b[1] == 254) || (b[0] == 100 && b[1] >= 64 && b[1] < 128))
        return true;
    auto host_order = [](uint32_t v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    };
    uint32_t h = host_order(ip);
    for (uint32_t bc : broadcasts) {
        uint32_t hb = host_order(bc);
        if (hb == 0xFFFFFFFFu || hb == 0)
            continue; // the limited broadcast says nothing about a subnet
        uint32_t ones = ~hb & (hb + 1); // the lowest zero bit of hb
        uint32_t hostmask = ones - 1;   // the trailing one bits
        if (hostmask == 0 || hostmask > 0x00FFFFFFu)
            continue; // not a broadcast address of a /8 or smaller network
        if ((h | hostmask) == hb)
            return true;
    }
    return false;
}

// ip in network byte order (as Endpoint::ip), port in host order (0 = no port)
inline std::string mask_ip(uint32_t ip, uint16_t port)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&ip);
    char s[48];
    if (b[0] == 127 || ip == 0)
        snprintf(s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    else if (b[0] == 10)
        snprintf(s, sizeof s, "10.x.x.x");
    else if (b[0] == 192 && b[1] == 168)
        snprintf(s, sizeof s, "192.168.x.x");
    else if (b[0] == 172 && b[1] >= 16 && b[1] < 32)
        snprintf(s, sizeof s, "172.%u.x.x", b[1]);
    else if (b[0] == 100 && b[1] >= 64 && b[1] < 128)
        snprintf(s, sizeof s, "100.64.x.x (VPN/CGNAT)");
    else if (b[0] == 169 && b[1] == 254)
        snprintf(s, sizeof s, "169.254.x.x");
    else {
        uint32_t h = 0x811C9DC5u; // FNV-1a of the four bytes
        for (int i = 0; i < 4; i++)
            h = (h ^ b[i]) * 0x01000193u;
        snprintf(s, sizeof s, "public #%04X", h & 0xFFFF);
    }
    std::string r = s;
    if (port)
        r += ":" + std::to_string(port);
    return r;
}

} // namespace net
