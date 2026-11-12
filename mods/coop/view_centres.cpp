#include "view_centres.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <windows.h>

#include "fixes.h"
#include "game.h"
#include "sporekit.h"
#include "sporekit_game.hpp"

namespace views {

static const SkHost* H;
static SkMod* M;
static std::map<uint8_t, Centre> g_centres;

// BUG-0280: off = the game's own list only (the camera of the host): NPCs, nests and spawns far
// from the host go back to the pool even next to a partner (0.7.3.x)
static bool g_keep = true;
COOP_FIX("npc.view_centres", "BUG-0280", g_keep, "creature: the host keeps NPCs, nests and spawns alive around every partner too");

bool keep_alive_on() { return g_keep; }

// a position that is not finite is no centre (BUG-0292): it would make every stage's box NaN
void note(uint8_t id, const net::Vec3& pos, uint32_t mode, double now)
{
    if (!finite_pos(pos)) {
        g_centres.erase(id);
        return;
    }
    g_centres[id] = {pos, id, mode, now};
}
void forget(uint8_t id) { g_centres.erase(id); }
void clear() { g_centres.clear(); }

bool of(uint8_t id, uint32_t mode, double now, net::Vec3& out, double max_age)
{
    auto it = g_centres.find(id);
    if (it == g_centres.end() || it->second.mode != mode || now - it->second.at > max_age || now < it->second.at)
        return false;
    out = it->second.pos;
    return true;
}

size_t partners(uint32_t mode, double now, Centre* out, size_t max, double max_age)
{
    size_t n = 0;
    for (auto& [id, c] : g_centres) {
        if (n >= max)
            break;
        if (c.mode != mode || now - c.at > max_age || now < c.at)
            continue;
        out[n++] = c;
    }
    return n;
}

// The relevance pass of the Creature stage: SporeCoop::Relevance_Update (GOG 2024 0xACE4E0,
// thiscall(mgr, int, uint32 dt_ms) ret 8), called by SporeCoop::Relevance_Tick 0xAD2300, which
// pushes one sphere {centre, 100^2, 250^2} (the camera's view point) onto the vector mgr+0x20
// (begin) / +0x24 (end) / +0x28 (capacity) before and erases the vector after. The pass enables
// herds and spawn points within the first radius of any sphere, disables those out of the second
// one of every sphere, and sends creatures out of every sphere's second radius back to the pool
// (creature vt 0xC8(1)). The hook lends the vector a buffer of ours for the one call: the game's
// spheres first, then one per partner with the radii of the game's first sphere; the game's own
// pointers come back right after it, so the game's allocator never sees our buffer.
struct Sphere {
    float x, y, z, enter_sq, leave_sq;
};
static_assert(sizeof(Sphere) == 0x14, "the game's sphere is 5 floats");
constexpr size_t kMaxSpheres = 16;
static Sphere g_buf[kMaxSpheres];
static uint32_t g_passes, g_passes_extra, g_extra_max, g_game_spheres_max;
static double g_last_extra_at;

// The pass runs in the Creature stage (0x1654C01) and the Tribe stage (0x1654C02, Relevance_Tick
// after 0xACE420 / 0xAD11E0). A partner's centre there is what coop.cpp notes for it: in Creature
// its avatar's position, in Tribe its pointer (modes.cpp rts_fill: the point of the planet under its
// mouse, which lies in its view; it stays where it was while the mouse is off the planet).
// `npc.view_centres_tribe off`: Creature only (the first version of this fix).
static bool g_tribe = true;
COOP_FIX("npc.view_centres_tribe", "BUG-0280", g_tribe, "tribe: the host keeps animals and herds alive around every partner's view too");
static bool stage_on(uint32_t mode) { return mode == 0x1654C01u || (g_tribe && mode == 0x1654C02u); }

using RelevanceFn = void*(SK_THISCALL*)(void* mgr, uint32_t a, uint32_t dt_ms);
static RelevanceFn o_Relevance;

static Sphere g_game_last; // the game's own first sphere at the last pass (diagnostics)
static bool g_on_ours;     // BUG-0882: the game came into the last pass with its vector on g_buf

// BUG-0882 (see h_Relevance): what was lent, for the ledger's undo
// (the switch is game.lend_ledger in game.cpp)
#define g_lend_fix (game::lend_ledger_on())
struct Lent {
    Sphere** v = nullptr;
    Sphere *b = nullptr, *e = nullptr, *cap = nullptr;
};
static Lent g_lent;
bool lend_ledger_on() { return game::lend_ledger_on(); }
static bool g_test_unwind;
static uint32_t g_unwound, g_lend_undone;
static void undo_lend(void* p, uintptr_t)
{
    Lent* l = (Lent*)p;
    if (l->v && !IsBadWritePtr(l->v, 12) && l->v[0] >= g_buf && l->v[0] < g_buf + kMaxSpheres) { // still on ours
        l->v[0] = l->b;
        l->v[1] = l->e;
        l->v[2] = l->cap;
        g_lend_undone++;
    }
    l->v = nullptr;
}

static void* SK_THISCALL h_Relevance(void* mgr, uint32_t a, uint32_t dt_ms)
{
    g_passes++;
    if (mgr && !IsBadReadPtr((uint8_t*)mgr + 0x20, 12)) { // BUG-0882 (tests): the vector the game comes with
        Sphere* v0 = *(Sphere**)((uint8_t*)mgr + 0x20);
        g_on_ours = v0 >= g_buf && v0 < g_buf + kMaxSpheres;
    }
    if (mgr && !IsBadReadPtr((uint8_t*)mgr + 0x20, 8)) {
        Sphere* b0 = *(Sphere**)((uint8_t*)mgr + 0x20);
        if (b0 && b0 != *(Sphere**)((uint8_t*)mgr + 0x24) && !IsBadReadPtr(b0, sizeof(Sphere)))
            g_game_last = *b0;
    }
    if (!g_keep || g_centres.empty() || !mgr || !stage_on(game::mode_id()))
        return o_Relevance(mgr, a, dt_ms);
    Sphere** v = (Sphere**)((uint8_t*)mgr + 0x20);
    Sphere *b = v[0], *e = v[1], *cap = v[2];
    size_t own = (b && e > b) ? (size_t)(e - b) : 0;
    if (own == 0 || own >= kMaxSpheres) // the game always has its camera sphere here
        return o_Relevance(mgr, a, dt_ms);
    double now = H->time_seconds();
    Centre c[kMaxSpheres];
    size_t n = partners(game::mode_id(), now, c, kMaxSpheres - own);
    if (!n)
        return o_Relevance(mgr, a, dt_ms);
    memcpy(g_buf, b, own * sizeof(Sphere));
    for (size_t i = 0; i < n; i++)
        g_buf[own + i] = {c[i].pos.x, c[i].pos.y, c[i].pos.z, b[0].enter_sq, b[0].leave_sq};
    // BUG-0882: the game's handler catches faults inside the pass (BUG-0110); a fault unwinds past the restore below
    // and the game's sphere vector stays on our static buffer (the next pass then copies from it and "restores" it
    // for good; the game's vector code may free or grow it: heap corruption). The lend goes into the swap ledger, so
    // the frame's swap guard puts the game's pointers back. Off (`coop.fix game.lend_ledger off`): no entry.
    g_lent = {v, b, e, cap};
    size_t mark = g_lend_fix ? game::swap_open(undo_lend, &g_lent, 0, "relevance sphere vector (view centres)") : 0;
    v[0] = g_buf;
    v[1] = g_buf + own + n;
    v[2] = g_buf + kMaxSpheres;
    void* r = o_Relevance(mgr, a, dt_ms);
    if (g_test_unwind) { // tests (BUG-0882, `coop.views unwind`): as if a fault in the pass unwound past us
        g_test_unwind = false;
        g_unwound++;
        return r;
    }
    v[0] = b;
    v[1] = e;
    v[2] = cap;
    g_lent.v = nullptr;
    if (g_lend_fix)
        game::swap_close(mark);
    g_passes_extra++;
    g_last_extra_at = now;
    if (n > g_extra_max)
        g_extra_max = (uint32_t)n;
    if (own > g_game_spheres_max)
        g_game_spheres_max = (uint32_t)own;
    return r;
}

std::string status()
{
    char b[200];
    double now = H ? H->time_seconds() : 0;
    snprintf(b, sizeof b, "views: on=%d hooked=%d partners=%u passes=%u passes_with_partners=%u max_partners=%u "
             "game_spheres_max=%u last_extra=%.1fs ago",
             (int)g_keep, (int)(o_Relevance != nullptr), (unsigned)g_centres.size(), g_passes, g_passes_extra,
             g_extra_max, g_game_spheres_max, g_last_extra_at > 0 ? now - g_last_extra_at : -1.0);
    std::string s = b;
    snprintf(b, sizeof b, "\n  game sphere (%.1f %.1f %.1f) enter %.0f m leave %.0f m", g_game_last.x, g_game_last.y,
             g_game_last.z, g_game_last.enter_sq > 0 ? sqrtf(g_game_last.enter_sq) : 0.f,
             g_game_last.leave_sq > 0 ? sqrtf(g_game_last.leave_sq) : 0.f);
    s += b;
    for (auto& [id, c] : g_centres) {
        snprintf(b, sizeof b, "\n  player %u mode %08X pos (%.1f %.1f %.1f) age %.1f s", id, c.mode, c.pos.x, c.pos.y,
                 c.pos.z, now - c.at);
        s += b;
    }
    return s;
}

static int c_views(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "unwind")) { // tests (BUG-0882): the next pass with partners leaves without its restore
        g_test_unwind = true;
        H->out_printf(out, "the next relevance pass with partners leaves like a fault\n");
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    // BUG-0882: where the game's sphere vector (relevance manager +0x20) points now: its own heap, or our buffer
    H->out_printf(out, "\n  game vector on_our_buffer=%d unwound=%u undone=%u lend_fix=%d", (int)g_on_ours, g_unwound,
                  g_lend_undone, (int)g_lend_fix);
    return 0;
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    H->command_add(M, "coop.views", "coop.views - the partners' view centres the host keeps its world alive around (BUG-0280)",
                   c_views, nullptr, SK_CMD_MAIN_THREAD);
    uintptr_t a = H->sym("SporeCoop::Relevance_Update");
    if (!a || H->hook_create(M, (void*)a, (void*)&h_Relevance, (void**)&o_Relevance) != 0) {
        H->log(M, SK_WARN, "views: no hook on the creature relevance pass: NPCs live only around the host");
        o_Relevance = nullptr;
        return false;
    }
    return true;
}

} // namespace views
