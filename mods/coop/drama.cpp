// BUG-0486 / BUG-0465: the game's Drama Manager runs random world events in each game by itself:
// a UFO visits and abducts creatures, meteors fall, the sea monster takes a swimmer in deep water.
// In a session each game ran its own: a guest saw a UFO the host never had, and the UFO took the
// guest's mirrors of the host's creatures in the guest's game only (the detector's guest_only:ufo).
// The host owns the world: on a guest its own Drama Manager does not start the events of the kinds
// in the mask (default: the UFO). The sea monster stays: it is the guest's own avatar's deep-water
// death, which keeps a swimmer from leaving the island (docs/re/creature.md#creature-drama-events).
// `coop.fix creature.guest_events off`: a guest starts every event as before (0.7.4).
#include "drama.h"
#include "call_guard.h"
#include "lifecycle.h"

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
// BUG-1059: the last events belong to the drama manager of their world (coop.drama read them after a load)
WORLD_STATE("drama.last_events", nullptr, [] {
    for (auto& e : g_last_event)
        e = nullptr;
}, []() -> size_t {
    size_t n = 0;
    for (void* e : g_last_event)
        n += e != nullptr;
    return n;
});

static bool is_guest() { return S && S->role() == net::Role::Client && S->connected(); }

// BUG-0736 (class BUG-0418): a guest's Summon Flock (ability 0x33 -> DramaManager_StartKind(8), cSummonCreatureEvent
// start 0xD50EA0: a herd of the event's species 70 m from the avatar, its members into the local pack, then made to
// follow the avatar) was made in the guest's own game only: creatures the host's world never had (the host never saw
// them; on the guest they were AI-less creatures of its own game). Now the guest's game does not start the event; it
// asks the host (player_rules K_FLOCK), whose game starts the same event with the guest's puppet as the avatar
// (cGameNounManager +0x54, as the AI swap): the flock comes to the guest's creature and joins the party's pack (the
// host's: what the guests show as NF_PACK allies). `coop.fix creature.flock_relay off`: the guest's own event (0.7.10).
bool g_flock_relay = true;
COOP_FIX("creature.flock_relay", "BUG-0736", g_flock_relay, "creature: a guest's Summon Flock is made by the host's game next to the guest's creature (both games show it)");
static unsigned g_flock_relayed, g_flock_refused;
// BUG-0800: the summon event started for a puppet was remembered for good (g_flock_event / g_flock_puppet below): the
// Drama Manager builds its events once and runs the same object again, so the host's own later Summon Flock ran its
// update with the guest's puppet as the avatar and its flock followed the guest. Now the event is forgotten when the
// game starts it again without our swap (the host's own summon) and when its update finds it inactive (+0xC 0).
// `coop.fix creature.flock_forget off`: kept for good (0.7.11).
static bool g_flock_forget = true;
COOP_FIX("creature.flock_forget", "BUG-0800", g_flock_forget, "creature: the host's own Summon Flock after a guest's follows the host (the guest's summon event is forgotten)");
static bool g_in_summon_for = false; // summon_for is starting the event for a puppet now
CALL_GUARD("drama.summon_for", g_in_summon_for); // BUG-1047
static unsigned g_flock_forgotten;

// W2 (BUG-0564): in Tribe a guest's own game started a creature attack on a village (kind 7) of its own world:
// once the whole-planet re-alignment had removed villages the host's world has not, the event's start faulted
// on a null (0xD521BA, data 0x120, lab 20261104-211318). The host's world has its own attacks (its members and
// the animals' mirrors are what the guest shows): on a guest in Tribe the kind does not start.
// `coop.fix tribe.guest_attacks off`: a guest starts it (0.7.6).
static bool g_guest_attacks_fix = true;
COOP_FIX("tribe.guest_attacks", "BUG-0564", g_guest_attacks_fix,
         "tribe: a guest's own game starts no creature attack on a village (Drama Manager event of the host's world)");

// BUG-1078: the creature attack event (kind 7) keeps its attacked tribe as a counted reference (object property
// 0x5F11333): SpawnNpcTribe hands it the Nth NPC tribe the game makes (DramaManager_NoteNpcTribe 0xD516B0). A guest's
// game makes NPC tribes for the host's (the member sync) and the re-alignment destroys the ones the host's world has
// not any more (realign.villages, tribe::remove_village after the host's game over and reload): the event's reference
// keeps the destroyed tribe object, without its hut. On a guest the event is held back (tribe.guest_attacks); once
// the game is alone (the guest left, or the host did) it starts, and its start (CreatureAttackEvent_Start 0xD51BE0)
// reads the hut's combatant without a check: 0xD521BA, data 0x120. Before a creature attack starts, an attacked
// tribe whose place the start reads (its second tool, else its hut) is gone is forgotten (the game's own setter with
// null): the start takes the tribe of its other branch (the tribe list at its picked index) or none.
// `coop.fix tribe.attack_target off`: the start reads the stale tribe (0.7.13).
static bool g_attack_target_fix = true;
COOP_FIX("tribe.attack_target", "BUG-1078", g_attack_target_fix,
         "tribe: a creature attack whose attacked village is gone (removed by the re-alignment) forgets it before it starts");
static uintptr_t a_EvGetObj, a_EvSetObj;
static unsigned g_targets_dropped;
constexpr uint32_t kAttackTribeKey = 0x5F11333, kTribeType = 0x4F396A66;

static void* attack_tribe(void* ev)
{
    return ev && a_EvGetObj ? skg::thiscall<void*>(a_EvGetObj, ev, kAttackTribeKey, kTribeType) : nullptr;
}

// what CreatureAttackEvent_Start reads of the attacked tribe: tools (vt 0xBC) [1] when it has more than one, else the
// hut (vt 0xAC); null: the start would fault
static void* attack_place(void* tribe)
{
    if (!tribe || !skg::readable(tribe, 0x370))
        return nullptr;
    void* hut = skg::vcall<void*>(tribe, 0xAC / 4);
    void** tools = skg::vcall<void**>(tribe, 0xBC / 4);
    if (tools && skg::readable(tools, 8) && (void**)tools[1] - (void**)tools[0] > 1)
        return ((void**)tools[0])[1];
    return hut;
}

static void drop_stale_attack_tribe(void* ev, const char* why)
{
    void* t = attack_tribe(ev);
    if (!t || attack_place(t) || !a_EvSetObj)
        return;
    bool alive = game::object_alive(t);
    skg::thiscall<void>(a_EvSetObj, ev, kAttackTribeKey, (void*)nullptr);
    g_targets_dropped++;
    H->log(M, SK_INFO, "drama: the creature attack's village %p is gone (no hut, %s): forgotten before %s (tribe.attack_target, BUG-1078)",
           t, alive ? "listed alive" : "destroyed", why);
}

static void* g_flock_event;  // the summon event started for a puppet (host)
static void* g_flock_puppet; // that puppet
static void forget_flock(const char* why)
{
    g_flock_event = nullptr;
    g_flock_puppet = nullptr;
    g_flock_forgotten++;
    H->log(M, SK_INFO, "drama: the summon event of a partner's creature is over: forgotten (%s, BUG-0800)", why);
}

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
    if (g_flock_relay && kind == 8 && is_guest() && game::in_creature_stage()) {
        g_flock_refused++; // BUG-0736: the host makes it (player_rules sent the ability)
        return;
    }
    if (((g_guest_events_fix && kind < 9 && (g_mask >> kind & 1)) || tribe_attack) && is_guest()) {
        if (g_refused[kind]++ < 5)
            H->log(M, SK_INFO, "drama: this game would start its own %s event: not on a guest, the host's world has "
                               "its own (creature.guest_events)", kKind[kind]);
        return; // stays inactive: the manager tries again after the event's next wait
    }
    if (kind == 8 && g_flock_forget && !g_in_summon_for && ev == g_flock_event) { // BUG-0800: the host's own summon
        forget_flock("the game starts it for its own avatar");
    }
    if (kind == 7 && g_attack_target_fix && game::mode_id() == modes::kTribe)
        drop_stale_attack_tribe(ev, "its start"); // BUG-1078
    if (kind < 9) {
        g_started[kind]++;
        H->log(M, SK_INFO, "drama: %s event starts (%s)", kKind[kind], is_guest() ? "guest" : "host or alone");
    }
    o_Start(mgr, ev, arg);
}

static uintptr_t a_Get, a_StartKind;
// BUG-0736: the summon event's update (SporeCoop::SummonEvent_Update 0xD512A0, thiscall(event, u32) ret 4, slot of the
// event vtable at .rdata 0x147B3D8) posts "follow" (BT_PostStimulus(0, 4, FLT_MAX, avatar)) to each member every frame
// and releases members 50 / 100 m from the avatar (cGameNounManager +0x54). An event started for a partner's puppet
// runs its update with that puppet as the avatar, so its flock follows the partner.
using SummonUpdate_t = void(SK_THISCALL*)(void*, uint32_t);
static SummonUpdate_t o_SummonUpdate;
static unsigned g_flock_updates;
static void SK_THISCALL h_SummonUpdate(void* ev, uint32_t a)
{
    if (g_flock_forget && ev && ev == g_flock_event && !((const uint8_t*)ev)[0xC])
        forget_flock("its update found it inactive"); // BUG-0800
    void* nm = ev && ev == g_flock_event && g_flock_relay ? game::noun_manager() : nullptr;
    void* pup = g_flock_puppet;
    if (!nm || !pup || !game::is_puppet(pup) || !game::object_alive(pup)) {
        o_SummonUpdate(ev, a);
        return;
    }
    void*& av = skg::field<void*>(nm, game::kNounAvatarOff);
    void* real = av;
    // (BUG-0800: in the swap ledger, so a fault inside the update gives the avatar back, BUG-0110 / BUG-0882)
    size_t mark = game::lend_ledger_on() ? game::swap_open(game::swap_undo_u32, &av, (uintptr_t)real, "flock avatar") : 0;
    av = pup;
    o_SummonUpdate(ev, a);
    av = real;
    if (game::lend_ledger_on())
        game::swap_close(mark);
    g_flock_updates++;
}

void init(const SkHost* host, SkMod* mod, const net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    uintptr_t a = H->sym("SporeCoop::DramaManager_StartEvent");
    a_Get = H->sym("SporeCoop::DramaManager_Get");
    a_StartKind = H->sym("SporeCoop::DramaManager_StartKind");
    a_EvGetObj = H->sym("SporeCoop::DramaEvent_GetObject"); // BUG-1078
    a_EvSetObj = H->sym("SporeCoop::DramaEvent_SetObject");
    if (!a_EvGetObj || !a_EvSetObj)
        H->log(M, SK_WARN, "drama: SporeCoop::DramaEvent_GetObject / SetObject missing: a creature attack on a removed village is not held back");
    if (a)
        H->hook_create(M, (void*)a, (void*)h_Start, (void**)&o_Start);
    else // BUG-1059: the message named the wrong symbol
        H->log(M, SK_WARN, "drama: SporeCoop::DramaManager_StartEvent missing: a guest's own world events are not held back");
    if (uintptr_t u = H->sym("SporeCoop::SummonEvent_Update"))
        H->hook_create(M, (void*)u, (void*)h_SummonUpdate, (void**)&o_SummonUpdate);
    else
        H->log(M, SK_WARN, "drama: SporeCoop::SummonEvent_Update missing: a flock summoned for a partner follows the host's avatar, not the partner");
}

bool summon_for(void* puppet)
{
    void* mgr = a_Get ? skg::cdecl_call<void*>(a_Get) : nullptr;
    void* nm = game::noun_manager();
    if (!mgr || !a_StartKind || !nm || !puppet || !game::is_puppet(puppet) || is_guest())
        return false;
    void*& av = skg::field<void*>(nm, game::kNounAvatarOff);
    void* real = av;
    if (!real)
        return false;
    // (a puppet of the partner's own creature has no archetype tuning: it borrows the avatar's, as in the AI swap)
    void*& tun = skg::field<void*>(puppet, 0xE84);
    void* lent = !tun ? skg::field<void*>(real, 0xE84) : nullptr;
    if (lent)
        tun = lent;
    unsigned before = g_started[8];
    size_t mark = game::lend_ledger_on() ? game::swap_open(game::swap_undo_u32, &av, (uintptr_t)real, "flock avatar") : 0;
    av = puppet;
    {
        callguard::Hold in_summon_for(g_in_summon_for);
        skg::thiscall<void>(a_StartKind, mgr, 8, 0);
    }
    av = real;
    if (game::lend_ledger_on())
        game::swap_close(mark);
    if (lent && tun == lent)
        tun = nullptr;
    bool ok = g_started[8] != before;
    g_flock_relayed += ok;
    if (ok) {
        g_flock_event = g_last_event[8];
        g_flock_puppet = puppet;
    }
    H->log(M, SK_INFO, "drama: summon event for a partner's creature: %s", ok ? "started" : "none idle");
    return ok;
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
    if (a.size() >= 1 && a[0] == "target") {
        // BUG-1078 tests: the creature attack event's attacked tribe (object property 0x5F11333); `target <obj hex>`
        // sets it with the game's own setter, as SpawnNpcTribe does for the NPC tribe of its pick (0xD516B0)
        void* ev = g_last_event[7];
        if (!ev)
            return "no creature_attack event seen yet (coop.drama start 7 first)\n";
        if (a.size() >= 2) {
            void* t = (void*)(uintptr_t)strtoull(a[1].c_str(), nullptr, 16);
            if (!t || !skg::readable(t, 0x370) || game::cast(t, kTribeType) != t)
                return "not a tribe\n";
            if (!a_EvSetObj)
                return "no setter symbol\n";
            skg::thiscall<void>(a_EvSetObj, ev, kAttackTribeKey, t);
        }
        void* t = attack_tribe(ev);
        snprintf(b, sizeof b, "attack_target event=%p tribe=%p place=%p alive=%d fix=%d dropped=%u\n", ev, t,
                 t ? attack_place(t) : nullptr, t ? (int)game::object_alive(t) : -1, (int)g_attack_target_fix,
                 g_targets_dropped);
        return b;
    }
    if (a.size() >= 1 && a[0] == "mate") {
        // BUG-0177 (docs/re/creature.md#creature-mating-call): the mate the drama manager picked near the nest (mgr
        // +0x38 mpMate, marked +0xB58 & 0x10), and the avatar's behaviour tree node (brain +0xB4C, +0x1D8: 02D852F0
        // while it goes to mate)
        void* mgr = a_Get ? skg::cdecl_call<void*>(a_Get) : nullptr;
        if (!mgr || !skg::readable(mgr, 0x40))
            return "no drama manager\n";
        uint8_t* mate = *(uint8_t**)((uint8_t*)mgr + 0x38);
        uint8_t* av = (uint8_t*)game::avatar();
        auto node = [](uint8_t* c) -> uint32_t {
            if (!c || !skg::readable(c + 0xB4C, 4))
                return 0;
            uint8_t* br = *(uint8_t**)(c + 0xB4C);
            return br && skg::readable(br + 0x1D8, 4) ? *(uint32_t*)(br + 0x1D8) : 0;
        };
        bool ok = mate && skg::readable(mate, 0xB60);
        net::Vec3 p = ok ? game::position(mate) : net::Vec3{};
        snprintf(b, sizeof b, "mate %p flags=%X node=%08X pos=(%.1f %.1f %.1f) | avatar node=%08X eggs=%d flags=%X\n",
                 (void*)mate, ok ? *(uint32_t*)(mate + 0xB58) : 0, ok ? node(mate) : 0, p.x, p.y, p.z, node(av),
                 av && skg::readable(av + 0x1678, 4) ? *(int*)(av + 0x1678) : -1,
                 av && skg::readable(av + 0xB58, 4) ? *(uint32_t*)(av + 0xB58) : 0);
        return b;
    }
    snprintf(b, sizeof b, "drama: fix=%d mask=%X guest=%d mode=%08X flock_relay=%d relayed=%u refused=%u updates=%u "
             "flock_event=%p forgotten=%u\n",
             (int)g_guest_events_fix, g_mask, (int)is_guest(), game::mode_id(), (int)g_flock_relay, g_flock_relayed,
             g_flock_refused, g_flock_updates, g_flock_event, g_flock_forgotten);
    out += b;
    for (int k = 0; k < 9; k++) {
        const uint8_t* ev = (const uint8_t*)g_last_event[k];
        if (!ev && !g_started[k] && !g_refused[k])
            continue;
        snprintf(b, sizeof b, "  %-16s started=%u refused=%u event=%p active=%d\n", kKind[k], g_started[k], g_refused[k],
                 (const void*)ev, ev && skg::readable(ev + 0xC, 1) ? (int)ev[0xC] : -1); // BUG-1059
        out += b;
    }
    return out;
}

} // namespace drama
