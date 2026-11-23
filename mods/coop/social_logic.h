// The Creature stage's social rules that need no game (tests/npc_tests.cpp): the guest's species events of its own
// game while the host is away (BUG-1527) and the hold of a won social meter (BUG-1515).
//
// BUG-1527: while the host is away (its editor, a load: coop.cpp g_host_away) a guest plays its own game's animals.
// Its kills and impressions of them changed its game's species record (killed 0 -> 1 on the hunt card), and every
// 0.5 s the guest wrote the host's last table over it (social.cpp enforce_table): "the count goes up one, then back to
// zero". Now the guest keeps its own record while the host is away and keeps the events (SoloEvents); when the host is
// back the guest sends them (MSG_SOCIAL K_SOLO, a host of creature 1.3: kSoloMinor) and the host's game applies each
// through its own ApplyEvent (the extinction, the alliance and the cards follow); the guest takes the host's table
// again once one newer than the send came (or kSoloHoldS later).
#pragma once

#include <cstdint>
#include <vector>

namespace social_logic {

// the events a guest's own game makes that the host's game replays: 0 adult killed, 3 one creature impressed, 5 young
// killed (docs/re/creature.md: ApplyEvent 0xBA49E0). 2 (attacked) and the egg events are not kept: an attack only
// turns a relation for a while, the egg node runs on the host's own avatar.
inline bool solo_event(int event) { return event == 0 || event == 3 || event == 5; }

constexpr size_t kSoloMax = 32;      // species x event entries kept (a message holds at most this many)
constexpr uint16_t kSoloCountMax = 50; // events of one entry (a long hunt alone; a peer's larger count is refused)
constexpr double kSoloHoldS = 3.0;   // the guest takes the host's table again at the latest this long after the send
constexpr uint16_t kSoloMinor = 3;   // the creature family minor from which a host reads K_SOLO (WP-0 of 0.7.17)

struct SoloEvent {
    uint32_t key[3];
    uint8_t event;
    uint16_t count;
};

// the guest's events of its own game during one away time, one entry per species and event
struct SoloEvents {
    std::vector<SoloEvent> v;
    uint32_t dropped = 0; // events past kSoloMax entries or kSoloCountMax

    bool add(const uint32_t key[3], int event)
    {
        if (!solo_event(event))
            return false;
        for (auto& e : v)
            if (e.event == event && e.key[0] == key[0] && e.key[1] == key[1] && e.key[2] == key[2]) {
                if (e.count >= kSoloCountMax) {
                    dropped++;
                    return false;
                }
                e.count++;
                return true;
            }
        if (v.size() >= kSoloMax) {
            dropped++;
            return false;
        }
        v.push_back({{key[0], key[1], key[2]}, (uint8_t)event, 1});
        return true;
    }
    bool empty() const { return v.empty(); }
    void clear() { v.clear(); }
};

// guest: does an event of its own game count as its own (kept, not overwritten)? fix: social.solo_table; mirror: the
// event's creature is a proxy of a host NPC or a puppet (those are the host's, social.cpp drops them anyway)
inline bool keep_own(bool fix, bool guest, bool host_away, bool mirror) { return fix && guest && host_away && !mirror; }

// guest: write the host's table into the game now? Not while the host is away (fix), nor after a send of solo events
// until a table newer than the send came or kSoloHoldS passed
inline bool enforce_now(bool fix, bool host_away, double now, double sent_at, double table_at)
{
    if (!fix)
        return true;
    if (host_away)
        return false;
    if (sent_at >= 0 && table_at <= sent_at && now - sent_at < kSoloHoldS)
        return false;
    return true;
}

// host: is a received entry one to apply (the wire's bounds)
inline bool entry_ok(const SoloEvent& e) { return solo_event(e.event) && e.count >= 1 && e.count <= kSoloCountMax && e.key[0]; }

// BUG-1515: a won meter (won_at >= 0) is closed only kWinHold s after the win (fix: social.meter_hold)
inline bool meter_close_due(bool fix, double won_at, double now, double hold)
{
    return !fix || won_at < 0 || now - won_at >= hold;
}

} // namespace social_logic
