// BUG-1475 (#217 a, the idea of PR #217 by IltonBJSilva; WP-K1 of docs/plan-0.7.17.md): a guest plays its own captain
// on its own game in Space. docs/re/space.md#space-own-captain, the rules captain::own_step (captain_logic.h).
//
// A guest in the host's Space world plays the host's empire, whose captain (cEmpire +0xC4, mCaptainKey) is the host's
// (the save's). With creature.mode own and a creation of its own in play (creations::plays_own), the guest's game
// takes that creation as the empire's captain 5 s after the Space world came (the world's own start first), when this
// game can load it and has its species (BUG-1111's portrait and BUG-1278's stats read both; their guards stay). The key
// is written as cEmpire_SetCaptainKey 0xC31100 writes it, without the setter's 0xC75B40: that one writes the player's
// consequence cards into the captain creation's Sporepedia metadata in its own database (0x555F70, Ghidra 2026-11-23),
// which would change the guest's own My Creations creature for good. A captain the game sets later (the outfitter's
// result, "Name Your Captain") is the player's own: kept as its captain, remembered per world (worlds.ini [captain]).
// An own creation under a key of the host's world (BUG-0278: shadowed here) keeps the host's captain.
// Saves: mCaptainKey is a key attribute (cEmpire's table 0x1572B60, entry 0x1572D04, written by Stream_WriteResourceKey);
// while a save is written (savestrip's save hooks) the field holds the world's captain and ours again after it: the
// guest's save names the host's captain only. At the session's end the world's captain is put back.
// `coop.fix space.own_captain off`: the empire keeps the host's captain (0.7.16.1).
#include "own_captain.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "captain_logic.h"
#include "creations.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "savestrip.h"
#include "sporekit_game.hpp"

namespace own_captain {

static const SkHost* H;
static SkMod* M;
static net::Session* S;
static uintptr_t a_Empire;

static bool g_fix = true;
COOP_FIX("space.own_captain", "BUG-1475", g_fix,
         "space: a guest's empire has the guest's own creature as its captain on the guest's game (the host's in saves)");

struct K3 {
    uint32_t i = 0, t = 0, g = 0;
};
static K3 g_orig, g_mine;             // the world's captain, ours (instance 0: none set)
static uint32_t g_epoch, g_world;     // the Space world they belong to
static double g_since, g_next;
static bool g_saving;                 // the save hooks put the world's captain in
static bool g_adopted;                // g_mine is a captain the game made for this player
static bool g_orig_read;              // g_orig holds the world's captain
static unsigned g_sets, g_adopts, g_saves, g_puts, g_shadowed, g_unknown;
static const char* g_why = "";

static uint8_t* empire()
{
    uint8_t* e = a_Empire ? skg::cdecl_call<uint8_t*>(a_Empire) : nullptr;
    return e && skg::readable(e + 0xC4, 12) ? e : nullptr;
}
static K3 at(uint8_t* e)
{
    K3 k;
    memcpy(&k, e + 0xC4, 12);
    return k;
}
static void write(uint8_t* e, const K3& k) { memcpy(e + 0xC4, &k, 12); }
static bool same(const K3& a, const K3& b) { return a.i == b.i && a.t == b.t && a.g == b.g; }

static bool guest() { return S && S->role() == net::Role::Client && S->connected(); }

// the creation this game would take as its captain now ({} with g_why: none)
static K3 wanted()
{
    if (creations::one_creature()) {
        g_why = "creature.mode host";
        return {};
    }
    if (!creations::plays_own(creations::kCreature)) {
        g_why = "no own creature in play";
        return {};
    }
    creations::Key k = creations::mine(creations::kCreature).key;
    if (uint32_t note = creations::world_note("captain", g_world)) { // the player's own captain of this world
        creations::Key n{note, captain::kCreatureType, captain::kCreatureGroup};
        if (creations::key_known(n) && !creations::host_shadows(creations::kCreature, n) &&
            game::species_by_key({n.instance, n.type, n.group}))
            k = n;
    }
    if (!k.instance || !captain::is_creature(k.type, k.group)) {
        g_why = "no own creature key";
        return {};
    }
    if (creations::host_shadows(creations::kCreature, k)) {
        g_shadowed++;
        g_why = "the own key is one of the host's world here (BUG-0278)";
        return {};
    }
    if (!creations::key_known(k) || !game::species_by_key({k.instance, k.type, k.group})) {
        g_unknown++;
        g_why = "the own creature is not loadable here yet";
        return {};
    }
    g_why = "";
    return {k.instance, k.type, k.group};
}

// the world's captain back (session end, the switch turned off)
static void put_back()
{
    uint8_t* e = g_mine.i ? empire() : nullptr;
    if (e && same(at(e), g_mine) && g_orig.i) {
        write(e, g_orig);
        g_puts++;
    }
    g_mine = {};
}

void tick(double now)
{
    if (now < g_next)
        return;
    g_next = now + 0.5;
    const bool space = game::mode_id() == modes::kSpace;
    const uint32_t ep = space ? game::stage_epoch(modes::kSpace) : 0;
    if (!space || ep != g_epoch) { // another world (or none): its own captain and empire
        g_epoch = ep;
        g_world = modes::current_world();
        g_since = now;
        g_orig = g_mine = {};
        g_adopted = g_orig_read = false;
    }
    uint8_t* e = space ? empire() : nullptr;
    if (!e)
        return;
    if (!g_fix || !guest()) {
        if (g_mine.i)
            put_back();
        return;
    }
    const K3 cur = at(e);
    if (now - g_since < captain::kSettle)
        return; // (the world's own start: its captain is set at the Space enter, "Name Your Captain" may ask)
    if (!g_orig_read) {
        g_orig = cur;
        g_orig_read = true;
    }
    const K3 want = wanted();
    if (!want.i && g_mine.i && !g_adopted) { // no own creature any more (host mode, another choice): the world's
        put_back();
        return;
    }
    if (g_mine.i && !g_adopted && want.i && want.i != g_mine.i && same(cur, g_mine)) {
        write(e, want); // another own creature chosen: it is the captain now
        g_mine = want;
        g_sets++;
        return;
    }
    switch (captain::own_step(true, want.i || g_mine.i, now - g_since, cur.i, g_mine.i, g_orig.i, want.i)) {
    case captain::Own::kSet:
        write(e, want);
        g_mine = want;
        if (++g_sets <= 8)
            H->log(M, SK_INFO, "own captain: our empire's captain is our own %08X here (the world's %08X, BUG-1475)", want.i,
                   g_orig.i);
        break;
    case captain::Own::kAdopt: // the game made a new captain for us (the outfitter): ours
        g_mine = cur;
        g_adopted = true;
        g_adopts++;
        creations::world_note_set("captain", g_world, cur.i);
        H->log(M, SK_INFO, "own captain: the game gave us the captain %08X: kept as ours (BUG-1475)", cur.i);
        break;
    case captain::Own::kHold:
        if (!g_mine.i)
            g_mine = cur;
        break;
    case captain::Own::kNone:
        break;
    }
}

// savestrip's save hooks (main thread): the world's captain for the save, ours after it
static void save_start()
{
    uint8_t* e = g_mine.i && g_orig.i ? empire() : nullptr;
    if (e && same(at(e), g_mine)) {
        write(e, g_orig);
        g_saving = true;
        g_saves++;
    }
}
static void save_end()
{
    uint8_t* e = g_saving ? empire() : nullptr;
    if (e && same(at(e), g_orig))
        write(e, g_mine);
    g_saving = false;
}

LIFECYCLE_HOOK("own_captain.session", lifecycle::kSessionEnd, -41, [](const lifecycle::Ctx&) { put_back(); }, true);

std::string status()
{
    uint8_t* e = game::mode_id() == modes::kSpace ? empire() : nullptr;
    K3 cur = e ? at(e) : K3{};
    char b[400];
    snprintf(b, sizeof b,
             "own captain: fix=%d guest=%d captain=%08X/%08X/%08X mine=%08X world=%08X sets=%u adopts=%u saves=%u "
             "puts=%u shadowed=%u unknown=%u why='%s'\n",
             (int)g_fix, (int)guest(), cur.i, cur.t, cur.g, g_mine.i, g_orig.i, g_sets, g_adopts, g_saves, g_puts,
             g_shadowed, g_unknown, g_why);
    return b;
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    a_Empire = H->sym("Simulator::GetPlayerEmpire");
    savestrip::add_save_hooks(save_start, save_end);
    H->command_add(M, "coop.owncaptain",
                   "coop.owncaptain [save] - this guest's own captain in Space (BUG-1475); save: the save hooks once "
                   "(tests: the key a save would write)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "save")) {
                           save_start();
                           uint8_t* e = empire();
                           K3 k = e ? at(e) : K3{};
                           save_end();
                           H->out_printf(out, "a save would write captain %08X\n", k.i);
                       }
                       H->out_printf(out, "%s", status().c_str());
                       return 0;
                   },
                   nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace own_captain
