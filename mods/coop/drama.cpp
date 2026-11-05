// BUG-0486 / BUG-0465: the game's Drama Manager runs random world events in each game by itself:
// a UFO visits and abducts creatures, meteors fall, the sea monster takes a swimmer in deep water.
// In a session each game ran its own: a guest saw a UFO the host never had, and the UFO took the
// guest's mirrors of the host's creatures in the guest's game only (the detector's guest_only:ufo).
// The host owns the world: on a guest its own Drama Manager does not start the events of the kinds
// in the mask (default: the UFO). The sea monster stays: it is the guest's own avatar's deep-water
// death, which keeps a swimmer from leaving the island (docs/re/creature.md#creature-drama-events).
// `coop.fix creature.guest_events off`: a guest starts every event as before (0.7.4).
#include "drama.h"

#include <cstdio>
#include <cstdlib>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"

namespace drama {

static const SkHost* H;
static SkMod* M;
static const net::Session* S;

static bool g_guest_events_fix = true;
COOP_FIX("creature.guest_events", "BUG-0486", g_guest_events_fix,
         "creature: a guest's own game starts no UFO visit (Drama Manager event of the host's world)");

// event kinds of cDramaManager (0xD4E020): 0 ?, 1 night predator, 2 UFO abduction, 3 meteor strike,
// 4 baby game, 5 rain storm, 6 sea monster, 7 creature attack, 8 summon creature
static const char* kKind[9] = {"kind0", "night_predator", "ufo", "meteor", "baby_game", "rain",
                               "sea_monster", "creature_attack", "summon"};
static unsigned g_mask = 1u << 2;
static unsigned g_started[9], g_refused[9];
static void* g_last_event[9];

static bool is_guest() { return S && S->role() == net::Role::Client && S->connected(); }

// W2 (BUG-0564): in Tribe a guest's own game started a creature attack on a village (kind 7) of its own world:
// once the whole-planet re-alignment had removed villages the host's world has not, the event's start faulted
// on a null (0xD521BA, data 0x120, lab 20261104-211318). The host's world has its own attacks (its members and
// the animals' mirrors are what the guest shows): on a guest in Tribe the kind does not start.
// `coop.fix tribe.guest_attacks off`: a guest starts it (0.7.6).
static bool g_guest_attacks_fix = true;
COOP_FIX("tribe.guest_attacks", "BUG-0564", g_guest_attacks_fix,
         "tribe: a guest's own game starts no creature attack on a village (Drama Manager event of the host's world)");

// SporeCoop::DramaManager_StartEvent: thiscall(cDramaManager*, event*, int) ret 8: marks the event
// active (+0xC = 1), counts down its uses (+0x18) and calls its start (vt+0x30); the kind is +0x14
using Start_t = void(SK_THISCALL*)(void*, void*, int);
static Start_t o_Start;
static void SK_THISCALL h_Start(void* mgr, void* ev, int arg)
{
    unsigned kind = ev ? *(const unsigned*)((const uint8_t*)ev + 0x14) : 99;
    if (kind < 9)
        g_last_event[kind] = ev;
    bool tribe_attack = g_guest_attacks_fix && kind == 7 && game::mode_id() == modes::kTribe;
    if (((g_guest_events_fix && kind < 9 && (g_mask >> kind & 1)) || tribe_attack) && is_guest()) {
        if (g_refused[kind]++ < 5)
            H->log(M, SK_INFO, "drama: this game would start its own %s event: not on a guest, the host's world has "
                               "its own (creature.guest_events)", kKind[kind]);
        return; // stays inactive: the manager tries again after the event's next wait
    }
    if (kind < 9) {
        g_started[kind]++;
        H->log(M, SK_INFO, "drama: %s event starts (%s)", kKind[kind], is_guest() ? "guest" : "host or alone");
    }
    o_Start(mgr, ev, arg);
}

static uintptr_t a_Get, a_StartKind;

void init(const SkHost* host, SkMod* mod, const net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    uintptr_t a = H->sym("SporeCoop::DramaManager_StartEvent");
    a_Get = H->sym("SporeCoop::DramaManager_Get");
    a_StartKind = H->sym("SporeCoop::DramaManager_StartKind");
    if (a)
        H->hook_create(M, (void*)a, (void*)h_Start, (void**)&o_Start);
    else
        H->log(M, SK_WARN, "drama: SporeCoop::DramaManager_StartEvent missing: a guest's own world events are not held back");
}

std::string debug(const std::vector<std::string>& a)
{
    char b[256];
    if (a.size() >= 2 && a[0] == "mask") {
        g_mask = (unsigned)strtoul(a[1].c_str(), nullptr, 16);
    } else if (a.size() >= 2 && a[0] == "start") {
        // the game's own "start every idle event of this kind" (0xD4D740, as the stage's start does)
        unsigned kind = (unsigned)atoi(a[1].c_str());
        void* mgr = a_Get ? skg::cdecl_call<void*>(a_Get) : nullptr;
        if (!mgr || !a_StartKind || kind > 8)
            return "no drama manager / symbol / kind\n";
        skg::thiscall<void>(a_StartKind, mgr, (int)kind, 0);
    }
    std::string out;
    snprintf(b, sizeof b, "drama: fix=%d mask=%X guest=%d mode=%08X\n", (int)g_guest_events_fix, g_mask, (int)is_guest(),
             game::mode_id());
    out += b;
    for (int k = 0; k < 9; k++) {
        const uint8_t* ev = (const uint8_t*)g_last_event[k];
        if (!ev && !g_started[k] && !g_refused[k])
            continue;
        snprintf(b, sizeof b, "  %-16s started=%u refused=%u event=%p active=%d\n", kKind[k], g_started[k], g_refused[k],
                 (const void*)ev, ev ? (int)ev[0xC] : -1);
        out += b;
    }
    return out;
}

} // namespace drama
