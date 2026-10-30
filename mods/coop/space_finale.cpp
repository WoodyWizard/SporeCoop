// Feature 9 of Space 0.7: the galaxy core together (docs/09-coop-design.md, game knowledge in
// docs/re/space.md section 10). MSG_SPACE kinds 40..49.
//
// What the players get:
//  * every player tells the others how close it is to the galactic core (outside the zone, in the
//    "near the core" zone where the Grox live and the travel range shrinks, at the core's gate =
//    the core star is the active star): the partners get a toast when someone approaches or
//    reaches the gate, and the F9 Space page offers "Fly to <name>" to a partner who is there
//    (only to someone who made it: the trip through the Grox stays a challenge for one of you)
//  * entering the core (zooming into the core star) waits for the partners in the session: the
//    player at the gate is told who is missing, the partners are called; when everybody is at the
//    gate (or the player presses "Enter now" on the Space page) the core opens
//  * when one player enters the core, every partner at the gate enters with it (the native
//    SPG_CenterOfGalaxy cinematic, Steve, the Staff of Life), the others get Steve's gift right
//    away (the game's own "player gets tools" message: the Staff of Life with its charges, the
//    collectible, the event log line) and the achievement "42"; a partner who joins later learns
//    that the core was reached and gets the gift too; nobody gets it twice (the game's own flag)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "space_ext.h"
#include "spacecoop.h"
#include "sporekit_game.hpp"

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;

namespace finale {

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

enum Kind : uint8_t { K_STATE = 40, K_WAIT = 41, K_REACHED = 42 };
// how close a player is to the core
enum Zone : uint8_t { Z_NONE = 0, Z_FAR = 1, Z_NEAR = 2, Z_GATE = 3 };
enum Flag : uint8_t { F_ENTERED = 1, F_STAFF = 2, F_WAITING = 4 };

// game ids (docs/re/space.md 16)
constexpr uint32_t kFlagEnteredCore = 0x0681C28E; // cPlayer flag: SPG_CenterOfGalaxy played (once)
constexpr uint32_t kFlagCoreTools = 0x061F1454;   // cPlayer flag: Steve's gift received (once)
constexpr uint32_t kMsgCoreTools = 0x06526395;    // "player gets tools" (sent by the cinematic)
constexpr uint32_t kAchievement42 = 0xF88C1342;   // achievement "42" (spg-42.prop)
// (while SPG_CenterOfGalaxy plays: cCinematicManager +0x148 = 0x2F7C7BDA, the `cin` of the status)
constexpr uint32_t kToolStaff = 0x72E076A7;       // genesisdevice = the Staff of Life
constexpr uint32_t kToolDrive = 0x2569FAE8;       // interstellardrive
constexpr int kStarCore = 1;                      // cStarRecord +0x48 type: the galactic core

static const SkHost* H;
static SkMod* M;
static uintptr_t a_Enter, a_HasFlag, a_StarMgr, a_Context, a_ActiveStarRec, a_PlayerUFOGet, a_Teleport, a_ModeSpace,
    a_EnterSystem, a_MsgMgr, a_AchMgr, a_AchAdd,
    a_CinematicMgr, a_SpaceGame, a_TimeMgr, a_GetTool;

struct Peer {
    uint8_t zone = Z_NONE, flags = 0;
    uint32_t star = 0; // its active star (StarID)
    uint32_t prev = 0; // the last star before the core (the system next to it)
    float dist = 0;    // from the galaxy centre
    double t = 0;      // last state
    bool told_near = false, told_gate = false, told_entered = false;
};

static std::map<uint8_t, Peer> g_peer;
static std::map<uint8_t, std::string> g_reached_by; // who entered the core in this session
static uint32_t g_core;                              // StarID of the core (0 = not found yet)
static double g_core_scan;
static uint8_t g_zone, g_flags, g_sent_zone = 0xFF, g_sent_flags = 0xFF;
static uint32_t g_star, g_prev; // active star, the last one that was not the core
static float g_dist;
static double g_next_eval, g_next_send;
static bool g_waiting, g_alone, g_go, g_reward_pending;
static std::string g_reward_from;
static uint32_t g_reward_fails;
static uint32_t g_waits, g_entries, g_entries_with, g_rewards, g_reward_skipped, g_flights, g_blocked;
// BUG-0320: the wait at the gate ended because every partner arrived. The tick clears g_waiting at
// that moment, so an entry before the partner's K_REACHED came in was counted as alone.
// `coop.fix space.finale_waited off`: the old count.
static bool g_waited_all;
static bool g_waited_fix = true;
COOP_FIX("space.finale_waited", "BUG-0320", g_waited_fix,
         "space finale: an entry after waiting for everybody counts as together");

// a flight to a partner at the core: galaxy view first (the core is not a solar system), then the
// free instant galaxy travel of the `space -flyto` cheat, then into the system if the partner is there
struct Fly {
    uint32_t star = 0, sys = 0; // target star; in a system: the system to jump to instead (next to the core)
    bool enter = false;
    double next = 0, until = 0;
    int steps = 0;
};
static Fly g_fly;

// ---------------- the game ----------------
// (GetCurrentContext reads SpacePlayerData, which exists only in the Space stage: it faults earlier)
static int context() { return a_Context && spaceext::in_space() ? cdecl_call<int>(a_Context) : -1; }

static void* player()
{
    void* nm = game::noun_manager();
    return nm && !IsBadReadPtr((uint8_t*)nm + 0x74, 4) ? F<void*>(nm, 0x74) : nullptr;
}

static bool has_flag(uint32_t id)
{
    void* p = player();
    return p && a_HasFlag && thiscall<bool>(a_HasFlag, p, id);
}

static void* my_ufo()
{
    void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    return mgr ? F<void*>(mgr, 0x40) : nullptr;
}

static void* active_star_record() { return a_ActiveStarRec ? cdecl_call<void*>(a_ActiveStarRec) : nullptr; }

static bool cinematic_playing(uint32_t* id = nullptr)
{
    void* cm = a_CinematicMgr ? cdecl_call<void*>(a_CinematicMgr) : nullptr;
    if (id)
        *id = cm ? F<uint32_t>(cm, 0x148) : 0;
    return cm && F<uint32_t>(cm, 0x2C) != 0;
}

// a cinematic or caption plays, or the game is paused (cGameTimeManager +0x48 bit 0, the check of
// the native zoom-out 0x10423E0): no view changes then (a switch under a tutorial caption crashed
// the game in the UI later, 2026-10-23)
static bool busy()
{
    if (cinematic_playing())
        return true;
    void* tm = a_TimeMgr ? cdecl_call<void*>(a_TimeMgr) : nullptr;
    return tm && (F<uint8_t>(tm, 0x48) & 1);
}

static void* inventory()
{
    void* sg = a_SpaceGame ? *(void**)a_SpaceGame : nullptr;
    return sg ? F<void*>(sg, 0x40) : nullptr;
}

// charges of a tool in the cargo, -1 = no such tool (cPlayerInventory::GetTool: the item's tool
// part is an embedded base at item+0x0C, Cast without AddRef)
static int tool_charges(uint32_t hash)
{
    void* inv = spaceext::in_space() ? inventory() : nullptr;
    if (!inv || !a_GetTool)
        return -1;
    uint32_t key[3] = {hash, 0, 0};
    if (!vcall<bool>(inv, 0x74 / 4, (const void*)key)) // HasTool (GetTool also finds tools still to unlock)
        return -1;
    void* tool = thiscall<void*>(a_GetTool, inv, (const void*)key);
    return tool ? F<int>(tool, 0x7C) : -1;
}

// the core star: the one star record of type 1 (StarType::GalacticCore), at the galaxy centre
static void* core_record()
{
    if (!spaceext::in_space())
        return nullptr;
    if (g_core)
        if (void* r = spacecoop::star_record(g_core))
            return r;
    g_core = 0;
    double now = H->time_seconds();
    if (now < g_core_scan || !a_StarMgr)
        return nullptr;
    g_core_scan = now + 10; // a full scan of the star table at most every 10 s
    void* sm = cdecl_call<void*>(a_StarMgr);
    if (!sm || IsBadReadPtr((uint8_t*)sm + 0xC8, 8))
        return nullptr;
    uint8_t* b = F<uint8_t*>(sm, 0xC8);
    uint8_t* e = F<uint8_t*>(sm, 0xCC);
    if (!b || e <= b || (e - b) % 20 || IsBadReadPtr(b, e - b))
        return nullptr;
    for (uint8_t* s = b; s < e; s += 20) { // sectors: vector<cStarRecord*> + 8 bytes
        void** rb = *(void***)s;
        void** re = *(void***)(s + 4);
        if (!rb || re <= rb || IsBadReadPtr(rb, (uint8_t*)re - (uint8_t*)rb))
            continue;
        for (void** r = rb; r < re; r++)
            if (*r && !IsBadReadPtr(*r, 0x74) && F<int>(*r, 0x48) == kStarCore) {
                g_core = F<uint32_t>(*r, 0x70);
                H->log(M, SK_INFO, "finale: the galactic core is star %u at (%.1f, %.1f)", g_core, F<float>(*r, 0x3C),
                       F<float>(*r, 0x40));
                return *r;
            }
    }
    return nullptr;
}

// where the player is in galaxy coordinates: the ship in the galaxy view, else its star
static bool galaxy_pos(float out[2])
{
    int ctx = context();
    if (ctx == 2) {
        void* u = my_ufo();
        if (!u)
            return false;
        out[0] = F<float>(u, 0x38);
        out[1] = F<float>(u, 0x3C);
        return true;
    }
    void* r = active_star_record();
    if (!r || IsBadReadPtr(r, 0x48))
        return false;
    out[0] = F<float>(r, 0x3C);
    out[1] = F<float>(r, 0x40);
    return true;
}

// the radius of the "approaching the core" zone (cStarManager +0x1D0: the travel range shrinks
// inside it, the game says "As you approach the galactic core..."), 100 in the tested saves
static float zone_radius()
{
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    float r = sm && !IsBadReadPtr((uint8_t*)sm + 0x1D0, 4) ? F<float>(sm, 0x1D0) : 0.f;
    return r > 1.f && r < 10000.f ? r : 100.f;
}

static void evaluate()
{
    uint8_t zone = Z_NONE, flags = 0;
    uint32_t star = 0;
    float dist = 0;
    int ctx = context();
    if (spaceext::in_space() && ctx >= 0) {
        float p[2];
        void* core = core_record();
        void* act = active_star_record();
        star = act && !IsBadReadPtr(act, 0x74) ? F<uint32_t>(act, 0x70) : 0;
        if (galaxy_pos(p)) {
            float cx = core ? F<float>(core, 0x3C) : 0.f, cy = core ? F<float>(core, 0x40) : 0.f;
            dist = std::sqrt((p[0] - cx) * (p[0] - cx) + (p[1] - cy) * (p[1] - cy));
            zone = dist < zone_radius() ? Z_NEAR : Z_FAR;
        }
        if (core && act == core)
            zone = Z_GATE;
        if (has_flag(kFlagEnteredCore))
            flags |= F_ENTERED;
        if (has_flag(kFlagCoreTools))
            flags |= F_STAFF;
    }
    if (g_waiting)
        flags |= F_WAITING;
    g_zone = zone;
    g_flags = flags;
    g_star = star;
    if (star && star != g_core)
        g_prev = star;
    g_dist = dist;
}

// ---------------- entering the core ----------------
// partners in space who are not at the gate yet (the ones the core waits for)
static std::vector<std::string> missing()
{
    std::vector<std::string> v;
    if (!spaceext::connected() || !spaceext::ctx().partners)
        return v;
    for (auto& p : spaceext::ctx().partners()) {
        auto it = g_peer.find(p.id);
        if (it == g_peer.end() || it->second.zone != Z_GATE)
            v.push_back(p.name);
    }
    return v;
}

static std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (auto& x : v)
        s += (s.empty() ? "" : ", ") + x;
    return s;
}

using Enter_t = void(__cdecl*)();
static Enter_t o_Enter;
static void __cdecl h_Enter()
{
    bool before = has_flag(kFlagEnteredCore);
    if (!before && !g_go && !g_alone) {
        auto miss = missing();
        if (!miss.empty()) {
            g_blocked++;
            if (!g_waiting) { // (the zoom-in asks every frame while it lasts: say it once)
                g_waiting = true;
                g_waits++;
                spaceext::notice(std::string(spaceext::tr("The galactic core waits for your partners: ",
                                                          "Ядро галактики ждёт напарников: ")) +
                                 join(miss) +
                                 spaceext::tr(". Co-op window (F9), Space: \"Enter now\" to go alone.",
                                              ". Окно co-op (F9), Космос: \"Войти сейчас\", чтобы идти одному."));
                spaceext::send(K_WAIT, net::Writer());
            }
            return;
        }
    }
    g_go = false;
    bool with = g_waiting || (g_waited_all && g_waited_fix) || !g_reached_by.empty();
    g_waiting = g_waited_all = false;
    o_Enter();
    if (!before && has_flag(kFlagEnteredCore)) {
        g_entries++;
        if (with)
            g_entries_with++;
        g_reached_by[spaceext::my_id()] = spaceext::name_of(spaceext::my_id());
        H->log(M, SK_INFO, "finale: entered the galactic core");
        spaceext::send(K_REACHED, net::Writer());
        g_next_send = 0; // the new flags go out at once
    }
}

// the core is entered from inside its "solar system" (context 1, active star = the core): the
// system update 0x1009C90 calls 0x1002DD0 every frame while the ship sits in the centre and the
// camera is zoomed in; called directly the cinematic places the ship itself
static double g_enter_at; // a partner entered: our own entry after the system view is up

static bool in_core_system()
{
    void* core = core_record();
    return core && active_star_record() == core && context() == 1;
}

static void enter_now(void*)
{
    if (has_flag(kFlagEnteredCore) || busy() || !in_core_system())
        return;
    g_go = true;
    h_Enter();
    g_go = false;
}

// at the gate in the galaxy view: into the core's system first (the native galaxy -> system)
static void enter_system(void*)
{
    void* mode = a_ModeSpace ? *(void**)a_ModeSpace : nullptr;
    void* core = core_record();
    if (mode && core && active_star_record() == core && context() == 2 && !busy())
        thiscall<void>(a_EnterSystem, mode);
}

// ---------------- Steve's gift for partners ----------------
static void give_reward(void*)
{
    g_reward_pending = false;
    if (!spaceext::in_space() || !player())
        return;
    if (has_flag(kFlagCoreTools)) {
        g_reward_skipped++;
        return;
    }
    void* mm = a_MsgMgr ? cdecl_call<void*>(a_MsgMgr) : nullptr;
    if (!mm)
        return;
    // the cinematic's own message: the player data gives the core tools (the Staff of Life with its
    // charges), marks the collectible, writes the event log line and sets the flag (docs/re/space.md 16)
    vcall<void>(mm, 0x14 / 4, kMsgCoreTools, (void*)nullptr, (void*)nullptr); // MessageSend
    if (a_AchMgr && a_AchAdd)
        if (void* am = cdecl_call<void*>(a_AchMgr))
            thiscall<void>(a_AchAdd, am, kAchievement42, 1);
    bool ok = has_flag(kFlagCoreTools);
    if (ok)
        g_rewards++;
    else
        g_reward_fails++;
    H->log(M, ok ? SK_INFO : SK_WARN, "finale: Steve's gift from %s: %s (staff charges %d)", g_reward_from.c_str(),
           ok ? "given" : "the game did not take the message", tool_charges(kToolStaff));
    if (ok)
        spaceext::notice(g_reward_from +
                         spaceext::tr(" reached the galactic core: Steve's gift is yours too, the Staff of Life is in your cargo!",
                                      " достиг(ла) ядра галактики: дар Стива и ваш, Посох жизни в вашем трюме!"));
    g_next_send = 0;
}

static void schedule_reward(const std::string& from)
{
    if (g_reward_pending || has_flag(kFlagCoreTools) || g_reward_fails >= 3) // (not taken 3 times: give up, logged)
        return;
    g_reward_pending = true;
    g_reward_from = from;
    H->run_outside_frame(give_reward, nullptr);
}

// a partner entered the core: enter with it when at the gate, else take the gift
static void partner_entered(uint8_t id, bool live)
{
    std::string who = spaceext::name_of(id);
    g_reached_by[id] = who;
    if (has_flag(kFlagEnteredCore))
        return;
    void* core = core_record();
    if (live && core && active_star_record() == core && context() >= 1) {
        spaceext::notice(who + spaceext::tr(" enters the galactic core: together!", " входит в ядро галактики: вместе!"));
        if (context() == 2 && !g_fly.star) // (a flight to the gate enters the system itself)
            H->run_outside_frame(enter_system, nullptr);
        g_enter_at = H->time_seconds() + (context() == 2 ? 4.0 : 0.0);
        return;
    }
    if (live)
        spaceext::notice(who + spaceext::tr(" entered the galactic core!", " вошёл(ла) в ядро галактики!"));
    // Steve's gift follows from reward_tick
}

// Steve's gift for whoever lacks it: a partner reached the core, or the own save says the core was
// entered (a guest's copy of the host's save) without the gift. Checked again and again: a world
// load (a rejoin) replaces the game state, the gift of the old world is gone
static double g_reward_next;
static void reward_tick(double now)
{
    if (now < g_reward_next || g_reward_pending || !spaceext::in_space() || g_flags & F_STAFF || busy() ||
        context() < 0 || g_enter_at || g_waiting)
        return;
    g_reward_next = now + 3;
    std::string from;
    for (auto& [id, n] : g_reached_by)
        if (id != spaceext::my_id())
            from = n;
    if (from.empty() && g_flags & F_ENTERED && !g_entries)
        from = spaceext::tr("Your crew", "Ваш экипаж");
    if (!from.empty())
        schedule_reward(from);
}

// ---------------- flying to a partner at the core ----------------
static bool has_drive() { return tool_charges(kToolDrive) >= 0; }

static void fly_step(void*)
{
    if (!g_fly.star)
        return;
    if (busy()) {
        g_fly.next = H->time_seconds() + 1;
        return;
    }
    void* rec = spacecoop::star_record(g_fly.star);
    void* mode = a_ModeSpace ? *(void**)a_ModeSpace : nullptr;
    int ctx = context();
    if (!rec || !mode || ctx < 0 || ++g_fly.steps > 4 || (ctx == 1 && active_star_record() == rec)) {
        g_fly = Fly(); // (in the target system already, e.g. entered by the joint entry)
        return;
    }
    if (ctx == 2) { // the galaxy view: the free instant travel of the `space -flyto` cheat
        if (active_star_record() != rec) {
            cdecl_call<void>(a_Teleport, rec);
            g_flights++;
            if (g_fly.enter) {
                g_fly.next = H->time_seconds() + 1.5;
                return;
            }
        } else if (g_fly.enter)
            thiscall<void>(a_EnterSystem, mode);
        g_fly = Fly();
        return;
    }
    // in a system or on a planet: the jump of the Space page (spacecoop: the native sequence of the
    // respawn) into the partner's system, or for a partner at the gate into the system it came
    // from, next to the core (the core is no solar system); a system -> galaxy switch by the mod
    // (0xFDA5D0 outside the game's zoom update) broke a later caption (docs/re/space.md 16.4)
    uint32_t sys = g_fly.sys ? g_fly.sys : g_fly.star;
    void* sysrec = spacecoop::star_record(sys);
    if (sysrec && active_star_record() != sysrec) {
        std::string e = spacecoop::travel_to_star(sys);
        if (e.empty())
            g_flights++;
        else
            spaceext::notice(std::string(spaceext::tr("Can't fly there: ", "Не долететь: ")) + e);
    }
    if (g_fly.sys)
        spaceext::notice(spaceext::tr("The galactic core is close: zoom out to the galaxy view and fly into it "
                                      "(or \"Fly to\" again there).",
                                      "Ядро галактики близко: отдалите камеру до карты галактики и летите в него "
                                      "(или снова \"Лететь к\" там)."));
    g_fly = Fly();
}

static std::string fly_to_star(uint32_t star, bool enter, uint32_t sys = 0)
{
    if (!spaceext::in_space())
        return "not in space";
    if (!spacecoop::star_record(star))
        return "no such star";
    if (!has_drive())
        return spaceext::tr("you need the interstellar drive", "нужен межзвёздный двигатель");
    if (spacecoop::disabled())
        return "the ship is disabled";
    if (context() != 2 && star == g_core && !spacecoop::star_record(sys))
        return spaceext::tr("zoom out to the galaxy view first", "сначала отдалите камеру до карты галактики");
    g_fly = Fly();
    g_fly.star = star;
    g_fly.sys = sys;
    g_fly.enter = enter;
    g_fly.next = 0;
    g_fly.until = H->time_seconds() + 30;
    return "";
}

static std::string fly_to(uint8_t id)
{
    auto it = g_peer.find(id);
    if (it == g_peer.end() || it->second.zone < Z_NEAR || !it->second.star)
        return spaceext::tr("the partner is not near the core", "напарник не у ядра");
    bool in_system = false;
    if (spaceext::ctx().partners)
        for (auto& p : spaceext::ctx().partners())
            if (p.id == id)
                in_system = (p.place & 0x80000000u) != 0;
    bool gate = it->second.zone == Z_GATE;
    return fly_to_star(it->second.star, in_system || gate, gate ? it->second.prev : 0);
}

static void fly_tick(double now)
{
    if (!g_fly.star)
        return;
    if (now > g_fly.until) {
        g_fly = Fly();
        return;
    }
    if (now < g_fly.next || busy())
        return;
    void* u = my_ufo();
    if (u && F<uint32_t>(u, 0x7F0)) // the death cinematic of the own ship
        return;
    g_fly.next = now + 10; // (the step sets the real next time)
    H->run_outside_frame(fly_step, nullptr);
}

// ---------------- network ----------------
static void send_state()
{
    net::Writer w;
    w.u8(g_zone);
    w.u8(g_flags);
    w.u32(g_star);
    w.u32(g_prev);
    w.f32(g_dist);
    spaceext::send(K_STATE, w);
    g_sent_zone = g_zone;
    g_sent_flags = g_flags;
}

static void on_message(uint8_t kind, uint8_t from, net::Reader& r)
{
    std::string who = spaceext::name_of(from);
    if (kind == K_STATE) {
        Peer& p = g_peer[from];
        uint8_t zone = r.u8(), flags = r.u8();
        uint32_t star = r.u32(), prev = r.u32();
        float dist = r.f32();
        if (!r.ok())
            return;
        bool first = p.t == 0;
        p.zone = zone;
        p.flags = flags;
        p.star = star;
        p.prev = prev;
        p.dist = dist;
        p.t = H->time_seconds();
        if (zone >= Z_NEAR && !p.told_near && zone != Z_GATE) {
            p.told_near = true;
            spaceext::notice(who + spaceext::tr(" approaches the galactic core, Grox space! Fly there: co-op window (F9), Space.",
                                                " подлетает к ядру галактики, космос гроксов! Лететь туда: окно co-op (F9), Космос."));
        }
        if (zone == Z_GATE && !p.told_gate && !(flags & F_ENTERED)) {
            p.told_gate = p.told_near = true;
            spaceext::notice(who + spaceext::tr(" is at the gate of the galactic core!", " у врат ядра галактики!"));
        }
        if (zone < Z_NEAR)
            p.told_near = p.told_gate = false;
        // a partner who entered the core before we joined (or we missed K_REACHED). A guest joins
        // with a copy of the host's save and inherits the host's flag: the host does not count a
        // guest with the flag while it has the flag itself
        if ((flags & F_ENTERED) && !p.told_entered && !has_flag(kFlagEnteredCore)) {
            p.told_entered = true;
            g_reached_by[from] = who;
            if (first && !has_flag(kFlagCoreTools))
                spaceext::notice(who + spaceext::tr(" has reached the galactic core.", " уже достиг(ла) ядра галактики."));
        }
        return;
    }
    if (kind == K_WAIT) {
        g_peer[from].flags |= F_WAITING;
        if (!has_flag(kFlagEnteredCore))
            spaceext::notice(who + spaceext::tr(" waits for you at the galactic core: \"Fly to\" on the Space page (F9).",
                                                " ждёт вас у ядра галактики: \"Лететь к\" на странице Космос (F9)."));
        return;
    }
    if (kind == K_REACHED) {
        Peer& p = g_peer[from];
        p.flags |= F_ENTERED;
        p.told_entered = true;
        partner_entered(from, true);
    }
}

// ---------------- frame ----------------
static void tick(double now)
{
    if (now < g_next_eval)
        return;
    g_next_eval = now + 0.5;
    evaluate();
    if (spaceext::connected() && (g_zone != g_sent_zone || g_flags != g_sent_flags || now >= g_next_send)) {
        send_state();
        g_next_send = now + 3;
    }
    // waiting at the gate: open when everybody is there (or "Enter now"), stop when we left
    if (g_waiting) {
        void* core = core_record();
        if (!core || active_star_record() != core || g_flags & F_ENTERED) {
            g_waiting = g_waited_all = false;
        } else if ((g_alone || missing().empty()) && !busy()) {
            g_enter_at = now; // everybody is here (or "Enter now"): open it
            g_waited_all = !g_alone;
            g_waiting = false;
        }
    }
    if (g_enter_at && now >= g_enter_at) {
        if (has_flag(kFlagEnteredCore) || active_star_record() != core_record() || now > g_enter_at + 30)
            g_enter_at = 0; // done, left, or gave up
        else if (in_core_system() && !busy()) {
            g_enter_at = 0;
            H->run_outside_frame(enter_now, nullptr);
        }
    }
    // partners who left the session
    if (spaceext::connected() && spaceext::ctx().partners) {
        auto ps = spaceext::ctx().partners();
        for (auto it = g_peer.begin(); it != g_peer.end();)
            if (now - it->second.t > 15 && std::none_of(ps.begin(), ps.end(), [&](auto& p) { return p.id == it->first; }))
                it = g_peer.erase(it);
            else
                ++it;
    }
    reward_tick(now);
    fly_tick(now);
}

static void session_end()
{
    g_peer.clear();
    g_reached_by.clear();
    g_waiting = g_alone = g_go = g_waited_all = false;
    g_enter_at = 0;
    g_reward_next = 0;
    g_reward_fails = 0;
    g_fly = Fly();
    g_sent_zone = g_sent_flags = 0xFF;
}

// ---------------- the Space page ----------------
static std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> v;
    if (!spaceext::in_space() || !spaceext::connected())
        return v;
    using spaceext::tr;
    spaceext::Row head;
    if (g_flags & F_ENTERED || !g_reached_by.empty()) {
        std::vector<std::string> who;
        for (auto& [id, n] : g_reached_by)
            who.push_back(n);
        head.text = std::string(tr("Galactic core: reached", "Ядро галактики: достигнуто")) +
                    (who.empty() ? "" : std::string(tr(" by ", ": ")) + join(who)) +
                    (g_flags & F_STAFF ? tr(", you have the Staff of Life", ", Посох жизни у вас") : "");
        head.color = 0xFF60FF60;
    } else if (g_zone == Z_GATE)
        head.text = tr("Galactic core: you are at its gate", "Ядро галактики: вы у врат");
    else if (g_zone == Z_NEAR)
        head.text = tr("Galactic core: close, in Grox space", "Ядро галактики: близко, космос гроксов");
    else {
        // far from the core and nobody near it: no row (the page stays short in normal play)
        bool close_by = std::any_of(g_peer.begin(), g_peer.end(), [](auto& kv) { return kv.second.zone >= Z_NEAR; });
        if (!close_by)
            return v;
        head.text = tr("Galactic core: not reached yet", "Ядро галактики: ещё не достигнуто");
    }
    v.push_back(head);
    if (g_waiting) {
        spaceext::Row w;
        w.text = std::string(tr("The core waits for: ", "Ядро ждёт: ")) + join(missing());
        w.button = tr("Enter now", "Войти сейчас");
        w.color = 0xFFFFD040;
        w.on_click = [] {
            g_alone = true;
            g_enter_at = H->time_seconds();
        };
        v.push_back(w);
    }
    for (auto& [id, p] : g_peer) {
        if (p.zone < Z_NEAR)
            continue;
        spaceext::Row r;
        std::string n = spaceext::name_of(id);
        if (p.zone == Z_GATE)
            r.text = n + (p.flags & F_WAITING ? tr(": at the core's gate, waiting for you", ": у врат ядра, ждёт вас")
                                              : tr(": at the gate of the galactic core", ": у врат ядра галактики"));
        else
            r.text = n + tr(": near the galactic core", ": рядом с ядром галактики");
        if (p.zone == Z_GATE)
            r.color = 0xFFFFD040;
        bool there = g_zone == Z_GATE && p.zone == Z_GATE;
        if (!there && p.star && p.star != g_star && !(g_flags & F_ENTERED && p.zone == Z_GATE)) {
            r.button = std::string(tr("Fly to ", "Лететь к ")) + n;
            uint8_t pid = id;
            r.on_click = [pid] {
                std::string e = fly_to(pid);
                if (!e.empty())
                    spaceext::notice(std::string(spaceext::tr("Can't fly there: ", "Не долететь: ")) + e);
            };
        }
        v.push_back(std::move(r));
    }
    return v;
}

static std::string status()
{
    char b[512];
    std::string peers;
    for (auto& [id, p] : g_peer) {
        char x[64];
        snprintf(x, sizeof x, " %u:z%u/f%u/s%u", id, p.zone, p.flags, p.star);
        peers += x;
    }
    std::string who;
    for (auto& [id, n] : g_reached_by)
        who += (who.empty() ? "" : ",") + n;
    uint32_t cin = 0;
    if (!cinematic_playing(&cin))
        cin = 0;
    snprintf(b, sizeof b,
             "finale: core=%u ctx=%d busy=%d cin=%08X zone=%u dist=%.1f star=%u entered=%d staff=%d charges=%d waiting=%d alone=%d "
             "reached_by=%s waits=%u blocked=%u entries=%u entries_with=%u rewards=%u reward_skipped=%u flights=%u "
             "flying=%u peers=[%s ]",
             g_core, context(), busy(), cin, g_zone, g_dist, g_star, (g_flags & F_ENTERED) != 0, (g_flags & F_STAFF) != 0,
             tool_charges(kToolStaff), g_waiting, g_alone, who.c_str(), g_waits, g_blocked, g_entries, g_entries_with,
             g_rewards, g_reward_skipped, g_flights, g_fly.star, peers.c_str());
    return b;
}

// a star at about `d` from the core, the nearest one to the own position (tests: "near")
static uint32_t star_at_distance(float d)
{
    void* core = core_record();
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    float p[2];
    if (!core || !sm || !galaxy_pos(p))
        return 0;
    float cx = F<float>(core, 0x3C), cy = F<float>(core, 0x40);
    uint8_t* b = F<uint8_t*>(sm, 0xC8);
    uint8_t* e = F<uint8_t*>(sm, 0xCC);
    uint32_t best = 0;
    float bestd = 1e30f;
    for (uint8_t* s = b; s && s < e; s += 20) {
        void** rb = *(void***)s;
        void** re = *(void***)(s + 4);
        for (void** r = rb; rb && r < re; r++) {
            if (!*r || F<int>(*r, 0x48) == kStarCore)
                continue;
            float x = F<float>(*r, 0x3C), y = F<float>(*r, 0x40);
            float dc = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            if (std::fabs(dc - d) > 2.f)
                continue;
            float dm = (x - p[0]) * (x - p[0]) + (y - p[1]) * (y - p[1]);
            if (dm < bestd) {
                bestd = dm;
                best = F<uint32_t>(*r, 0x70);
            }
        }
    }
    return best;
}

// coop.space finale [core | goto core|near|<StarID> [enter] | enter | alone | reward]
static std::string debug(const std::vector<std::string>& a)
{
    if (a.empty())
        return status();
    if (a[0] == "core") {
        void* c = core_record();
        char b[160];
        snprintf(b, sizeof b, "core %u at (%.2f, %.2f) radius %.1f context %d active %u", g_core,
                 c ? F<float>(c, 0x3C) : 0.f, c ? F<float>(c, 0x40) : 0.f, zone_radius(), context(),
                 active_star_record() ? F<uint32_t>(active_star_record(), 0x70) : 0u);
        return b;
    }
    if (a[0] == "goto" && a.size() > 1) { // tests: fly like "Fly to" does, to the core or a star near it
        uint32_t s = a[1] == "core" ? (core_record() ? g_core : 0)
                     : a[1] == "near" ? star_at_distance(zone_radius() * 0.6f)
                                      : (uint32_t)strtoul(a[1].c_str(), nullptr, 0);
        if (!s)
            return "no star";
        std::string e = fly_to_star(s, a.size() > 2 && a[2] == "enter");
        return e.empty() ? "flying to " + std::to_string(s) : e;
    }
    if (a[0] == "enter") { // tests: what the zoom-in at the core's centre does (goes through the wait)
        if (!in_core_system())
            return "not in the core's system (goto core enter first)";
        H->run_outside_frame([](void*) { h_Enter(); }, nullptr);
        return "entering";
    }
    if (a[0] == "alone") {
        g_alone = true;
        return "the core does not wait";
    }
    if (a[0] == "reward") { // tests: Steve's gift without the trip
        g_reward_pending = false;
        schedule_reward("test");
        return "reward";
    }
    return "usage: coop.space finale [core | goto core|near|<star> [enter] | enter | alone | reward]";
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    auto S = [](const char* n) { return H->sym(n); };
    a_Enter = S("SporeCoop::Space_EnterGalacticCore");
    a_HasFlag = S("Simulator::cPlayer::HasFlag");
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_Context = S("SporeCoop::Space_GetCurrentContext");
    a_ActiveStarRec = S("Simulator::GetActiveStarRecord");
    a_PlayerUFOGet = S("Simulator::cSimulatorPlayerUFO::Get");
    a_Teleport = S("Simulator::SpaceTeleportTo");
    a_ModeSpace = S("Simulator::cAppModeSpace::ptr");
    a_EnterSystem = S("Simulator::cAppModeSpace::EnterSolarSystem");
    a_MsgMgr = S("App::IMessageManager::Get");
    a_AchMgr = S("Pollinator::cAchievementsManager::Get");
    a_AchAdd = S("Pollinator::cAchievementsManager::AddProgress");
    a_CinematicMgr = S("Simulator::cCinematicManager::Get");
    a_SpaceGame = S("Simulator::cSimulatorSpaceGame::ptr");
    a_TimeMgr = S("Simulator::cGameTimeManager::Get");
    a_GetTool = S("Simulator::cPlayerInventory::GetTool");
    if (!a_Enter || !a_HasFlag || !a_StarMgr || !a_Context || !a_ActiveStarRec || !a_PlayerUFOGet || !a_Teleport ||
        !a_ModeSpace || !a_EnterSystem || !a_MsgMgr || !a_SpaceGame)
        return false;
    return H->hook_create(M, (void*)a_Enter, (void*)h_Enter, (void**)&o_Enter) == 0;
}

static bool reg = spaceext::add({.name = "finale",
                                 .kind_lo = 40,
                                 .kind_hi = 49,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = session_end,
                                 .rows = rows,
                                 .status = status,
                                 .debug = debug});

} // namespace finale
