// Cell stage co-op, "Symbiosis" (docs/09-coop-design.md, RE notes docs/re/cell.md):
//  - one tide pool: the host simulates NPC cells and food, guests show passive mirrors
//    (no local spawning, no local AI) and send their bites / eaten food to the host
//  - food progress is shared (the host keeps the total)
//  - a lethal hit knocks a player out for 10 s (translucent, "KO" countdown seen by everybody);
//    a partner right next to it revives it, otherwise it respawns small next to the partner;
//    health regenerates while a partner is close
#pragma once

#include "syncdbg.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace cellsync {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<std::vector<std::pair<std::string, net::Vec3>>()> partners; // same place
    std::function<void(const std::string&)> notice;
    std::function<std::string()> my_name;
    std::function<int(void* cell)> puppet_owner; // host: player id of a puppet cell, -1 if none
    struct Puppet {
        int id;
        std::string name;
        void* cell;
    };
    std::function<std::vector<Puppet>()> puppets; // remote players' cells in our pool
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
bool voluntary_leave_cleanup_enabled(); // test switch; other session endings always clean up
bool totals_resend_enabled(); // BUG-0128 switch (coop.pool fix resend): DNA total resent every 2 s too
void on_player_leave(int id); // a remote player left: its knockout and food bookkeeping go
void draw_overlay();
void join_partner(); // our cell next to the nearest partner (after a level rebuild) // knockout countdown / revive progress / off-screen arrows (SK_EV_OVERLAY)
std::string status();
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
std::string debug(const std::vector<std::string>& args);

bool food_total(int& food); // the own game's food total S+0x1C (support sync-check); false outside Cell

} // namespace cellsync
