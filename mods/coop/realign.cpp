// Whole-planet re-alignment after transitions (realign.h, realign_rule.h, docs/re/tribe.md#tribe-realign).
#include "realign.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/protocol.h"
#include "realign_rule.h"
#include "sporekit_game.hpp"
#include "tribe.h"

namespace realign {

using namespace skg;

static const SkHost* H;
static SkMod* M;
static Ctx C;

// BUG-0564: a village (tribe, hut, tools, food mat, map mark) of a guest's game the host's world has not goes.
// `coop.fix realign.villages off`: such a village is emptied of its members only (BUG-0483, 0.7.6).
static bool g_fix_villages = true;
COOP_FIX("realign.villages", "BUG-0564", g_fix_villages,
         "tribe: after the host's game over and reload (or a rejoin) a guest's villages the host's world has not are gone");
// BUG-0567 / BUG-0566: a hut's or tool's health on a guest is the host's (a damaged, smoking building of the old
// world mended; the enemy hut's health bar going down as the host's). `coop.fix realign.buildings off`: each
// game keeps its own (0.7.6).
static bool g_fix_buildings = true;
COOP_FIX("realign.buildings", "BUG-0567 BUG-0566", g_fix_buildings,
         "tribe: a hut's and a tool's health on a guest is the host's (no broken building of the old world)");

constexpr uint8_t kStCreature = 1, kStTribe = 2, kStCiv = 4;
constexpr uintptr_t kHp = 0x120 + 0x38, kMaxHp = 0x120 + 0x2C; // huts / tools: cCombatant at +0x120 (docs/re/tribe.md)

enum Kind : uint8_t { kVillage, kHut, kTool, kFoodMat, kNest, kCity, kKinds };
struct KindDef {
    const char* name;
    uint32_t noun;
    uint8_t stages;
    float tol;
    bool health; // state = health in 1/1000 of the maximum
};
static const KindDef kDefs[kKinds] = {
    {"village", 0x018C6D19, kStTribe, 40.f, false}, // the member sync's 40 m (tribe.cpp guest_state)
    {"hut", 0x01E4DAAE, kStTribe, 10.f, true},
    {"tool", 0x018C8F0C, kStTribe, 3.f, true},
    {"food_mat", 0x0629BAFE, kStTribe, 10.f, false},
    {"nest", 0x52AA6122, kStCreature | kStTribe, 10.f, false},
    {"city", 0x018C43E8, kStCiv, 30.f, false},
};

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

static uint8_t stage_bit()
{
    uint32_t m = game::mode_id();
    return m == modes::kCreature ? kStCreature : m == modes::kTribe ? kStTribe : m == modes::kCiv ? kStCiv : 0;
}

static void* player_tribe()
{
    void* nm = game::noun_manager();
    return nm && skg::readable(nm, 0x74) ? field<void*>(nm, 0x70) : nullptr; // cGameNounManager::mpPlayerTribe
}

struct Local {
    void* p;
    uint8_t kind;
    Obj o;
};

static uint16_t health_of(void* o)
{
    if (!skg::readable(o, kHp + 4))
        return 0;
    float hp = field<float>(o, kHp), mx = field<float>(o, kMaxHp);
    if (!(mx > 0) || !std::isfinite(hp))
        return 0;
    return (uint16_t)std::clamp((int)std::lround(hp / mx * 1000.f), 0, 1000);
}

// this game's objects of the stage's kinds on the whole planet (the noun list: every placed one, not only those
// near the camera)
static std::vector<Local> collect(uint8_t sb)
{
    struct U {
        uint8_t sb;
        void* pt;
        std::vector<Local> out;
    } u{sb, player_tribe(), {}};
    game::for_each_object(
        [](void* o, uint32_t noun, void* up) {
            auto* u = (U*)up;
            for (uint8_t k = 0; k < kKinds; k++) {
                if (kDefs[k].noun != noun || !(kDefs[k].stages & u->sb) || !game::object_alive(o))
                    continue;
                Local l{o, k, {}};
                net::Vec3 p;
                if (k == kVillage && skg::readable(o, 0x130)) { // cTribe position +0x124 (tribe.cpp tribe_pos)
                    p = {field<float>(o, 0x124), field<float>(o, 0x128), field<float>(o, 0x12C)};
                } else if (!game::object_position(o, p))
                    break;
                l.o.x = p.x, l.o.y = p.y, l.o.z = p.z;
                if (k == kVillage)
                    l.o.fl = o == u->pt ? kFlPlayer : 0;
                else if (k == kHut && skg::readable(o, 0x234))
                    l.o.fl = field<void*>(o, 0x230) == u->pt && u->pt ? kFlPlayer : 0; // the hut's tribe (docs/re/tribe.md)
                else if (k == kTool && skg::readable(o, 0x1FC))
                    l.o.fl = field<void*>(o, 0x1F8) == u->pt && u->pt ? kFlPlayer : 0;
                if (kDefs[k].health)
                    l.o.st = health_of(o);
                if (u->out.size() < 448)
                    u->out.push_back(l);
                break;
            }
            return true;
        },
        &u);
    return u.out;
}

// ---------------------------------------------------------------- host
constexpr uint8_t kMsgPlanet = 2; // MSG_WOBJ sub-kind (worldobj.cpp: 0 snapshot, 1 request)
constexpr int kPerChunk = 56, kMaxChunks = 8;
constexpr double kInterval = 2.0;
static uint16_t g_cycle;
static double g_sent_at = -100;
static uint32_t g_host_epoch;
static std::set<uint8_t> g_in_place;
static uint32_t g_tx, g_tx_soon;
static bool g_soon;

static void host_tick(double now)
{
    uint8_t sb = stage_bit();
    if (!sb || (!g_fix_villages && !g_fix_buildings)) // both off: nothing sent (0.7.6)
        return;
    uint32_t ep = game::stage_epoch(game::mode_id());
    if (ep != g_host_epoch) { // a load, the game over's reload: the guests re-align at once
        g_host_epoch = ep;
        g_soon = true;
    }
    std::set<uint8_t> in;
    for (auto& p : C.sess->players())
        if (p.id != 0 && (!C.in_place || C.in_place(p.id)))
            in.insert(p.id);
    for (uint8_t id : in)
        if (!g_in_place.count(id)) // a guest (back) in our place: a join, a rejoin, an editor return
            g_soon = true;
    g_in_place.swap(in);
    if (g_in_place.empty() || !(now - g_sent_at >= kInterval || (g_soon && now - g_sent_at >= 0.5)))
        return;
    g_tx_soon += g_soon;
    g_soon = false;
    g_sent_at = now;
    auto all = collect(sb);
    int chunks = std::clamp((int)((all.size() + kPerChunk - 1) / kPerChunk), 1, kMaxChunks);
    g_cycle++;
    for (int c = 0; c < chunks; c++) {
        net::Writer w;
        w.u8(kMsgPlanet);
        w.u16(g_cycle);
        w.u8((uint8_t)c);
        w.u8((uint8_t)chunks);
        w.u8(sb);
        w.u32(ep);
        size_t from = (size_t)c * kPerChunk, to = std::min(all.size(), from + kPerChunk);
        w.u8((uint8_t)(to > from ? to - from : 0));
        for (size_t i = from; i < to; i++) {
            const Local& l = all[i];
            w.u8(l.kind);
            w.f32(l.o.x);
            w.f32(l.o.y);
            w.f32(l.o.z);
            w.u16(l.o.st);
            w.u8(l.o.fl);
        }
        for (uint8_t id : g_in_place)
            C.sess->send_unreliable_to(id, net::MSG_WOBJ, w);
    }
    g_tx++;
}

// ---------------------------------------------------------------- guest
struct Rows {
    uint32_t host = 0, local = 0, lack = 0, extra = 0, state_off = 0;
};
static Rows g_rows[kKinds];
static uint32_t g_removed[kKinds], g_state_fixed[kKinds], g_rx, g_compares, g_incomplete, g_host_changes;
static uint16_t g_rx_cycle;
static std::set<uint8_t> g_rx_chunks;
static std::vector<std::pair<uint8_t, Obj>> g_rx_objs;
static uint8_t g_rx_sb;
static uint32_t g_rx_epoch, g_seen_epoch;
static bool g_have_epoch, g_pending;
static std::vector<std::pair<uint8_t, Obj>> g_list; // the last complete host list, compared in tick
static uint8_t g_list_sb;
static Streaks g_streak[kKinds];
static double g_settle_until;
static uint32_t g_own_epoch;
static bool g_log_next;

static void guest_reset()
{
    for (auto& s : g_streak)
        s.clear();
    g_pending = false;
    g_rx_chunks.clear();
    g_rx_objs.clear();
}

void on_planet(uint8_t from, net::Reader& r)
{
    if (from != 0 || is_host())
        return;
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), sb = r.u8();
    uint32_t ep = r.u32();
    uint8_t n = r.u8();
    if (!r.ok() || chunks == 0 || chunks > kMaxChunks || chunk >= chunks || n > kPerChunk)
        return;
    std::vector<std::pair<uint8_t, Obj>> objs;
    for (int i = 0; i < n; i++) {
        Obj o;
        uint8_t k = r.u8();
        o.x = r.f32(), o.y = r.f32(), o.z = r.f32();
        o.st = r.u16();
        o.fl = r.u8();
        if (!r.ok())
            return;
        if (k >= kKinds || !std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.z))
            continue; // a kind of a newer host: skipped
        objs.push_back({k, o});
    }
    g_rx++;
    if (cycle != g_rx_cycle || g_rx_chunks.empty()) {
        if (!g_rx_chunks.empty())
            g_incomplete++;
        g_rx_cycle = cycle;
        g_rx_chunks.clear();
        g_rx_objs.clear();
    }
    g_rx_sb = sb;
    g_rx_epoch = ep;
    g_rx_chunks.insert(chunk);
    g_rx_objs.insert(g_rx_objs.end(), objs.begin(), objs.end());
    if ((int)g_rx_chunks.size() == chunks) {
        g_list.swap(g_rx_objs);
        g_list_sb = sb;
        g_pending = true;
        g_rx_chunks.clear();
        g_rx_objs.clear();
        if (!g_have_epoch || ep != g_seen_epoch) { // the host's world changed (or the first list)
            if (g_have_epoch) {
                g_host_changes++;
                g_log_next = true;
                H->log(M, SK_INFO, "realign: the host's world changed (a load or the game over's reload): re-aligning this game's planet");
            }
            g_have_epoch = true;
            g_seen_epoch = ep;
            for (auto& s : g_streak)
                s.clear();
        }
    }
}

static void compare(double now)
{
    uint8_t sb = stage_bit();
    if (!sb || sb != g_list_sb)
        return; // the host's list of another stage (one of the two still changes)
    g_compares++;
    auto locals = collect(sb);
    std::string summary;
    for (uint8_t k = 0; k < kKinds; k++) {
        if (!(kDefs[k].stages & sb))
            continue;
        std::vector<Obj> h, l;
        std::vector<const Local*> lp;
        for (auto& [hk, o] : g_list)
            if (hk == k)
                h.push_back(o);
        for (auto& x : locals)
            if (x.kind == k) {
                l.push_back(x.o);
                lp.push_back(&x);
            }
        Match m = match(h, l, kDefs[k].tol);
        Rows& rw = g_rows[k];
        rw = Rows{(uint32_t)h.size(), (uint32_t)l.size(), (uint32_t)m.lack.size(), (uint32_t)m.extra.size(), 0};
        // the state of bound pairs
        for (auto& [hi, li] : m.bound) {
            if (!kDefs[k].health || !state_off(h[hi].st, l[li].st))
                continue;
            rw.state_off++;
            void* o = lp[li]->p;
            if (!g_fix_buildings || !skg::readable(o, kHp + 4))
                continue;
            float mx = field<float>(o, kMaxHp);
            if (!(mx > 0))
                continue;
            field<float>(o, kHp) = std::max(1.f, mx * h[hi].st / 1000.f); // never 0: the guest's game runs no defeat
            g_state_fixed[k]++;
            if (g_state_fixed[k] <= 8)
                H->log(M, SK_INFO, "realign: %s at (%.0f %.0f %.0f): health %u/1000 here, the host's %u: the host's now", kDefs[k].name,
                       l[li].x, l[li].y, l[li].z, l[li].st, h[hi].st);
        }
        // extra objects: the villages go (their huts, tools and food mats with them)
        std::vector<uint64_t> keys, due;
        for (int li : m.extra)
            keys.push_back((uint64_t)(uintptr_t)lp[li]->p);
        g_streak[k].step(keys, h.size(), l.size(), due);
        if (k == kVillage && g_fix_villages) {
            int done = 0;
            for (uint64_t key : due) {
                if (done >= 4)
                    break;
                for (int li : m.extra)
                    if ((uint64_t)(uintptr_t)lp[li]->p == key) {
                        int r = tribe::remove_village(lp[li]->p);
                        if (r > 0) {
                            done++;
                            g_removed[k]++;
                            H->log(M, SK_INFO, "realign: the village at (%.0f %.0f %.0f) is not in the host's world: removed here (BUG-0564)",
                                   l[li].x, l[li].y, l[li].z);
                        }
                        break;
                    }
            }
        }
        if (g_log_next && (rw.lack || rw.extra || rw.state_off)) {
            char b[96];
            snprintf(b, sizeof b, " %s %u/%u lack=%u extra=%u state=%u", kDefs[k].name, rw.local, rw.host, rw.lack, rw.extra, rw.state_off);
            summary += b;
        }
    }
    if (g_log_next) {
        H->log(M, SK_INFO, "realign: first look after the host's change:%s", summary.empty() ? " the same planet" : summary.c_str());
        g_log_next = false;
    }
    (void)now;
}

static void guest_tick(double now)
{
    uint32_t own = game::stage_epoch(game::mode_id());
    if (own != g_own_epoch) { // this game's own transition (a load, a rejoin's world, a new stage)
        g_own_epoch = own;
        guest_reset();
        g_settle_until = now + 3.0;
    }
    if (!g_pending)
        return;
    g_pending = false;
    if ((C.host_away && C.host_away()) || now < g_settle_until || game::leaving_stage())
        return;
    compare(now);
}

// ---------------------------------------------------------------- the rest
void tick(double now)
{
    if (!C.sess)
        return;
    if (is_host() && C.sess->connected())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

void on_session_end()
{
    guest_reset();
    g_have_epoch = false;
    g_in_place.clear();
    g_list.clear();
}

std::string status()
{
    std::string s;
    char b[256];
    snprintf(b, sizeof b, "realign role=%s villages=%d buildings=%d tx=%u soon=%u rx=%u compares=%u incomplete=%u host_changes=%u\n",
             is_host() ? "host" : is_guest() ? "guest" : "none", (int)g_fix_villages, (int)g_fix_buildings, g_tx, g_tx_soon, g_rx, g_compares,
             g_incomplete, g_host_changes);
    s += b;
    for (uint8_t k = 0; k < kKinds; k++) {
        const Rows& r = g_rows[k];
        snprintf(b, sizeof b, "kind %s here=%u host=%u lack=%u extra=%u state_off=%u removed=%u state_fixed=%u\n", kDefs[k].name, r.local,
                 r.host, r.lack, r.extra, r.state_off, g_removed[k], g_state_fixed[k]);
        s += b;
    }
    return s;
}

static int kind_of(const char* n)
{
    for (int k = 0; k < kKinds; k++)
        if (!strcmp(kDefs[k].name, n))
            return k;
    return -1;
}

// coop.realign                         the counters and the last compare's rows per kind
// coop.realign planet [kind]           this game's whole-planet list (tests compare host and guest)
// coop.realign hp <kind> <x> <y> <z> <permille>   tests: the health of the nearest hut / tool (within 15 m)
static int c_realign(void*, int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "";
    if (!strcmp(a, "planet")) {
        int want = argc > 2 ? kind_of(argv[2]) : -1;
        auto all = collect(stage_bit());
        for (auto& l : all)
            if (want < 0 || l.kind == want)
                H->out_printf(out, "pl %s (%.1f %.1f %.1f) st=%u fl=%u\n", kDefs[l.kind].name, l.o.x, l.o.y, l.o.z, l.o.st, l.o.fl);
        H->out_printf(out, "planet %zu objects\n", all.size());
        return 0;
    }
    if (!strcmp(a, "hp") && argc > 6) {
        int k = kind_of(argv[2]);
        if (k < 0 || !kDefs[k].health) {
            H->out_printf(out, "no kind with health\n");
            return 1;
        }
        Obj at;
        at.x = (float)atof(argv[3]), at.y = (float)atof(argv[4]), at.z = (float)atof(argv[5]);
        float pm = (float)atof(argv[6]);
        const Local* best = nullptr;
        float bd = 15.f;
        auto all = collect(stage_bit());
        for (auto& l : all)
            if (l.kind == k && dist(l.o, at) < bd) {
                bd = dist(l.o, at);
                best = &l;
            }
        if (!best || !skg::readable(best->p, kHp + 4) || !(field<float>(best->p, kMaxHp) > 0)) {
            H->out_printf(out, "none near\n");
            return 1;
        }
        field<float>(best->p, kHp) = std::max(1.f, field<float>(best->p, kMaxHp) * pm / 1000.f);
        H->out_printf(out, "%s at (%.1f %.1f %.1f): health %u/1000\n", kDefs[k].name, best->o.x, best->o.y, best->o.z, health_of(best->p));
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    H->command_add(M, "coop.realign",
                   "coop.realign [planet [kind] | hp <kind> <x> <y> <z> <permille>] - the whole planet re-aligned with the host's after "
                   "transitions (BUG-0564, BUG-0567)",
                   c_realign, nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace realign
