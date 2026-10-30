#include "discovery.h"

#include <algorithm>

#include "fixes.h"

namespace net {

static uint32_t ip4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t v;
    uint8_t* p = reinterpret_cast<uint8_t*>(&v);
    p[0] = a; p[1] = b; p[2] = c; p[3] = d;
    return v;
}

bool Discovery::start()
{
    if (active())
        return true;
    if (!sock_.open(0))
        return false;
    sock_.enable_broadcast();
    if (targets.empty())
        targets = {ip4(255, 255, 255, 255), ip4(127, 0, 0, 1)};
    last_query_ = 0;
    return true;
}

void Discovery::stop()
{
    sock_.close();
    games_.clear();
}

void Discovery::update(uint32_t now)
{
    if (!active())
        return;
    if (last_query_ == 0 || now - last_query_ >= interval_ms) {
        last_query_ = now ? now : 1;
        Writer w;
        w.u32(kDiscoverQuery);
        w.u16(kProtoVersion);
        for (uint32_t ip : targets)
            for (uint16_t i = 0; i < kDiscoverPorts; i++)
                sock_.send_to(Endpoint{ip, (uint16_t)(kDefaultPort + i)}, w.data.data(), w.size());
    }
    uint8_t buf[512];
    for (int i = 0; i < 64; i++) {
        Endpoint from;
        int n = sock_.recv_from(from, buf, sizeof buf);
        if (n <= 0)
            break;
        Reader r(buf, (size_t)n);
        DiscoverReply rep;
        if (!rep.read(r) || rep.proto != kProtoVersion)
            continue;
        on_reply(from, rep, now);
    }
    // BUG-0039: entries were never removed (a silent host was only hidden), so answers with a new
    // port each time (any machine on the LAN can send them) grew the list and the scan per answer
    if (g_fix.lan_prune)
        games_.erase(std::remove_if(games_.begin(), games_.end(),
                                    [&](const Game& g) { return now - g.seen_ms >= kShowMs; }),
                     games_.end());
}

void Discovery::on_reply(const Endpoint& from, const DiscoverReply& rep, uint32_t now)
{
    Endpoint host{from.ip, rep.port};
    bool found = false;
    uint32_t lo = ip4(127, 0, 0, 1);
    for (auto& g : games_) {
        // the same host can answer on loopback and on its LAN address: keep one
        bool same = g.host == host || (g.info.port == rep.port && g.info.name == rep.name &&
                                       (g.host.ip == lo || host.ip == lo));
        // BUG-0210: ... and on each of its LAN addresses (cable and Wi-Fi): its game id tells
        bool same_id = g_fix.lan_dedup && rep.has_id && g.info.has_id && g.info.host_id == rep.host_id &&
                       g.info.port == rep.port;
        if (same || same_id) {
            if (g.host.ip == lo && host.ip != lo)
                g.host = host; // prefer an address friends can use too
            else if (!(g.host == host) && same_id && now - g.addr_seen_ms >= kSwitchMs)
                g.host = host; // the shown address went quiet (its adapter is down): use one that answers
            if (g.host == host)
                g.addr_seen_ms = now;
            g.info = rep;
            g.seen_ms = now;
            found = true;
        }
    }
    if (found)
        return;
    if (g_fix.lan_prune && games_.size() >= kMaxGames) {
        // full: a new answer replaces the one not heard from the longest, if that one is stale
        auto old = std::min_element(games_.begin(), games_.end(), [&](const Game& a, const Game& b) {
            return now - a.seen_ms > now - b.seen_ms;
        });
        if (now - old->seen_ms < 2000)
            return; // every entry answered recently: keep them
        *old = {host, rep, now, now};
        return;
    }
    games_.push_back({host, rep, now, now});
}

std::vector<Discovery::Game> Discovery::games(uint32_t now) const
{
    std::vector<Game> v;
    for (auto& g : games_)
        if (now - g.seen_ms < kShowMs)
            v.push_back(g);
    return v;
}

} // namespace net
