// Bytes and messages per message type, both directions, for all connections of this game (4P step 1, 2026-11-15):
// counted where a message is queued (out) or parsed (in), with its message header; the packet headers are not split
// per type. Portable (no Windows). diag_stats.cpp logs the top types once a minute and starts over.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace net {

struct TrafficCount {
    uint64_t bytes[256] = {};
    uint32_t msgs[256] = {};
    void add(uint8_t type, size_t n) { bytes[type] += n; msgs[type]++; }
    void clear() { *this = TrafficCount{}; }
};
inline TrafficCount g_traffic_out, g_traffic_in;

// "62:12.3KB/410 13:8.1KB/300 ..." the `top` types by bytes
inline std::string traffic_text(const TrafficCount& t, int top)
{
    std::vector<int> order;
    for (int i = 0; i < 256; i++)
        if (t.msgs[i]) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return t.bytes[a] > t.bytes[b]; });
    std::string s;
    uint64_t total = 0;
    for (int i : order) total += t.bytes[i];
    char b[64];
    std::snprintf(b, sizeof b, "%.1f KB:", total / 1024.0);
    s = b;
    for (int k = 0; k < (int)order.size() && k < top; k++) {
        std::snprintf(b, sizeof b, " %d=%.1fKB/%u", order[k], t.bytes[order[k]] / 1024.0, t.msgs[order[k]]);
        s += b;
    }
    return s;
}

} // namespace net
