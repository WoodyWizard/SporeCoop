// Space feature "map" (MSG_SPACE kinds 10..19): the roster of every player's empire (space_map.h)
// and the partners and their colonies on the galaxy map. docs/re/space.md 13.
#include "space_map.h"
#include "player_life.h"

#include "net/utf8.h"

#include <windows.h>

#include <algorithm>
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "game.h"
#include "modes.h"
#include "mouse.h"
#include "space_ext.h"
#include "spacecoop.h"
#include "sporekit_game.hpp"
#include "cell_logic.h"
#include "find.h"
#include "fixes.h"

using skg::cdecl_call;
using skg::thiscall;

namespace spacemap {

static const SkHost* H;
static SkMod* M;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

enum Kind : uint8_t {
    K_ROSTER = 10,     // u32 version, u8 part, u8 parts, [part 0: head], u8 n, n colonies
    K_ROSTER_REQ = 11, // (nothing): everybody sends its roster now (a player joined / rejoined)
    K_KEPT = 12,       // host -> guest (BUG-0195): u8 target id, u8 n, n x u32 PlanetID: your own colonies, kept
    K_KEPT_DONE = 13,  // guest -> host: u8 n, n x u32 PlanetID it could not make its own (dropped by the host)
};

// packets are at most 1200 bytes (kind, origin and the session header come on top)
constexpr size_t kChunkBytes = 1000;
constexpr double kPeriod = 5.0, kScan = 1.0;

static uintptr_t a_PlayerEmpire, a_StarMgr, a_PlayerUFOGet;

// ---------------- reading the game ----------------
static bool readable(const void* p, size_t n) { return p && !IsBadReadPtr(p, n); }

// eastl::string16 {begin, end, capacity} -> UTF-8
static std::string str16(void* s)
{
    if (!readable(s, 12))
        return "";
    const char16_t* b = F<const char16_t*>(s, 0);
    const char16_t* e = F<const char16_t*>(s, 4);
    if (!b || e <= b || e - b > 256 || !readable(b, (e - b) * 2))
        return "";
    std::string out;
    for (const char16_t* c = b; c < e; c++) {
        uint32_t u = *c;
        if (u >= 0xD800 && u < 0xDC00 && c + 1 < e) { // surrogate pair
            u = 0x10000 + ((u - 0xD800) << 10) + (c[1] - 0xDC00);
            c++;
        }
        if (u < 0x80)
            out += (char)u;
        else if (u < 0x800) {
            out += (char)(0xC0 | u >> 6);
            out += (char)(0x80 | (u & 0x3F));
        } else if (u < 0x10000) {
            out += (char)(0xE0 | u >> 12);
            out += (char)(0x80 | ((u >> 6) & 0x3F));
            out += (char)(0x80 | (u & 0x3F));
        } else {
            out += (char)(0xF0 | u >> 18);
            out += (char)(0x80 | ((u >> 12) & 0x3F));
            out += (char)(0x80 | ((u >> 6) & 0x3F));
            out += (char)(0x80 | (u & 0x3F));
        }
    }
    return out;
}

// eastl::vector<T*> {begin, end, capacity}: the pointers, empty if not readable
static std::vector<void*> ptr_vec(void* v, size_t max = 4096)
{
    std::vector<void*> out;
    if (!readable(v, 8))
        return out;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    if (!b || e <= b || (size_t)(e - b) > max || !readable(b, (e - b) * 4))
        return out;
    out.assign(b, e);
    return out;
}

static void* player_empire() { return a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr; }

// (ids come from the network: the game's own GetStarRecord faults on an id that is no star)
static void* star_record(uint32_t id)
{
    void* r = spacecoop::star_record(id);
    return readable(r, 0xB0) ? r : nullptr;
}

std::string star_name(uint32_t star)
{
    void* r = star_record(star);
    return r ? str16((uint8_t*)r + 0x60) : ""; // cStarRecord::mName
}

static uint32_t star_of(uint32_t place)
{
    if (place & 0x80000000u)
        return place & 0x7FFFFFFFu;
    if (place & 0x40000000u)
        return place & 0xFFFFFFu; // PlanetID: star in the low 24 bits
    return 0;
}

// the six spices of the game: prop files spice1..spice6 (group SpaceTrading 0x034D97FA, instance =
// FNV-1 of the name; their text "Red Spice" ..., Spore_Game.package / PatchData.package)
struct Spice {
    uint32_t key;
    const char *en, *ru;
};
static const Spice kSpices[] = {
    {0x1CA01562u, "Red Spice", "Красная специя"},     {0x1CA01561u, "Blue Spice", "Синяя специя"},
    {0x1CA01560u, "Yellow Spice", "Жёлтая специя"},   {0x1CA01567u, "Green Spice", "Зелёная специя"},
    {0x1CA01566u, "Pink Spice", "Розовая специя"},    {0x1CA01565u, "Purple Spice", "Фиолетовая специя"},
};
static const Spice* spice_of(uint32_t key)
{
    for (auto& s : kSpices)
        if (s.key == key)
            return &s;
    return nullptr;
}

// cEmpire (ModAPI layout, checked at runtime, docs/re/space.md 13.1): cIdentityColorable at +0x0C
// (mIDColorID +0x10, mCachedColor ColorRGB floats +0x30), mEmpireName string16 +0x3C, mPoliticalID
// +0x84, mStars vector<cStarRecord*> +0x88, mHomeStar +0xB0, mHomePlanet +0xB4, money +0xD0.
// cStarRecord: position +0x3C, mEmpireID +0x54, mName +0x60, StarID +0x70, mPlanets +0x84.
// cPlanetRecord: mName +0x18, mCivData +0x15C (cCivData: pol +0x14, mCities vector +0x3C),
// PlanetID +0x184, tech level +0x194 (5 = empire colony), spice key +0x198.
static bool read_local(Roster& r)
{
    void* emp = player_empire();
    if (!readable(emp, 0xD4))
        return false;
    r.political_id = F<uint32_t>(emp, 0x84);
    r.empire_name = str16((uint8_t*)emp + 0x3C);
    const float* c = &F<float>(emp, 0x30);
    if (c[0] >= 0 && c[0] <= 1.001f && c[1] >= 0 && c[1] <= 1.001f && c[2] >= 0 && c[2] <= 1.001f &&
        c[0] + c[1] + c[2] > 0.01f)
        r.color = 0xFF000000u | (uint32_t)(c[0] * 255.f + .5f) << 16 | (uint32_t)(c[1] * 255.f + .5f) << 8 |
                  (uint32_t)(c[2] * 255.f + .5f);
    r.home_star = F<uint32_t>(emp, 0xB0);
    r.home_planet = F<uint32_t>(emp, 0xB4);
    if (r.home_star == 0xFFFFFFFFu)
        r.home_star = 0;
    if (r.home_planet == 0xFFFFFFFFu)
        r.home_planet = 0;
    r.money = F<int>(emp, 0xD0);
    r.place = spaceext::ctx().my_place ? spaceext::ctx().my_place() : 0;
    r.star_now = star_of(r.place);
    if (r.star_now) {
        r.star_now_name = star_name(r.star_now);
        if (void* s = star_record(r.star_now)) {
            auto& q = F<float[3]>(s, 0x3C);
            r.galaxy_pos = {q[0], q[1], q[2]};
        }
    } else if (void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr) {
        void* u = F<void*>(mgr, 0x40);
        if (readable(u, 0x48)) {
            auto& q = F<float[3]>(u, 0x38);
            r.galaxy_pos = {q[0], q[1], q[2]};
        }
    }
    r.colonies.clear();
    for (void* s : ptr_vec((uint8_t*)emp + 0x88, 1024)) {
        if (!readable(s, 0x98))
            continue;
        for (void* p : ptr_vec((uint8_t*)s + 0x84, 64)) {
            if (!readable(p, 0x1A4) || F<int>(p, 0x194) != 5)
                continue;
            Colony col;
            col.planet = F<uint32_t>(p, 0x184);
            col.star = col.planet & 0xFFFFFFu;
            if (spaceext::allies_own_mirror(col.planet))
                continue; // the partner's colony, shown as one of mine (BUG-0197)
            bool mine = F<uint32_t>(s, 0x54) == r.political_id;
            for (void* cd : ptr_vec((uint8_t*)p + 0x15C, 32))
                if (readable(cd, 0x48) && F<uint32_t>(cd, 0x14) == r.political_id) {
                    mine = true;
                    void** b = F<void**>(cd, 0x3C);
                    void** e = F<void**>(cd, 0x40);
                    if (b && e > b && e - b < 1000)
                        col.cities = (uint16_t)(col.cities + (e - b));
                }
            if (!mine)
                continue;
            col.name = str16((uint8_t*)p + 0x18);
            col.star_name = str16((uint8_t*)s + 0x60);
            memcpy(col.spice, (uint8_t*)p + 0x198, 12);
            if (const Spice* sp = spice_of(col.spice[0]))
                col.spice_name = sp->en;
            col.home = col.planet == r.home_planet;
            r.colonies.push_back(std::move(col));
            if (r.colonies.size() >= 250)
                break;
        }
    }
    return true;
}

// ---------------- the rosters ----------------
static std::map<uint8_t, Roster> g_rosters;
static uint32_t g_changes;
static std::vector<std::function<void(uint8_t)>> g_listeners;

struct Pending {
    uint32_t version = 0;
    uint8_t parts = 0;
    std::set<uint8_t> got;
    Roster r;
};
static std::map<uint8_t, Pending> g_pending;

static std::vector<uint8_t> g_last_bytes; // my roster's content as last sent (change detection)
static uint32_t g_version;
static double g_next_scan, g_last_send;
static bool g_force_send, g_requested;
static std::set<uint8_t> g_players; // session players seen (a new one gets everybody's roster)
static uint32_t g_sent, g_got, g_reqs;

const Roster* roster_of(uint8_t id)
{
    auto it = g_rosters.find(id);
    return it == g_rosters.end() ? nullptr : &it->second;
}

std::vector<uint8_t> roster_ids()
{
    std::vector<uint8_t> v;
    uint8_t me = spaceext::my_id();
    if (g_rosters.count(me))
        v.push_back(me);
    for (auto& [id, r] : g_rosters)
        if (id != me)
            v.push_back(id);
    return v;
}

uint32_t roster_changes() { return g_changes; }
void on_roster_change(std::function<void(uint8_t)> f) { g_listeners.push_back(std::move(f)); }

static void changed(uint8_t id)
{
    g_changes++;
    for (auto& f : g_listeners)
        f(id);
}

static void put_colony(net::Writer& w, const Colony& c)
{
    w.u32(c.planet);
    w.str8(c.name.substr(0, 64));
    w.str8(c.star_name.substr(0, 64));
    for (uint32_t k : c.spice)
        w.u32(k);
    w.str8(c.spice_name.substr(0, 32));
    w.u16(c.cities);
    w.u8(c.home ? 1 : 0);
}

static Colony get_colony(net::Reader& r)
{
    Colony c;
    c.planet = r.u32();
    c.star = c.planet & 0xFFFFFFu;
    c.name = r.str8();
    c.star_name = r.str8();
    for (uint32_t& k : c.spice)
        k = r.u32();
    c.spice_name = r.str8();
    c.cities = r.u16();
    c.home = r.u8() & 1;
    return c;
}

static void put_head(net::Writer& w, const Roster& r, bool with_pos)
{
    w.u32(r.home_star);
    w.u32(r.home_planet);
    w.u32(r.place);
    w.f32(with_pos ? r.galaxy_pos.x : 0.f);
    w.f32(with_pos ? r.galaxy_pos.y : 0.f);
    w.f32(with_pos ? r.galaxy_pos.z : 0.f);
    w.str8(r.empire_name.substr(0, 64));
    w.str8(r.star_now_name.substr(0, 64));
    w.u32(r.color);
    w.u32(r.political_id);
    w.u32((uint32_t)r.money);
}

static void get_head(net::Reader& rd, Roster& r)
{
    r.home_star = rd.u32();
    r.home_planet = rd.u32();
    r.place = rd.u32();
    r.star_now = star_of(r.place);
    r.galaxy_pos.x = rd.f32();
    r.galaxy_pos.y = rd.f32();
    r.galaxy_pos.z = rd.f32();
    r.empire_name = rd.str8();
    r.star_now_name = rd.str8();
    r.color = rd.u32();
    r.political_id = rd.u32();
    r.money = (int)rd.u32();
}

// what counts as a change: everything but the ship's galaxy position (it moves while flying) and
// the money (it changes all the time); those go out with the periodic resend
static std::vector<uint8_t> content(const Roster& r)
{
    net::Writer w;
    Roster c = r;
    c.money = 0;
    put_head(w, c, false);
    for (auto& col : r.colonies)
        put_colony(w, col);
    return w.data;
}

static void send_roster(const Roster& r)
{
    // part 0: the head and the first colonies; then as many parts as the colonies need
    std::vector<net::Writer> parts;
    size_t i = 0;
    do {
        net::Writer body;
        if (parts.empty())
            put_head(body, r, true);
        net::Writer cols;
        uint8_t n = 0;
        while (i < r.colonies.size() && n < 255) {
            net::Writer one;
            put_colony(one, r.colonies[i]);
            if (body.size() + cols.size() + one.size() + 8 > kChunkBytes && n > 0)
                break;
            cols.raw(one.data.data(), one.size());
            n++;
            i++;
        }
        body.u8(n);
        body.raw(cols.data.data(), cols.size());
        parts.push_back(std::move(body));
    } while (i < r.colonies.size() && parts.size() < 255);
    for (size_t p = 0; p < parts.size(); p++) {
        net::Writer w;
        w.u32(r.version);
        w.u8((uint8_t)p);
        w.u8((uint8_t)parts.size());
        w.raw(parts[p].data.data(), parts[p].size());
        spaceext::send(K_ROSTER, w);
    }
    g_sent++;
}

static void kept_roster(uint8_t id);
static void on_roster(uint8_t origin, net::Reader& rd)
{
    uint32_t version = rd.u32();
    uint8_t part = rd.u8(), parts = rd.u8();
    if (!rd.ok() || !parts || part >= parts)
        return;
    Pending& pd = g_pending[origin];
    if (pd.version != version || pd.parts != parts) {
        pd = Pending{};
        pd.version = version;
        pd.parts = parts;
        pd.r.id = origin;
        pd.r.version = version;
    }
    if (pd.got.count(part))
        return; // this part again: its colonies are in already
    if (part == 0)
        get_head(rd, pd.r);
    uint8_t n = rd.u8();
    for (uint8_t k = 0; k < n && rd.ok(); k++) {
        Colony c = get_colony(rd);
        if (rd.ok())
            pd.r.colonies.push_back(std::move(c));
    }
    if (!rd.ok()) {
        g_pending.erase(origin);
        return;
    }
    pd.got.insert(part);
    if (pd.got.size() < parts)
        return;
    Roster r = std::move(pd.r);
    g_pending.erase(origin);
    r.updated = H->time_seconds();
    auto old = g_rosters.find(origin);
    bool diff = old == g_rosters.end() || content(old->second) != content(r);
    g_rosters[origin] = std::move(r);
    g_got++;
    kept_roster(origin);
    if (diff)
        changed(origin);
}

static void roster_tick(double now)
{
    if (!spaceext::connected())
        return;
    // players: drop the rosters of those who left, send mine at once when somebody new is there
    std::set<uint8_t> ids;
    for (auto& p : spaceext::ctx().sess->players())
        ids.insert(p.id);
    for (auto it = g_rosters.begin(); it != g_rosters.end();)
        if (!ids.count(it->first)) {
            uint8_t id = it->first;
            it = g_rosters.erase(it);
            g_pending.erase(id);
            changed(id);
        } else
            ++it;
    for (uint8_t id : ids)
        if (!g_players.count(id) && id != spaceext::my_id())
            g_force_send = true;
    g_players = ids;
    if (!spaceext::in_space())
        return;
    if (!g_requested) { // (re)joined or entered space: ask the others for theirs
        g_requested = true;
        spaceext::send(K_ROSTER_REQ, net::Writer{});
        g_reqs++;
    }
    if (now < g_next_scan && !g_force_send)
        return;
    g_next_scan = now + kScan;
    Roster r;
    r.id = spaceext::my_id();
    if (!read_local(r))
        return;
    std::vector<uint8_t> bytes = content(r);
    bool diff = bytes != g_last_bytes;
    if (diff) {
        g_last_bytes = std::move(bytes);
        g_version++;
    }
    r.version = g_version;
    r.updated = now;
    bool had = g_rosters.count(r.id) != 0;
    g_rosters[r.id] = r;
    if (diff || !had)
        changed(r.id);
    if (diff || g_force_send || now - g_last_send >= kPeriod) {
        g_force_send = false;
        g_last_send = now;
        send_roster(r);
    }
}

// BUG-0195 (kept colonies, below): the guest's side
static std::vector<uint32_t> g_restore;    // guest: the host's list, still to apply
static bool g_restore_on;
// guest: the Space world when it joined. The host's list arrives with the guest's first roster, which may
// come from the guest's own world before the host's arrives (lab 20261104-100007: applied there, lost with
// the load): it is applied once a world was loaded since the join (the host shares its world on every join)
static uint32_t g_join_epoch;
static bool g_join_seen;

static void roster_end()
{
    std::vector<uint8_t> ids;
    for (auto& [id, r] : g_rosters)
        ids.push_back(id);
    g_rosters.clear();
    g_pending.clear();
    g_players.clear();
    g_last_bytes.clear();
    g_requested = false;
    g_force_send = false;
    g_join_seen = false; // BUG-0195: the next session's join
    g_restore.clear();
    g_restore_on = false;
    for (uint8_t id : ids)
        changed(id);
}

// ---------------- a guest's own colonies across a rejoin (BUG-0195) ----------------
// Every player flies in a copy of the host's save: a colony a guest founds lives only in the guest's
// running game, and a guest that leaves and joins again loads the host's world without it (the host
// shows it meanwhile as a city of the guest's mirror empire, space_allies.cpp, undone before every
// save). The host keeps each guest's own colonies (its roster's colonies that are not the host's) by
// the player's name for the Space world it plays (world_epoch); when that player joins again the
// host sends them (K_KEPT) and the guest makes each one a record-level colony of its own empire
// (spaceext::colonize_own) before it answers (K_KEPT_DONE with the planets it could not take: a star
// of another empire now). Until that answer the host does not take the guest's roster for the list
// (its first roster after the load has none of them). Memory only: a host that restarts its game
// starts with no list (docs/11-rivals.md#rivals-guest-colonies has the side-file design).
// `coop.fix space.keep_colonies off`: nothing kept, the colonies are gone after the rejoin (0.7.5).
static bool g_keep_fix = true;
COOP_FIX("space.keep_colonies", "BUG-0195", g_keep_fix, "space: a guest's own colonies come back when it rejoins (the host keeps them)");
struct Kept {
    std::vector<uint32_t> planets;
    uint8_t id = 0xFF;      // the session id it has now, 0xFF = not here
    bool restoring = false; // sent, no answer yet
    double sent = 0;
};
static std::map<std::string, Kept> g_kept; // host: player name -> its own colonies
static uint32_t g_kept_epoch;
static uint32_t g_kept_sent, g_kept_done, g_restored, g_restore_failed;

static void send_kept(uint8_t id, Kept& k)
{
    net::Writer w;
    w.u8(id);
    uint8_t n = (uint8_t)std::min<size_t>(k.planets.size(), 200);
    w.u8(n);
    for (uint8_t i = 0; i < n; i++)
        w.u32(k.planets[i]);
    spaceext::send(K_KEPT, w);
    k.restoring = true;
    k.sent = H->time_seconds();
    g_kept_sent++;
    H->log(M, SK_INFO, "space map: %u own colonies of %s sent back to it (BUG-0195)", (unsigned)n,
           spaceext::name_of(id).c_str());
}

static void kept_roster(uint8_t id)
{
    if (!g_keep_fix || !spaceext::is_host() || id == spaceext::my_id() || !roster_of(spaceext::my_id()))
        return;
    std::string name = spaceext::name_of(id);
    if (name.empty())
        return;
    Kept& k = g_kept[name];
    if (k.id != id) { // this player's first roster since it joined: its colonies back first
        for (auto& [n, o] : g_kept)
            if (o.id == id)
                o.id = 0xFF;
        k.id = id;
        k.restoring = false;
        if (!k.planets.empty()) {
            send_kept(id, k);
            return;
        }
    }
    if (k.restoring)
        return;
    std::vector<uint32_t> own;
    for (auto& c : g_rosters[id].colonies)
        if (!c.home && own_colony(id, c))
            own.push_back(c.planet);
    k.planets = std::move(own);
}

static void kept_tick(double now)
{
    if (spaceext::is_host()) {
        uint32_t e = spaceext::world_epoch();
        if (e && e != g_kept_epoch) { // another Space world: its players' colonies are not this one's
            g_kept.clear();
            g_kept_epoch = e;
        }
        std::set<uint8_t> here;
        for (auto& p : spaceext::ctx().sess->players())
            here.insert(p.id);
        for (auto& [n, k] : g_kept) {
            if (k.id != 0xFF && !here.count(k.id))
                k.id = 0xFF, k.restoring = false; // left
            else if (k.id != 0xFF && k.restoring && now - k.sent > 10.0)
                send_kept(k.id, k); // no answer (a lost message, the guest still loading)
        }
        return;
    }
    if (!g_join_seen) {
        g_join_seen = true;
        g_join_epoch = spaceext::in_space() ? spaceext::world_epoch() : 0;
    }
    if (!g_restore_on || !spaceext::in_space() || spaceext::world_epoch() == g_join_epoch)
        return;
    std::vector<uint32_t> failed, wait;
    for (uint32_t p : g_restore) {
        std::string why = spaceext::colonize_own(p);
        if (why == "wait")
            wait.push_back(p);
        else if (!why.empty()) {
            failed.push_back(p);
            g_restore_failed++;
            H->log(M, SK_WARN, "space map: kept colony %08X can not be mine again: %s", p, why.c_str());
        } else
            g_restored++;
    }
    g_restore = std::move(wait);
    if (!g_restore.empty())
        return;
    g_restore_on = false;
    net::Writer w;
    w.u8((uint8_t)failed.size());
    for (uint32_t p : failed)
        w.u32(p);
    spaceext::send(K_KEPT_DONE, w);
    g_force_send = true; // the roster with them, at once
    if (g_restored)
        spaceext::notice(spaceext::tr("Your colonies are back in your empire.", "Ваши колонии снова в вашей империи."));
}

static void on_kept(uint8_t origin, net::Reader& r, bool done)
{
    if (done) {
        if (!spaceext::is_host())
            return;
        uint8_t n = r.u8();
        std::set<uint32_t> failed;
        for (uint8_t i = 0; i < n && r.ok(); i++)
            failed.insert(r.u32());
        for (auto& [name, k] : g_kept)
            if (k.id == origin && k.restoring) {
                k.restoring = false;
                k.planets.erase(std::remove_if(k.planets.begin(), k.planets.end(),
                                               [&](uint32_t p) { return failed.count(p) != 0; }),
                                k.planets.end());
                g_kept_done++;
            }
        return;
    }
    uint8_t target = r.u8(), n = r.u8();
    if (!r.ok() || target != spaceext::my_id() || spaceext::is_host())
        return;
    (void)origin;
    std::vector<uint32_t> v;
    for (uint8_t i = 0; i < n && r.ok(); i++)
        v.push_back(r.u32());
    if (!r.ok())
        return;
    g_restore = std::move(v);
    g_restore_on = true;
}

static std::string kept_status()
{
    std::string s;
    char b[200];
    snprintf(b, sizeof b, "kept: fix=%d players=%u sent=%u done=%u restored=%u failed=%u pending=%u\n", (int)g_keep_fix,
             (unsigned)g_kept.size(), g_kept_sent, g_kept_done, g_restored, g_restore_failed, (unsigned)g_restore.size());
    s += b;
    for (auto& [n, k] : g_kept) {
        snprintf(b, sizeof b, "  kept '%s' id=%d restoring=%d colonies=%u:", n.c_str(), k.id == 0xFF ? -1 : k.id,
                 (int)k.restoring, (unsigned)k.planets.size());
        s += b;
        for (uint32_t p : k.planets) {
            snprintf(b, sizeof b, " %08X", p);
            s += b;
        }
        s += "\n";
    }
    return s;
}

bool own_colony(uint8_t id, const Colony& c)
{
    if (id == spaceext::my_id())
        return true;
    const Roster* me = roster_of(spaceext::my_id());
    if (!me)
        return true;
    for (auto& m : me->colonies)
        if (m.planet == c.planet)
            return false;
    return true;
}

// ---------------- colours ----------------
static int color_dist(uint32_t a, uint32_t b)
{
    int d = 0;
    for (int s = 0; s < 24; s += 8)
        d += std::abs((int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF));
    return d;
}

uint32_t player_color(uint8_t id)
{
    if (pfind::g_colors_fix && pfind::color_index(id) >= 0)
        return pfind::color_of(id); // F2: the session's player colour, the same in every stage and on every game
    static const uint32_t pal[] = {0xFFFFD040, 0xFF60C8FF, 0xFFFF70D0, 0xFF80FF80, 0xFFFF9040};
    const Roster* r = roster_of(id);
    const Roster* me = roster_of(spaceext::my_id());
    uint32_t c = r ? r->color : 0;
    if (id == spaceext::my_id())
        return c ? c : pal[0];
    if (c && (!me || !me->color || color_dist(c, me->color) > 120))
        return c;
    return pal[1 + id % 4];
}

// ---------------- the galaxy map ----------------
// In the galaxy view every partner gets a name tag in its colour on its star (or where its ship
// flies), the stars of its colonies a mark; a click on a tag travels there (docs/re/space.md 13).
// Drawn by the overlay (projected star positions, cStarRecord +0x3C, with the game camera), only
// in the galaxy view, never while a comm screen or a cinematic covers it (coop.cpp already hides
// the world labels under the pause menu and the co-op window).
static uintptr_t a_Context, a_CommMgr, a_CommActive, a_CinematicMgr, a_TravelCost, a_ActiveStarRecord, a_AddMoney,
    a_Teleport;

struct Tag {
    uint8_t id;
    uint32_t star;        // 0: a partner's ship in flight (the nearest star is the target)
    bool colony;
    int x0, y0, x1, y1;   // the clickable box (back buffer pixels)
    int sx, sy;           // the projected point of the star / ship
    net::Vec3 pos;        // galaxy coordinates
};
static std::vector<Tag> g_tags; // drawn in the last frame (main thread: draw and tick)
static bool g_covered, g_lmb, g_drawn; // g_drawn: the overlay drew the map since the last tick
static double g_cover_checked;
static uint32_t g_tag_clicks, g_trips, g_trip_cost, g_frames_drawn, g_frames_covered;
static std::map<uint8_t, size_t> g_col_pick; // the Space page: which colony of a partner is shown
// BUG-0422: the departed player's roster and colonies were shown for a newcomer under its id, and the newcomer
// (already "seen") was not sent everybody's rosters
PLAYER_STATE("space_map", [](uint8_t id) { g_rosters.erase(id); g_pending.erase(id); g_players.erase(id); g_col_pick.erase(id); },
             [](uint8_t id) -> size_t { return g_rosters.count(id) + g_pending.count(id) + g_players.count(id) + g_col_pick.count(id); });

static int context() { return a_Context ? cdecl_call<int>(a_Context) : -1; }

static bool galaxy_pos_of_star(uint32_t star, net::Vec3& out)
{
    void* r = star_record(star);
    if (!r)
        return false;
    auto& q = F<float[3]>(r, 0x3C);
    out = {q[0], q[1], q[2]};
    return true;
}

// where a partner is: its star, or its ship in the galaxy view (its live player state, only while
// it is in Space)
struct Where {
    uint32_t star = 0; // 0: in flight in the galaxy view
    net::Vec3 pos{};   // galaxy coordinates
};
static bool where_is(uint8_t id, Where& w)
{
    if (spaceext::ctx().partners)
        for (auto& p : spaceext::ctx().partners())
            if (p.id == id) {
                w.star = star_of(p.place);
                if (w.star)
                    return galaxy_pos_of_star(w.star, w.pos);
                w.pos = p.pos; // the galaxy view: the ship's position is in galaxy coordinates
                return std::isfinite(p.pos.x) && (p.pos.x != 0 || p.pos.y != 0 || p.pos.z != 0);
            }
    return false; // not in Space (or no state yet): no tag (its roster may be old)
}

// the star nearest to a galaxy point (a partner's ship in flight): the sectors of cStarManager
// +0xC8 (docs/re/space.md 9.1)
static uint32_t nearest_star(const net::Vec3& at, float max_d = 3.f)
{
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    if (!readable(sm, 0xD0))
        return 0;
    uint8_t* sb = F<uint8_t*>(sm, 0xC8);
    uint8_t* se = F<uint8_t*>(sm, 0xCC);
    if (!sb || se <= sb || (se - sb) / 20 > 4096 || !readable(sb, se - sb))
        return 0;
    uint32_t best = 0;
    float bd = max_d;
    for (uint8_t* sec = sb; sec + 20 <= se; sec += 20) {
        void** b = F<void**>(sec, 0);
        void** e = F<void**>(sec, 4);
        if (!b || e <= b || e - b > 4096 || !readable(b, (e - b) * 4))
            continue;
        for (void** it = b; it < e; ++it) {
            void* r = *it;
            if (!readable(r, 0x74))
                continue;
            auto& q = F<float[3]>(r, 0x3C);
            float d = net::dist(net::Vec3{q[0], q[1], q[2]}, at);
            if (d < bd) {
                bd = d;
                best = F<uint32_t>(r, 0x70);
            }
        }
    }
    return best;
}

// UTF-8 code points * 9 px (Arial 18 bold, like coop.cpp's text_width)
static int text_w(const std::string& s) { return utf8::overlay_width(s); }

static void text(int x, int y, uint32_t argb, const std::string& s)
{
    H->overlay_text(x + 1, y + 1, 0xC0000000, s.c_str()); // a shadow: readable on bright nebulae
    H->overlay_text(x, y, argb, s.c_str());
}

static bool project(const net::Vec3& p, int sw, int sh, int& x, int& y)
{
    float w[3] = {p.x, p.y, p.z}, sc[2] = {0, 0};
    if (!H->world_to_screen(w, sc) || !std::isfinite(sc[0]) || !std::isfinite(sc[1]))
        return false;
    x = (int)sc[0];
    y = (int)sc[1];
    return x >= 0 && y >= 0 && x < sw && y < sh;
}

static void draw_map()
{
    g_tags.clear();
    g_drawn = true;
    if (!spaceext::in_space() || !spaceext::connected() || context() != 2)
        return;
    if (g_covered) {
        g_frames_covered++;
        return;
    }
    g_frames_drawn++;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    uint8_t me = spaceext::my_id();
    // colonies first (the partners' tags go on top): a hollow diamond around each colony star,
    // the owner's name next to it while few are on the screen (zoomed in), else at the home star
    for (uint8_t id : roster_ids()) {
        if (id == me)
            continue;
        const Roster* r = roster_of(id);
        uint32_t col = player_color(id);
        std::string nm = spaceext::name_of(id);
        struct S {
            uint32_t star;
            int x, y;
            bool home;
            net::Vec3 p;
        };
        std::vector<S> vis;
        std::set<uint32_t> seen;
        for (auto& c : r->colonies) {
            if (!own_colony(id, c) || !seen.insert(c.star).second)
                continue;
            net::Vec3 p;
            int x, y;
            if (galaxy_pos_of_star(c.star, p) && project(p, sw, sh, x, y))
                vis.push_back({c.star, x, y, c.home, p});
        }
        bool names = vis.size() <= 8, rings = vis.size() <= 30;
        for (auto& v : vis) {
            if (rings) // a dotted ring around the star, like a territory mark
                for (int k = 0; k < 10; k++) {
                    double a = k * 2 * M_PI / 10;
                    text(v.x + (int)std::lround(14 * std::cos(a)) - 3, v.y + (int)std::lround(14 * std::sin(a)) - 10,
                         col, "•");
                }
            else
                text(v.x - 6, v.y - 10, col, "○");
            if (names || v.home) {
                std::string t = v.home ? nm + spaceext::tr("'s home", ": дом") : nm;
                text(v.x + 18, v.y - 10, (col & 0x00FFFFFF) | 0xD0000000, t);
                // only the name is clickable: a click on the star itself stays the game's (its
                // own flight in range, the star's card)
                g_tags.push_back({id, v.star, true, v.x + 16, v.y - 12, v.x + 20 + text_w(t), v.y + 12, v.x, v.y, v.p});
            } else // (no label: nothing to click; a click on the star itself stays the game's)
                g_tags.push_back({id, v.star, true, 0, 0, -1, -1, v.x, v.y, v.p});
        }
    }
    // the partners: "▼ Bob" above their star (or ship), an arrow on the screen edge when off screen
    for (uint8_t id : roster_ids()) {
        if (id == me)
            continue;
        Where w;
        if (!where_is(id, w))
            continue;
        uint32_t col = player_color(id);
        std::string t = spaceext::name_of(id);
        if (!w.star)
            t += spaceext::tr(" (flying)", " (в полёте)");
        int x, y;
        if (project(w.pos, sw, sh, x, y)) {
            std::string s = "▼ " + t; // ▼
            int tw = text_w(s);
            text(x - tw / 2, y - 36, col, s);
            g_tags.push_back({id, w.star, false, x - tw / 2 - 4, y - 38, x + tw / 2 + 4, y - 12, x, y, w.pos});
            continue;
        }
        float sc[2] = {-1e9f, -1e9f}, p3[3] = {w.pos.x, w.pos.y, w.pos.z};
        bool front = H->world_to_screen(p3, sc) != 0 && sc[0] > -1e8f;
        if (sw <= 0)
            continue;
        if (!front) {
            // not projectable (outside the view): the direction from my ship towards it, found by
            // walking the galaxy segment from the ship to the farthest point still on the screen
            void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
            void* u = mgr ? F<void*>(mgr, 0x40) : nullptr;
            int mx, my;
            net::Vec3 me{};
            if (readable(u, 0x48)) {
                me = {F<float>(u, 0x38), F<float>(u, 0x3C), F<float>(u, 0x40)};
                if (project(me, sw, sh, mx, my)) {
                    float lo = 0, hi = 1;
                    for (int it = 0; it < 12; it++) {
                        float m = (lo + hi) / 2;
                        net::Vec3 q{me.x + (w.pos.x - me.x) * m, me.y + (w.pos.y - me.y) * m, me.z + (w.pos.z - me.z) * m};
                        int qx, qy;
                        if (project(q, sw, sh, qx, qy)) {
                            lo = m;
                            sc[0] = (float)qx, sc[1] = (float)qy;
                            front = true;
                        } else
                            hi = m;
                    }
                }
            }
        }
        if (!pfind::arrows_on()) // F2: the edge arrows follow all.partner_arrows (never in Rivals mode)
            continue;
        float cx = sw / 2.f, cy = sh / 2.f;
        float dx = front ? sc[0] - cx : 0.f, dy = front ? sc[1] - cy : 1.f;
        float fx, fy;
        if (!cell_logic::edge_point(cx, cy, dx, dy, 90, 60, 1, 0.5f, fx, fy)) // a NaN position (BUG-0160)
            continue;
        int ex = (int)fx, ey = (int)fy;
        const char* arrow = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? "►" : "◄") : (dy > 0 ? "▼" : "▲");
        std::string s = std::string(arrow) + " " + t;
        int tw = text_w(s);
        text(ex - tw / 2, ey - 10, col, s);
        g_tags.push_back({id, w.star, false, ex - tw / 2 - 4, ey - 12, ex + tw / 2 + 4, ey + 12, -1, -1, w.pos});
    }
}

// a comm screen (talking to an empire) or a cinematic covers the galaxy view
static void cover_tick(double now)
{
    if (now - g_cover_checked < 0.2)
        return;
    g_cover_checked = now;
    bool c = false;
    void* cm = a_CommMgr ? cdecl_call<void*>(a_CommMgr) : nullptr;
    if (cm && a_CommActive)
        c = thiscall<bool>(a_CommActive, cm);
    void* cin = a_CinematicMgr ? cdecl_call<void*>(a_CinematicMgr) : nullptr;
    if (cin && F<uint32_t>(cin, 0x2C) != 0)
        c = true;
    g_covered = c;
}

// a trip to a star: free to a partner (as the 0.6 jump), the normal galaxy flight cost to one of a
// partner's colonies (the game's own price for the distance, cheaper to own / allied stars:
// 0xFFC390), without the drive's range limit (a partner's colony is a friendly port)
static std::string fly_to(uint32_t star, bool pay)
{
    if (!star)
        return "no star";
    int cost = 0;
    void* emp = player_empire();
    if (pay && a_TravelCost && a_ActiveStarRecord && a_PlayerUFOGet && emp) {
        void* cur = cdecl_call<void*>(a_ActiveStarRecord);
        void* dst = star_record(star);
        void* ufo = cdecl_call<void*>(a_PlayerUFOGet);
        if (cur && dst && ufo && cur != dst)
            cost = thiscall<int>(a_TravelCost, ufo, cur, dst);
        if (cost < 0 || cost > 10000000)
            cost = 0;
        if (cost > F<int>(emp, 0xD0)) {
            char b[96];
            snprintf(b, sizeof b, "%s %d", spaceext::tr("not enough money for the trip:", "не хватает денег на перелёт:"),
                     cost);
            return b;
        }
    }
    std::string e = spacecoop::travel(star);
    if (!e.empty())
        return e;
    g_trips++;
    if (cost > 0 && a_AddMoney && emp) {
        thiscall<void>(a_AddMoney, emp, -cost);
        g_trip_cost += (uint32_t)cost;
    }
    return "";
}

static bool g_teleport_fix = true;
COOP_FIX("map.teleport", "BUG-0063", g_teleport_fix, "space: flying to a partner in the galaxy view is refused, costs nothing");

// to a partner: its star system (the 0.6 jump), or, while it flies in the galaxy view, the star
// next to its ship (SpaceTeleportTo: instant and free in the galaxy view, docs/re/space.md 7.3)
static std::string go_to_partner(uint8_t id)
{
    Where w;
    if (!where_is(id, w))
        return "the partner's place is not known";
    if (w.star) {
        std::string e = spacecoop::jump(id);
        if (e.empty())
            g_trips++;
        return e;
    }
    uint32_t s = nearest_star(w.pos);
    if (!s)
        return "no star near the partner";
    if (context() == 2 && a_Teleport) {
        // BUG-0063: through spacecoop (refused while disabled, the energy put back);
        // `coop.space map teleportfix 0`: the old direct call (tests)
        if (g_teleport_fix) {
            std::string e = spacecoop::teleport_in_galaxy(s);
            if (e.empty())
                g_trips++;
            return e;
        }
        void* r = star_record(s);
        if (!r)
            return "no star record";
        cdecl_call<void>(a_Teleport, r);
        g_trips++;
        return "";
    }
    return spacecoop::travel(s);
}

static void click_task(void* p)
{
    Tag* t = (Tag*)p;
    std::string e = t->colony ? fly_to(t->star, true) : go_to_partner(t->id);
    if (!e.empty())
        spaceext::notice(spaceext::tr("can't fly there: ", "не долететь: ") + e);
    delete t;
}

// a primary click on a tag (the virtual cursor of the tests too: input.click posts the window's
// WM_LBUTTONDOWN). BUG-0090 / BUG-0145: the click comes from the game window's own messages
// (mouse.h), so clicks in other windows over the game and the secondary button of a swapped mouse
// are not taken. `coop.space map clickfix 0` (mouse::g_fix): the old polling of the physical left button (tests).
static uint32_t g_click_seen;
static void click_tick()
{
    HWND w = (HWND)H->game_window();
    mouse::install(w);
    int x = 0, y = 0;
    POINT pt;
    RECT rc;
    if (mouse::g_fix) {
        uint32_t n = mouse::clicks();
        bool edge = n != g_click_seen;
        g_click_seen = n;
        g_lmb = false;
        if (!edge || g_tags.empty() || !w)
            return;
        mouse::last_click(x, y);
        pt = {x, y};
    } else {
        bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool edge = down && !g_lmb;
        g_lmb = down;
        g_click_seen = mouse::clicks();
        if (!edge || g_tags.empty())
            return;
        if (!w || !GetCursorPos(&pt) || !ScreenToClient(w, &pt))
            return;
    }
    if (!GetClientRect(w, &rc) || rc.right <= 0 || rc.bottom <= 0)
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    x = sw > 0 ? pt.x * sw / rc.right : pt.x;
    y = sh > 0 ? pt.y * sh / rc.bottom : pt.y;
    // partners' tags are on top: the last drawn wins
    for (auto it = g_tags.rbegin(); it != g_tags.rend(); ++it)
        if (x >= it->x0 && x <= it->x1 && y >= it->y0 && y <= it->y1) {
            g_tag_clicks++;
            H->run_outside_frame(click_task, new Tag(*it));
            return;
        }
}

// ---------------- the Space page ----------------
static std::string place_text(uint8_t id)
{
    Where w;
    if (!where_is(id, w))
        return spaceext::tr("somewhere in space", "где-то в космосе");
    if (!w.star) {
        uint32_t s = nearest_star(w.pos);
        std::string n = s ? star_name(s) : "";
        return n.empty() ? spaceext::tr("flying in the galaxy", "летит по галактике")
                         : std::string(spaceext::tr("flying near ", "летит у ")) + n;
    }
    const Roster* r = roster_of(id);
    std::string n = r && r->star_now == w.star && !r->star_now_name.empty() ? r->star_now_name : star_name(w.star);
    if (n.empty())
        n = spaceext::tr("a star", "звезда");
    for (auto& p : spaceext::ctx().partners ? spaceext::ctx().partners() : std::vector<spacecoop::Partner>{})
        if (p.id == id && (p.place & 0x40000000u) && r)
            for (auto& c : r->colonies)
                if (c.planet == (p.place & 0x3FFFFFFFu) && !c.name.empty())
                    return spaceext::tr("on ", "на ") + c.name + " (" + n + ")";
    return spaceext::tr("at ", "у звезды ") + n;
}

static std::string spice_text(const Colony& c)
{
    if (const Spice* sp = spice_of(c.spice[0]))
        return spaceext::tr(sp->en, sp->ru);
    if (!c.spice_name.empty())
        return c.spice_name;
    if (!c.spice[0])
        return spaceext::tr("no spice", "без специй");
    char b[24];
    snprintf(b, sizeof b, "spice %08X", c.spice[0]);
    return b;
}

static std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> v;
    if (!spaceext::in_space() || !spaceext::connected())
        return v;
    uint8_t me = spaceext::my_id();
    uint32_t here = star_of(spaceext::ctx().my_place ? spaceext::ctx().my_place() : 0);
    for (uint8_t id : roster_ids()) {
        if (id == me)
            continue;
        const Roster* r = roster_of(id);
        std::string nm = spaceext::name_of(id);
        uint32_t col = player_color(id);
        Where w;
        bool known = where_is(id, w);
        spaceext::Row a;
        a.text = nm + ": " + place_text(id);
        a.color = col;
        if (known && (!w.star || w.star != here)) {
            a.button = spaceext::tr("Fly there", "Лететь туда");
            a.on_click = [id] {
                std::string e = go_to_partner(id);
                if (!e.empty())
                    spaceext::notice(spaceext::tr("can't fly there: ", "не долететь: ") + e);
            };
        }
        v.push_back(std::move(a));
        if (!r)
            continue;
        // the partner's own colonies (those we both have since the start are in my game too), one
        // at a time: "Next" picks the following one (the page has few rows)
        std::vector<const Colony*> own;
        for (auto& c : r->colonies)
            if (own_colony(id, c))
                own.push_back(&c);
        if (own.empty())
            continue;
        size_t n = own.size();
        size_t& pick = g_col_pick[id];
        if (pick >= n)
            pick = 0;
        if (n > 1) {
            spaceext::Row b;
            b.text = std::string("  ") + nm + spaceext::tr("'s colonies: ", ": колонии ") + std::to_string(pick + 1) +
                     "/" + std::to_string(n) + spaceext::tr(" (\u25CB on the galaxy map)", " (\u25CB на карте галактики)");
            b.color = col;
            b.button = spaceext::tr("Next", "Дальше");
            b.on_click = [id] { g_col_pick[id]++; };
            v.push_back(std::move(b));
        }
        const Colony& c = *own[pick];
        spaceext::Row cr;
        std::string sn = c.star_name.empty() ? star_name(c.star) : c.star_name;
        std::string cities = std::to_string(c.cities) +
                             (c.cities == 1 ? spaceext::tr(" city", " город") : spaceext::tr(" cities", " гор."));
        cr.text = std::string("  ") + (n == 1 ? spaceext::tr("colony ", "колония ") : "") +
                  (c.name.empty() ? spaceext::tr("a planet", "планета") : c.name) + (sn.empty() ? "" : " (" + sn + ")") +
                  ": " + spice_text(c) + ", " + cities;
        cr.color = col;
        if (c.star != here) {
            cr.button = spaceext::tr("Fly there", "Лететь туда");
            uint32_t star = c.star;
            cr.on_click = [star] {
                std::string e = fly_to(star, true);
                if (!e.empty())
                    spaceext::notice(spaceext::tr("can't fly there: ", "не долететь: ") + e);
            };
        }
        v.push_back(std::move(cr));
    }
    return v;
}

// ---------------- the feature ----------------
static std::string fmt_roster(const Roster& r)
{
    char b[256];
    snprintf(b, sizeof b, "roster %u (%s) v%u home %u/%08X place %08X at %.2f %.2f %.2f empire '%s' color %08X pol %08X money %d colonies %u age %.1f\n",
             r.id, spaceext::name_of(r.id).c_str(), r.version, r.home_star, r.home_planet, r.place, r.galaxy_pos.x,
             r.galaxy_pos.y, r.galaxy_pos.z, r.empire_name.c_str(), r.color, r.political_id, r.money,
             (unsigned)r.colonies.size(), H->time_seconds() - r.updated);
    std::string s = b;
    for (auto& c : r.colonies) {
        snprintf(b, sizeof b, "  colony %08X star %u '%s' at '%s' spice %08X '%s' cities %u%s%s\n", c.planet, c.star,
                 c.name.c_str(), c.star_name.c_str(), c.spice[0], c.spice_name.c_str(), c.cities, c.home ? " home" : "",
                 own_colony(r.id, c) ? " own" : " shared");
        s += b;
    }
    return s;
}

static std::string status()
{
    char b[300];
    snprintf(b, sizeof b,
             "map: rosters=%u changes=%u sent=%u got=%u reqs=%u version=%u tags=%u covered=%d drawn=%u hidden=%u "
             "clicks=%u trips=%u trip_cost=%u",
             (unsigned)g_rosters.size(), g_changes, g_sent, g_got, g_reqs, g_version, (unsigned)g_tags.size(),
             (int)g_covered, g_frames_drawn, g_frames_covered, g_tag_clicks, g_trips, g_trip_cost);
    return b;
}

static std::string debug(const std::vector<std::string>& a)
{
    if (a.empty() || a[0] == "status")
        return status() + "\n";
    if (a[0] == "roster") { // coop.space map roster [id]
        std::string s;
        for (uint8_t id : roster_ids())
            if (a.size() < 2 || atoi(a[1].c_str()) == id)
                s += fmt_roster(g_rosters[id]);
        return s.empty() ? "no rosters\n" : s;
    }
    if (a[0] == "kept") // BUG-0195: the host's list of its guests' own colonies, the guest's restore
        return kept_status();
    if (a[0] == "send") { // tests: send my roster now
        g_force_send = true;
        return "sending\n";
    }
    if (a[0] == "tags") { // tests: the tags of the last frame (box, the projected star / ship point)
        std::string s;
        char b[200];
        for (auto& t : g_tags) {
            snprintf(b, sizeof b, "tag %u %s star %u box %d %d %d %d at %d %d pos %.3f %.3f %.3f\n", t.id,
                     t.colony ? "colony" : "player", t.star, t.x0, t.y0, t.x1, t.y1, t.sx, t.sy, t.pos.x, t.pos.y,
                     t.pos.z);
            s += b;
        }
        snprintf(b, sizeof b, "context %d covered %d\n", context(), (int)g_covered);
        return s + b;
    }
    if (a[0] == "project" && a.size() > 1) { // tests: where a star is on the screen
        net::Vec3 p;
        uint32_t star = (uint32_t)strtoul(a[1].c_str(), nullptr, 0);
        if (!galaxy_pos_of_star(star, p))
            return "no star\n";
        float w[3] = {p.x, p.y, p.z}, sc[2] = {-1, -1};
        int ok = H->world_to_screen(w, sc);
        char b[160];
        snprintf(b, sizeof b, "star %u '%s' pos %.3f %.3f %.3f screen %d %d %d\n", star, star_name(star).c_str(), p.x,
                 p.y, p.z, ok, (int)sc[0], (int)sc[1]);
        return b;
    }
    if (a[0] == "fly" && a.size() > 1) { // tests: the colony trip (cost) to a star; "free": no cost
        std::string e = fly_to((uint32_t)strtoul(a[1].c_str(), nullptr, 0), !(a.size() > 2 && a[2] == "free"));
        return e.empty() ? "flying\n" : e + "\n";
    }
    if ((a[0] == "clickfix" || a[0] == "teleportfix") && a.size() > 1) { // BUG-0090/0145, BUG-0063 (tests)
        bool& on = a[0] == "clickfix" ? mouse::g_fix : g_teleport_fix; // clickfix: the menu intro's click too
        on = a[1] != "0";
        return a[0] + " " + (on ? "1" : "0") + "\n";
    }
    if (a[0] == "postclick" && a.size() > 2) { // tests: only the window message of a primary click, no
        // button state (a swapped mouse's primary button: the physical right one, WM_LBUTTONDOWN)
        HWND w = (HWND)H->game_window();
        if (!w)
            return "no window\n";
        LPARAM lp = MAKELPARAM(atoi(a[1].c_str()), atoi(a[2].c_str()));
        PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        PostMessageW(w, WM_LBUTTONUP, 0, lp);
        return "posted\n";
    }
    if (a[0] == "go" && a.size() > 1) { // tests: what a click on a partner's tag does
        std::string e = go_to_partner((uint8_t)atoi(a[1].c_str()));
        return e.empty() ? "going\n" : e + "\n";
    }
    if (a[0] == "planets") { // tests: stars near the ship (or a star) with their planet records
        // coop.space map planets [star]: PlanetID name type tech atmosphere temperature water
        std::vector<uint32_t> stars;
        if (a.size() > 1)
            stars.push_back((uint32_t)strtoul(a[1].c_str(), nullptr, 0));
        else {
            Roster me;
            read_local(me);
            void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
            uint8_t* sb = readable(sm, 0xD0) ? F<uint8_t*>(sm, 0xC8) : nullptr;
            uint8_t* se = sb ? F<uint8_t*>(sm, 0xCC) : nullptr;
            for (uint8_t* sec = sb; sb && sec + 20 <= se && stars.size() < 40; sec += 20)
                for (void* r : ptr_vec(sec, 4096))
                    if (readable(r, 0x98) && net::dist(net::Vec3{F<float>(r, 0x3C), F<float>(r, 0x40), F<float>(r, 0x44)},
                                                       me.galaxy_pos) < (a.size() > 2 ? atof(a[2].c_str()) : 4.0))
                        stars.push_back(F<uint32_t>(r, 0x70));
        }
        std::string s;
        char b[200];
        for (uint32_t st : stars) {
            void* r = star_record(st);
            if (!r)
                continue;
            net::Vec3 p;
            galaxy_pos_of_star(st, p);
            snprintf(b, sizeof b, "star %u '%s' empire %08X type %d at %.2f %.2f %.2f\n", st, star_name(st).c_str(),
                     F<uint32_t>(r, 0x54), F<int>(r, 0x48), p.x, p.y, p.z);
            s += b;
            for (void* pl : ptr_vec((uint8_t*)r + 0x84, 64))
                if (readable(pl, 0x1A4)) {
                    snprintf(b, sizeof b,
                             "  planet %08X '%s' type %d tech %d scores %.2f %.2f %.2f civs %u spice %08X nodes %u\n",
                             F<uint32_t>(pl, 0x184), str16((uint8_t*)pl + 0x18).c_str(), F<int>(pl, 0x28),
                             F<int>(pl, 0x194), F<float>(pl, 0xB0), F<float>(pl, 0xB4), F<float>(pl, 0xB8),
                             (unsigned)ptr_vec((uint8_t*)pl + 0x15C, 32).size(), F<uint32_t>(pl, 0x198),
                             (unsigned)ptr_vec((uint8_t*)pl + 0xE4, 256).size());
                    s += b;
                }
            if (s.size() > 7000)
                break;
        }
        return s.empty() ? "no stars\n" : s;
    }
    if (a[0] == "colonize") { // tests: fire the colony tool (Colony Incredi-Pak) at the ground under the cursor
        // cPlaceColonyToolStrategy::OnHit(tool, pos, kHitGround, 0) is what the tool does on a click
        // (charge, drop animation, CreateColony), without the aiming check "Colonies must be placed
        // near spice" (docs/re/space.md 13.4). The terrain cursor (noun 0x18C40BC) is the point.
        uintptr_t onhit = H->sym("Simulator::cPlaceColonyToolStrategy::OnHit"),
                  gettool = H->sym("Simulator::cPlayerInventory::GetTool"),
                  game = H->sym("Simulator::cSimulatorSpaceGame::ptr");
        if (!onhit || !gettool || !game || context() != 0)
            return "not on a planet\n";
        void* sg = *(void**)game;
        void* inv = readable(sg, 0x44) ? F<void*>(sg, 0x40) : nullptr;
        uint32_t key[3] = {0x733B280Cu, 0, 0}; // placecolony
        void* tool = inv ? thiscall<void*>(gettool, inv, key) : nullptr;
        if (!tool)
            return "no colony tool (coop.space tool add placecolony)\n";
        // the ground point under the ship: cPlanetModel::GetHeightAt(dir) is the terrain's distance
        // from the planet's centre (the planet's coordinates are centred on it); a colony at the
        // centre (the terrain cursor sits at 0,0,0 off the ground) made the game fault in a loop
        uintptr_t pmget = H->sym("Simulator::cPlanetModel::Get"), height = H->sym("Simulator::cPlanetModel::GetHeightAt");
        void* pm = pmget ? cdecl_call<void*>(pmget) : nullptr;
        void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
        void* u = mgr ? F<void*>(mgr, 0x40) : nullptr;
        if (!pm || !height || !readable(u, 0x48))
            return "no planet model / ship\n";
        float ship[3] = {F<float>(u, 0x38), F<float>(u, 0x3C), F<float>(u, 0x40)};
        float sl = std::sqrt(ship[0] * ship[0] + ship[1] * ship[1] + ship[2] * ship[2]);
        float h = thiscall<float>(height, pm, ship);
        if (sl < 1 || !(h > sl * 0.3f && h < sl))
            return "no ground under the ship\n";
        net::Vec3 at{ship[0] / sl * h, ship[1] / sl * h, ship[2] / sl * h};
        float pos[3] = {at.x, at.y, at.z};
        bool ok = thiscall<bool>(onhit, nullptr, tool, pos, 1, 0);
        char b[120];
        snprintf(b, sizeof b, "colony tool %s at %.1f %.1f %.1f\n", ok ? "fired" : "refused", pos[0], pos[1], pos[2]);
        return b;
    }
    if (a[0] == "cost" && a.size() > 1) { // tests: the price of the trip to a star
        void* cur = a_ActiveStarRecord ? cdecl_call<void*>(a_ActiveStarRecord) : nullptr;
        void* dst = star_record((uint32_t)strtoul(a[1].c_str(), nullptr, 0));
        void* ufo = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
        if (!cur || !dst || !ufo || !a_TravelCost)
            return "no cost\n";
        void* emp = player_empire();
        char b[96];
        snprintf(b, sizeof b, "cost %d money %d\n", thiscall<int>(a_TravelCost, ufo, cur, dst),
                 emp ? F<int>(emp, 0xD0) : -1);
        return b;
    }
    return "usage: coop.space map [status | roster [id] | send | tags | project <star> | fly <star> [free] | go <id> | "
           "cost <star> | planets [star] | colonize]\n";
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    a_PlayerEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_StarMgr = H->sym("Simulator::cStarManager::Get");
    a_PlayerUFOGet = H->sym("Simulator::cSimulatorPlayerUFO::Get");
    a_Context = H->sym("SporeCoop::Space_GetCurrentContext");
    a_CommMgr = H->sym("Simulator::cCommManager::Get");
    a_CommActive = H->sym("Simulator::cCommManager::IsCommScreenActive");
    a_CinematicMgr = H->sym("Simulator::cCinematicManager::Get");
    a_TravelCost = H->sym("SporeCoop::cSimulatorPlayerUFO_TravelCost");
    a_ActiveStarRecord = H->sym("Simulator::GetActiveStarRecord");
    a_AddMoney = H->sym("Simulator::cEmpire::AddMoney");
    a_Teleport = H->sym("Simulator::SpaceTeleportTo");
    bool ok = a_PlayerEmpire && a_StarMgr;
    H->log(M, ok ? SK_INFO : SK_WARN, "space map: roster %s", ok ? "on" : "off (symbols missing)");
    return ok;
}

static void tick(double now)
{
    // no overlay frame since the last tick (coop.cpp skips the world labels under the pause menu
    // and the co-op window): no tags to show or click
    if (!g_drawn)
        g_tags.clear();
    g_drawn = false;
    roster_tick(now);
    if (spaceext::connected())
        kept_tick(now);
    if (!spaceext::in_space())
        return;
    cover_tick(now);
    click_tick();
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    if (kind == K_ROSTER)
        on_roster(origin, r);
    else if (kind == K_ROSTER_REQ)
        g_force_send = true;
    else if (kind == K_KEPT || kind == K_KEPT_DONE)
        on_kept(origin, r, kind == K_KEPT_DONE);
}

static bool reg = spaceext::add({.name = "map",
                                 .kind_lo = 10,
                                 .kind_hi = 19,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = roster_end,
                                 .draw_overlay = draw_map,
                                 .rows = rows,
                                 .status = status,
                                 .debug = debug});

} // namespace spacemap
