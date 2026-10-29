#include "spacecoop.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <unordered_map>

#include "game.h"
#include "modes.h"
#include "net/utf8.h"
#include "net/validate.h"
#include "space_ext.h"
#include "space_logic.h"
#include "sporekit_game.hpp"
#include "fixes.h"

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;

namespace spacecoop {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

// cGameDataUFO (docs/re/space.md): combatant +0x508, max hp +0x534, state +0x53C, hp +0x540,
// invincible +0x588, death cinematic handle +0x7F0
constexpr uintptr_t kCombat = 0x508, kMaxHp = 0x534, kState = 0x53C, kHp = 0x540, kInvincible = 0x588,
                    kCinematic = 0x7F0, kPos = 0x38;
constexpr double kDownTime = 20.0, kRepairHold = 2.0;
static float g_repair_dist = 60.f; // coop.space repairdist <m> (tests: 0 = nobody can repair)

enum Kind : uint8_t { K_DOWN = 1, K_RAID = 2, K_MISSION = 3, K_TERRA = 4, K_GIFT = 5, K_GIFT_BACK = 6 };

static uintptr_t a_PlayerUFOGet, a_Context, a_ActivePlanet, a_StarMgr, a_GetStarRecord, a_ModeSpace, a_EnterSystem,
    a_LeavePlanet, a_TravelSystem, a_PlanetCleanup, a_PlanetCleanupMgr, a_AddHealth, a_SetHealth, a_Destroyed,
    a_SetState, a_TerraA, a_TerraW, a_TerraT, a_TerraMgr, a_SpaceGame, a_AddItemByKey, a_Trading, a_CreateTrading,
    a_AddMoney, a_PlayerEmpire, a_CinematicMgr, a_MessageMgr;

struct Key {
    uint32_t v[3];
};

// ---- state ----
static double g_down_until, g_repair_t, g_last_down;
static bool g_down, g_down_fresh; // fresh: disabled since the last down_tick (BUG-0147)
static bool g_helper_fix = true;   // coop.space helperfix 0: any partner here rescues, also a disabled or frozen one (BUG-0131)
static bool g_repair_fix = true;   // coop.space repairfix 0: the old head start of up to 0.5 s (tests)
COOP_FIX("space.helper", "BUG-0131", g_helper_fix, "space: a disabled or frozen partner does not count as a rescuer");
COOP_FIX("space.repair", "BUG-0147", g_repair_fix, "space: a new disable starts its repair from 0");
// BUG-0228, BUG-0229: the ship the disabled state was set on, with its object serial and the Space
// world it lived in (spaceext::world_epoch). The invincible byte is cleared on that ship only, at
// once in Space or owed until the first Space frame back in the same world (an editor, an
// adventure, a session end outside Space); a trip to an editor pauses the 20 s, another world
// (a load, the galaxy menu) ends the state without a death.
struct DownShip {
    void* u = nullptr;
    uint32_t serial = 0, epoch = 0;
};
static DownShip g_down_ship;
static bool g_invincible_owed;
static bool g_down_clear_fix = true; // off: clear only when in Space at the moment the state ends (0.7.3.0)
static bool g_down_world_fix = true; // off: the 20 s run on outside Space, the first Space frame decides
COOP_FIX("space.down_clear", "BUG-0228", g_down_clear_fix,
         "space: the disabled ship's invincibility is cleared on the first Space frame back when the state ended outside Space");
COOP_FIX("space.down_world", "BUG-0229", g_down_world_fix,
         "space: a disabled state pauses in an editor / adventure and ends without a death in another Space world");
static uint32_t g_down_world_ends, g_down_owed_cleared, g_down_owed_dropped;
static std::unordered_map<uint8_t, double> g_partner_down; // partner -> until
static uint32_t g_rescues, g_lost, g_raids_seen, g_raids_told, g_raid_bonus, g_missions_sent, g_tools_got,
    g_terra_sent, g_terra_got, g_gifts_sent, g_gifts_got, g_jumps, g_respawns_forced;
static uint32_t g_bad_origin; // MSG_SPACE dropped: a header that does not fit its sender
static std::unordered_map<uint8_t, uint32_t> g_last_star; // partner -> last known StarID
static bool g_applying;
static float g_terra_acc[3];
static uint32_t g_terra_planet;
static double g_terra_last;
static std::unordered_map<void*, double> g_raid_since; // raid mission -> start
static std::string g_last_raid_text;

static bool connected() { return C.sess && C.sess->connected(); }

// any cinematic of the game running (cCinematicManager +0x2C: 0 none, 2 playing; docs/re/space.md 7.2):
// the death caption, but also the respawn caption "We've built you a new ship..." (CVG2SPG_short)
static bool cinematic_playing()
{
    void* cm = a_CinematicMgr ? cdecl_call<void*>(a_CinematicMgr) : nullptr;
    return cm && F<uint32_t>(cm, 0x2C) != 0;
}
static bool in_space() { return g_ok && !spaceext::away() && game::mode_id() == modes::kSpace; }

static void* my_ufo()
{
    void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    return mgr ? F<void*>(mgr, 0x40) : nullptr;
}

static net::Vec3 ufo_pos(void* u)
{
    auto& p = F<float[3]>(u, kPos);
    return {p[0], p[1], p[2]};
}

// The distance that counts for a repair. On a planet the ships fly on spheres around the planet's
// centre (the origin of the planet scene) whose radius is each player's own zoom (cGameDataUFO
// mZoomAltitude, ~520 m zoomed in to ~970 m before leaving to the system on the home planet of the
// space fixture, docs/re/space.md 0 and 7.10): two ships over the same spot at different zooms were 200 m
// apart in 3D and never repaired, though the top-down camera showed them on top of each other
// (coop_space_rescue.py on GOG 2024, where the retried landings left the two players at different
// zooms). So on a planet: the distance along the lower sphere, ignoring the altitude.
static float repair_distance(const net::Vec3& a, const net::Vec3& b)
{
    uint32_t place = C.my_place ? C.my_place() : 0;
    float ra = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z), rb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    if (!(place & 0x40000000u) || ra < 1.f || rb < 1.f)
        return net::dist(a, b);
    net::Vec3 ua{a.x / ra, a.y / ra, a.z / ra}, ub{b.x / rb, b.y / rb, b.z / rb};
    return net::dist(ua, ub) * std::min(ra, rb);
}

static uint32_t star_of(uint32_t place)
{
    if (place & 0x80000000u)
        return place & 0x7FFFFFFFu;
    if (place & 0x40000000u)
        return place & 0xFFFFFFu; // PlanetID: star in the low 24 bits
    return 0;
}

// the partners in our place; `friends`: not those at war with us (rivals mode, space_rivalry.cpp):
// an enemy does not rescue our ship, terraform with us or earn us a "together" bonus
static std::vector<Partner> partners_here(bool friends = true)
{
    std::vector<Partner> r;
    uint32_t mine = C.my_place ? C.my_place() : 0;
    if (!mine || !C.partners)
        return r;
    for (auto& p : C.partners())
        if (p.place == mine &&
            (!friends || spaceext::relation(C.sess->my_id(), p.id) != spaceext::Relation::War))
            r.push_back(p);
    return r;
}

// BUG-0131: the partners who can rescue or repair us: playing (a recent state, in the world, not
// dead) and not disabled themselves. Two disabled ships next to each other repaired each other.
static std::vector<Partner> helpers_here()
{
    std::vector<Partner> r = partners_here();
    if (g_helper_fix)
        r.erase(std::remove_if(r.begin(), r.end(), [](const Partner& p) { return !p.playing || g_partner_down.count(p.id); }),
                r.end());
    return r;
}

static void send(uint8_t kind, const net::Writer& body)
{
    if (!connected())
        return;
    net::Writer w;
    w.u8(kind);
    w.u8(C.sess->my_id());
    w.raw(body.data.data(), body.data.size());
    if (C.sess->role() == net::Role::Host)
        C.sess->broadcast_reliable(net::MSG_SPACE, w);
    else
        C.sess->send_reliable_to(0, net::MSG_SPACE, w);
}

static std::string name(uint8_t id) { return C.sess ? C.sess->name_of(id) : "?"; }

// ---------------- rescue ----------------
static void* my_combatant() { void* u = my_ufo(); return u ? (uint8_t*)u + kCombat : nullptr; }

bool disabled() { return g_down; }

// BUG-0228: the recorded disabled ship is the player's live ship of this Space world
static bool down_ship_here()
{
    void* u = g_down_ship.u;
    return u && in_space() && u == my_ufo() && g_down_ship.epoch == spaceext::world_epoch() && game::ufo_listed(u) &&
           game::object_alive(u) && game::object_serial(u) == g_down_ship.serial;
}

static void note_down_ship(void* u)
{
    bool live = u && game::ufo_listed(u) && game::object_alive(u);
    g_down_ship = {live ? u : nullptr, live ? game::object_serial(u) : 0u, spaceext::world_epoch()};
    g_invincible_owed = false;
}

float filter_own_damage(void* ufo, float dmg, int type)
{
    if (!in_space() || !connected() || ufo != my_ufo() || type == 10)
        return dmg;
    if (g_down)
        return 0.f;
    float hp = F<float>(ufo, kHp);
    double now = H->time_seconds();
    // (a ship whose HP got to 1 or less through small hits is disabled too: `hp <= 1` used to let
    // the next hit kill it next to the partner, play_space.py 2026-10-23)
    if (hp - dmg >= 1.f || F<int>(ufo, kState) == 2 || now - g_last_down < 60)
        return dmg;
    auto here = helpers_here();
    if (here.empty())
        return dmg; // nobody can help: the native death
    // disabled instead of destroyed: HP stays at 1, no more damage, no shooting
    g_down = true;
    g_down_until = now + kDownTime;
    g_last_down = now;
    note_down_ship(ufo);
    g_repair_t = 0; // invincible from the next frame on (down_tick): this hit still shows
    g_down_fresh = g_repair_fix;
    net::Writer w;
    w.u8(1);
    w.u8((uint8_t)kDownTime);
    send(K_DOWN, w);
    if (C.notice)
        C.notice("your ship is disabled: a partner can repair it by flying close (20 s)");
    return std::max(0.f, hp - 1.f);
}

static void end_down(bool rescued)
{
    void* u = my_ufo();
    g_down = false;
    net::Writer w;
    w.u8(rescued ? 2 : 3);
    w.u8(0);
    send(K_DOWN, w);
    g_invincible_owed = false;
    g_down_ship = {};
    if (!u)
        return;
    F<uint8_t>(u, kInvincible) = 0;
    if (rescued) {
        g_rescues++;
        if (a_AddHealth)
            thiscall<void>(a_AddHealth, (uint8_t*)u + kCombat, F<float>(u, kMaxHp) * 0.5f);
        if (C.notice)
            C.notice("your ship is repaired");
        return;
    }
    // nobody came: the native death (cinematic, respawn at the nearest colony)
    g_lost++;
    if (a_SetHealth)
        thiscall<void>(a_SetHealth, (uint8_t*)u + kCombat, 0.f);
    F<int>(u, kState) = 2;
    if (a_Destroyed)
        thiscall<void>(a_Destroyed, u);
}

// BUG-0098: the rescue ends outside Space too (left the stage, a load, the session ended): the
// singleton's ship pointer may be freed then. Clear the invincible byte only on the live own ship;
// a new ship starts without it anyway.
// BUG-0228: and only on the ship the state was set on (same pointer, serial and Space world), at
// once in Space, else owed until the first Space frame back (down_owed_tick): in an editor or an
// adventure the ship lives on with the byte at 1.
static void clear_down_invincible()
{
    if (!g_down_clear_fix) {
        void* u = my_ufo();
        if (u && in_space() && game::ufo_listed(u) && game::object_alive(u))
            F<uint8_t>(u, kInvincible) = 0;
        return;
    }
    if (down_ship_here() && !cinematic_playing()) {
        F<uint8_t>(g_down_ship.u, kInvincible) = 0;
        g_invincible_owed = false;
        g_down_ship = {};
        return;
    }
    g_invincible_owed = g_down_ship.u != nullptr;
}

// BUG-0228: an owed clear, at the top of the frame (also after the session ended)
static void down_owed_tick()
{
    if (!g_invincible_owed || g_down || !in_space())
        return;
    if (g_down_ship.epoch != spaceext::world_epoch() || !game::ufo_listed(g_down_ship.u) ||
        !game::object_alive(g_down_ship.u) || game::object_serial(g_down_ship.u) != g_down_ship.serial) {
        g_invincible_owed = false; // another world or the ship is gone: a new ship starts without the byte
        g_down_ship = {};
        g_down_owed_dropped++;
        return;
    }
    if (g_down_ship.u != my_ufo() || cinematic_playing())
        return; // not the player's ship yet / a cinematic may hold the byte: next frame
    F<uint8_t>(g_down_ship.u, kInvincible) = 0;
    g_invincible_owed = false;
    g_down_ship = {};
    g_down_owed_cleared++;
    H->log(M, SK_INFO, "space: the disabled state ended outside Space: the ship's invincibility cleared on return");
}

// BUG-0229: while disabled, every frame (before the early return of tick): another Space world
// ends the state without a death; outside Space in the same world (an editor, an adventure) the
// 20 s stand still
static void down_world_tick(double now)
{
    static double last = now;
    double dt = std::min(0.5, std::max(0.0, now - last));
    last = now;
    if (!g_down || !g_down_world_fix)
        return;
    if (g_down_ship.epoch != spaceext::world_epoch()) {
        g_down = false;
        g_down_world_ends++;
        H->log(M, SK_INFO, "space: the disabled state ended without a death: another Space world (epoch %u -> %u)",
               g_down_ship.epoch, spaceext::world_epoch());
        clear_down_invincible();
        return;
    }
    if (!in_space())
        g_down_until += dt;
}

static void down_tick(double now)
{
    if (!g_down)
        return;
    void* u = my_ufo();
    if (!u || !in_space() || !connected()) {
        g_down = false;
        clear_down_invincible();
        return;
    }
    F<uint8_t>(u, kInvincible) = 1;
    bool close_by = false;
    for (auto& p : helpers_here())
        if (repair_distance(p.pos, ufo_pos(u)) < g_repair_dist)
            close_by = true;
    static double last = now;
    if (g_down_fresh) { // BUG-0147: the first step of a new disable is 0, not up to 0.5 s from the last one
        g_down_fresh = false;
        last = now;
    }
    double dt = std::min(0.5, now - last);
    last = now;
    g_repair_t = close_by ? g_repair_t + dt : std::max(0.0, g_repair_t - dt);
    if (g_repair_t >= kRepairHold)
        end_down(true);
    else if (now > g_down_until) {
        if (g_down_world_fix && g_down_ship.u && !down_ship_here()) {
            // BUG-0229: not the ship the state was set on (another world, a new ship): no death
            g_down = false;
            g_down_world_ends++;
            H->log(M, SK_INFO, "space: the disabled state ended without a death: not the ship it was set on");
            clear_down_invincible();
            return;
        }
        end_down(false);
    }
}

// A destroyed ship waits for the end of its death cinematic (UFO message handler 0xC3B6A0 ->
// Respawn). If that cinematic is gone but the ship is still dead (the end message was lost, e.g. a
// fault in another listener of it), post the handler's own retry message 0x67B2A95: the game
// respawns the ship the native way (docs/re/space.md 7.2).
static void respawn_tick(double now)
{
    static double stuck_since;
    void* u = my_ufo();
    if (!u || F<int>(u, kState) != 2 || !F<uint32_t>(u, kCinematic) || cinematic_playing() || !a_MessageMgr) {
        stuck_since = 0;
        return;
    }
    if (!stuck_since)
        stuck_since = now;
    if (now - stuck_since < 5)
        return;
    stuck_since = now; // once per 5 s until the ship is back
    H->log(M, SK_WARN, "space: the ship is destroyed but no death cinematic runs: respawn (msg 0x67B2A95)");
    if (void* mm = cdecl_call<void*>(a_MessageMgr))
        vcall<void>(mm, 0x18 / 4, 0x67B2A95u, (void*)nullptr, (void*)nullptr, 0); // MessagePost
    g_respawns_forced++;
}

// ---------------- jump ----------------
static std::string travel_to(uint32_t star, bool force = false);

// BUG-0072: the button of the co-op window shows the partner can_jump picks and presses jump(0xFF):
// both take the first partner with a known star that is not mine (the old jump(0xFF) took the first
// one with any known star, so a partner in my system made every press fail with 3 players), and
// the notice names that partner (it named player 0). `coop.space jumpfix 0`: the old pick (tests).
static bool g_jump_fix = true;
COOP_FIX("space.jump", "BUG-0072", g_jump_fix, "space: Jump to picks a partner whose star is not mine, the notice names it");
static bool pick_jump_target(uint8_t to, uint8_t& id, uint32_t& star, bool skip_mine)
{
    std::vector<spacelogic::JumpPartner> v;
    if (C.partners)
        for (auto& p : C.partners()) {
            uint32_t s = star_of(p.place);
            if (!s && g_last_star.count(p.id))
                s = g_last_star[p.id];
            v.push_back({p.id, s});
        }
    return spacelogic::pick_jump_target(v, star_of(C.my_place ? C.my_place() : 0), to, skip_mine, id, star);
}

bool can_jump(std::string& to)
{
    if (!in_space() || !connected() || !C.partners)
        return false;
    uint8_t id = 0;
    uint32_t star = 0;
    if (!pick_jump_target(0xFF, id, star, true))
        return false;
    to = name(id);
    return true;
}

std::string jump(uint8_t to)
{
    if (!in_space())
        return "not in space";
    if (g_down)
        return "the ship is disabled";
    uint8_t id = to;
    uint32_t star = 0;
    // a chosen partner in my system is still found (and refused below); "any partner" skips them
    if (!pick_jump_target(to, id, star, to == 0xFF && g_jump_fix) && to == 0xFF && g_jump_fix)
        pick_jump_target(to, id, star, false);
    if (!star)
        return "the partner's star is not known (in the galaxy view?)";
    if (star == star_of(C.my_place ? C.my_place() : 0))
        return "already in the partner's system";
    std::string e = travel_to(star);
    if (e.empty() && C.notice)
        C.notice("jumped to " + name(g_jump_fix ? id : (to == 0xFF ? 0 : to)) + "'s star system");
    return e;
}

std::string travel(uint32_t star)
{
    if (!in_space())
        return "not in space";
    if (g_down)
        return "the ship is disabled";
    if (star == star_of(C.my_place ? C.my_place() : 0))
        return "already there";
    return travel_to(star);
}
std::string travel_to_star(uint32_t star) { return in_space() ? travel_to(star) : "not in space"; }

// BUG-0063: a click on a partner flying in the galaxy view teleports the ship next to it
// (SpaceTeleportTo); it went past the disabled check and spent energy (TravelToStar; with none
// left it damages the ship). The same guards and energy refund as travel_to.
std::string teleport_in_galaxy(uint32_t star)
{
    static uintptr_t teleport = H->sym("Simulator::SpaceTeleportTo");
    if (!in_space())
        return "not in space";
    if (g_down)
        return "the ship is disabled";
    if (!teleport || !a_Context)
        return "symbols missing";
    void* u = my_ufo();
    if ((u && F<uint32_t>(u, kCinematic)) || cinematic_playing())
        return "a cinematic is playing";
    if (!u || cdecl_call<int>(a_Context) != 2)
        return "not in the galaxy view";
    void* rec = star_record(star);
    if (!rec)
        return "no star record";
    float energy = F<float>(u, 0x7F4);
    F<float>(u, 0x7F4) = std::max(energy, F<float>(u, 0x7F8));
    cdecl_call<void>(teleport, rec);
    F<float>(u, 0x7F4) = energy;
    g_jumps++;
    return "";
}

// cStarManager::GetStarRecord faults on an id that is not a star of this galaxy (seen with id 1,
// 2026-10-23: GetStarRecord+0x32): look the record up in its table (+0xC8, 20-byte sectors whose
// first dword is the record array, StarID = sector << 12 | index) and check the record's own id.
// BUG-0092: the index is checked against the sector's end (+4) before the array is read; an id from
// the network may name an index past it (`coop.space starfix 0`: the old read, for the tests).
static bool g_star_fix = true;
COOP_FIX("space.star", "BUG-0092", g_star_fix, "space: a star id's index is checked against its table");
static uint32_t g_star_oob; // lookups whose index was past the sector's end (read or refused)
static uint32_t g_star_oob_read; // ... and read anyway (the old path)
void* star_record(uint32_t id)
{
    if (!id || id == 0xFFFFFFFFu || !a_StarMgr)
        return nullptr;
    void* sm = cdecl_call<void*>(a_StarMgr);
    if (!sm || IsBadReadPtr((uint8_t*)sm + 0xC8, 8))
        return nullptr;
    uint8_t* b = F<uint8_t*>(sm, 0xC8);
    uint8_t* e = F<uint8_t*>(sm, 0xCC);
    uint32_t sec = id >> 12, idx = id & 0xFFF;
    if (!b || e < b || sec >= (uint32_t)((e - b) / 20) || IsBadReadPtr(b + sec * 20, 8))
        return nullptr;
    void** arr = F<void**>(b + sec * 20, 0);
    void** arr_end = F<void**>(b + sec * 20, 4);
    if (!arr)
        return nullptr;
    if (arr_end <= arr || idx >= (uint32_t)(arr_end - arr)) {
        g_star_oob++;
        if (g_star_fix)
            return nullptr;
        g_star_oob_read++;
    }
    if (IsBadReadPtr(arr + idx, 4))
        return nullptr;
    void* r = arr[idx];
    if (!r || IsBadReadPtr(r, 0x74) || F<uint32_t>(r, 0x70) != id)
        return nullptr;
    return r;
}

// coop.space jumpto <star> [force] [old]: "old" = the 0.6 jump (TravelToStarSystem), for comparisons
static bool g_jump_old;

// The jump passes through the galaxy view for a moment. SystemToGalaxy posts 0x5E37A3B, whose handler
// 0xFDABE0 shows what belongs to the player's first look at the galaxy (in the tutorial the caption
// SPG_EnteredGalaxy_201 about the beacon, which waits for OK and then blocks the next jump, also in a
// save without the interstellar drive; hints). The player does not see that galaxy view: skip the
// handler once, right after the jump; a later real zoom-out shows it as usual.
using GalaxyEntered_t = void(SK_THISCALL*)(void*);
static GalaxyEntered_t o_GalaxyEntered;
static double g_galaxy_pass_until;
static uint32_t g_galaxy_msgs_skipped;
static void __fastcall h_GalaxyEntered(void* self, void*)
{
    if (H->time_seconds() < g_galaxy_pass_until) {
        g_galaxy_pass_until = 0;
        g_galaxy_msgs_skipped++;
        return;
    }
    o_GalaxyEntered(self);
}

static std::string travel_to(uint32_t star, bool force)
{
    static uintptr_t to_galaxy = H->sym("SporeCoop::cAppModeSpace_SystemToGalaxy"),
                     teleport = H->sym("Simulator::SpaceTeleportTo"),
                     active_rec = H->sym("Simulator::GetActiveStarRecord");
    if (!a_ModeSpace || !a_EnterSystem || !a_LeavePlanet || !a_Context || !a_StarMgr || !a_GetStarRecord ||
        !to_galaxy || !teleport || !active_rec)
        return "symbols missing";
    void* u = my_ufo();
    // leaving the planet while the respawn caption still plays destroys the planet object its end
    // handler uses: the next cinematic end faults and the ship is never respawned (docs/re/space.md 7.2)
    if (((u && F<uint32_t>(u, kCinematic)) || cinematic_playing()) && !force)
        return "a cinematic is playing";
    void* mode = *(void**)a_ModeSpace;
    int ctx = cdecl_call<int>(a_Context);
    void* rec = star_record(star);
    if (!rec)
        return "no such star";
    if (!mode || ctx < 0 || !u)
        return "can't travel now";
    if (rec == cdecl_call<void*>(active_rec)) {
        if (ctx != 2)
            return "already in that system";
        thiscall<void>(a_EnterSystem, mode); // the galaxy view at that very star: just go in
        g_jumps++;
        return "";
    }
    // the player's own way (docs/re/space.md 7.3): planet -> system -> galaxy view, a free instant
    // flight to the star (the space -flyto cheat), then into its system
    if (ctx == 0) {
        void* planet = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        if (planet && a_PlanetCleanup && a_PlanetCleanupMgr && *(void**)a_PlanetCleanupMgr)
            thiscall<void>(a_PlanetCleanup, *(void**)a_PlanetCleanupMgr, planet);
        thiscall<void>(a_LeavePlanet, mode, 0);
        ctx = cdecl_call<int>(a_Context);
    }
    if (g_jump_old) {
        if (ctx == 2)
            thiscall<void>(a_EnterSystem, mode);
        thiscall<void>(a_TravelSystem, mode, rec, 1);
        g_jumps++;
        return "";
    }
    if (ctx == 1) {
        if (o_GalaxyEntered)
            g_galaxy_pass_until = H->time_seconds() + 5;
        thiscall<void>(to_galaxy, mode);
        ctx = cdecl_call<int>(a_Context);
    }
    if (ctx != 2) {
        g_galaxy_pass_until = 0; // BUG-0095: no galaxy pass, so no skip left for the player's own zoom-out
        return "can't reach the galaxy view";
    }
    // the flight costs energy (cSimulatorPlayerUFO::TravelToStar; with none left it damages the
    // ship): the co-op jump is free, so the energy is put back afterwards
    float energy = F<float>(u, 0x7F4);
    F<float>(u, 0x7F4) = std::max(energy, F<float>(u, 0x7F8));
    cdecl_call<void>(teleport, rec); // refunds the money, TravelToStar, snaps to the star
    F<float>(u, 0x7F4) = energy;
    bool there = cdecl_call<void*>(active_rec) == rec;
    thiscall<void>(a_EnterSystem, mode); // the target's system, or back into the old one
    if (!there)
        return "the flight was refused";
    g_jumps++;
    return "";
}

// ---------------- missions and raids ----------------
static bool is_raid(void* m)
{
    uint32_t id = F<uint32_t>(m, 0x78);
    return id == 0xA4B0A2DFu || id == 0xC4956AA7u || id == 0xD646C543u || id == 0x77184BAEu;
}

static uint32_t mission_place(void* m)
{
    void* planet = F<void*>(m, 0x1E8);
    void* rec = planet && !IsBadReadPtr(planet, 0x140) ? F<void*>(planet, 0x13C) : nullptr;
    return rec && !IsBadReadPtr(rec, 0x188) ? (F<uint32_t>(rec, 0x184) | 0x40000000u) : 0;
}

static void add_money(int n)
{
    void* emp = a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr;
    if (emp && a_AddMoney)
        thiscall<void>(a_AddMoney, emp, n);
}

static void* inventory()
{
    void* sg = a_SpaceGame ? *(void**)a_SpaceGame : nullptr;
    return sg ? F<void*>(sg, 0x40) : nullptr;
}

static std::function<void(void*, uint32_t, int)> g_raid_listener;
void set_raid_listener(std::function<void(void* mission, uint32_t place, int state)> fn) { g_raid_listener = std::move(fn); }

// The planets of a star are generated the first time something needs them (a flight there, the
// jump, a mission: cStarManager::RequirePlanetsForStar 0xBB4C60 -> GeneratePlanetsForStar 0xBB4270)
// from the game's global random generator (Math::RandomNumberGenerator, one LCG dword at
// 0x1602760), so two games made different planets for the same unvisited star (count, PlanetIDs: a
// partner's planet place did not exist on the other side). Seeding the global generator was not
// enough (other draws in between: the names, cSpaceNames::GenerateRandomName, draw a number of times
// that depends on the names made before; 6 and 10 planets for the same seed): the generator's own
// draws (the calls whose return address is inside GeneratePlanetsForStar) use a private generator
// seeded from the star (StarID and galaxy position, the same in both games) instead (docs/re/space.md 7.3).
using RandomInt_t = uint32_t(SK_THISCALL*)(void*, uint32_t);
using RandomFloat_t = float(SK_THISCALL*)(void*);
using RequirePlanets_t = void(SK_THISCALL*)(void*, void*, void*);
static RandomInt_t o_RandomInt;
static RandomFloat_t o_RandomFloat;
static RequirePlanets_t o_RequirePlanets;
static uintptr_t a_Rng;
static uint32_t g_planets_seeded;

// BUG-0123: the generation state is per thread. It was process-wide: a GenerateRandomName or a
// RequirePlanetsForStar on another thread (or two threads changing the name counter at once, which
// could leave it non-zero for good) switched the seeding of the generating thread off, and its
// planets differed from the other game's. `coop.space seedfix 0`: the old shared state (tests).
struct GenState {
    uint32_t rng;
    bool generating;
    int in_names;
};
static thread_local GenState t_gen;
static GenState g_gen_shared; // the old state (seedfix 0)
static DWORD g_gen_thread_shared;
static bool g_seed_fix = true;
COOP_FIX("space.seed", "BUG-0123", g_seed_fix, "space: planet seeding state is per thread");
static DWORD g_main_tid;                     // set by tick (the main thread)
static uint32_t g_gen_off_main, g_names_off_main; // hooks seen on other threads (logged once)
static GenState& gen() { return g_seed_fix ? t_gen : g_gen_shared; }

// every draw of the generating thread while it generates, except the names'
static bool generator_draw(void* rng)
{
    GenState& g = gen();
    return g.generating && !g.in_names && rng == (void*)a_Rng &&
           (g_seed_fix || GetCurrentThreadId() == g_gen_thread_shared);
}

static uint32_t SK_THISCALL h_RandomInt(void* rng, uint32_t n)
{
    return o_RandomInt(generator_draw(rng) ? &gen().rng : rng, n);
}

static float SK_THISCALL h_RandomFloat(void* rng)
{
    return o_RandomFloat(generator_draw(rng) ? &gen().rng : rng);
}

static void off_main(uint32_t& n, const char* what)
{
    if (!g_main_tid || GetCurrentThreadId() == g_main_tid)
        return;
    if (!n++)
        H->log(M, SK_WARN, "space: %s runs on thread %lu, not on the main thread (BUG-0123)", what,
               (unsigned long)GetCurrentThreadId());
}

static void names_enter() { gen().in_names++; }
static void names_leave() { gen().in_names--; }

using RandomName_t = void*(SK_THISCALL*)(void*, void*, uint32_t);
static RandomName_t o_RandomName;
static void* SK_THISCALL h_RandomName(void* names, void* out, uint32_t kind)
{
    off_main(g_names_off_main, "GenerateRandomName");
    names_enter();
    void* r = o_RandomName(names, out, kind);
    names_leave();
    return r;
}

static void SK_THISCALL h_RequirePlanets(void* sm, void* star, void* p3)
{
    off_main(g_gen_off_main, "RequirePlanetsForStar");
    GenState& g = gen();
    if (!star || IsBadReadPtr(star, 0xB0) || F<uint8_t>(star, 0xAC) || g.generating) {
        o_RequirePlanets(sm, star, p3);
        return;
    }
    uint32_t x, z;
    memcpy(&x, (uint8_t*)star + 0x3C, 4);
    memcpy(&z, (uint8_t*)star + 0x44, 4);
    g.rng = (F<uint32_t>(star, 0x70) * 0x9E3779B1u ^ x ^ (z << 16 | z >> 16)) | 1u;
    g_gen_thread_shared = GetCurrentThreadId();
    g.generating = true;
    o_RequirePlanets(sm, star, p3);
    g.generating = false;
    g_planets_seeded++;
    H->log(M, SK_DEBUG, "space: planets of star %08X generated (seeded): %d", F<uint32_t>(star, 0x70),
           (int)F<uint8_t>(star, 0xAC));
}

// tests (BUG-0123): another thread "inside GenerateRandomName" for `ms` (the hook's bookkeeping,
// no game call), while this game generates a star
static void names_held(int ms)
{
    std::thread([ms] {
        names_enter();
        Sleep(ms);
        names_leave();
    }).detach();
}

using SetState_t = void(SK_THISCALL*)(void*, int);
static SetState_t o_SetState;
static void SK_THISCALL h_SetState(void* m, int s)
{
    int old = m ? F<int>(m, 0x84) : -1;
    o_SetState(m, s);
    if (!m || old == s || !in_space() || !connected())
        return;
    if (is_raid(m)) {
        uint32_t place = mission_place(m);
        // only raids on my own colonies: space_allies.cpp starts raids on a partner's mirror colony
        // in my galaxy (that partner gets its own alerts)
        void* emp = a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr;
        void* star = star_record(place & 0xFFFFFFu);
        if (!emp || !star || F<uint32_t>(star, 0x54) != F<uint32_t>(emp, 0x84))
            return;
        if (g_raid_listener && (s == 3 || s == 5 || s == 6))
            g_raid_listener(m, place, s);
        if (s == 3) { // a raid starts on our colony
            g_raids_seen++;
            g_raid_since[m] = H->time_seconds();
            net::Writer w;
            w.u32(place);
            w.u8(3);
            send(K_RAID, w);
        } else if (s == 5 || s == 6) {
            g_raid_since.erase(m);
            bool helped = false;
            for (auto& p : partners_here())
                (void)p, helped = true;
            if (s == 5 && helped) {
                add_money(2000); // money is shared: added once, here
                g_raid_bonus++;
                if (C.notice)
                    C.notice("colony defended together: +2000");
            }
            net::Writer w;
            w.u32(place);
            w.u8((uint8_t)s);
            send(K_RAID, w);
        }
        return;
    }
    if (s == 5 && !(F<uint32_t>(m, 0x130) & 1)) { // a mission (not an event) is completed
        net::Writer w;
        std::vector<Key> keys;
        Key k;
        memcpy(k.v, (uint8_t*)m + 0xA4, 12);
        if (k.v[0])
            keys.push_back(k);
        uint8_t* b = F<uint8_t*>(m, 0x11C);
        uint8_t* e = F<uint8_t*>(m, 0x120);
        for (uint8_t* it = b; it && it + 12 <= e && keys.size() < 8; it += 12) {
            memcpy(k.v, it, 12);
            if (k.v[0])
                keys.push_back(k);
        }
        w.f32(F<float>(m, 0xA0));
        w.u8((uint8_t)keys.size());
        for (auto& kk : keys)
            for (uint32_t v : kk.v)
                w.u32(v);
        send(K_MISSION, w);
        g_missions_sent++;
        if (!partners_here().empty()) {
            int bonus = (int)(F<float>(m, 0xA0) * 0.25f);
            if (bonus > 0) {
                add_money(bonus);
                if (C.notice)
                    C.notice("mission done with a partner nearby: +" + std::to_string(bonus));
            }
        }
    }
}

// ---------------- terraforming ----------------
using Terra_t = void(SK_THISCALL*)(void*, void*, float);
static Terra_t o_Terra[3];
static void note_terra(int which, void* planet, float d)
{
    if (g_applying || !in_space() || !connected() || !a_ActivePlanet || planet != cdecl_call<void*>(a_ActivePlanet))
        return;
    uint32_t place = C.my_place ? C.my_place() : 0;
    if (!(place & 0x40000000u))
        return;
    if (g_terra_planet != place) {
        g_terra_acc[0] = g_terra_acc[1] = g_terra_acc[2] = 0;
        g_terra_planet = place;
    }
    g_terra_acc[which] += d;
}
static void SK_THISCALL h_TerraA(void* tm, void* p, float d) { note_terra(0, p, d); o_Terra[0](tm, p, d); }
static void SK_THISCALL h_TerraW(void* tm, void* p, float d) { note_terra(1, p, d); o_Terra[1](tm, p, d); }
static void SK_THISCALL h_TerraT(void* tm, void* p, float d) { note_terra(2, p, d); o_Terra[2](tm, p, d); }

static void terra_tick(double now)
{
    if (now - g_terra_last < 0.5)
        return;
    g_terra_last = now;
    if (!g_terra_planet || (!g_terra_acc[0] && !g_terra_acc[1] && !g_terra_acc[2]))
        return;
    bool here = false;
    for (auto& p : partners_here())
        (void)p, here = true;
    if (here) {
        net::Writer w;
        w.u32(g_terra_planet);
        for (float v : g_terra_acc)
            w.f32(v);
        send(K_TERRA, w);
        g_terra_sent++;
    }
    g_terra_acc[0] = g_terra_acc[1] = g_terra_acc[2] = 0;
}

static void apply_terra(uint32_t planet_place, const float d[3])
{
    uint32_t mine = C.my_place ? C.my_place() : 0;
    if (mine != planet_place || !a_TerraMgr || !a_ActivePlanet)
        return;
    void* tm = cdecl_call<void*>(a_TerraMgr);
    void* p = cdecl_call<void*>(a_ActivePlanet);
    if (!tm || !p)
        return;
    g_applying = true;
    for (int i = 0; i < 3; i++)
        if (d[i] != 0 && o_Terra[i])
            o_Terra[i](tm, p, d[i]);
    g_applying = false;
    g_terra_got++;
}

// ---------------- cargo ----------------
// the first spice (trading object, item type 4) in the cargo
static int find_spice(void* inv, Key& key, int& count)
{
    if (!inv)
        return -1;
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        uint8_t* it = vcall<uint8_t*>(inv, 0x60 / 4, i);
        if (!it || F<int>(it, 0x14) != 4)
            continue;
        const uint32_t* k = vcall<const uint32_t*>(it, 0x18 / 4);
        if (!k)
            continue;
        memcpy(key.v, k, 12);
        count = F<int>(it, 0x10);
        return i;
    }
    return -1;
}

static bool add_spice(const Key& key, int n)
{
    void* inv = inventory();
    void* tr = a_Trading ? cdecl_call<void*>(a_Trading) : nullptr;
    if (!inv || !tr || !a_CreateTrading || n <= 0)
        return false;
    void* item = nullptr;
    thiscall<void>(a_CreateTrading, tr, &item, key.v, n, 0.f);
    if (!item)
        return false;
    bool ok = vcall<bool>(inv, 0x80 / 4, item, (void*)nullptr, true);
    vcall<void>(item, 0x08 / 4); // Release
    return ok;
}

static uint8_t gift_target()
{
    if (C.sess)
        for (auto& p : C.sess->players())
            if (p.id != C.sess->my_id())
                return p.id;
    return 0xFF;
}

int spice(std::string& to)
{
    if (!in_space() || !connected())
        return -1;
    uint8_t t = gift_target();
    if (t == 0xFF)
        return -1;
    to = name(t);
    Key k;
    int c = 0;
    return find_spice(inventory(), k, c) >= 0 ? c : 0;
}

int give_spice(int n)
{
    uint8_t to = gift_target();
    void* inv = inventory();
    Key k;
    int c = 0;
    int i = find_spice(inv, k, c);
    if (to == 0xFF || i < 0 || n <= 0)
        return 0;
    n = std::min(n, c);
    void* out = nullptr;
    if (!vcall<bool>(inv, 0x7C / 4, i, &out, n == c ? -1 : n))
        return 0;
    if (out)
        vcall<void>(out, 0x08 / 4);
    net::Writer w;
    w.u8(to);
    for (uint32_t v : k.v)
        w.u32(v);
    w.u32((uint32_t)n);
    send(K_GIFT, w);
    g_gifts_sent++;
    if (C.notice)
        C.notice(std::to_string(n) + " spice sent to " + name(to));
    return n;
}

// ---------------- messages ----------------
void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_SPACE || !g_ok)
        return;
    const uint8_t* body = r.cur();
    size_t body_n = r.left();
    uint8_t kind = r.u8(), origin = r.u8();
    if (!r.ok())
        return;
    bool host = C.sess->role() == net::Role::Host;
    if (!net::space_origin_ok(host, from, origin, kind)) {
        g_bad_origin++;
        return;
    }
    if (host) { // relay to the other guests
        net::Writer w;
        w.raw(body, body_n);
        C.sess->broadcast_reliable(net::MSG_SPACE, w, from);
    }
    if (origin == C.sess->my_id())
        return;
    double now = H->time_seconds();
    switch (kind) {
    case K_DOWN: {
        uint8_t st = r.u8(), secs = r.u8();
        if (!r.ok())
            break;
        if (st == 1) {
            g_partner_down[origin] = now + secs;
            if (C.notice)
                C.notice(name(origin) + "'s ship is disabled: fly close to repair it");
        } else {
            g_partner_down.erase(origin);
            if (C.notice)
                C.notice(st == 2 ? name(origin) + "'s ship is repaired" : name(origin) + "'s ship was destroyed");
        }
        break;
    }
    case K_RAID: {
        uint32_t place = r.u32();
        uint8_t st = r.u8();
        if (!r.ok())
            break;
        if (st == 3) {
            g_raids_told++;
            if (star_of(place))
                g_last_star[origin] = star_of(place);
            g_last_raid_text = name(origin) + "'s colony is raided";
            if (C.notice)
                C.notice(g_last_raid_text + ": jump there from the co-op window (coop.space jump)");
        } else if (C.notice)
            C.notice(st == 5 ? name(origin) + "'s colony is safe" : name(origin) + "'s colony was plundered");
        break;
    }
    case K_MISSION: {
        r.f32();
        uint8_t n = r.u8();
        if (!r.ok() || r.left() < (size_t)n * 12)
            break; // cut: no notice, no tools
        if (spaceext::relation(C.sess->my_id(), origin) != spaceext::Relation::Ally)
            break; // rivals mode: mission rewards are shared between allies only
        void* inv = inventory();
        int got = 0;
        for (int i = 0; i < n && r.ok(); i++) {
            Key k;
            for (auto& v : k.v)
                v = r.u32();
            if (!inv || !a_AddItemByKey || !r.ok())
                continue;
            if (vcall<bool>(inv, 0x74 / 4, k.v)) // HasTool: no second copy
                continue;
            thiscall<void>(a_AddItemByKey, inv, k.v);
            got++;
        }
        g_tools_got += got;
        if (C.notice)
            C.notice(name(origin) + " completed a mission" +
                     (got ? ": you get its reward tools too (" + std::to_string(got) + ")" : ""));
        break;
    }
    case K_TERRA: {
        uint32_t place = r.u32();
        float d[3] = {r.f32(), r.f32(), r.f32()};
        if (r.ok())
            apply_terra(place, d);
        break;
    }
    case K_GIFT:
    case K_GIFT_BACK: {
        uint8_t to = r.u8();
        Key k;
        for (auto& v : k.v)
            v = r.u32();
        int n = (int)r.u32();
        if (!r.ok() || to != C.sess->my_id())
            break;
        if (add_spice(k, n)) {
            if (kind == K_GIFT)
                g_gifts_got++;
            if (C.notice)
                C.notice(kind == K_GIFT ? name(origin) + " gave you " + std::to_string(n) + " spice"
                                        : name(origin) + " had no room: " + std::to_string(n) + " spice came back");
        } else if (kind == K_GIFT) { // no free cargo slot: send it back
            net::Writer w;
            w.u8(origin);
            for (uint32_t v : k.v)
                w.u32(v);
            w.u32((uint32_t)n);
            send(K_GIFT_BACK, w);
            if (C.notice)
                C.notice("no free cargo slot for " + name(origin) + "'s spice");
        }
        break;
    }
    default:
        spaceext::dispatch(kind, origin, r); // the features of space_ext.h
        break;
    }
}

// ---------------- frame ----------------
void tick(double now)
{
    g_main_tid = GetCurrentThreadId();
    spaceext::tick_all(now); // features check the stage and the session themselves (and the world epoch)
    down_world_tick(now);    // BUG-0229: first end or pause the disabled state,
    down_owed_tick();        // BUG-0228: then an owed clear of its invincibility
    if (!in_space() || !connected())
        return;
    if (C.partners)
        for (auto& p : C.partners())
            if (uint32_t s = star_of(p.place))
                g_last_star[p.id] = s;
    down_tick(now);
    respawn_tick(now);
    terra_tick(now);
    for (auto it = g_partner_down.begin(); it != g_partner_down.end();)
        it = now > it->second + 2 ? g_partner_down.erase(it) : std::next(it);
    // a disabled partner here: a beacon on its ship
    if (C.beacon)
        for (auto& p : partners_here())
            if (g_partner_down.count(p.id)) {
                int left = (int)std::max(0.0, g_partner_down[p.id] - now);
                C.beacon(p.pos, "REPAIR " + p.name + " (" + std::to_string(left) + " s)", 0.25, 0xFFFF6040);
            }
}

void draw_overlay()
{
    spaceext::overlay_all();
    if (!g_down || !in_space())
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    double now = H->time_seconds();
    int left = (int)std::max(0.0, g_down_until - now);
    char b[160];
    snprintf(b, sizeof b, "SHIP DISABLED  %d s  -  a partner repairs it by flying close", left);
    uint32_t col = ((int)(now * 3)) & 1 ? 0xFFFF5030 : 0xFFFFD040;
    H->overlay_text(sw / 2 - utf8::overlay_width(b) / 2, sh / 4, col, b); // cells, not bytes (BUG-0159)
    if (g_repair_t > 0) {
        std::string bar = "repair ";
        int n = (int)(g_repair_t / kRepairHold * 10);
        for (int i = 0; i < 10; i++)
            bar += i < n ? "■" : "□";
        H->overlay_text(sw / 2 - 80, sh / 4 + 26, 0xFF60FF60, bar.c_str());
    }
}

void on_session_end()
{
    if (g_down) {
        g_down = false;
        clear_down_invincible();
    }
    g_partner_down.clear();
    g_last_star.clear();
    g_raid_since.clear();
    g_terra_planet = 0;
    spaceext::session_end_all();
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    auto S = [](const char* n) { return H->sym(n); };
    a_PlayerUFOGet = S("Simulator::cSimulatorPlayerUFO::Get");
    a_Context = S("SporeCoop::Space_GetCurrentContext");
    a_ActivePlanet = S("SporeCoop::Space_GetActivePlanet");
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_GetStarRecord = S("Simulator::cStarManager::GetStarRecord");
    a_ModeSpace = S("Simulator::cAppModeSpace::ptr");
    a_EnterSystem = S("Simulator::cAppModeSpace::EnterSolarSystem");
    a_LeavePlanet = S("Simulator::cAppModeSpace::LeavePlanet");
    a_TravelSystem = S("Simulator::cAppModeSpace::TravelToStarSystem");
    a_PlanetCleanup = S("SporeCoop::PlanetModelCleanup");
    a_PlanetCleanupMgr = S("SporeCoop::PlanetModelMgr_ptr");
    a_AddHealth = S("Simulator::cCombatant::AddHealth");
    a_SetHealth = S("Simulator::cCombatant::SetHealth");
    a_Destroyed = S("Simulator::cGameDataUFO::PlayerDestroyed");
    a_SetState = S("Simulator::cMission::SetState");
    a_TerraA = S("Simulator::cTerraformingManager::AddAtmosphereDelta");
    a_TerraW = S("Simulator::cTerraformingManager::AddWaterDelta");
    a_TerraT = S("Simulator::cTerraformingManager::AddTemperatureDelta");
    a_TerraMgr = S("Simulator::cTerraformingManager::Get");
    a_SpaceGame = S("Simulator::cSimulatorSpaceGame::ptr");
    a_AddItemByKey = S("Simulator::cPlayerInventory::AddItemByKey");
    a_Trading = S("Simulator::cSpaceTrading::Get");
    a_CreateTrading = S("Simulator::cSpaceTrading::CreateTradingObject");
    a_AddMoney = S("Simulator::cEmpire::AddMoney");
    a_PlayerEmpire = S("Simulator::GetPlayerEmpire");
    a_CinematicMgr = S("Simulator::cCinematicManager::Get");
    a_MessageMgr = S("App::IMessageManager::Get");
    g_ok = a_PlayerUFOGet && a_Context && a_ActivePlanet;
    if (a_SetState)
        H->hook_create(M, (void*)a_SetState, (void*)h_SetState, (void**)&o_SetState);
    if (uintptr_t ge = S("SporeCoop::cAppModeSpace_OnGalaxyEntered"))
        H->hook_create(M, (void*)ge, (void*)h_GalaxyEntered, (void**)&o_GalaxyEntered);
    a_Rng = S("Math::RandomNumberGenerator::global_ptr");
    uintptr_t ri = S("Math::RandomNumberGenerator::RandomInt"), rf = S("Math::RandomNumberGenerator::RandomFloat"),
              rp = S("Simulator::cStarManager::RequirePlanetsForStar"),
              rn = S("Simulator::cSpaceNames::GenerateRandomName");
    if (a_Rng && ri && rf && rp && rn && H->hook_create(M, (void*)ri, (void*)h_RandomInt, (void**)&o_RandomInt) == 0 &&
        H->hook_create(M, (void*)rf, (void*)h_RandomFloat, (void**)&o_RandomFloat) == 0 &&
        H->hook_create(M, (void*)rn, (void*)h_RandomName, (void**)&o_RandomName) == 0)
        H->hook_create(M, (void*)rp, (void*)h_RequirePlanets, (void**)&o_RequirePlanets);
    if (a_TerraA && a_TerraW && a_TerraT) {
        H->hook_create(M, (void*)a_TerraA, (void*)h_TerraA, (void**)&o_Terra[0]);
        H->hook_create(M, (void*)a_TerraW, (void*)h_TerraW, (void**)&o_Terra[1]);
        H->hook_create(M, (void*)a_TerraT, (void*)h_TerraT, (void**)&o_Terra[2]);
    }
    H->log(M, SK_INFO, "space co-op: rescue %s, jump %s, missions %s, terraform %s, cargo %s",
           a_AddHealth && a_Destroyed ? "on" : "off", a_TravelSystem && a_GetStarRecord ? "on" : "off",
           a_SetState ? "on" : "off", a_TerraA ? "on" : "off", a_CreateTrading && a_Trading ? "on" : "off");
    spaceext::init_all(H, M, &C);
    return g_ok;
}

std::vector<syncdbg::Entry> nav_entries()
{
    std::vector<syncdbg::Entry> v;
    if (!in_space() || !a_StarMgr || !a_Context)
        return v;
    int ctx = cdecl_call<int>(a_Context);
    void* sm = cdecl_call<void*>(a_StarMgr);
    if (!sm || IsBadReadPtr(sm, 0xD0))
        return v;
    char key[24];
    if (ctx == 0) {
        // on a planet: its cities (GetAllCities 0xACE1C0, cCity +0x124 position, +0x30 political
        // id), key T<index> for ours (a click opens the city planner), X<index> for others
        static uintptr_t all = H->sym("Simulator::cGameNounManager::GetAllCities");
        void* nm = game::noun_manager();
        void* vec = all && nm ? thiscall<void*>(all, nm) : nullptr;
        void* me = my_ufo();
        if (!vec || !me)
            return v;
        void** b = F<void**>(vec, 0);
        void** e = F<void**>(vec, 4);
        int i = 0;
        for (void** it = b; it && it < e && v.size() < 64; ++it, ++i) {
            void* c = *it;
            if (!c || IsBadReadPtr(c, 0x130) || !game::object_alive(c))
                continue;
            snprintf(key, sizeof key, "%c%d", F<uint32_t>(c, 0x30) == F<uint32_t>(me, 0x30) ? 'T' : 'X', i);
            auto& q = F<float[3]>(c, 0x124);
            v.push_back({key, {q[0], q[1], q[2]}, {0, 0, 0, 1}});
        }
    } else if (ctx == 1) {
        // the loaded cPlanet objects of the current system: cStarManager +0x50..+0x54 (the list
        // RecordToPlanet 0xBB5B20 searches); cPlanet +0x04 position, +0x13C record (docs/re/space.md 7.9).
        // The list also keeps the planets of the player's colonies in other systems (positions in
        // their own system): only the active star's planets (PlanetID & 0xFFFFFF = StarID)
        static uintptr_t active_rec = H->sym("Simulator::GetActiveStarRecord");
        void* star = active_rec ? cdecl_call<void*>(active_rec) : nullptr;
        uint32_t sid = star && !IsBadReadPtr(star, 0x74) ? F<uint32_t>(star, 0x70) : 0xFFFFFFFFu;
        void** b = F<void**>(sm, 0x50);
        void** e = F<void**>(sm, 0x54);
        for (void** it = b; it && it < e && v.size() < 64; ++it) {
            void* p = *it;
            if (!p || IsBadReadPtr(p, 0x140))
                continue;
            void* rec = F<void*>(p, 0x13C);
            if (!rec || IsBadReadPtr(rec, 0x198) || (F<uint32_t>(rec, 0x184) & 0xFFFFFFu) != sid)
                continue;
            snprintf(key, sizeof key, "%c%08X", F<int>(rec, 0x194) == 5 ? 'C' : 'P', F<uint32_t>(rec, 0x184));
            auto& q = F<float[3]>(p, 0x04);
            v.push_back({key, {q[0], q[1], q[2]}, {0, 0, 0, 1}});
        }
    } else if (ctx == 2) {
        // galaxy view: star records near the own ship. cStarManager +0xC8 = sectors (20 bytes each,
        // first dword = array of cStarRecord*, as GetStarRecord 0xBA6EB0 indexes them: StarID =
        // sector << 12 | index); cStarRecord +0x3C position (galaxy coordinates), +0x54 empire id,
        // +0x70 StarID (docs/re/space.md 7.9)
        void* me = my_ufo();
        if (!me)
            return v;
        net::Vec3 at = ufo_pos(me);
        uint8_t* sb = F<uint8_t*>(sm, 0xC8);
        uint8_t* se = F<uint8_t*>(sm, 0xCC);
        if (!sb || se <= sb || (se - sb) / 20 > 4096 || IsBadReadPtr(sb, se - sb))
            return v;
        // (stars are ~1 unit apart and the drive reaches a few units: only the neighbourhood; every
        // star of the galaxy would fill the list in sector order and miss the near ones)
        for (uint8_t* sec = sb; sec + 20 <= se && v.size() < 200; sec += 20) {
            void** b = F<void**>(sec, 0);
            void** e = F<void**>(sec, 4);
            if (!b || e <= b || e - b > 4096 || IsBadReadPtr(b, (e - b) * 4))
                continue;
            for (void** it = b; it < e && v.size() < 200; ++it) {
                void* r = *it;
                if (!r || IsBadReadPtr(r, 0x74))
                    continue;
                auto& q = F<float[3]>(r, 0x3C);
                net::Vec3 p{q[0], q[1], q[2]};
                if (net::dist(p, at) > 8.f)
                    continue;
                snprintf(key, sizeof key, "%c%08X", F<uint32_t>(r, 0x54) == F<uint32_t>(me, 0x30) ? 'H' : 'S',
                         F<uint32_t>(r, 0x70));
                v.push_back({key, p, {0, 0, 0, 1}});
            }
        }
    }
    return v;
}

std::string status()
{
    char b[400];
    snprintf(b, sizeof b,
             "space co-op: down=%d rescues=%u lost=%u raids=%u raids_told=%u raid_bonus=%u missions_sent=%u "
             "tools_got=%u terra_sent=%u terra_got=%u gifts_sent=%u gifts_got=%u jumps=%u planets_seeded=%u galaxy_msgs_skipped=%u respawns_forced=%u "
             "partners_here=%u partners_down=%u bad_origin=%u\n",
             (int)g_down, g_rescues, g_lost, g_raids_seen, g_raids_told, g_raid_bonus, g_missions_sent, g_tools_got,
             g_terra_sent, g_terra_got, g_gifts_sent, g_gifts_got, g_jumps, g_planets_seeded, g_galaxy_msgs_skipped, g_respawns_forced,
             (unsigned)partners_here().size(), (unsigned)g_partner_down.size(), g_bad_origin);
    return b + spaceext::status_all();
}

// tests / fixtures (docs/re/space.md 9.6): what the player's empire has, and test setup that the
// tutorial does not allow early (a colony without the colony tool, a first contact without flying there)
//   coop.space empire                          money, drive, colonies (PlanetID, stored spice), contacts
//   coop.space empire colony <PlanetID> [spice] the record-level colony of docs/re/space.md 5.3
//   coop.space empire contact <StarID>          the first contact with the empire of that star
static bool g_fpu_fix = true; // coop.space fpufix 0: ApplyRelationship called as void (BUG-0126, tests)
COOP_FIX("space.fpu", "BUG-0126", g_fpu_fix, "space: ApplyRelationship is called with its float result popped");

// The x87 register stack: TOP (0 = empty, each value left on it lowers TOP by one) and the tag word
// (0xFFFF = all registers empty). Read from a command handler, between frames: any value there
// was leaked by someone (BUG-0126).
static std::string fpu_state()
{
    uint8_t env[28] = {};
    __asm__ volatile("fnstenv %0\n\tfldenv %0" : "+m"(env));
    uint16_t sw, tw;
    memcpy(&sw, env + 4, 2);
    memcpy(&tw, env + 8, 2);
    int used = 0;
    for (int i = 0; i < 8; i++)
        used += ((tw >> (2 * i)) & 3) != 3;
    char b[80];
    snprintf(b, sizeof b, "fpu top=%d tag=%04X used=%d", (sw >> 11) & 7, tw, used);
    return b;
}

static std::string empire_cmd(const std::vector<std::string>& a)
{
    static uintptr_t spd_ptr = H->sym("Simulator::SpacePlayerData::ptr"),
                     nouns = H->sym("Simulator::cGameNounManager::Get"),
                     planet_rec = H->sym("Simulator::cStarManager::GetPlanetRecord"),
                     add_star = H->sym("Simulator::cEmpire::AddStarOwnership"),
                     add_planet = H->sym("SporeCoop::cEmpire_AddPlanet"),
                     set_tech = H->sym("SporeCoop::cPlanetRecord_SetTechLevel"),
                     fill = H->sym("SporeCoop::cPlanetRecord_FillPlanetDataForTechLevel"),
                     empire_of = H->sym("Simulator::cStarManager::GetEmpireForStar"),
                     contacted = H->sym("SporeCoop::cEmpire_SetContactedByPlayer"),
                     rel_get = H->sym("Simulator::cRelationshipManager::Get"),
                     rel_apply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
    void* emp = a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr;
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    if (!emp || !sm || !spd_ptr || !*(void**)spd_ptr)
        return "not in space";
    uint32_t pol = F<uint32_t>(emp, 0x84);
    char b[200];
    auto set_spice = [&](void* rec, float spice) { // every city of the player's cCivData on the planet
        int n = 0;
        auto civs = F<void**>(rec, 0x15C), civs_end = F<void**>(rec, 0x160);
        for (auto c = civs; c && c < civs_end; c++)
            if (*c && F<uint32_t>(*c, 0x14) == pol)
                for (auto city = F<void**>(*c, 0x3C); city && city < F<void**>(*c, 0x40); city++)
                    if (*city) {
                        F<float>(*city, 0x1C) = spice; // cCityData mSpiceProduction (stored spice)
                        n++;
                    }
        return n;
    };
    if (a.size() > 2 && a[1] == "colony") {
        if (!planet_rec || !add_star || !add_planet || !set_tech || !fill || !a_GetStarRecord)
            return "symbols missing";
        uint32_t id = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        void* rec = thiscall<void*>(planet_rec, sm, id);
        void* star = thiscall<void*>(a_GetStarRecord, sm, id & 0xFFFFFFu);
        if (!rec || !star)
            return "no such planet";
        void* active = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        if (active && F<void*>(active, 0x13C) == rec)
            return "not on the planet the ship is at"; // SetTechLevel would change the live planet too
        if (F<int>(rec, 0x194) == 5)
            return "already a colony";
        if (F<uint32_t>(star, 0x54) != pol) { // the record-level star claim of 0xFE9AF3
            thiscall<void>(add_star, emp, star);
            F<uint32_t>(star, 0x54) = pol; // SetEmpireID
            F<int>(star, 0x4C) = 5;        // SetTechLevel(5)
            F<uint32_t>(star, 0x5C) |= 0x100;
        }
        cdecl_call<void>(set_tech, rec, 5);
        cdecl_call<void>(fill, rec, 5); // the player's cCivData + a cCityData (pol = star empire id)
        thiscall<void>(add_planet, emp, rec);
        int n = set_spice(rec, a.size() > 3 ? (float)atof(a[3].c_str()) : 0.f);
        snprintf(b, sizeof b, "colony %08X: tech %d, cities %d", id, F<int>(rec, 0x194), n);
        return b;
    }
    if (a.size() > 2 && a[1] == "contact") {
        if (!empire_of || !contacted || !rel_get || !rel_apply || !a_GetStarRecord)
            return "symbols missing";
        void* star = thiscall<void*>(a_GetStarRecord, sm, (uint32_t)strtoul(a[2].c_str(), nullptr, 16));
        if (!star)
            return "no such star";
        uint32_t e = F<uint32_t>(star, 0x54);
        if (e == 0xFFFFFFFFu || e == pol) // GetEmpireForStar would CREATE an empire for a free star
            return e == pol ? "an own star" : "no empire at that star";
        void* other = thiscall<void*>(empire_of, sm, star);
        if (!other)
            return "no empire";
        thiscall<void>(contacted, other);
        // the relationship event of the game's own first contact (message 0x3AC86B5 handler 0xFE3233)
        // BUG-0126: it returns a float in ST0; a `void` call never pops it and leaks one x87 register
        // per call (8 calls: every fld in the process gives NaN). `coop.space fpufix 0`: the old call.
        if (g_fpu_fix)
            (void)thiscall<float>(rel_apply, cdecl_call<void*>(rel_get), F<uint32_t>(other, 0x84), pol, 0x05B6CF09u,
                                  1.0f);
        else
            thiscall<void>(rel_apply, cdecl_call<void*>(rel_get), F<uint32_t>(other, 0x84), pol, 0x05B6CF09u, 1.0f);
        snprintf(b, sizeof b, "contact with %08X", F<uint32_t>(other, 0x84));
        return b;
    }
    // coop.space empire near [n]: the n nearest stars of NPC empires and the n nearest free stars
    // (no empire, not the Grox) around the active star, for tests of the jump (docs/re/space.md 7.3):
    // "npc|free <StarID> pol=<empire> d=<galaxy units> contact=<0|1> flags=<hex> visited=<star +0x5C & 0x30> planets=<+0xAC>"
    if (a.size() > 1 && a[1] == "near") {
        static uintptr_t active_rec = H->sym("Simulator::GetActiveStarRecord");
        void* here = active_rec ? cdecl_call<void*>(active_rec) : nullptr;
        if (!here)
            return "no active star";
        size_t n = a.size() > 2 ? (size_t)atoi(a[2].c_str()) : 3;
        const float* h = &F<float>(here, 0x3C);
        void* nm = nouns ? cdecl_call<void*>(nouns) : nullptr;
        void* player = nm ? F<void*>(nm, 0x74) : nullptr;
        auto contact = [&](uint32_t e) { // the cPlayer contact flags (bit 1 contacted, bit 2 aware)
            if (player)
                for (auto p = F<uint32_t*>(player, 0x11D8); p && p < F<uint32_t*>(player, 0x11DC); p += 2)
                    if (p[0] == e)
                        return p[1];
            return 0u;
        };
        std::vector<std::pair<float, void*>> npc, free_;
        uint8_t* sb = F<uint8_t*>(sm, 0xC8);
        uint8_t* se = F<uint8_t*>(sm, 0xCC);
        for (uint8_t* sec = sb; sb && sec + 20 <= se; sec += 20)
            for (void** it = F<void**>(sec, 0); it && it < F<void**>(sec, 4); ++it) {
                void* r = *it;
                if (!r || r == here || IsBadReadPtr(r, 0xB0))
                    continue;
                const float* q = &F<float>(r, 0x3C);
                float d = sqrtf((q[0] - h[0]) * (q[0] - h[0]) + (q[1] - h[1]) * (q[1] - h[1]) +
                                (q[2] - h[2]) * (q[2] - h[2]));
                uint32_t e = F<uint32_t>(r, 0x54), fl = F<uint32_t>(r, 0x5C);
                if (e == 0xFFFFFFFFu && !(fl & 0x8))
                    free_.push_back({d, r});
                else if (e != pol && F<int>(r, 0x4C) == 5 && !(fl & 0x8))
                    npc.push_back({d, r});
            }
        uint32_t he = F<uint32_t>(here, 0x54);
        snprintf(b, sizeof b, "here %08X pol=%08X contact=%d flags=%X visited=%d planets=%d\n", F<uint32_t>(here, 0x70),
                 he, (contact(he) & 2) ? 1 : 0, contact(he), (F<uint32_t>(here, 0x5C) & 0x30) ? 1 : 0,
                 (int)F<uint8_t>(here, 0xAC));
        std::string out = b;
        for (auto* v : {&npc, &free_}) {
            std::sort(v->begin(), v->end());
            for (size_t i = 0; i < v->size() && i < n; i++) {
                void* r = (*v)[i].second;
                uint32_t e = F<uint32_t>(r, 0x54);
                snprintf(b, sizeof b, "%s %08X pol=%08X d=%.1f contact=%d flags=%X visited=%d planets=%d\n",
                         v == &npc ? "npc" : "free", F<uint32_t>(r, 0x70), e, (*v)[i].first,
                         (contact(e) & 2) ? 1 : 0, contact(e),
                         (F<uint32_t>(r, 0x5C) & 0x30) ? 1 : 0, (int)F<uint8_t>(r, 0xAC));
                out += b;
            }
        }
        return out;
    }
    // the report
    std::string s;
    void* inv = inventory();
    uint32_t drive[3] = {0x2569FAE8u, 0, 0}; // interstellardrive
    bool has_drive = inv && vcall<bool>(inv, 0x74 / 4, drive);
    void* spd = *(void**)spd_ptr;
    auto col = F<void**>(spd, 0x20), col_end = F<void**>(spd, 0x24);
    int ncol = col && col_end > col ? (int)(col_end - col) : 0;
    std::string lines;
    for (int i = 0; i < ncol && i < 64; i++) {
        void* rec = col[i] ? F<void*>(col[i], 0x13C) : nullptr;
        if (!rec)
            continue;
        float spice = 0;
        auto civs = F<void**>(rec, 0x15C), civs_end = F<void**>(rec, 0x160);
        for (auto c = civs; c && c < civs_end; c++)
            if (*c && F<uint32_t>(*c, 0x14) == pol)
                for (auto city = F<void**>(*c, 0x3C); city && city < F<void**>(*c, 0x40); city++)
                    if (*city)
                        spice += F<float>(*city, 0x1C);
        snprintf(b, sizeof b, "colony %08X spice=%.0f\n", F<uint32_t>(rec, 0x184), spice);
        lines += b;
    }
    // cPlayer (game nouns +0x74) +0x11D8: sorted {pol, flags} pairs, bit 1 = contacted (0xC7A890)
    int ncon = 0;
    void* nm = nouns ? cdecl_call<void*>(nouns) : nullptr;
    void* player = nm ? F<void*>(nm, 0x74) : nullptr;
    if (player)
        for (auto p = F<uint32_t*>(player, 0x11D8); p && p < F<uint32_t*>(player, 0x11DC); p += 2)
            if (p[0] != pol && (p[1] & 2)) {
                snprintf(b, sizeof b, "contact %08X\n", p[0]);
                lines += b;
                ncon++;
            }
    snprintf(b, sizeof b, "empire %08X money=%d drive=%d colonies=%d contacts=%d\n", pol, F<int>(emp, 0xD0),
             (int)has_drive, ncol, ncon);
    return b + lines;
}

// tests: coop.space down | hp | jump [id] | give <n> | spice add <n> | terra <a> <w> <t> | money
std::string debug(const std::vector<std::string>& a)
{
    if (a.empty())
        return status();
    std::string ext;
    if (spaceext::debug(a, ext)) // coop.space features | rows | press <n> | <feature> ...
        return ext;
    void* u = my_ufo();
    char b[200];
    if (a[0] == "hit" && a.size() > 1 && u) { // the game's damage path on the own ship
        float hp0 = F<float>(u, kHp);
        static const float zero[3] = {0, 0, 0}; // the hit direction must not be null
        vcall<int>((uint8_t*)u + kCombat, 0x18 / 4, (float)atof(a[1].c_str()), 0xFFFFFFFFu, 0, zero,
                   (void*)nullptr);
        snprintf(b, sizeof b, "hp %.1f -> %.1f max %.1f down=%d invincible=%d state=%d", hp0, F<float>(u, kHp),
                 F<float>(u, kMaxHp), (int)g_down, (int)F<uint8_t>(u, kInvincible), F<int>(u, kState));
        return b;
    }
    if (a[0] == "hp" && u) {
        // cine=1: the death caption ("...you return to your homeworld") waits for the player's OK;
        // movie=1: any cinematic runs (also the respawn caption "We've built you a new ship...")
        snprintf(b, sizeof b, "hp %.1f max %.1f down=%d left=%.1f repair=%.1f state=%d cine=%d movie=%d",
                 F<float>(u, kHp), F<float>(u, kMaxHp), (int)g_down, g_down ? g_down_until - H->time_seconds() : 0.0,
                 g_repair_t, F<int>(u, kState), F<uint32_t>(u, kCinematic) ? 1 : 0, (int)cinematic_playing());
        return b;
    }
    if (a[0] == "jump") {
        std::string e = jump(a.size() > 1 ? (uint8_t)atoi(a[1].c_str()) : 0xFF);
        return e.empty() ? "jumping" : e;
    }
    if (a[0] == "star") { // the own star id; active: the planet the ship is at / flies to (PlanetID)
        uint32_t pl = C.my_place ? C.my_place() : 0;
        void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        void* rec = p && !IsBadReadPtr(p, 0x140) ? F<void*>(p, 0x13C) : nullptr;
        snprintf(b, sizeof b, "place %08X star %u active %08X", pl, star_of(pl),
                 rec && !IsBadReadPtr(rec, 0x188) ? F<uint32_t>(rec, 0x184) : 0u);
        return b;
    }
    if (a[0] == "planets") { // tests: the loaded planets of the system (cStarManager +0x50, on screen or not)
        std::string out;
        for (auto& e : nav_entries())
            if (e.key.size() == 9 && (e.key[0] == 'P' || e.key[0] == 'C'))
                out += e.key + " ";
        return out.empty() ? "none" : out;
    }
    if (a[0] == "land") { // tests: land on the active planet (the landing of Respawn)
        uintptr_t f = H->sym("SporeCoop::cAppModeSpace_LandOnActivePlanet");
        void* mode = a_ModeSpace ? *(void**)a_ModeSpace : nullptr;
        if (!f || !mode || cdecl_call<int>(a_Context) != 1)
            return "not in a system";
        if (!cdecl_call<void*>(a_ActivePlanet) || !u || F<uint32_t>(u, kCinematic) || cinematic_playing() ||
            F<int>(u, kState) == 2)
            return "no active planet / the ship is not flying";
        thiscall<void>(f, mode);
        return "landing";
    }
    if (a[0] == "takeoff") { // tests: planet -> system view (the jump's first step, as a zoom out does)
        void* mode = a_ModeSpace ? *(void**)a_ModeSpace : nullptr;
        if (!mode || !a_LeavePlanet || cdecl_call<int>(a_Context) != 0)
            return "not on a planet";
        if ((u && F<uint32_t>(u, kCinematic)) || cinematic_playing())
            return "a cinematic is playing";
        void* planet = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        if (planet && a_PlanetCleanup && a_PlanetCleanupMgr && *(void**)a_PlanetCleanupMgr)
            thiscall<void>(a_PlanetCleanup, *(void**)a_PlanetCleanupMgr, planet);
        thiscall<void>(a_LeavePlanet, mode, 0);
        return "left the planet";
    }
    if (a[0] == "helperfix" && a.size() > 1) { // BUG-0131: 0 = a disabled or frozen partner rescues too
        g_helper_fix = a[1] != "0";
        return std::string("helperfix ") + (g_helper_fix ? "1" : "0");
    }
    if (a[0] == "repairfix" && a.size() > 1) { // BUG-0147: 0 = a new disable starts with the old step
        g_repair_fix = a[1] != "0";
        return std::string("repairfix ") + (g_repair_fix ? "1" : "0");
    }
    if (a[0] == "energy" && u) { // tests (BUG-0063): the ship's energy (+0x7F4, max +0x7F8); "energy <v>" sets it
        if (a.size() > 1)
            F<float>(u, 0x7F4) = (float)atof(a[1].c_str());
        snprintf(b, sizeof b, "energy %.2f max %.2f hp %.1f max %.1f", F<float>(u, 0x7F4), F<float>(u, 0x7F8),
                 F<float>(u, kHp), F<float>(u, kMaxHp));
        return b;
    }
    if (a[0] == "disable" && a.size() > 1) { // tests (BUG-0063): the disabled flag alone, no rescue messages
        g_down = a[1] != "0";
        if (g_down) {
            g_down_until = H->time_seconds() + kDownTime;
            note_down_ship(in_space() ? u : nullptr);
        } else
            clear_down_invincible();
        return std::string("down=") + (g_down ? "1" : "0");
    }
    if (a[0] == "away" && a.size() > 1) { // tests (BUG-0226/0228/0229): act as outside Space, the world stays
        spaceext::set_away(a[1] != "0");
        return std::string("away=") + (spaceext::away() ? "1" : "0");
    }
    if (a[0] == "downstate") { // tests (BUG-0228/0229)
        snprintf(b, sizeof b, "down=%d left=%.1f owed=%d epoch=%u ship_epoch=%u world_ends=%u owed_cleared=%u owed_dropped=%u away=%d",
                 (int)g_down, g_down ? g_down_until - H->time_seconds() : 0.0, (int)g_invincible_owed,
                 spaceext::world_epoch(), g_down_ship.epoch, g_down_world_ends, g_down_owed_cleared,
                 g_down_owed_dropped, (int)spaceext::away());
        return b;
    }
    if (a[0] == "seedfix" && a.size() > 1) { // BUG-0123: 0 = one generation state for all threads (tests)
        g_seed_fix = a[1] != "0";
        return std::string("seedfix ") + (g_seed_fix ? "1" : "0");
    }
    if (a[0] == "namesheld" && a.size() > 1) { // tests (BUG-0123): another thread inside the name hook
        names_held(atoi(a[1].c_str()));
        snprintf(b, sizeof b, "names held %d ms on another thread; off-main: generations %u names %u", atoi(a[1].c_str()),
                 g_gen_off_main, g_names_off_main);
        return b;
    }
    if (a[0] == "jumpfix" && a.size() > 1) { // BUG-0072: 0 = jump(0xFF) takes a partner in my system (tests)
        g_jump_fix = a[1] != "0";
        return std::string("jumpfix ") + (g_jump_fix ? "1" : "0");
    }
    if (a[0] == "fpufix" && a.size() > 1) { // BUG-0126: 0 = empire contact leaks one x87 register (tests)
        g_fpu_fix = a[1] != "0";
        return std::string("fpufix ") + (g_fpu_fix ? "1" : "0");
    }
    if (a[0] == "fpu") // tests (BUG-0126): the x87 register stack between frames
        return fpu_state();
    if (a[0] == "starfix" && a.size() > 1) { // BUG-0092: 0 = the old unchecked index (tests)
        g_star_fix = a[1] != "0";
        return std::string("starfix ") + (g_star_fix ? "1" : "0");
    }
    if (a[0] == "starrec" && a.size() > 1) { // tests (BUG-0092): look a star id up; the sector's size
        uint32_t id = (uint32_t)strtoul(a[1].c_str(), nullptr, 0);
        void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
        uint8_t* sb = sm && !IsBadReadPtr((uint8_t*)sm + 0xC8, 8) ? F<uint8_t*>(sm, 0xC8) : nullptr;
        uint8_t* se = sb ? F<uint8_t*>(sm, 0xCC) : nullptr;
        uint32_t sec = id >> 12;
        long n = -1;
        if (sb && se > sb && sec < (uint32_t)((se - sb) / 20) && !IsBadReadPtr(sb + sec * 20, 8))
            n = (long)(F<void**>(sb + sec * 20, 4) - F<void**>(sb + sec * 20, 0));
        uint32_t o0 = g_star_oob_read;
        void* r = star_record(id);
        snprintf(b, sizeof b, "star %08X rec=%s sector_size=%ld read_outside=%u oob=%u", id, r ? "yes" : "none", n,
                 g_star_oob_read - o0, g_star_oob);
        return b;
    }
    if (a[0] == "jumpto" && a.size() > 1) { // "force": even during a cinematic (tests: the old jump)
        bool force = false;
        g_jump_old = false;
        for (size_t i = 2; i < a.size(); i++)
            force |= a[i] == "force", g_jump_old |= a[i] == "old";
        std::string e = travel_to((uint32_t)strtoul(a[1].c_str(), nullptr, 0), force);
        g_jump_old = false;
        return e.empty() ? "jumping" : e;
    }
    if (a[0] == "goto" && u) { // put the own ship next to a partner (tests of the repair)
        for (auto& p : C.partners ? C.partners() : std::vector<Partner>{})
            if (C.my_place && p.place == C.my_place()) {
                float q[3] = {p.pos.x + 20.f, p.pos.y, p.pos.z};
                memcpy((uint8_t*)u + kPos, q, 12);
                memcpy((uint8_t*)u + 0x718, q, 12); // next position
                memcpy((uint8_t*)u + 0x750, q, 12); // destination
                snprintf(b, sizeof b, "moved next to %s (%.0f %.0f %.0f)", p.name.c_str(), q[0], q[1], q[2]);
                return b;
            }
        return "no partner here";
    }
    if (a[0] == "repairdist" && a.size() > 1) { // tests: the repair range (0: a disabled ship is never repaired)
        g_repair_dist = (float)atof(a[1].c_str());
        snprintf(b, sizeof b, "repair range %.0f m", g_repair_dist);
        return b;
    }
    if (a[0] == "dist" && u) {
        std::string s;
        for (auto& p : partners_here(false)) {
            // (3D; then the distance a repair measures: on a planet without the altitude; and on a
            // planet the partner's height over ours: its zoom, ~15 m per wheel notch)
            net::Vec3 me = ufo_pos(u);
            int n = snprintf(b, sizeof b, "%s %.1f m; repair %.1f m; ", p.name.c_str(), net::dist(p.pos, me),
                             repair_distance(p.pos, me));
            if ((C.my_place ? C.my_place() : 0) & 0x40000000u)
                snprintf(b + n, sizeof b - n, "alt %+.0f m; ",
                         std::sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y + p.pos.z * p.pos.z) -
                             std::sqrt(me.x * me.x + me.y * me.y + me.z * me.z));
            s += b;
        }
        return s.empty() ? "no partner here" : s;
    }
    if (a[0] == "give" && a.size() > 1)
        return "given " + std::to_string(give_spice(atoi(a[1].c_str())));
    if (a[0] == "spice") {
        Key k;
        int c = 0;
        int i = find_spice(inventory(), k, c);
        if (a.size() > 2 && a[1] == "add") {
            if (i < 0)
                return "no spice to copy the key from";
            return add_spice(k, atoi(a[2].c_str())) ? "added" : "not added";
        }
        if (a.size() > 2 && a[1] == "make") { // tests: spice of the local planet (record+0x198)
            uintptr_t home = H->sym("SporeCoop::GetPlayerHomePlanetRecord");
            void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
            void* recs[2] = {p ? F<void*>(p, 0x13C) : nullptr, home ? cdecl_call<void*>(home) : nullptr};
            for (void* rec : recs)
                if (rec && F<uint32_t>(rec, 0x198)) {
                    memcpy(k.v, (uint8_t*)rec + 0x198, 12);
                    return add_spice(k, atoi(a[2].c_str())) ? "made" : "not added (no cargo slot?)";
                }
            return "no planet spice key";
        }
        snprintf(b, sizeof b, "spice slot %d count %d key %08X %08X %08X", i, c, k.v[0], k.v[1], k.v[2]);
        return b;
    }
    if (a[0] == "terra" && a.size() > 3) { // as if the own tool changed the planet
        void* tm = a_TerraMgr ? cdecl_call<void*>(a_TerraMgr) : nullptr;
        void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        if (!tm || !p)
            return "not on a planet";
        h_TerraA(tm, p, (float)atof(a[1].c_str()));
        h_TerraW(tm, p, (float)atof(a[2].c_str()));
        h_TerraT(tm, p, (float)atof(a[3].c_str()));
        void* rec = F<void*>(p, 0x13C);
        snprintf(b, sizeof b, "planet b0=%.3f b4=%.3f b8=%.3f", F<float>(rec, 0xB0), F<float>(rec, 0xB4),
                 F<float>(rec, 0xB8));
        return b;
    }
    if (a[0] == "tools" || (a[0] == "tool" && a.size() > 2 && a[1] == "add")) {
        // tests: the tools in the inventory (instance hash = FNV-1 of the tool file name, e.g. scan,
        // abduct, interplanetarydrive, interstellardrive) and adding one by name (setup: the fixture's
        // ship has no interstellar drive yet, docs/re/space.md 7.7)
        void* inv = inventory();
        if (!inv)
            return "no inventory";
        Key tk{};
        std::string s;
        int n = vcall<int>(inv, 0x5C / 4);
        for (int i = 0; i < n && i < 256; i++) {
            uint8_t* it = vcall<uint8_t*>(inv, 0x60 / 4, i);
            if (!it || F<int>(it, 0x14) != 2)
                continue;
            const uint32_t* k = vcall<const uint32_t*>(it, 0x18 / 4);
            if (!k)
                continue;
            memcpy(tk.v, k, 12);
            snprintf(b, sizeof b, "tool %08X %08X %08X\n", k[0], k[1], k[2]);
            s += b;
        }
        if (a[0] == "tools")
            return s.empty() ? "no tools" : s;
        if (!tk.v[0] || !a_AddItemByKey) // tool keys are {instance, 0, 0} in this build
            return "no tool to copy the key type/group from";
        const std::string& nm = a[2];
        uint32_t h = 0x811C9DC5u;
        if (nm.size() > 2 && nm[0] == '0' && nm[1] == 'x')
            h = (uint32_t)strtoul(nm.c_str(), nullptr, 16);
        else
            for (char c : nm)
                h = (h * 0x01000193u) ^ (uint8_t)tolower((unsigned char)c);
        tk.v[0] = h;
        if (vcall<bool>(inv, 0x74 / 4, tk.v)) // HasTool
            return "already there";
        thiscall<void>(a_AddItemByKey, inv, tk.v);
        snprintf(b, sizeof b, "added %08X: %s", h, vcall<bool>(inv, 0x74 / 4, tk.v) ? "ok" : "not in the inventory");
        return b;
    }
    if (a[0] == "empire")
        return empire_cmd(a);
    if (a[0] == "planet") {
        void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
        void* rec = p ? F<void*>(p, 0x13C) : nullptr;
        if (!rec)
            return "not on a planet";
        snprintf(b, sizeof b, "planet b0=%.3f b4=%.3f b8=%.3f", F<float>(rec, 0xB0), F<float>(rec, 0xB4),
                 F<float>(rec, 0xB8));
        return b;
    }
    return "usage: coop.space [hit <dmg> | hp | jump [id] | give <n> | spice [add <n>] | terra <a> <w> <t> | planet]";
}

} // namespace spacecoop

// ---------------- the Space page: a partner's disabled ship (space_map.cpp has the partners' places) ----------------
namespace spacecoop {
static std::vector<spaceext::Row> squad_rows()
{
    std::vector<spaceext::Row> v;
    if (!in_space() || !connected() || !C.partners)
        return v;
    uint32_t here = star_of(C.my_place ? C.my_place() : 0);
    for (auto& p : C.partners()) {
        uint32_t s = star_of(p.place);
        bool known = s || g_last_star.count(p.id);
        if (!s && known)
            s = g_last_star[p.id];
        spaceext::Row r;
        bool down = g_partner_down.count(p.id) != 0;
        if (!down)
            continue; // where the partner is: the row of space_map.cpp
        if (s && s == here)
            r.text = p.name + spaceext::tr(": in your system", ": в вашей системе");
        else if (!p.place)
            r.text = p.name + spaceext::tr(": in the galaxy view", ": на карте галактики");
        else
            r.text = p.name + spaceext::tr(": in another star system", ": в другой звёздной системе");
        if (down) {
            r.text += spaceext::tr(" - ship disabled!", " - корабль подбит!");
            r.color = 0xFFFF6040;
        }
        if (s && s != here) {
            r.button = spaceext::tr("Jump there", "Прыгнуть");
            uint8_t id = p.id;
            r.on_click = [id] {
                std::string e = jump(id);
                if (!e.empty() && C.notice)
                    C.notice("can't jump: " + e);
            };
        }
        v.push_back(std::move(r));
    }
    return v;
}
static bool squad_reg = spaceext::add({.name = "squad", .rows = squad_rows});
} // namespace spacecoop
