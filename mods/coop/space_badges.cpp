// Space feature "badges" (MSG_SPACE kinds 85..89): a guest's own badges and captain rank across a rejoin or a
// reload (BUG-0786, docs/re/space.md#space-badges).
//
// The badges live in the game's cBadgeManager (cSimulatorSpaceGame +0x70), saved with the Space world: mBadgeMap
// (+0x2C, badge id -> state: 1 earned, 2 / 3 available), mStageMap (+0x48, the rank at which a badge was earned),
// mEventCounts (+0x64, the counters the badge requirements read) and the tracked badge card (+0xA4). The captain
// rank is not stored: it is the highest rank badge earned (BadgeManager_GetRank). Every player flies in a copy of the
// host's save, so a guest that left and joined again (or loaded the host's world again) got the host's badges,
// counters and rank: the same class as the cargo hold (BUG-0678, space_trade.cpp K_INV).
// The same scheme as the hold: the guest sends its badge tables to the host when they change (K_BADGES; the first
// one of every Space world loaded from the host is marked fresh). The host keeps the last one per player name for the
// Space world it plays; a fresh table of a player it kept one for is the host's copy: the kept one goes back
// (K_BADGES_KEPT, resent after 10 s until K_BADGES_DONE) and the guest's game gets it written into its manager the
// way the game's own Read does (the maps freed and filled again by their operator[]), then a progress refresh. (No
// record on the guest's disk as the hold has, BUG-0679: a host restart loses the kept badges.) A Space save of the guest's own (own_world::space_other) is left alone (BUG-0816).
// `coop.fix space.keep_badges off`: nothing comes back, the rejoined guest has the host's badges (0.7.12).
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "fixes.h"
#include "modes.h"
#include "net/protocol.h"
#include "own_world.h"
#include "space_ext.h"
#include "sporekit_game.hpp"

using skg::cdecl_call;
using skg::thiscall;

namespace spacebadges {

enum Kind : uint8_t {
    K_BADGES = 85,      // guest -> host {to 0, serial, fresh u8, tables}: my badges now
    K_BADGES_KEPT = 86, // host -> guest {to, serial, have u8[, tables]}: your own badges, kept since you were here
    K_BADGES_DONE = 87, // guest -> host {to 0, serial}: written (or not needed)
};

static const SkHost* H;
static SkMod* M;
static uintptr_t a_SpaceGame, a_MapBadge, a_MapStage, a_MapEvent, a_FreeNodes, a_Next, a_Progress, a_Rank;

static bool g_fix = true;
COOP_FIX("space.keep_badges", "BUG-0786", g_fix, "space: a guest's own badges and captain rank come back when it rejoins or reloads (the host keeps them)");

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

constexpr uintptr_t kBadgeMap = 0x2C, kStageMap = 0x48, kEventMap = 0x64, kCard = 0xA4;
constexpr size_t kMaxEntries = 1000;

struct Badges {
    std::vector<std::pair<uint32_t, int32_t>> badge, stage, event;
    uint32_t card = 0;
    bool operator==(const Badges& o) const
    {
        return badge == o.badge && stage == o.stage && event == o.event && card == o.card;
    }
};

static void* manager()
{
    if (!spaceext::in_space())
        return nullptr;
    void* sg = a_SpaceGame ? *(void**)a_SpaceGame : nullptr;
    void* m = sg ? F<void*>(sg, 0x70) : nullptr;
    return m && !IsBadReadPtr(m, 0xC0) ? m : nullptr;
}

// an eastl::map<uint32, int>: +4 the anchor (its +4 left = begin), +0x14 size; a node: +0x10 key, +0x14 value
static bool read_map(void* mgr, uintptr_t off, std::vector<std::pair<uint32_t, int32_t>>& out)
{
    uint8_t* map = (uint8_t*)mgr + off;
    void* anchor = map + 4;
    uint32_t n = F<uint32_t>(map, 0x14);
    if (n > kMaxEntries)
        return false;
    out.clear();
    void* node = F<void*>(map, 8);
    for (uint32_t i = 0; node && node != anchor && i <= n; i++) {
        if (IsBadReadPtr(node, 0x18))
            return false;
        out.push_back({F<uint32_t>(node, 0x10), F<int32_t>(node, 0x14)});
        node = cdecl_call<void*>(a_Next, node);
    }
    return out.size() == n;
}

static bool read_badges(Badges& b)
{
    void* m = manager();
    if (!m || !read_map(m, kBadgeMap, b.badge) || !read_map(m, kStageMap, b.stage) || !read_map(m, kEventMap, b.event))
        return false;
    b.card = F<uint32_t>(m, kCard);
    return true;
}

// the map made again as the game's own Read does (0xFE40C6..0xFE40E9): its nodes freed, the anchor reset, then the
// map's operator[] per key
static void write_map(void* mgr, uintptr_t off, uintptr_t at, const std::vector<std::pair<uint32_t, int32_t>>& v)
{
    uint8_t* map = (uint8_t*)mgr + off;
    thiscall<void>(a_FreeNodes, map, F<void*>(map, 0xC));
    F<void*>(map, 4) = map + 4;
    F<void*>(map, 8) = map + 4;
    F<void*>(map, 0xC) = nullptr;
    F<uint8_t>(map, 0x10) = 0;
    F<uint32_t>(map, 0x14) = 0;
    // two passes: operator[] of a key it inserts gives back the value of the node before it (seen in the lab: every
    // badge got its successor's state); for a key that is there it gives that node's value (0xBAE0A1)
    for (auto& [k, val] : v) {
        uint32_t key = k;
        thiscall<int32_t*>(at, map, &key);
    }
    for (auto& [k, val] : v) {
        uint32_t key = k;
        if (int32_t* p = thiscall<int32_t*>(at, map, &key))
            *p = val;
    }
}

static bool write_badges(const Badges& b)
{
    void* m = manager();
    if (!m)
        return false;
    write_map(m, kBadgeMap, a_MapBadge, b.badge);
    write_map(m, kStageMap, a_MapStage, b.stage);
    write_map(m, kEventMap, a_MapEvent, b.event);
    F<uint32_t>(m, kCard) = b.card;
    thiscall<void>(a_Progress, m, 0xC, 0); // badge progress changed: the game's own refresh (message 0x38CF2FB)
    return true;
}

static int rank()
{
    void* m = manager();
    return m ? thiscall<int>(a_Rank, m) : -1;
}

static void put_list(net::Writer& w, const std::vector<std::pair<uint32_t, int32_t>>& v)
{
    uint16_t n = (uint16_t)std::min<size_t>(v.size(), kMaxEntries);
    w.u16(n);
    for (uint16_t i = 0; i < n; i++) {
        w.u32(v[i].first);
        w.u32((uint32_t)v[i].second);
    }
}

static bool get_list(net::Reader& r, std::vector<std::pair<uint32_t, int32_t>>& v)
{
    v.clear();
    uint16_t n = r.u16();
    if (!r.ok() || n > kMaxEntries)
        return false;
    for (uint16_t i = 0; i < n && r.ok(); i++) {
        uint32_t k = r.u32();
        int32_t val = (int32_t)r.u32();
        v.push_back({k, val});
    }
    return r.ok();
}

static void put_badges(net::Writer& w, const Badges& b)
{
    put_list(w, b.badge);
    put_list(w, b.stage);
    put_list(w, b.event);
    w.u32(b.card);
}

static bool get_badges(net::Reader& r, Badges& b)
{
    if (!get_list(r, b.badge) || !get_list(r, b.stage) || !get_list(r, b.event))
        return false;
    b.card = r.u32();
    for (auto& [k, s] : b.badge)
        if (s < 0 || s > 16)
            return false; // states 1..3 seen; nothing else is a badge state
    return r.ok();
}

// ---- host: the badges kept per player name ----
struct Kept {
    Badges b;
    bool have = false;
    uint8_t id = 0xFF;
    bool restoring = false;
    uint32_t serial = 0;
    double sent = 0;
};
static std::map<std::string, Kept> g_kept;
static uint32_t g_kept_epoch, g_serial, g_tables, g_sent_back, g_done;
// ---- guest ----
static bool g_join_seen;
static uint32_t g_join_epoch, g_fresh_epoch, g_applied_epoch, g_sent, g_restored, g_own_held;
static Badges g_last;
static bool g_have_last;
static double g_next, g_epoch_t;
static uint32_t g_epoch_seen;
constexpr double kSettle = 3.0; // s of a new Space world before its badges count (the load's own writes are done)

static bool partner_here(uint8_t id)
{
    if (!spaceext::connected())
        return false;
    for (auto& p : spaceext::ctx().sess->players())
        if (p.id == id)
            return true;
    return false;
}

static bool own_save() { return own_world::space_other(); } // BUG-0816: a Space save of the guest's own

static void send_kept(uint8_t id, Kept& k)
{
    net::Writer w;
    w.u8(id);
    w.u32(k.serial = ++g_serial);
    w.u8(1);
    put_badges(w, k.b);
    spaceext::send(K_BADGES_KEPT, w);
    k.restoring = true;
    k.sent = H->time_seconds();
    g_sent_back++;
    H->log(M, SK_INFO, "badges: the own badges of %s (%u badges, %u counters) sent back to it (BUG-0786)",
           spaceext::name_of(id).c_str(), (unsigned)k.b.badge.size(), (unsigned)k.b.event.size());
}

// the badges of a world now mine: written, the record follows them
static bool apply(const Badges& b, uint32_t e, const char* from)
{
    int r0 = rank();
    if (!write_badges(b))
        return false;
    g_applied_epoch = e;
    g_restored++;
    g_have_last = false; // the badges now go to the host at once
    g_next = 0;
    H->log(M, SK_INFO, "badges: own badges back from %s: %u badges (%u earned), rank %d -> %d (BUG-0786)", from,
           (unsigned)b.badge.size(), (unsigned)std::count_if(b.badge.begin(), b.badge.end(), [](auto& x) { return x.second == 1; }),
           r0, rank());
    spaceext::notice(spaceext::tr("Your badges and rank are back.", "Ваши значки и звание восстановлены."));
    return true;
}

static void tick(double now)
{
    if (!g_fix)
        return; // off: nothing read, sent or kept (0.7.12)
    if (!spaceext::connected())
        return;
    if (spaceext::is_host()) {
        uint32_t e = spaceext::world_epoch();
        if (e && e != g_kept_epoch) { // another Space world: the badges kept were of the old one
            g_kept.clear();
            g_kept_epoch = e;
        }
        for (auto& [n, k] : g_kept) {
            if (k.id != 0xFF && !partner_here(k.id))
                k.id = 0xFF, k.restoring = false;
            else if (k.id != 0xFF && k.restoring && now - k.sent > 10.0)
                send_kept(k.id, k);
        }
        return;
    }
    if (!g_join_seen) {
        g_join_seen = true;
        g_join_epoch = spaceext::in_space() ? spaceext::world_epoch() : 0;
    }
    if (own_save()) {
        g_own_held++;
        return;
    }
    if (spaceext::in_space() && !modes::current_world())
        return; // which save this is is not known yet
    uint32_t e = spaceext::world_epoch();
    if (!spaceext::in_space() || !e || e == g_join_epoch || now < g_next)
        return;
    g_next = now + 1.0;
    if (e != g_epoch_seen)
        g_epoch_seen = e, g_epoch_t = now;
    if (now - g_epoch_t < kSettle)
        return; // the first table of a world after its load has settled: what comes back is not overwritten by it
    Badges b;
    if (!read_badges(b))
        return;
    bool fresh = e != g_fresh_epoch;
    if (!fresh && g_have_last && b == g_last)
        return;
    g_last = b;
    g_have_last = true;
    g_fresh_epoch = e;
    net::Writer w;
    w.u8(0);
    w.u32(++g_sent);
    w.u8(fresh ? 1 : 0);
    put_badges(w, b);
    spaceext::send(K_BADGES, w);
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    uint8_t to = r.u8();
    uint32_t id = r.u32();
    if (!r.ok())
        return;
    if (kind == K_BADGES || kind == K_BADGES_DONE) {
        if (!spaceext::is_host() || to != 0)
            return;
        std::string name = spaceext::name_of(origin);
        if (name.empty() || name == "?")
            return;
        Kept& k = g_kept[name];
        if (kind == K_BADGES_DONE) {
            if (k.restoring && k.id == origin && id == k.serial) {
                k.restoring = false;
                g_done++;
            }
            return;
        }
        bool fresh = r.u8() != 0;
        Badges b;
        if (!get_badges(r, b))
            return;
        if (k.id != origin)
            k.id = origin, k.restoring = false;
        if (fresh && k.have && g_fix) { // the host's copy: the player's own go back
            send_kept(origin, k);
            return;
        }
        if (fresh) { // nothing kept (a first join): the save's badges stay
            net::Writer w;
            w.u8(origin);
            w.u32(0);
            w.u8(0);
            spaceext::send(K_BADGES_KEPT, w);
        }
        if (k.restoring)
            return; // the old copy, sent before it got its own back
        k.b = std::move(b);
        k.have = true;
        g_tables++;
        H->log(M, SK_INFO, "badges: %s's table kept: %u badges (%u earned), %u counters%s", name.c_str(), (unsigned)k.b.badge.size(),
               (unsigned)std::count_if(k.b.badge.begin(), k.b.badge.end(), [](auto& x) { return x.second == 1; }),
               (unsigned)k.b.event.size(), fresh ? " (fresh)" : "");
        return;
    }
    if (kind != K_BADGES_KEPT || spaceext::is_host() || to != spaceext::my_id())
        return;
    uint32_t e = spaceext::world_epoch();
    if (!r.u8()) { // nothing kept for me: the badges of this world are mine
        return;
    }
    Badges b;
    if (!get_badges(r, b))
        return;
    if (own_save())
        g_own_held++;
    else if (g_fix && spaceext::in_space() && e != g_applied_epoch && e != g_join_epoch) {
        if (!apply(b, e, "the host"))
            return; // no manager now: the host sends it again
    } else if (e != g_applied_epoch && g_fix)
        return; // not in this world yet: the host sends it again
    net::Writer w;
    w.u8(0);
    w.u32(id);
    spaceext::send(K_BADGES_DONE, w);
}

static void on_session_end()
{
    g_join_seen = false;
    g_fresh_epoch = 0;
    g_have_last = false;
    g_next = 0;
    for (auto& [n, k] : g_kept)
        k.id = 0xFF, k.restoring = false;
}

static std::string status()
{
    char b[300];
    snprintf(b, sizeof b,
             "badges: fix=%d rank=%d kept=%u tables=%u sent_back=%u done=%u | guest sent=%u restored=%u own_save=%u",
             (int)g_fix, rank(), (unsigned)g_kept.size(), g_tables, g_sent_back, g_done, g_sent, g_restored, g_own_held);
    return b;
}

static std::string debug(const std::vector<std::string>& a)
{
    // coop.space badges [give <id> | event <k> <n> | kept]: this game's badge manager
    void* m = manager();
    if (!m)
        return "badges: no badge manager (not in Space)";
    if (a.size() >= 2 && a[0] == "give") {
        uint32_t id = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        uintptr_t give = H->sym("SporeCoop::BadgeManager_GiveBadge");
        if (!give)
            return "badges: no GiveBadge symbol";
        thiscall<void>(give, m, id);
        return "badges: gave " + a[1] + ", rank now " + std::to_string(rank());
    }
    if (a.size() >= 3 && a[0] == "event") {
        thiscall<void>(a_Progress, m, (int)strtol(a[1].c_str(), nullptr, 0), (int)strtol(a[2].c_str(), nullptr, 0));
        return "badges: event " + a[1] + " += " + a[2];
    }
    std::string s = status() + "\n";
    char b[96];
    if (a.size() >= 1 && a[0] == "kept") {
        for (auto& [n, k] : g_kept) {
            snprintf(b, sizeof b, "kept %s: %u badges, id %u, restoring %d\n", n.c_str(), (unsigned)k.b.badge.size(), k.id,
                     (int)k.restoring);
            s += b;
        }
        return s;
    }
    Badges t;
    if (!read_badges(t))
        return s + "unreadable";
    snprintf(b, sizeof b, "rank=%d card=%08X\n", rank(), t.card);
    s += b;
    for (auto& [k, v] : t.badge) {
        snprintf(b, sizeof b, "badge %08X %d\n", k, v);
        s += b;
    }
    for (auto& [k, v] : t.stage) {
        snprintf(b, sizeof b, "stage %08X %d\n", k, v);
        s += b;
    }
    for (auto& [k, v] : t.event) {
        snprintf(b, sizeof b, "event %d %d\n", (int)k, v);
        s += b;
    }
    return s;
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    a_SpaceGame = H->sym("Simulator::cSimulatorSpaceGame::ptr");
    a_MapBadge = H->sym("SporeCoop::MapU32Int_BadgeMap_At");
    a_MapStage = H->sym("SporeCoop::MapU32Int_StageMap_At");
    a_MapEvent = H->sym("SporeCoop::MapU32Int_EventCounts_At");
    a_FreeNodes = H->sym("SporeCoop::RbTree_FreeNodes");
    a_Next = H->sym("SporeCoop::RbTree_Next");
    a_Progress = H->sym("Simulator::cBadgeManager::AddToBadgeProgress");
    a_Rank = H->sym("SporeCoop::BadgeManager_GetRank");
    return a_SpaceGame && a_MapBadge && a_MapStage && a_MapEvent && a_FreeNodes && a_Next && a_Progress && a_Rank;
}

static bool reg = spaceext::add({.name = "badges",
                                 .kind_lo = 85,
                                 .kind_hi = 89,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = on_session_end,
                                 .status = status,
                                 .debug = debug});

} // namespace spacebadges
