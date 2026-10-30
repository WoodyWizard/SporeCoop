// Finding co-op games on the local network: broadcast a query to the default game ports,
// hosts answer with their name, player count and stage (portable, unit tested).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "protocol.h"
#include "udp.h"

namespace net {

class Discovery {
public:
    struct Game {
        Endpoint host;
        DiscoverReply info;
        uint32_t seen_ms = 0;
        uint32_t addr_seen_ms = 0; // BUG-0210: the last answer from `host` itself (not another address)
    };
    // where queries go (network byte order IPs): 255.255.255.255 and 127.0.0.1 by default
    std::vector<uint32_t> targets;
    uint32_t interval_ms = 1500;

    bool start();
    void stop();
    bool active() const { return sock_.is_open(); }
    void update(uint32_t now_ms);
    std::vector<Game> games(uint32_t now_ms) const; // answers from the last few seconds
    // one answer from `from` (what update() does per datagram; tests and `coop.lan fake` call it)
    void on_reply(const Endpoint& from, const DiscoverReply& rep, uint32_t now_ms);
    size_t stored() const { return games_.size(); } // entries kept, shown or not (tests)

    static constexpr uint32_t kShowMs = 5000; // a host that has not answered for this long is gone
    static constexpr uint32_t kSwitchMs = 3000; // BUG-0210: show another address of the game after this
    static constexpr size_t kMaxGames = 32;   // BUG-0039: fake answers cannot grow the list for ever

private:
    UdpSocket sock_;
    uint32_t last_query_ = 0;
    std::vector<Game> games_;
};

} // namespace net
