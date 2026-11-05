// Cell stage co-op, "Symbiosis" (docs/09-coop-design.md, RE notes docs/re/cell.md):
//  - one tide pool: the host simulates NPC cells and food, guests show passive mirrors
//    (no local spawning, no local AI) and send their bites / eaten food to the host
//  - food progress is shared (the host keeps the total)
//  - a lethal hit knocks a player out for 10 s (translucent, "KO" countdown seen by everybody);
//    a partner right next to it revives it, otherwise it respawns small next to the partner;
//    health regenerates while a partner is close
#pragma once

#include "view_centres.h"

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
    // a partner's meals added to the shared food here (the DNA cause ledger, dna_ledger.h): `from` the
    // guest whose meal the host applied, 0xFF on a guest (the host's total: some partner's)
    std::function<void(int from, int before, int amount)> food_cause;
    std::function<std::string()> my_name;
    std::function<int(void* cell)> puppet_owner; // host: player id of a puppet cell, -1 if none
    struct Puppet {
        int id;
        std::string name;
        void* cell;
    };
    std::function<std::vector<Puppet>()> puppets; // remote players' cells in our pool
    std::function<bool()> host_away; // guest: the host is away (coop.cpp g_host_away): we play our own pool (BUG-0205)
    // BUG-0510 (dna_pace.h): an own meal's share of the shared food; the host's divisor to send; a
    // guest heard the host's divisor
    std::function<int(int)> pace_meal;
    std::function<int()> pace_announce;
    std::function<void(int)> pace_heard;
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
bool voluntary_leave_cleanup_enabled(); // test switch; other session endings always clean up
bool totals_resend_enabled();
bool away_enabled(); // BUG-0205 switch (pool.away): a host editor or load counts as away in Cell too // BUG-0128 switch (coop.pool fix resend): DNA total resent every 2 s too
void on_player_leave(int id); // a remote player left: its knockout and food bookkeeping go
void draw_overlay();
void dna_note(const char* when); // BUG-0351: food, spent DNA and the editor budget in the log
int editor_budget();
bool spent_dna(int& v);           // W1: cCellSerializableData +0x2C, the DNA the editors spent (false: not in Cell)
void set_spent_dna(int v);              // BUG-0560: DNAForFood(food) - spent, what the game's request gives the editor (-1: none)
void join_partner(); // our cell next to the nearest partner (after a level rebuild) // knockout countdown / revive progress / off-screen arrows (SK_EV_OVERLAY)
std::string status();
const views::Set& view_centres(); // host: itself + every partner in the Cell stage (BUG-0174)
uint32_t own_model();
int* part_states(); // BUG-0467: the collected Cell parts (S+0x34, 13 ints), null outside a live Cell game // the own cell's model instance (0: none)
std::string trace_line(); // support trace (trace.h): the own cell, food around it, what it did since the last line
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
std::string debug(const std::vector<std::string>& args);
// tests (BUG-0455): the host sends its cell `idx` with the model instance `inst` (a creation the guests lack)
void set_fake_model(uint32_t idx, uint32_t inst);

bool food_total(int& food); // the own game's food total S+0x1C (support sync-check); false outside Cell

} // namespace cellsync
