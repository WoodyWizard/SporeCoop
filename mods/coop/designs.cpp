// The shared world's designs on every game (designs.h, docs/re/creations.md#creations-stage-designs).
#include "designs.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "civ.h"
#include "civ_editors.h"
#include "civ_rivals.h"
#include "creations.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "savestrip.h"
#include "space_ext.h"
#include "space_rivals.h"
#include "species_line.h"
#include "sporekit_game.hpp"
#include "tribe_rivals.h"

namespace coop_core {
void feed(const std::string& s);
}

namespace designs {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

using skg::thiscall;
using skg::vcall;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }
static bool R(const void* p, size_t n) { return p && skg::readable(p, n); }
static uintptr_t sym(const char* n) { return H ? H->sym(n) : 0; }
static bool is_host() { return S && S->connected() && S->role() == net::Role::Host; }
static bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }
static std::string host_name()
{
    std::string n = S ? S->name_of(0) : "";
    return n.empty() ? "the host" : n;
}

// BUG-1156: a guest's tribe takes the host's outfit (the profile reload of the game's Tribal Outfitter). Off: the
// guest's tribe keeps the outfit it had until the next world from the host (0.7.14).
bool g_outfit_fix = true;
COOP_FIX("tribe.outfit_sync", "BUG-1156", g_outfit_fix,
         "tribe: a guest's tribe wears the host's new outfit after the host's Tribal Outfitter");
// BUG-1157: a guest's player civilization has the host's designs (the culture set, through the game's own setter).
// Off: the guest keeps the designs it had until the next world from the host (0.7.14).
bool g_civ_fix = true;
COOP_FIX("civ.design_sync", "BUG-1157", g_civ_fix,
         "civ: a guest's civilization takes the host's new building and vehicle designs");
// BUG-1158: a guest's own Tribe outfitter and Civ design editors in the host's world are refused with a line, and a
// guest's own design pick that the host's row takes back is told. Off: they open and change only the guest's game
// (0.7.14: lost at the next world from the host, or with the rows above at once).
static bool g_own_fix = true;
COOP_FIX("editors.guest_designs", "BUG-1158", g_own_fix,
         "tribe, civ: a guest's own outfitter and design editors are refused with a line (their result was the guest's alone and got lost)");
// BUG-1272 (WP-T2 of docs/plan-0.7.16.md, docs/re/tribe.md#tribe-own-outfitter): in creature.mode own a guest's Tribal
// Outfitter (the tribe card's edit badge) opens for its own species line (species::own_key(Tribe)), its return does
// not reload the tribe's species slot on the guest, and the result is the guest's tribe outfit (species_line: every
// game dresses the guest's members in it, tribe_species.cpp). Off: BUG-1158's refusal in every mode (0.7.15).
static bool g_guest_outfitter_fix = true;
COOP_FIX("tribe.guest_outfitter", "BUG-1272", g_guest_outfitter_fix,
         "tribe: a guest's own Tribal Outfitter dresses its own members on every game (creature.mode own; host mode: refused)");
// BUG-1298 (WP-S2b): a guest's own editor (this Tribal Outfitter, the Civ design editors and outfitter of
// civ_editors.cpp, the arrival editors of civ_arrival.cpp) open when the session ends (the host left, the link died,
// the player left): the request stays ours until the editor returns, its return is taken as ours also without a
// session (the shared tribe slot, the shared civilization and the world's player species stay as they were on this
// game), and the result is kept until this game is a guest again whose host answered with its stored row; then it goes
// to the species line and is kept until the host's row names it (a drop before that: again at the next session).
// Kept in this game's memory (a restart loses it; the creation stays in My Creations). Off: the session's end drops the
// request (0.7.16 before WP-S2: the native return reloads the slot with the guest's outfit, Civ_SetDesign or the
// Civ outfitter's player species run on the guest's game, and the result is lost).
bool g_own_offline_fix = true;
COOP_FIX("designs.own_editor_offline", "BUG-1298", g_own_offline_fix,
         "tribe, civ: a guest's own editor open when the session ends keeps the shared tribe and civilization as they were and its result reaches the line at the next join");
// BUG-1562: in active Rivals stages (Tribe Rivals, Civ Rivals, Space Rivals), each player owns their own tribe,
// civilization, or empire. Host outfit/design sync must not overwrite a rival guest's player_tribe() or player_civ(),
// and guest editors/outfitters must not be refused or rerouted as shared-world editors.
static bool g_rivals_isolate_fix = true;
COOP_FIX("rivals.designs_isolate", "BUG-1562", g_rivals_isolate_fix,
         "rivals: isolate host outfit/design sync and guest editor guards in Tribe, Civ, and Space Rivals");

// ---- counters (tests)
static unsigned g_out_applied, g_out_failed, g_out_waits, g_out_restored, g_out_saves;
static unsigned g_civ_set, g_civ_own_back, g_civ_own_line, g_veh_pending;
static unsigned g_refused, g_rivals_skipped;
static double g_told_at = -100;

static bool tribe_rivals_on() { return g_rivals_isolate_fix && tribe_rivals::active(); }
static bool civ_rivals_on() { return g_rivals_isolate_fix && civ_rivals::active(); }
static bool space_rivals_on() { return g_rivals_isolate_fix && (spacerivals::active() || spaceext::rivals()); }
static bool rivals_stage_on(uint32_t mode)
{
    if (!g_rivals_isolate_fix)
        return false;
    if (mode == modes::kTribe)
        return tribe_rivals::active();
    if (mode == modes::kCiv)
        return civ_rivals::active();
    if (mode == modes::kSpace)
        return spacerivals::active() || spaceext::rivals();
    return false;
}

// =====================================================================================================
// Tribe: the outfit
// =====================================================================================================
constexpr uintptr_t kTribeProfile = 0x18CC; // cTribe: the species profile of its members (0xC8E9B0(tribe, 0))
constexpr uintptr_t kProfKey = 0x504, kProfModel = 0x510, kProfGen = 0x53C;

static void* player_tribe()
{
    uintptr_t g = sym("Simulator::cGameNounManager::GetPlayerTribe");
    void* nm = game::noun_manager();
    void* t = nm && g ? thiscall<void*>(g, nm) : nullptr;
    return R(t, kTribeProfile + 4) ? t : nullptr;
}
static uint8_t* tribe_profile(void* t)
{
    uint8_t* p = t ? F<uint8_t*>(t, kTribeProfile) : nullptr;
    return R(p, kProfGen + 4) ? p : nullptr;
}
static creations::Key key_at(const uint8_t* p) { return {F<uint32_t>((void*)p, 0), F<uint32_t>((void*)p, 4), F<uint32_t>((void*)p, 8)}; }

// guest: what was applied (the profile, the key of ours it plays, the host's raw keys for saves)
struct Applied {
    uint8_t* prof = nullptr;
    creations::Key here, host, host_model;
};
static Applied g_out;
static bool g_out_queued;
static double g_out_wait_since = -1, g_out_fail_at = -100;
static uint32_t g_out_wait_key;
static uint8_t g_save_keep[12];
static bool g_save_swapped;

bool outfit_read(std::vector<Val>& out)
{
    uint8_t* p = tribe_profile(player_tribe());
    if (!p || !F<uint32_t>(p, kProfKey))
        return false;
    Val e;
    for (int i = 0; i < 3; i++) {
        e.v[i] = F<uint32_t>(p, kProfKey + 4 * i);
        e.v[3 + i] = F<uint32_t>(p, kProfModel + 4 * i);
    }
    out.push_back(e);
    // the creation itself, for a key of the host's own My Creations (once per guest and key; nothing for others)
    if (g_outfit_fix && is_host() && !tribe_rivals_on())
        for (auto& pl : S->players())
            if (pl.id != 0)
                creations::host_species_out(pl.id, {e.v[0], e.v[1], e.v[2]});
    return true;
}

std::string outfit_fmt(const Val& e)
{
    char b[120];
    snprintf(b, sizeof b, "key=%08X/%08X/%08X model=%08X/%08X/%08X", e.v[0], e.v[1], e.v[2], e.v[3], e.v[4], e.v[5]);
    return b;
}

// the members' models from the reloaded profile, as the Tribal Outfitter's return does (cTribeUI 0xCDCC90:
// 0xCDCEBB..0xCDCF93) for each member of the player tribe (its vt 0x90 list)
static unsigned rebuild_members(void* t, uint8_t* prof)
{
    uintptr_t rebuild = sym("SporeCoop::Creature_RebuildModel"), after = sym("SporeCoop::Creature_AfterModel"),
              tool = sym("SporeCoop::TribeToolUI_AttachTool"), fx_find = sym("SporeCoop::cModifiers_Find"),
              fx_stop = sym("SporeCoop::CreatureModifiers_Remove"), anim_find = sym("SporeCoop::Creature_FindAnimSlot"),
              anim_set = sym("SporeCoop::Creature_RestartAnimSlot"), ui_get = sym("SporeCoop::cTribeUI_Get");
    if (!rebuild || !after || !tool || !fx_find || !fx_stop || !anim_find || !anim_set || !ui_get)
        return 0;
    void** v = vcall<void**>(t, 0x90 / 4);
    if (!R(v, 8))
        return 0;
    void** b = (void**)v[0];
    void** e = (void**)v[1];
    if (!b || e < b || e - b > 64 || !R(b, (e - b) * 4))
        return 0;
    void* ui = skg::cdecl_call<void*>(ui_get);
    void* tool_ui = R(ui, 0x244) ? F<void*>(ui, 0x240) : nullptr;
    std::vector<void*> ms(b, e); // (a copy: nothing below changes the list, but be safe)
    unsigned n = 0;
    for (void* m : ms) {
        if (!R(m, 0x1030) || F<void*>(m, 0xB20) != prof)
            continue; // (a member of another species: not the tribe's profile)
        F<uint32_t>(m, 0xB24) = F<uint32_t>(prof, kProfGen);
        float s = vcall<float>(m, 0x80 / 4, 1);
        void* sp = (uint8_t*)m + 0xC0;
        vcall<void>(sp, 0x40 / 4, s);
        thiscall<int>(rebuild, m, (char)1, 1);
        thiscall<void>(after, m);
        if (tool_ui)
            thiscall<void>(tool, tool_ui, m, F<int>(m, 0x102C));
        if (void* slots = F<void*>(m, 0xB48)) {
            int i = thiscall<int>(fx_find, slots, 5, 0, m, 0);
            if (i != -1)
                thiscall<void>(fx_stop, slots, i);
        }
        void* a = thiscall<void*>(anim_find, m, F<int>(m, 0xFC4), (uint32_t*)nullptr);
        thiscall<void>(anim_set, m, a);
        n++;
    }
    return n;
}

static void apply_outfit(void*)
{
    g_out_queued = false;
    if (!g_outfit_fix || !is_guest() || game::mode_id() != modes::kTribe || !g_out.host.instance)
        return;
    if (tribe_rivals_on()) {
        g_rivals_skipped++;
        return;
    }
    void* t = player_tribe();
    uint8_t* prof = tribe_profile(t);
    uintptr_t load = sym("SporeCoop::SpeciesProfile_Load"), size = sym("SporeCoop::cTribe_SpeciesSize");
    if (!prof || !load || !size)
        return;
    creations::Key k = g_out.here;
    uint32_t key[3] = {k.instance, k.type, k.group};
    if (!thiscall<bool>(load, prof, (const uint32_t*)key)) {
        g_out_failed++;
        g_out_fail_at = H->time_seconds();
        H->log(M, SK_WARN, "designs: the host's tribe outfit %08X (here %08X) could not be loaded (BUG-1156)",
               g_out.host.instance, k.instance);
        return;
    }
    g_out.prof = prof;
    // The species key stays the host's (+0x504): a save's main stream names the tribe's species by it before any
    // object record is written (seen: the guest's save named our key there, not the host's), and cSpeciesManager's
    // lookups by the host's key give our records already (creations.cpp BUG-0200 alias). The model key (+0x510) is
    // ours: the members' models are built from it (Creature_RebuildModel), so they are the host's outfit here too.
    uint32_t* sk = &F<uint32_t>(prof, kProfKey);
    sk[0] = g_out.host.instance, sk[1] = g_out.host.type, sk[2] = g_out.host.group;
    // the tribe's size from the species (as the game: 0xC996E0 with the distance of its place from the centre)
    void* sp = (uint8_t*)t + 0x120;
    const float* pos = vcall<const float*>(sp, 0x2C / 4);
    if (R(pos, 12))
        thiscall<void>(size, t, std::sqrt(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]));
    unsigned n = rebuild_members(t, prof);
    g_out_applied++;
    H->log(M, SK_INFO, "designs: the tribe wears the host's outfit %08X (here %08X), %u members rebuilt (BUG-1156)",
           g_out.host.instance, k.instance, n);
    coop_core::feed("your tribe wears " + host_name() + "'s new outfit");
}

int outfit_sync(const std::vector<Val>& want, bool apply)
{
    if (want.empty() || !want[0].v[0])
        return 0;
    if (tribe_rivals_on()) {
        g_rivals_skipped++;
        return 0;
    }
    const Val& w = want[0];
    creations::Key hk{w.v[0], w.v[1], w.v[2]}, hm{w.v[3], w.v[4], w.v[5]};
    uint8_t* prof = tribe_profile(player_tribe());
    if (!prof)
        return 0;
    creations::Key cur = key_at(prof + kProfKey), model = key_at(prof + kProfModel);
    creations::Key here = creations::local_species(hk);
    // aligned: the host's species key, and the model built from the host's creation (ours when it was remapped; the
    // save hook puts the host's model key in place while a save is written)
    if (cur == hk && (here == hk || model.instance == here.instance || (g_save_swapped && g_out.prof == prof)))
        return 0;
    double now = H->time_seconds();
    if (here == hk) { // not one of the host's own creations sent to us (yet): a premade or a download, or on its way
        if (!creations::key_known(hk)) {
            creations::want_species(hk);
            g_out_waits++;
            return 1;
        }
        if (g_out_wait_key != hk.instance) {
            g_out_wait_key = hk.instance;
            g_out_wait_since = now;
        }
        if (now - g_out_wait_since < 10.0) { // the host's own creation comes first (creations::host_species_out)
            g_out_waits++;
            return 1;
        }
    }
    if (!apply || g_out_queued || now - g_out_fail_at < 10.0)
        return 1;
    g_out.host = hk;
    g_out.host_model = hm;
    g_out.here = here;
    g_out_queued = true;
    H->run_outside_frame(apply_outfit, nullptr); // a model rebuild: outside the frame (CLAUDE.md 2)
    return 1;
}

// the host's model key while a save is written (no key of ours in a save), ours again after it
static bool out_ours_now()
{
    return g_out.prof && R(g_out.prof, kProfGen + 4) && F<uint32_t>(g_out.prof, kProfModel) == g_out.here.instance &&
           g_out.here.instance != g_out.host.instance && g_out.host_model.instance;
}
static void out_write_host()
{
    uint32_t* m = &F<uint32_t>(g_out.prof, kProfModel);
    m[0] = g_out.host_model.instance, m[1] = g_out.host_model.type, m[2] = g_out.host_model.group;
}
static void save_start()
{
    if (!g_outfit_fix || !out_ours_now())
        return;
    memcpy(g_save_keep, g_out.prof + kProfModel, 12);
    out_write_host();
    g_save_swapped = true;
    g_out_saves++;
}
static void save_end()
{
    if (!g_save_swapped)
        return;
    g_save_swapped = false;
    if (g_out.prof && R(g_out.prof, kProfGen + 4) && F<uint32_t>(g_out.prof, kProfModel) == g_out.host_model.instance)
        memcpy(g_out.prof + kProfModel, g_save_keep, 12);
}

// lifecycle-exempt: writes the game's tribe profile (the host's model key back), called by wsync::on_session_end
void on_session_end()
{
    // the guest plays on alone in its copy of the host's world: its later saves name the host's keys (the profile
    // keeps the look it has; the remapped package of ours stays registered for this process, creations.cpp)
    if (g_save_swapped)
        save_end();
    if (out_ours_now()) {
        out_write_host();
        g_out_restored++;
    }
    g_out = Applied{};
    g_out_wait_key = 0;
}
static void out_reset()
{
    g_out = Applied{};
    g_out_wait_key = 0;
    g_out_wait_since = -1;
}
static size_t out_held() { return g_out.prof ? 1 : 0; }
WORLD_STATE("designs.outfit", &g_outfit_fix, out_reset, out_held);

// =====================================================================================================
// Civ: the culture set
// =====================================================================================================
constexpr uintptr_t kCulture = 0x6C; // cCivilization: cCultureSet (an EASTL map<int design id, ResourceKey>)
constexpr size_t kMaxDesigns = 48;    // (wsync kMaxN)

static void* player_civ()
{
    uintptr_t g = sym("Simulator::cGameNounManager::GetPlayerCivilization");
    void* nm = game::noun_manager();
    void* c = nm && g ? thiscall<void*>(g, nm) : nullptr;
    return R(c, 0xB8) ? c : nullptr;
}
// the map's nodes: +0 right, +4 left, +8 parent, +0xC colour, +0x10 the design id, +0x14 the key (0xBF3DF0)
static void walk(uint8_t* n, uint8_t* anchor, int depth, std::vector<Val>& out)
{
    if (!n || n == anchor || depth > 32 || out.size() >= kMaxDesigns || !R(n, 0x20))
        return;
    walk(F<uint8_t*>(n, 4), anchor, depth + 1, out);
    if (out.size() < kMaxDesigns) {
        Val e;
        e.key = F<uint32_t>(n, 0x10);
        for (int i = 0; i < 3; i++)
            e.v[i] = F<uint32_t>(n, 0x14 + 4 * i);
        out.push_back(e);
    }
    walk(F<uint8_t*>(n, 0), anchor, depth + 1, out);
}
static std::vector<Val> culture(void* civ)
{
    std::vector<Val> out;
    uint8_t* cs = (uint8_t*)civ + kCulture;
    if (R(cs, 0x18))
        walk(F<uint8_t*>(cs, 0xC), cs + 4, 0, out);
    return out;
}
// the entry for `id` without generating one (GetDesignKey 0xBF9840 generates a missing one)
static const uint32_t* find_design(void* civ, uint32_t id)
{
    uint8_t* cs = (uint8_t*)civ + kCulture;
    if (!R(cs, 0x18))
        return nullptr;
    uint8_t *n = F<uint8_t*>(cs, 0xC), *best = nullptr;
    for (int d = 0; n && n != cs + 4 && d < 32 && R(n, 0x20); d++) {
        if ((int32_t)F<uint32_t>(n, 0x10) < (int32_t)id)
            n = F<uint8_t*>(n, 0);
        else {
            best = n;
            n = F<uint8_t*>(n, 4);
        }
    }
    return best && F<uint32_t>(best, 0x10) == id ? &F<uint32_t>(best, 0x14) : nullptr;
}

void* civ_player() { return player_civ(); }
std::vector<Val> civ_culture()
{
    void* civ = player_civ();
    return civ ? culture(civ) : std::vector<Val>{};
}
const uint32_t* civ_design(uint32_t id)
{
    void* civ = player_civ();
    return civ ? find_design(civ, id) : nullptr;
}

bool civ_read(std::vector<Val>& out)
{
    void* civ = player_civ();
    if (!civ)
        return false;
    for (auto& e : culture(civ))
        if (e.v[0])
            out.push_back(e);
    return true;
}

bool culture_key(uint32_t design_id, uint32_t out[3])
{
    void* civ = player_civ();
    const uint32_t* k = civ ? find_design(civ, design_id) : nullptr;
    if (!k || !k[0])
        return false;
    out[0] = k[0], out[1] = k[1], out[2] = k[2];
    return true;
}

std::string civ_fmt(const Val& e)
{
    char b[96];
    snprintf(b, sizeof b, "id=%08X key=%08X/%08X/%08X", e.key, e.v[0], e.v[1], e.v[2]);
    return b;
}

static bool vehicle_design(uint32_t id) { return id != 0x99E92F05u && id != 0x4E3F7777u && id != 0x47C10953u && id != 0x72C49181u; }
static std::map<uint32_t, uint32_t> g_civ_seen; // guest: design id -> the host's key this game last had
static double g_veh_until = 0, g_veh_at = 0;
static double g_own_told_at = -100;

int civ_sync(const std::vector<Val>& want, bool apply)
{
    if (civ_rivals_on() || space_rivals_on()) {
        if (!want.empty())
            g_rivals_skipped++;
        return 0;
    }
    void* civ = player_civ();
    uintptr_t set = sym("SporeCoop::Civ_SetDesign");
    if (!civ || !set)
        return 0;
    int n = 0;
    bool own = false, veh = false, line = false;
    for (auto& w : want) {
        const uint32_t* cur = find_design(civ, w.key);
        if (cur && cur[0] == w.v[0] && cur[1] == w.v[1] && cur[2] == w.v[2]) {
            g_civ_seen[w.key] = w.v[0];
            continue;
        }
        n++;
        if (!apply)
            continue;
        auto it = g_civ_seen.find(w.key);
        bool own_pick = it != g_civ_seen.end() && it->second == w.v[0] && cur && cur[0] != w.v[0];
        // BUG-1275: in own mode (civ.guest_editors) a guest's own pick that no hook of civ_editors.cpp saw is its own
        // design (its species line); the shared entry goes back to the host's without the BUG-1158 line
        if (own_pick && civedit::active()) {
            uint32_t mine[3] = {cur[0], cur[1], cur[2]};
            if (civedit::take_pick(w.key, mine)) {
                line = true;
                g_civ_own_line++;
                own_pick = false;
            }
        }
        own |= own_pick; // this game had the host's design and its own pick changed it (the host's did not change)
        uint32_t k[3] = {w.v[0], w.v[1], w.v[2]}, was = cur ? cur[0] : 0;
        thiscall<void>(set, civ, w.key, (const uint32_t*)k); // the culture set entry, then its buildings / vehicles
        g_civ_seen[w.key] = w.v[0];
        g_civ_set++;
        veh |= vehicle_design(w.key);
        H->log(M, SK_INFO, "designs: the civilization's design %08X is the host's %08X (was %08X)%s (BUG-1157)", w.key,
               w.v[0], was, own ? ", this game's own pick went back" : line ? ", this game's own pick went to its line" : "");
    }
    double now = H->time_seconds();
    if (veh)
        g_veh_until = now + 30.0; // the re-modelled vehicles: the host's own designs under keys of ours (BUG-0647)
    if (g_veh_until > now && now - g_veh_at >= 1.0) {
        g_veh_at = now;
        g_veh_pending = civ::redesign_vehicles();
        if (!g_veh_pending)
            g_veh_until = 0;
    }
    if (n && apply) {
        if (own && g_own_fix) { // BUG-1158: the guest's own pick (the planner's Sporepedia) went back
            g_civ_own_back++;
            if (now - g_own_told_at > 10.0) {
                g_own_told_at = now;
                coop_core::feed("the civilization's designs are shared: yours went back to " + host_name() + "'s");
            }
        } else if (!own && !line && now - g_told_at > 20.0) {
            g_told_at = now;
            coop_core::feed("your civilization has " + host_name() + "'s new designs");
        }
    }
    return n;
}
static void civ_reset() { g_civ_seen.clear(), g_veh_until = 0; }
static size_t civ_held() { return g_civ_seen.size(); }
WORLD_STATE("designs.civ", &g_civ_fix, civ_reset, civ_held);

// =====================================================================================================
// A guest's own editors (BUG-1158)
// =====================================================================================================
// ---- BUG-1272 (WP-T2): the guest's own Tribal Outfitter (docs/re/tribe.md#tribe-own-outfitter)
// The tribe card's edit badge: the planner (0xD130D0, message 0x53850BAE) builds the request from the card (editor
// +0xC 0x156276D1, key +0x10 = the species slot's +0x504, the Tribe tag 0x116DD1B at +0x90) and submits it. The result
// message 0x30C11C7 of that tag puts the result in cTribeUI +0x2F8 (0xCD81F0, null when cancelled); back in Tribe the
// return 0xCDCC90 (its third branch) reloads the slot with the result and rebuilds every member. Both are local to the
// game that opened the editor: on the guest the result of our request never reaches another game.
constexpr uint32_t kTribeTag = 0x116DD1Bu;    // EditorRequest +0x90 of the Tribe planner's requests (result +0xC)
constexpr uintptr_t kUiCityHall = 0x28, kUiVehicle = 0x29, kUiResult = 0x2F8; // cTribeUI (0xCDCC90's branches)
static bool g_oo_pending, g_oo_in_editor;     // our request went to the editor; its return is ours
static double g_oo_opened_at = -100;
static uint32_t g_oo_asked[3], g_oo_card[3];  // the key we asked for, the card's (the slot's) key
static creations::Key g_oo_result;            // the editor's result, to be taken as our tribe outfit
static creations::Key g_oo_sent;              // BUG-1298: taken by our line, until the host's row names it
static double g_oo_result_at = -100, g_oo_try_at = -100;
static bool g_oo_held;                        // BUG-1298: the result waits for the next session
static unsigned g_oo_opened, g_oo_returns, g_oo_skipped, g_oo_cancelled, g_oo_set, g_oo_set_failed, g_oo_offline,
    g_oo_acked, g_oo_resent;
using EditorReturn_t = void(SK_THISCALL*)(void*);
static EditorReturn_t o_EditorReturn;
// BUG-1316 (lab 20261119-111652 tribe_own_outfitter: returns=1 skipped=0, the guest's slot reloaded with its outfit):
// the result's +0x14 is the editor model's model type (docs/re/civ.md#civ-own-editors: +0x14 = editor model +0x58), not
// the outfitter's editor id 0x156276D1, so the result of our own outfitter was never taken. Our pending request, the
// Tribe tag and the outfitter's branch (+0x28 / +0x29 clear) name it. Off: +0x14 must be the editor id (as before).
static bool g_oo_result_fix = true;
COOP_FIX("tribe.own_outfit_result", "BUG-1316", g_oo_result_fix,
         "tribe: the result of a guest's own Tribal Outfitter goes to its own members, not into the shared tribe's species");
static uint32_t g_oo_rtype; // the last taken result's +0x14 (status)

// (without the return hook the slot would be reloaded with our outfit: then the refusal, as in host mode)
static bool own_outfitter_on() { return g_guest_outfitter_fix && species::own_mode() && o_EditorReturn; }

bool own_outfitter(void* req, uint32_t mode, uint32_t editor, bool own_world)
{
    if (!own_outfitter_on() || !is_guest() || own_world || mode != modes::kTribe || editor != kOutfitter ||
        !R(req, 0x9C) || F<uint32_t>(req, 0x90) != kTribeTag)
        return false;
    for (int i = 0; i < 3; i++)
        g_oo_card[i] = F<uint32_t>(req, 0x10 + 4 * i);
    // our line's species on our own game: the last outfit we made for our creature, else the creature (My
    // Creations); none (we play the world's own species): the card's key, the tribe's species, stays
    creations::Key k = species::own_key(species::Stage::Tribe);
    if (k.instance && k.type) {
        F<uint32_t>(req, 0x10) = k.instance, F<uint32_t>(req, 0x14) = k.type, F<uint32_t>(req, 0x18) = k.group;
    }
    for (int i = 0; i < 3; i++)
        g_oo_asked[i] = F<uint32_t>(req, 0x10 + 4 * i);
    g_oo_pending = true;
    g_oo_in_editor = false;
    g_oo_opened_at = H->time_seconds();
    g_oo_opened++;
    H->log(M, SK_INFO, "designs: our own Tribal Outfitter opens for %08X (the card's %08X) (BUG-1272)", g_oo_asked[0],
           g_oo_card[0]);
    return true;
}

bool own_outfitter_open() { return g_oo_pending; }

// cTribeUI 0xCDCC90 (thiscall, no arguments, plain ret): on the guest the return of our request does not reload the
// tribe's slot (the host's species: it would dress every member of the slot, the host's too, in our outfit here); the
// result is released as the game does and becomes our tribe outfit (tick). Its other steps (the planner's state
// 0xCCF090, +0x38C) run as after a cancelled outfitter.
static void SK_THISCALL h_EditorReturn(void* ui)
{
    if (g_oo_pending && R(ui, kUiResult + 4) && !F<uint8_t>(ui, kUiCityHall) && !F<uint8_t>(ui, kUiVehicle)) {
        g_oo_pending = false;
        g_oo_returns++;
        void* res = F<void*>(ui, kUiResult);
        if (res && R(res, 0x48))
            H->log(M, SK_INFO, "designs: our own Tribal Outfitter returns: result tag %08X type %08X key %08X (BUG-1316)",
                   F<uint32_t>(res, 0xC), F<uint32_t>(res, 0x14), F<uint32_t>(res, 0x18));
        if (!res) {
            g_oo_cancelled++;
            H->log(M, SK_INFO, "designs: our own Tribal Outfitter was cancelled (BUG-1272)");
        } else if (R(res, 0x48) && F<uint32_t>(res, 0xC) == kTribeTag &&
                   (g_oo_result_fix || F<uint32_t>(res, 0x14) == kOutfitter) &&
                   (is_guest() || g_own_offline_fix) && g_guest_outfitter_fix) {
            g_oo_rtype = F<uint32_t>(res, 0x14);
            if (!is_guest())
                g_oo_offline++; // BUG-1298: the session ended while the editor was open
            g_oo_result = {F<uint32_t>(res, 0x18), F<uint32_t>(res, 0x1C), F<uint32_t>(res, 0x20)};
            if (tribe_rivals_on()) {
                // BUG-1562: in Tribe Rivals, player_tribe() on the guest is the guest's own rival tribe (R1 swap),
                // so let native o_EditorReturn reload its species profile and rebuild its members directly.
                if (!species::own_mode())
                    g_oo_result = {};
                g_oo_result_at = H->time_seconds();
                g_oo_try_at = -100;
                H->log(M, SK_INFO, "designs: Tribe Rivals own outfit %08X/%08X/%08X: reloading guest rival tribe (BUG-1562)",
                       F<uint32_t>(res, 0x18), F<uint32_t>(res, 0x1C), F<uint32_t>(res, 0x20));
            } else {
                F<void*>(ui, kUiResult) = nullptr;
                vcall<void>(res, 0x8 / 4); // (its reference: released, as 0xCDCC90 does at its end)
                g_oo_skipped++;
                g_oo_result_at = H->time_seconds();
                g_oo_try_at = -100;
                H->log(M, SK_INFO, "designs: our own outfit %08X/%08X/%08X: the tribe's species slot stays (BUG-1272)",
                       g_oo_result.instance, g_oo_result.type, g_oo_result.group);
            }
        }
    }
    o_EditorReturn(ui);
}

void tick(double now)
{
    if (g_oo_pending) { // our request ends with its return to Tribe; anything else (a load, the menu) drops it
        uint32_t m = game::mode_id();
        bool editor = m == modes::kEditor || m == modes::kEdit;
        if (editor)
            g_oo_in_editor = true;
        else if (m != modes::kTribe || (!g_oo_in_editor && now - g_oo_opened_at > 20.0)) {
            g_oo_pending = false; // (the editor never opened: the game refused the request)
            H->log(M, SK_INFO, "designs: our own Tribal Outfitter request is dropped (mode %08X) (BUG-1272)", m);
        }
    }
    if (g_oo_sent.instance && g_own_offline_fix) { // BUG-1298: until the host's row names it
        if (species::host_took(g_oo_sent)) {
            g_oo_acked++;
            H->log(M, SK_INFO, "designs: the host has our tribe outfit %08X (BUG-1298)", g_oo_sent.instance);
            g_oo_sent = {};
        } else if (!is_guest()) {
            g_oo_result = g_oo_sent, g_oo_sent = {}; // (the session ended first: again at the next one)
            g_oo_resent++;
        }
    }
    if (!g_oo_result.instance || now - g_oo_try_at < 2.0)
        return;
    g_oo_try_at = now;
    if (g_own_offline_fix && S && S->connected() && S->role() == net::Role::Host) {
        H->log(M, SK_INFO, "designs: our outfit %08X of another host's world is dropped (this game hosts now, BUG-1298)",
               g_oo_result.instance);
        g_oo_result = {}, g_oo_held = false;
        return;
    }
    if (g_own_offline_fix && (!is_guest() || species::own_waiting())) { // BUG-1298: no session (or no answer yet)
        if (!g_oo_held && !is_guest()) {
            H->log(M, SK_INFO, "designs: our outfit %08X is kept for the next session (BUG-1298)", g_oo_result.instance);
            coop_core::feed("your new outfit is kept: your members wear it when you join again");
        }
        g_oo_held = true;
        g_oo_result_at = now; // (its 30 s count from the session's answer)
        return;
    }
    g_oo_held = false;
    if (species::set_tribe_outfit(g_oo_result)) {
        g_oo_set++;
        H->log(M, SK_INFO, "designs: our tribe outfit is %08X: every game dresses our members in it (BUG-1272)",
               g_oo_result.instance);
        coop_core::feed("your tribe members wear your new outfit (on every game)");
        if (g_own_offline_fix)
            g_oo_sent = g_oo_result;
        g_oo_result = {};
    } else if (now - g_oo_result_at > 30.0 || !is_guest()) { // (its records not readable here: the editor's file)
        g_oo_set_failed++;
        H->log(M, SK_WARN, "designs: our outfit %08X could not be shared (BUG-1272)", g_oo_result.instance);
        coop_core::feed("your new outfit could not be shared: your members keep their look");
        g_oo_result = {};
    }
}

static void oo_reset()
{
    if (g_own_offline_fix) { // BUG-1298: an open editor stays ours, a result waits for the next session
        if (g_oo_sent.instance && !g_oo_result.instance)
            g_oo_result = g_oo_sent;
        g_oo_sent = {};
        return;
    }
    g_oo_pending = false;
    g_oo_result = {};
    g_oo_sent = {};
}
static size_t oo_held()
{
    return (g_oo_pending ? 1 : 0) + (g_oo_result.instance ? 1 : 0) + (g_oo_sent.instance ? 1 : 0);
}
// (a session state, not a world state: the visit to the editor and back must not drop the pending request)
SESSION_STATE("designs.own_outfitter", &g_guest_outfitter_fix, oo_reset, oo_held);

std::string refuse_own_editor(uint32_t mode, uint32_t editor, bool own_world, const std::string& host)
{
    if (!g_own_fix || !is_guest() || own_world || rivals_stage_on(mode))
        return "";
    // (creature.mode own: own_outfitter took the card's request before, BUG-1272; one it did not take is refused:
    // its return would reload the shared slot)
    if (mode == modes::kTribe && editor == kOutfitter) {
        g_refused++;
        return "the tribe's outfit is shared: only " + host + " changes it";
    }
    if (mode == modes::kCiv && editor != kShipEditor && !civedit::allows(editor)) { // BUG-1275: own mode opens it
        g_refused++;
        return "the civilization's designs are shared: only " + host + " makes new ones";
    }
    return "";
}

bool outfit_texts(uint32_t mode, uint32_t editor) { return g_outfit_fix && mode == modes::kTribe && editor == kOutfitter; }

// =====================================================================================================
std::string status()
{
    char b[640];
    uint8_t* p = tribe_profile(player_tribe());
    void* civ = player_civ();
    snprintf(b, sizeof b,
             "designs: outfit fix=%d applied=%u failed=%u waits=%u saves=%u restored=%u here=%08X host=%08X | civ fix=%d "
             "set=%u own_back=%u own_line=%u veh_pending=%u designs=%zu | own editors fix=%d refused=%u | rivals "
             "fix=%d rivals_skipped=%u\n"
             "tribe profile %p key=%08X/%08X/%08X model=%08X/%08X/%08X gen=%u\n",
             (int)g_outfit_fix, g_out_applied, g_out_failed, g_out_waits, g_out_saves, g_out_restored, g_out.here.instance,
             g_out.host.instance, (int)g_civ_fix, g_civ_set, g_civ_own_back, g_civ_own_line, g_veh_pending,
             civ ? culture(civ).size() : (size_t)0, (int)g_own_fix, g_refused, (int)g_rivals_isolate_fix,
             g_rivals_skipped, (void*)p, p ? F<uint32_t>(p, kProfKey) : 0, p ? F<uint32_t>(p, kProfKey + 4) : 0,
             p ? F<uint32_t>(p, kProfKey + 8) : 0, p ? F<uint32_t>(p, kProfModel) : 0,
             p ? F<uint32_t>(p, kProfModel + 4) : 0, p ? F<uint32_t>(p, kProfModel + 8) : 0, p ? F<uint32_t>(p, kProfGen) : 0);
    std::string o = b;
    // BUG-1272: the guest's own outfitter (the key it asked for, the card's: the slot's)
    snprintf(b, sizeof b,
             "own outfitter: fix=%d on=%d hooked=%d opened=%u asked=%08X/%08X/%08X card=%08X pending=%d returns=%u "
             "skipped=%u cancelled=%u result=%08X set=%u set_failed=%u offline=%u held=%d sent=%08X acked=%u resent=%u rtype=%08X\n",
             (int)g_guest_outfitter_fix, (int)own_outfitter_on(), (int)(o_EditorReturn != nullptr), g_oo_opened,
             g_oo_asked[0], g_oo_asked[1], g_oo_asked[2], g_oo_card[0], (int)g_oo_pending, g_oo_returns, g_oo_skipped,
             g_oo_cancelled, g_oo_result.instance, g_oo_set, g_oo_set_failed, g_oo_offline, (int)g_oo_held,
             g_oo_sent.instance, g_oo_acked, g_oo_resent, g_oo_rtype);
    return o + b;
}

// `coop.designs` status; `culture` this game's player civilization's culture set; `set <hex id> <hex inst> [<hex
// type> <hex group>]` the game's own setter (cCivilization 0xBF4C50, what a planner pick calls) on this game's
// player civilization (tests: the host's pick); `members` the player tribe's members and their profile
std::string debug(const std::vector<std::string>& a)
{
    if (a.empty())
        return status();
    if (a[0] == "culture") {
        void* civ = player_civ();
        if (!civ)
            return "no player civilization here\n";
        std::string o;
        for (auto& e : culture(civ))
            o += civ_fmt(e) + "\n";
        return o + "end culture\n";
    }
    if (a[0] == "set" && a.size() >= 3) {
        void* civ = player_civ();
        uintptr_t set = sym("SporeCoop::Civ_SetDesign");
        if (!civ || !set)
            return "no player civilization here\n";
        uint32_t id = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        uint32_t k[3] = {(uint32_t)strtoul(a[2].c_str(), nullptr, 16),
                         a.size() > 3 ? (uint32_t)strtoul(a[3].c_str(), nullptr, 16)
                                      : (vehicle_design(id) ? 0x24682294u : 0x2399BE55u),
                         a.size() > 4 ? (uint32_t)strtoul(a[4].c_str(), nullptr, 16)
                                      : (vehicle_design(id) ? 0x40646200u : 0x40636200u)};
        thiscall<void>(set, civ, id, (const uint32_t*)k);
        const uint32_t* now = find_design(civ, id);
        char b[120];
        snprintf(b, sizeof b, "design %08X set: %08X/%08X/%08X\n", id, now ? now[0] : 0, now ? now[1] : 0, now ? now[2] : 0);
        return b;
    }
    if (a[0] == "members") {
        void* t = player_tribe();
        uint8_t* p = tribe_profile(t);
        if (!t || !p)
            return "no player tribe here\n";
        void** v = vcall<void**>(t, 0x90 / 4);
        std::string o;
        char b[160];
        for (void** m = v ? (void**)v[0] : nullptr; m && m < (void**)v[1] && m - (void**)v[0] < 64; m++) {
            if (!R(*m, 0xB30))
                continue;
            uint8_t* mp = F<uint8_t*>(*m, 0xB20);
            snprintf(b, sizeof b, "member %p profile=%p own=%d gen=%u/%u key=%08X\n", *m, (void*)mp, (int)(mp == p),
                     F<uint32_t>(*m, 0xB24), R(mp, kProfGen + 4) ? F<uint32_t>(mp, kProfGen) : 0,
                     F<uint32_t>(*m, 0xB28));
            o += b;
        }
        return o + status();
    }
    if (a[0] == "editor" && a.size() >= 2) { // tests (BUG-1158): an editor request of this game, through the Submit hook
        // as the planner builds one from a Sporepedia "new" (0xD130D0, message 0x4A314E6): the editor, the design's key
        uintptr_t alloc = sym("GeneralAllocator::new_"), ctor = sym("EditorRequest_ctor"),
                  submit = sym("Editors::EditorRequest::Submit");
        if (!alloc || !ctor || !submit)
            return "not possible here\n";
        uint32_t id = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        void* civ = player_civ();
        const uint32_t* k = civ ? find_design(civ, id) : nullptr;
        void* req = skg::cdecl_call<void*>(alloc, (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
        if (!req)
            return "not possible here\n";
        thiscall<void*>(ctor, req);
        F<uint32_t>(req, 0xC) = id;
        for (int i = 0; i < 3; i++)
            F<uint32_t>(req, 0x10 + 4 * i) = k ? k[i] : 0;
        for (int o : {0x36, 0x37, 0x38, 0x3C, 0x3D, 0x64, 0x6D})
            F<uint8_t>(req, o) = 1;
        F<uint8_t>(req, 0x39) = 0;
        bool r = skg::cdecl_call<bool>(submit, req);
        char b[96];
        snprintf(b, sizeof b, "editor %08X request submitted -> %d (refused %u)\n", id, (int)r, g_refused);
        return b;
    }
    return "coop.designs [culture | set <hex id> <hex inst> [<hex type> <hex group>] | members | editor <hex id>]\n";
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    savestrip::add_save_hooks(save_start, save_end); // BUG-1156: no key of ours in a guest's save
    if (uintptr_t a = H->sym("SporeCoop::TribeUI_EditorReturn")) // BUG-1272: our own outfitter's return
        H->hook_create(M, (void*)a, (void*)h_EditorReturn, (void**)&o_EditorReturn);
    H->command_add(M, "coop.designs",
                   "coop.designs [culture | set <hex id> <hex inst> | members] - the shared tribe outfit and civilization designs (E4)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       H->out_printf(out, "%s", debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, 0);
}

} // namespace designs
