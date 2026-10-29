// An address for the logs and reports (players attach them to public GitHub issues): loopback stays
// as it is, a LAN / VPN address keeps its network class, a public address becomes a short hash that
// tells two players apart but not where they are. The full address stays where the code needs it.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace net {

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
