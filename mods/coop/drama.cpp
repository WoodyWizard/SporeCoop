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
#include "creations.h"
#include "lifecycle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fixes.h"
#include "game.h"
#include "guest_pack.h"
#include "library.h"
#include "modes.h"
#include "sporekit_game.hpp"

namespace drama {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

static bool g_guest_events_fix = true;
COOP_FIX("creature.guest_events", "BUG-0486", g_guest_events_fix,
         "creature: a guest's own game starts no UFO visit (Drama Manager event of the host's world)");

// event kinds of cDramaManager (0xD4E020): 0 ?, 1 night predator, 2 UFO abduction, 3 meteor strike,
// 4 baby game, 5 rain storm, 6 sea monster, 7 creature attack, 8 summon creature
static const char* kKind[9] = {"kind0", "night_predator", "ufo", "meteor", "baby_game", "rain",
                               "sea_monster", "creature_attack", "summon"};
static unsigned g_mask = 1u << 2;
// BUG-9131: meteors, rain storms and the night predator ran in each game by itself, at different times (the owner saw
// meteors and the sky out of step between the players). The host's world owns them: a guest's own game starts
// none of these; the meteor strike and the rain storm the host starts are started on the guests too (MSG_DRAMA);
// the night predator is a creature the host's NPC sync already shows. Off: each game its own (0.7.8-lineages50).
static bool g_shared_fix = true;
COOP_FIX("creature.shared_events", "BUG-9131", g_shared_fix,
         "creature: meteors and rain storms follow the host's (a guest starts none of its own); no night predator of a guest's own");
// meteor strike, rain storm, and (owner, 2026-11-11: "the space ship wasn't synced") the UFO visit: on a guest the
// host's UFO comes as its own game's UFO that takes nothing (player_rules: every creature is "epic" to its pick)
static constexpr unsigned kRelay = (1u << 2) | (1u << 3) | (1u << 5);
static constexpr unsigned kHostOnly = 1u << 1;            // night predator (the host's creature is mirrored)
static bool g_from_host = false;                          // starting a kind the host sent
static unsigned g_relayed_tx[9], g_relayed_rx[9];
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

// BUG-9328: the UFO visit's ship model. The event's start (0xD4F8A0) rolls "empire ship" (prop 0x51BE5FAE) with the
// game's random numbers: an empire's ship (cEmpire +0xB8, the Grox one when it has none) or a random ship of this game's
// Sporepedia (0x102C800: an object template DB query for ships, one random result), then Simulator::SpawnUFO(7,
// political id, model key). A guest that starts the host's visit (BUG-9131) rolled and picked in its own game: another
// ship, also one of its Lineages Cache (the owner saw a Grox ship, the partner a test ship). Now the host's spawn key
// goes with the relay (MSG_DRAMA: u8 kind, then for the UFO 12 bytes of key and str8 the Lineages Cache hash of that
// creation, "" for none), and on a guest every ship the event's spawn function makes (the relayed visit's and the mirror
// of BUG-0558) takes that ship: a Lineages Cache creation by its hash (each game imports it under its own key: the
// host's 2926CA3F was the guest's 29215407, runtime 2026-11-21), any other by its key, when this game has it,
// else the game's own Grox ship (the key the empire branch gives the Grox, 0xC32810: prop 0x2AE0C7E!0x280852F1, int
// 0x4EA8A07, type 0x476A98C7, group 0x40656200): never a random ship of the guest's own. (Not the zero key: the game's
// default UFO 9D5DB21A it stands for is not in a Creature game's resources, runtime 2026-11-21: the ship had no model and
// was invisible.) `coop.fix creature.same_ship off`: the guest's own pick (0.7.14.1-lineages57-dev).
static bool g_same_ship_fix = true;
COOP_FIX("creature.same_ship", "BUG-9328", g_same_ship_fix,
         "creature: the visiting UFO is the host's ship model on every guest (the Grox ship when a guest lacks it)");
static bool g_in_event_spawn = false;  // inside UFOAbductionEvent_SpawnUFO now
static uint32_t g_spawn_key[3];        // the model key of the last ship that function made here (after our choice)
static bool g_spawn_key_new = false;   // set by a spawn during the host's event start (sent with the relay)
static uint32_t g_host_key[3];         // guest: the host's last visit ship key
static bool g_host_key_known = false;
static std::string g_host_hash;        // guest: the host's ship's Lineages Cache hash ("" when it is not a cached one)
static unsigned g_ship_same, g_ship_default, g_ship_spawns;
constexpr uint32_t kUfoModelType = 0x476A98C7, kUfoGroup = 0x40656200;

// the Grox ship's model key as the game reads it for the Grox empire (0xC32810); zeros when the prop is missing
static void grox_ship_key(uint32_t out[3])
{
    out[0] = out[1] = out[2] = 0;
    static uintptr_t get_pm = H->sym("App::IPropManager::Get"), get_int = H->sym("App::Property::GetInt32");
    void* pm = get_pm ? skg::cdecl_call<void*>(get_pm) : nullptr;
    void* list = nullptr;
    if (!pm || !get_int || !skg::vcall<bool>(pm, 0x2C / 4, 0x280852F1u, 0x02AE0C7Eu, &list) || !list)
        return;
    int32_t inst = 0;
    if (skg::cdecl_call<bool>(get_int, list, 0x04EA8A07u, &inst) && inst)
        out[0] = (uint32_t)inst, out[1] = kUfoModelType, out[2] = kUfoGroup;
    skg::vcall<int>(list, 0x04 / 4);
}

using EventSpawn_t = void*(SK_STDCALL*)(const void*, float, bool);
static EventSpawn_t o_EventSpawn;
static void* SK_STDCALL h_EventSpawn(const void* pos, float height, bool empire)
{
    const bool was = g_in_event_spawn;
    g_in_event_spawn = true;
    void* u = o_EventSpawn(pos, height, empire);
    g_in_event_spawn = was;
    return u;
}

using SpawnUfo_t = void*(SK_CDECL*)(int, const int*, const uint32_t*);
static SpawnUfo_t o_SpawnUfo;
static void* SK_CDECL h_SpawnUfo(int type, const int* political, const uint32_t* model)
{
    if (!g_in_event_spawn || type != 7)
        return o_SpawnUfo(type, political, model);
    uint32_t k[3] = {0, 0, 0};
    if (model && skg::readable(model, 12))
        k[0] = model[0], k[1] = model[1], k[2] = model[2];
    if (g_same_ship_fix && is_guest() && game::mode_id() == modes::kCreature) {
        creations::Key hk;
        hk.instance = g_host_key[0];
        hk.type = g_host_key[1] ? g_host_key[1] : kUfoModelType;
        hk.group = g_host_key[2];
        bool have = false;
        if (!g_host_hash.empty()) { // a cached creation: this game's own key of it (never the host's number)
            dbpf::Key mine{};
            if (library::key_of(g_host_hash, mine)) {
                hk.instance = mine.instance, hk.group = mine.group;
                have = creations::key_known(hk);
            }
        } else
            have = g_host_key_known && (g_host_key[0] || g_host_key[2]) && creations::key_known(hk);
        const uint32_t own[3] = {k[0], k[1], k[2]};
        if (have) {
            k[0] = hk.instance, k[1] = hk.type, k[2] = hk.group;
            g_ship_same++;
        } else {
            grox_ship_key(k); // the game's own (Maxis) ship
            g_ship_default++;
        }
        H->log(M, SK_INFO, "drama: the visit's ship here is %s %08X/%08X/%08X (this game picked %08X/%08X/%08X; "
               "creature.same_ship, BUG-9328)", have ? "the host's" : g_host_key_known ? "the Grox ship (the host's "
               "is not in this game)" : "the Grox ship (no host ship known yet)", k[0], k[1], k[2], own[0], own[1], own[2]);
    }
    memcpy(g_spawn_key, k, sizeof k);
    g_spawn_key_new = true;
    g_ship_spawns++;
    return o_SpawnUfo(type, political, k); // (a copy: the empire branch passes its empire's own key field)
}

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
// (Its switch tribe.guest_attacks was retired 2026-11-12, roadmap 0e: stable since 0.7.7.)

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
    const bool relay = g_shared_fix && kind < 9 && (kRelay >> kind & 1) && S && S->role() == net::Role::Host &&
                       S->connected() && game::mode_id() == modes::kCreature; // BUG-9131: the guests start it too
    // BUG-9328: the UFO's relay goes after the host's own start, with the ship key the start spawned
    const bool relay_after = relay && kind == 2 && g_same_ship_fix;
    if (relay && !relay_after) {
        net::Writer w;
        w.u8((uint8_t)kind);
        S->broadcast_reliable(net::MSG_DRAMA, w);
        g_relayed_tx[kind]++;
    }
    if (g_shared_fix && kind < 9 && ((kRelay | kHostOnly) >> kind & 1) && is_guest() && !g_from_host &&
        game::mode_id() == modes::kCreature) {
        if (g_refused[kind]++ < 5)
            H->log(M, SK_INFO, "drama: %s is the host's world event: not started on its own here (creature.shared_events)", kKind[kind]);
        return;
    }
    bool tribe_attack = kind == 7 && game::mode_id() == modes::kTribe;
    // BUG-9129: a guest's summon is the host's (its Summon Flock goes to the host as a request)
    tribe_attack = tribe_attack || (kind == 8 && guest_pack::enabled());
    if (g_flock_relay && kind == 8 && is_guest() && game::in_creature_stage() && !guest_pack::enabled()) {
        g_flock_refused++; // BUG-0736: the host makes it (player_rules sent the ability); Lineages' guest pack wins
        return;
    }
    if (((g_guest_events_fix && kind < 9 && (g_mask >> kind & 1)) || tribe_attack) && is_guest() && !g_from_host) {
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
    g_spawn_key_new = false;
    o_Start(mgr, ev, arg);
    if (relay_after) {
        net::Writer w;
        w.u8((uint8_t)kind);
        if (g_spawn_key_new) { // (none when the start made no ship: the guests take the Grox one)
            w.u32(g_spawn_key[0]), w.u32(g_spawn_key[1]), w.u32(g_spawn_key[2]);
            w.str8(library::hash_of(g_spawn_key[0], g_spawn_key[2]));
        }
        S->broadcast_reliable(net::MSG_DRAMA, w);
        g_relayed_tx[kind]++;
        H->log(M, SK_INFO, "drama: the UFO visit goes to the guests with its ship %08X/%08X/%08X (BUG-9328)",
               g_spawn_key_new ? g_spawn_key[0] : 0, g_spawn_key_new ? g_spawn_key[1] : 0, g_spawn_key_new ? g_spawn_key[2] : 0);
    }
}

static uintptr_t a_Get, a_StartKind;
// BUG-0736: the summon event's update (SporeCoop::SummonEvent_Update 0xD512A0, thiscall(event, u32) ret 4, slot of the
// event vtable at .rdata 0x147B3D8) posts "follow" (BT_PostStimulus(0, 4, FLT_MAX, avatar)) to each member every frame
// and releases members 50 / 100 m from the avatar (cGameNounManager +0x54). An event started for a partner's puppet
// runs its update with that puppet as the avatar, so its flock follows the partner.
using SummonUpdate_t = void(SK_THISCALL*)(void*, uint32_t);
static SummonUpdate_t o_SummonUpdate;
static unsigned g_flock_updates;
static void* g_seen_summon; // the last summon event the game updated (tests: `coop.drama summon`)
static uint32_t g_stale_tidied;
static double g_summon_seen_at = -1e9;
static bool g_flock_release = true; // (BUG-9245, its switch below)
static void* flock_herd(void* ev);
static std::pair<int, int> tidy_summon_list(void* herd);
using SummonEnd_t = void(SK_THISCALL*)(void*);
static SummonEnd_t o_SummonEnd;
static void SK_THISCALL h_SummonEnd(void* ev); // decision 5CD (below)
static uint32_t g_party_timeouts;
static void SK_THISCALL h_SummonUpdate(void* ev, uint32_t a)
{
    g_seen_summon = ev;
    if (ev && skg::readable(ev, 0x10) && ((const uint8_t*)ev)[0xC])
        g_summon_seen_at = H->time_seconds();
    // BUG-9245: a summon past its time (one a save brought back, one whose flock went to a partner) drops the creatures
    // that left its herd, so it can end; while its time runs, the game's start may still be placing them
    if (g_flock_release && ev && skg::readable(ev, 0x64) && ((const uint8_t*)ev)[0xC] && skg::field<float>(ev, 0x2C) < 0.f)
        if (auto n = tidy_summon_list(flock_herd(ev)); n.second) {
            g_stale_tidied++;
            H->log(M, SK_INFO, "drama: a summon event %.0f s past its time: %d creature(s) that left its herd out of its list, "
                   "%d left (BUG-9245)", -skg::field<float>(ev, 0x2C), n.second, n.first);
        }
    if (g_flock_forget && ev && ev == g_flock_event && !((const uint8_t*)ev)[0xC])
        forget_flock("its update found it inactive"); // BUG-0800
    // decision 5CD: a partner's summon runs inside that partner's own pack (its leader, its list, its count). 5 s past its
    // time it ends the way the manager ends an event (+0xC = 0, then its end, inside the party: the flock leaves the
    // partner's pack): its own "done?" waits for the flock to be 100 m from its leader, and a partner's flock stays at
    // its side (runtime 2026-11-17: 32 s past its time, still active, every summon of the world refused meanwhile)
    if (ev && ev == g_flock_event && guest_pack::party_mode() && skg::readable(ev, 0x64) && ((uint8_t*)ev)[0xC] &&
        skg::field<float>(ev, 0x2C) < -5.f) {
        ((uint8_t*)ev)[0xC] = 0;
        h_SummonEnd(ev);
        g_party_timeouts++;
        return;
    }
    // any other summon a minute past its time (one a save brought back with its flock still in its herd, one the mod lost
    // track of) ends the same way: else no Summon Flock of anyone starts again in that world
    if (g_flock_release && ev && ev != g_flock_event && skg::readable(ev, 0x64) && ((uint8_t*)ev)[0xC] &&
        skg::field<float>(ev, 0x2C) < -60.f) {
        H->log(M, SK_INFO, "drama: a summon event %.0f s past its time ends now (the game's end, BUG-9245)", -skg::field<float>(ev, 0x2C));
        ((uint8_t*)ev)[0xC] = 0;
        o_SummonEnd(ev);
        g_party_timeouts++;
        return;
    }
    if (ev && ev == g_flock_event && guest_pack::party_mode())
        if (const size_t tok = guest_pack::party_enter_puppet(g_flock_puppet)) {
            o_SummonUpdate(ev, a);
            guest_pack::party_leave_token(tok);
            g_flock_updates++;
            return;
        }
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

// decision 5CD: the summon's end (vt+0x34, SporeCoop::SummonEvent_End) takes each listed creature out of the pack
// (cPosseSimulator_RemoveMember): a partner's flock leaves that partner's pack, inside its party
static uint32_t g_party_ends;
static void SK_THISCALL h_SummonEnd(void* ev)
{
    if (ev && ev == g_flock_event && guest_pack::party_mode())
        if (const size_t tok = guest_pack::party_enter_puppet(g_flock_puppet)) {
            o_SummonEnd(ev);
            guest_pack::party_leave_token(tok);
            g_party_ends++;
            H->log(M, SK_INFO, "drama: a partner's summon ended inside its own pack (decision 5CD)");
            return;
        }
    o_SummonEnd(ev);
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    if (uintptr_t u = H->sym("SporeCoop::SummonEvent_End"))
        H->hook_create(M, (void*)u, (void*)h_SummonEnd, (void**)&o_SummonEnd);
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
    // BUG-9328: the visit's ship model (the event's spawn function and the spawn inside it)
    uintptr_t es = H->sym("SporeCoop::UFOAbductionEvent_SpawnUFO"), su = H->sym("Simulator::SpawnUFO");
    if (es && su) {
        H->hook_create(M, (void*)es, (void*)h_EventSpawn, (void**)&o_EventSpawn);
        H->hook_create(M, (void*)su, (void*)h_SpawnUfo, (void**)&o_SpawnUfo);
    } else
        H->log(M, SK_WARN, "drama: UFOAbductionEvent_SpawnUFO / Simulator::SpawnUFO missing: a guest's visiting ship is its own pick");
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

// BUG-9245: the summon event's "done?" (vt+0x3C, Steam 0xC3D2F0) is true only when its herd's list (prop 0x609F2F8 ->
// vt+0xC(0x52AA6117), +0x40..+0x44) is empty or every creature in it has +0x135 == 0; only its update's end clears that
// flag, for a member over 100 m from the leader. The duration alone ends nothing (the manager tick only counts it down).
// A partner's flock is handed to that partner's pack and stays at its side, so the event never ended: no later Summon
// Flock started, the host's included ("none idle"), and a save kept it so. On: the list drops the creatures that left
// the summon's herd (tidy_summon_list) at the hand-over and on every update of a summon past its time; an empty list
// ends the event at the manager's next tick, and its end (vt+0x34) finds nobody to send back: the end returns every
// listed creature to the game's pool (runtime 2026-11-16: at (0 0 0), herd 0; the pool later gave the objects out as
// other creatures, BUG-9247). +0x135 stays set: a creature with +0x135 == 0 is no longer updated by the game (runtime
// 2026-11-16: the first version of this fix cleared it and the flock stood frozen). Off: 63-dev (the event runs on).
COOP_FIX("creature.flock_release", "BUG-9245", g_flock_release,
         "creature: a partner's Summon Flock event ends once its flock is the partner's (later summons start, the flock stays)");

static void* flock_herd(void* ev)
{
    void** b = skg::field<void**>(ev, 0x5C);
    void** e = skg::field<void**>(ev, 0x60);
    if (!b || e <= b || e - b > 64 || !skg::readable(b, (e - b) * sizeof(void*)))
        return nullptr;
    for (void** p = b; p + 1 < e + 1 && p < e; p += 2) // (key, object) pairs
        if ((uint32_t)(uintptr_t)p[0] == 0x609F2F8u && p[1] && skg::readable(p[1], 4))
            return skg::vcall<void*>(p[1], 0xC / 4, 0x52AA6117u);
    return nullptr;
}

// The summon herd's list keeps only the creatures still in that herd: a partner's flock is in the partner's pack's herd
// by now (BUG-9233), an entry may be another object at a freed address, and a save keeps the list as it was (BUG-9245:
// a world saved with a partner's flock out held its summon event active after the load, 9 min past its time, every
// Summon Flock refused). The game never takes a creature out of this list when it changes herd (Creature_SetHerd).
// An empty list makes "done?" true: the game's own end runs at the manager's next tick and finds nobody to take back
// (it returns every listed creature to the game's pool, BUG-9247). Creatures still in the herd (the host's own flock)
// stay listed: the game goes on with them as before. Returns {kept, taken out}.
static std::pair<int, int> tidy_summon_list(void* herd)
{
    if (!herd || !skg::readable(herd, 0x48))
        return {0, 0};
    void** b = skg::field<void**>(herd, 0x40);
    void** e = skg::field<void**>(herd, 0x44);
    if (!b || e <= b || e - b > 64 || !skg::readable(b, (e - b) * sizeof(void*)))
        return {0, 0};
    void** w = b;
    int out = 0;
    for (void** p = b; p < e; ++p) {
        void* c = *p;
        if (c && skg::readable(c, 0x1680) && game::object_alive(c) && game::herd_of(c) == herd)
            *w++ = c;
        else
            out++;
    }
    skg::field<void**>(herd, 0x44) = w; // (the vector's end moves back; its buffer stays the herd's)
    return {(int)(w - b), out};
}

bool end_flock_event()
{
    void* ev = g_flock_event;
    if (!ev || !skg::readable(ev, 0x64) || !((const uint8_t*)ev)[0xC])
        return false;
    skg::field<uint32_t>(ev, 0x2C) = 0; // the duration left (docs/re/creature.md#creature-c2-audit-0713)
    std::pair<int, int> n{0, 0};
    if (g_flock_release)
        n = tidy_summon_list(flock_herd(ev));
    H->log(M, SK_INFO, "drama: the partner's summon event ends now (its flock leaves the host's pack, BUG-9233; %d out of its "
           "list, %d left in it, BUG-9245)", n.second, n.first);
    return true;
}

bool summon_running() { return H && H->time_seconds() - g_summon_seen_at < 2.0; }
void* summon_puppet() // decision 5FA: the partner's puppet whose summon runs now (null: none, or the host's own)
{
    return summon_running() && g_flock_event && g_seen_summon == g_flock_event && skg::readable(g_flock_event, 0x10) &&
                   ((const uint8_t*)g_flock_event)[0xC]
               ? g_flock_puppet
               : nullptr;
}

bool host_ufo_here() { return g_shared_fix && is_guest(); }

void on_message(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8();
    if (!r.ok() || from != 0 || !is_guest() || kind > 8 || !(kRelay >> kind & 1) || !g_shared_fix ||
        game::mode_id() != modes::kCreature)
        return;
    if (kind == 2) { // BUG-9328: this visit's ship only (an older host sends none)
        g_host_key_known = false;
        g_host_hash.clear();
    }
    if (kind == 2 && r.left() >= 12) {
        g_host_key[0] = r.u32(), g_host_key[1] = r.u32(), g_host_key[2] = r.u32();
        g_host_key_known = r.ok();
        g_host_hash = r.ok() && r.left() ? r.str8() : std::string();
        if (!r.ok() || (!g_host_hash.empty() && g_host_hash.size() != 64))
            g_host_hash.clear();
    }
    void* mgr = a_Get ? skg::cdecl_call<void*>(a_Get) : nullptr;
    if (!mgr || !a_StartKind)
        return;
    g_relayed_rx[kind]++;
    g_from_host = true;
    skg::thiscall<void>(a_StartKind, mgr, (int)kind, 0);
    g_from_host = false;
    H->log(M, SK_INFO, "drama: the host's %s starts here too", kKind[kind]);
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
    if (a.size() >= 1 && (a[0] == "summon" || a[0] == "endsummon")) {
        // tests (BUG-9245): the last summon event the game updated, its list; endsummon: the list tidied now (the
        // game's own end follows at the manager's next tick when nobody is left)
        void* ev = g_seen_summon;
        if (!ev || !skg::readable(ev, 0x64))
            return "no summon event updated yet\n";
        void* herd = flock_herd(ev);
        if (a[0] == "endsummon") {
            auto n = tidy_summon_list(herd);
            snprintf(b, sizeof b, "tidied: %d left, %d out of the list\n", n.first, n.second);
            out += b;
        }
        snprintf(b, sizeof b, "summon event %p active=%d duration=%.1f herd=%p ours=%d\n", ev, (int)((uint8_t*)ev)[0xC],
                 skg::field<float>(ev, 0x2C), herd, (int)(ev == g_flock_event));
        out += b;
        void** lb = herd && skg::readable(herd, 0x48) ? skg::field<void**>(herd, 0x40) : nullptr;
        void** le = lb ? skg::field<void**>(herd, 0x44) : nullptr;
        for (void** p = lb; p && le > lb && le - lb <= 64 && p < le; ++p) {
            void* c = *p;
            const bool ok = c && skg::readable(c, 0x1680);
            snprintf(b, sizeof b, "  %p alive=%d herd=%p in_herd=%d b135=%d species=%08X hp=%.1f\n", c,
                     ok ? (int)game::object_alive(c) : -1, ok ? game::herd_of(c) : nullptr, ok && game::herd_of(c) == herd,
                     ok ? (int)skg::field<uint8_t>(c, 0x135) : -1, ok ? game::species_instance(c) : 0u,
                     ok ? game::health(c) : -1.f);
            out += b;
        }
        return out;
    }
    if (a.size() >= 1 && a[0] == "ship") {
        // BUG-9328 (tests): the ship keys of the visit, and the model key of every live UFO (cGameDataUFO +0x780)
        snprintf(b, sizeof b, "ship fix=%d last_spawn=%08X/%08X/%08X host=%08X/%08X/%08X known=%d same=%u default=%u spawns=%u\n",
                 (int)g_same_ship_fix, g_spawn_key[0], g_spawn_key[1], g_spawn_key[2], g_host_key[0], g_host_key[1],
                 g_host_key[2], (int)g_host_key_known, g_ship_same, g_ship_default, g_ship_spawns);
        out += b;
        uint32_t g[3];
        grox_ship_key(g);
        creations::Key gk;
        gk.instance = g[0], gk.type = g[1], gk.group = g[2];
        out += "  host hash " + (g_host_hash.empty() ? std::string("none") : g_host_hash) + "\n";
        snprintf(b, sizeof b, "  grox ship %08X/%08X/%08X in this game=%d\n", g[0], g[1], g[2], (int)(g[0] && creations::key_known(gk)));
        out += b;
        game::for_each_object(
            [](void* o, uint32_t noun, void* p) {
                void* u = noun == 0x018EBADCu && game::object_alive(o) ? game::cast(o, 0xB033B403u) : nullptr;
                if (u && skg::readable((uint8_t*)u + 0x780, 12)) {
                    const uint32_t* k = (const uint32_t*)((uint8_t*)u + 0x780);
                    net::Vec3 v{};
                    game::object_position(o, v);
                    char t[128];
                    snprintf(t, sizeof t, "  ufo %p model=%08X/%08X/%08X pos=(%.1f %.1f %.1f)\n", o, k[0], k[1], k[2], v.x, v.y, v.z);
                    *(std::string*)p += t;
                }
                return true;
            },
            &out);
        return out;
    }
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
        snprintf(b, sizeof b, "  %-16s started=%u refused=%u relayed_tx=%u relayed_rx=%u event=%p active=%d\n", kKind[k],
                 g_started[k], g_refused[k], g_relayed_tx[k], g_relayed_rx[k], (const void*)ev,
                 ev && skg::readable(ev + 0xC, 1) ? (int)ev[0xC] : -1); // BUG-1059
        out += b;
    }
    return out;
}

} // namespace drama
