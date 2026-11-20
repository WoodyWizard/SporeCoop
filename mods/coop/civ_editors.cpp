// WP-C3 of docs/plan-0.7.16.md (BUG-1275): a guest's own Civ editors in own mode (civ_editors.h,
// docs/re/civ.md#civ-own-editors).
//
// Where the game writes the shared civilization after a player's design work (Ghidra, GOG 2024):
//  * the planner's apply 0xD095A0 (thiscall(planner, design id, key*) ret 8): Civ_SetDesign on the city's
//    civilization (the culture set entry, every object of that design re-modelled) and for the City Hall cPlayer
//    +0x1150. Its callers: the planner's re-open after an editor 0xD0E170 (the editor's result: planner +0x4C the
//    model type = design id, +0x50 the key, both from the result message 0x30C11C7) and the Sporepedia shopper's
//    accept 0xD0CC00 (planner +0x60 the design id).
//  * the planner's message 0x5132389 (a Sporepedia pick of a list of creations): Civ_SetDesign directly.
//  * the Civ outfitter's result (request tag 0x66787BB): cGameModeCiv's listener 0xCFD500 keeps the result
//    (0x572620, not when cancelled: result +0x44), and the Civ re-entry 0xCFBD70 (state 1, from the editor mode)
//    applies a creature result: cSpeciesManager_SetPlayerSpecies, Civ_SetSpeciesKey on every civilization with its
//    cities' citizens re-synced, cTribe_SetSpeciesSlot on every tribe.
// For a guest in own mode none of them runs for its own work: the apply is skipped, the pick's entries go back, the
// outfitter's result is handed to the game as cancelled; the result goes to the guest's species line instead (from
// tick(): the line packs the creation, sends it to the host, every game registers it). The guest's requests start
// from its own line (the outfitter from its Civ species, an "edit" from its own design: cGameModeManager's
// SubmitEditorRequest 0xB1DE20, which every planner request passes), and its planner's palette shows its own
// designs (0xD0C860, the tile's design message 0x4A344E9).
#include "civ_editors.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <deque>

#include "designs.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "own_world.h"
#include "species_line.h"
#include "sporekit_game.hpp"

namespace coop_core {
void feed(const std::string& s);
void civ_arrival_result(const void* data); // WP-X1 (BUG-1276)
}

namespace civedit {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

using skg::thiscall;

// BUG-1275: off: a guest's own Civ design editors and outfitter are refused and its own pick goes back (BUG-1158,
// 0.7.15); every hook below passes through
static bool g_fix = true;
COOP_FIX("civ.guest_editors", "BUG-1275", g_fix,
         "civ: in own mode a guest's design editors, Civ outfitter and planner picks make its own designs (its species line), the shared civilization stays the host's");

constexpr uint32_t kTagOutfitter = 0x066787BBu; // the Civ outfitter's request (planner message 0x53850BAE in Civ)
constexpr uint32_t kTagDesign = 0x91D4AF08u;    // a design editor's request of the planner (message 0x4A314E6)
constexpr uint32_t kUfoDesign = 0x98E03C0Du;    // the planner's UFO entry: the empire's, not the culture set's
constexpr uint32_t kMsgEditorResult = 0x030C11C7u, kMsgPick = 0x05132389u, kMsgNewDesign = 0x04A314E6u;
constexpr uint32_t kCreatureType = 0x2B978C46u;
constexpr uint32_t kBuildingType = 0x2399BE55u, kVehicleType = 0x24682294u;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }
static bool R(const void* p, size_t n) { return p && skg::readable(p, n); }

using Apply_t = void(SK_THISCALL*)(void*, uint32_t, const uint32_t*);
using Tile_t = bool(SK_THISCALL*)(void*, uint32_t, const uint32_t*, const uint32_t*);
using Msg_t = int(SK_THISCALL*)(void*, uint32_t, void*);
using Submit_t = void(SK_THISCALL*)(void*, void*);
static Apply_t o_Apply;
static Tile_t o_Tile;
static Msg_t o_PlannerMsg, o_CivMsg;
static Submit_t o_Submit;
static uintptr_t a_SetDesign, a_Planner;

static unsigned g_applies_skipped, g_picks_back, g_outfits_dropped, g_requests, g_keys_started, g_tiles, g_taken,
    g_take_failed, g_civ_sync_takes;
static bool g_last_stage_civ;

struct Take {
    bool outfit = false;
    uint32_t id = 0; // the design id (0 for the outfit)
    species::Key key;
    double since = -1;
};
static std::deque<Take> g_takes;
// BUG-1298 (WP-S2b, designs.own_editor_offline): our own editor request (a planner's design editor or the Civ
// outfitter, civ_arrival's editors) is outstanding: its return is ours also when the session ended meanwhile; the
// takes the line took wait for the host's row (a session's end before it: taken again at the next session)
static bool g_own_open, g_own_saw_editor;
static double g_own_open_at = -100, g_own_back_at = -1;
static std::vector<Take> g_unacked;
static unsigned g_offline_returns, g_acked, g_held_told;
struct Done {
    bool outfit;
    uint32_t id;
    species::Key key;
    bool ok;
};
static std::vector<Done> g_done; // (status, tests: the last ones)
struct Req {
    uint32_t tag = 0, editor = 0, did = 0, key = 0, was = 0;
};
static Req g_req;

static bool line_on()
{
    const fix::Info* f = fix::find("species.line");
    return !f || *f->on;
}
static bool guest_own()
{
    return g_fix && S && S->connected() && S->role() == net::Role::Client && species::own_mode() && line_on() &&
           !own_world::elsewhere();
}
bool active() { return guest_own() && game::mode_id() == modes::kCiv; }
// BUG-1298: our own editor was open when the session ended (its return and result are still ours)
static bool offline_own() { return designs::g_own_offline_fix && g_fix && g_own_open && !(S && S->connected()); }
void own_opened()
{
    if (!guest_own())
        return;
    g_own_open = true;
    g_own_saw_editor = false;
    g_own_open_at = H->time_seconds();
    g_own_back_at = -1;
}
// the return paths: in Civ, or in an editor started from Civ (the result message comes while the editor closes)
static bool active_return()
{
    if (!guest_own() && !offline_own())
        return false;
    uint32_t m = game::mode_id();
    return m == modes::kCiv || ((m == modes::kEditor || m == modes::kEdit) && g_last_stage_civ);
}

bool allows(uint32_t) { return guest_own(); }

static bool same(const uint32_t* a, const species::Key& b) { return a && a[0] == b.instance && a[1] == b.type && a[2] == b.group; }

static void queue(bool outfit, uint32_t id, const uint32_t* k)
{
    species::Key key{k[0], k[1], k[2]};
    for (auto& t : g_takes)
        if (t.outfit == outfit && t.id == id) {
            t.key = key; // the newer one
            return;
        }
    g_takes.push_back({outfit, id, key, -1});
    H->log(M, SK_INFO, "civedit: our own %s %08X/%08X/%08X goes to our species line (BUG-1275)",
           outfit ? "Civ outfit" : "design", k[0], k[1], k[2]);
}

bool take_pick(uint32_t id, const uint32_t key[3])
{
    if (!guest_own() || !id || id == kUfoDesign || !key || !key[0] || (key[1] != kBuildingType && key[1] != kVehicleType))
        return false;
    queue(false, id, key);
    g_civ_sync_takes++;
    return true;
}

bool take_editor(uint32_t id, const uint32_t key[3])
{
    if (!(guest_own() || offline_own()) || !id || id == kUfoDesign || !key || !key[0] || (key[1] != kBuildingType && key[1] != kVehicleType))
        return false;
    queue(false, id, key);
    return true;
}

// ---- the planner's apply: the shared civilization's design entry (skipped for our own work)
static void SK_THISCALL h_Apply(void* planner, uint32_t id, const uint32_t* key)
{
    if (id == kUfoDesign || !active_return() || !R(key, 12)) {
        o_Apply(planner, id, key);
        return;
    }
    g_applies_skipped++;
    if (!guest_own())
        g_offline_returns++; // BUG-1298
    const uint32_t* cur = designs::civ_design(id);
    bool culture = cur && cur[0] == key[0] && cur[1] == key[1];
    species::Key mine = species::own_design(id);
    if (key[0] && !culture && mine.instance != key[0])
        queue(false, id, key);
    H->log(M, SK_INFO, "civedit: the planner's design %08X = %08X stays ours: the civilization keeps %08X (BUG-1275)", id,
           key[0], cur ? cur[0] : 0);
}

// ---- the planner's palette tile: our own design shown
static bool SK_THISCALL h_Tile(void* planner, uint32_t id, const uint32_t* key, const uint32_t* png)
{
    if (active() && id != kUfoDesign) {
        species::Key mine = species::own_design(id);
        if (mine.instance && R(key, 12) && key[0] != mine.instance) {
            uint32_t k[3] = {mine.instance, mine.type, mine.group};
            g_tiles++;
            return o_Tile(planner, id, k, nullptr); // (the picture from our creation's key)
        }
    }
    return o_Tile(planner, id, key, png);
}

// ---- the planner's messages: a Sporepedia pick of creations sets culture entries itself
static int SK_THISCALL h_PlannerMsg(void* planner, uint32_t msg, void* data)
{
    if (msg != kMsgPick || !active() || !designs::civ_player() || !a_SetDesign)
        return o_PlannerMsg(planner, msg, data);
    std::vector<designs::Val> before = designs::civ_culture();
    int r = o_PlannerMsg(planner, msg, data);
    void* civ = designs::civ_player();
    if (!civ)
        return r;
    for (auto& e : designs::civ_culture()) {
        const designs::Val* was = nullptr;
        for (auto& b : before)
            if (b.key == e.key)
                was = &b;
        if (was && !memcmp(was->v, e.v, 12))
            continue;
        if (!e.v[0] || e.key == kUfoDesign)
            continue;
        uint32_t mine[3] = {e.v[0], e.v[1], e.v[2]};
        uint32_t old[3] = {was ? was->v[0] : 0u, was ? was->v[1] : 0u, was ? was->v[2] : 0u};
        if (old[0])
            thiscall<void>(a_SetDesign, civ, e.key, (const uint32_t*)old); // the host's again (and its objects)
        else if (uint32_t* p = (uint32_t*)designs::civ_design(e.key))
            memset(p, 0, 12); // (no design before: the entry empty again; nothing was drawn from it)
        g_picks_back++;
        H->log(M, SK_INFO, "civedit: our Sporepedia pick %08X for design %08X is ours: the civilization keeps %08X (BUG-1275)",
               mine[0], e.key, old[0]);
        if (mine[1] == kBuildingType || mine[1] == kVehicleType)
            queue(false, e.key, mine);
    }
    return r;
}

// ---- cGameModeCiv's listener: the Civ outfitter's result is not the civilization's
static int SK_THISCALL h_CivMsg(void* listener, uint32_t msg, void* data)
{
    if (msg == kMsgEditorResult)
        coop_core::civ_arrival_result(data); // WP-X1 (BUG-1276): our own arrival editors' results (their own tag, BUG-1329)
    if (msg != kMsgEditorResult || !R(data, 0x48) || F<uint32_t>(data, 0xC) != kTagOutfitter || !active_return())
        return o_CivMsg(listener, msg, data);
    uint8_t& cancelled = F<uint8_t>(data, 0x44);
    const uint32_t* k = &F<uint32_t>(data, 0x18);
    if (!cancelled && k[0] && k[1] == kCreatureType)
        queue(true, 0, k);
    if (!guest_own())
        g_offline_returns++; // BUG-1298
    uint8_t was = cancelled;
    cancelled = 1; // the game keeps no result: no player species, civilization key or tribe slot of ours
    g_outfits_dropped++;
    int r = o_CivMsg(listener, msg, data);
    cancelled = was;
    return r;
}

// ---- the planner's requests start from our own line
static void SK_THISCALL h_Submit(void* mgr, void* req)
{
    if (req && active() && R(req, 0x9C)) {
        Req q;
        q.tag = F<uint32_t>(req, 0x90);
        q.editor = F<uint32_t>(req, 0xC);
        uint32_t* k = &F<uint32_t>(req, 0x10);
        q.was = k[0];
        species::Key mine;
        if (q.tag == kTagOutfitter) {
            mine = species::own_key(species::Stage::Civ);
            if (mine.type != kCreatureType)
                mine = {};
        } else if (q.tag == kTagDesign) {
            void* planner = a_Planner ? *(void**)a_Planner : nullptr;
            q.did = R(planner, 0x50) ? F<uint32_t>(planner, 0x4C) : 0;
            if (k[0] && q.did) // an edit of the current design: of ours
                mine = species::own_design(q.did);
        }
        if (q.tag == kTagOutfitter || q.tag == kTagDesign) {
            g_requests++;
            if (mine.instance && !same(k, mine)) {
                k[0] = mine.instance, k[1] = mine.type, k[2] = mine.group;
                g_keys_started++;
            }
            q.key = k[0];
            g_req = q;
            own_opened(); // BUG-1298
            H->log(M, SK_INFO, "civedit: our %s request (editor %08X, design %08X) starts from %08X (was %08X, BUG-1275)",
                   q.tag == kTagOutfitter ? "Civ outfitter" : "design editor", q.editor, q.did, q.key, q.was);
        }
    }
    o_Submit(mgr, req);
}

bool own_outfit_open() { return g_own_open && g_req.tag == kTagOutfitter; }

void tick(double now)
{
    uint32_t m = game::mode_id();
    if (m == modes::kCiv)
        g_last_stage_civ = true;
    else if (m != modes::kEditor && m != modes::kEdit)
        g_last_stage_civ = false;
    if (g_own_open) { // BUG-1298: our own editor's return ends a few seconds back in Civ (the planner's re-open)
        if (m == modes::kEditor || m == modes::kEdit)
            g_own_saw_editor = true, g_own_back_at = -1;
        else if (m == modes::kCiv && g_own_saw_editor) {
            if (g_own_back_at < 0)
                g_own_back_at = now;
            else if (now - g_own_back_at > 10.0)
                g_own_open = false;
        } else if (m != modes::kCiv || now - g_own_open_at > 30.0)
            g_own_open = false; // (another stage, a load; or the editor never opened)
    }
    bool keep = designs::g_own_offline_fix && g_fix && !(S && S->connected());
    if (designs::g_own_offline_fix && !g_unacked.empty()) { // until the host's row names them
        if (guest_own())
            g_unacked.erase(std::remove_if(g_unacked.begin(), g_unacked.end(), [&](const Take& t) {
                                bool took = species::host_took(t.key);
                                g_acked += took;
                                return took;
                            }),
                            g_unacked.end());
        else if (keep) {
            for (auto& t : g_unacked)
                g_takes.push_back({t.outfit, t.id, t.key, -1});
            g_unacked.clear();
        }
    }
    if (g_takes.empty())
        return;
    if (!guest_own()) {
        if (keep) { // BUG-1298: no session: kept for the next one
            if (!g_held_told++)
                coop_core::feed("your new design is kept: it reaches the others when you join again");
            return;
        }
        g_takes.clear();
        return;
    }
    if (m != modes::kCiv)
        return; // (after the editor: the creation is in My Creations by then)
    if (designs::g_own_offline_fix && !species::own_ready())
        return; // BUG-1298: the host's stored row first (a set before it would make the line ignore it)
    g_held_told = 0;
    Take& t = g_takes.front();
    if (t.since < 0)
        t.since = now;
    bool ok = t.outfit ? species::set_civ_outfit(t.key) : species::set_design(t.id, t.key);
    if (!ok && now - t.since < 15.0)
        return; // (its records not readable yet: again next frame)
    if (ok)
        g_taken++;
    else
        g_take_failed++;
    H->log(M, ok ? SK_INFO : SK_WARN, "civedit: our own %s %08X (design %08X) %s (BUG-1275)", t.outfit ? "Civ outfit" : "design",
           t.key.instance, t.id, ok ? "is in our species line" : "could not be taken: the line refused it");
    if (ok)
        coop_core::feed(t.outfit ? "your Civ outfit is yours: your cities' citizens wear it on every game"
                                 : "your new design is yours: your cities and vehicles show it on every game "
                                   "(the civilization's designs stay the host's)");
    else
        coop_core::feed("your design could not be shared with the other players");
    if (ok && designs::g_own_offline_fix)
        g_unacked.push_back(t);
    g_done.push_back({t.outfit, t.id, t.key, ok});
    if (g_done.size() > 16)
        g_done.erase(g_done.begin());
    g_takes.pop_front();
}

static void reset()
{
    g_req = Req{};
    if (designs::g_own_offline_fix) { // BUG-1298: an open editor stays ours, results wait for the next session
        for (auto& t : g_unacked)
            g_takes.push_back({t.outfit, t.id, t.key, -1});
        g_unacked.clear();
        return;
    }
    g_takes.clear();
    g_unacked.clear();
    g_own_open = false;
}
static size_t held() { return g_takes.size() + g_unacked.size() + (g_own_open ? 1 : 0); }
SESSION_STATE("civ.guest_editors", &g_fix, reset, held);

// ---- status (tests): what the game's own fields say on this game
static const uint32_t* civ_species_key()
{
    void* civ = designs::civ_player();
    return R(civ, 0x274) ? &F<uint32_t>(civ, 0x268) : nullptr;
}
static const uint32_t* world_species_key()
{
    uintptr_t g = H->sym("Editors::cSpeciesManager::Get");
    void* mgr = g ? skg::cdecl_call<void*>(g) : nullptr;
    return R(mgr, 0xB0) ? &F<uint32_t>(mgr, 0xA4) : nullptr;
}
static const uint32_t* city_hall_key()
{
    void* nm = game::noun_manager();
    void* pl = R(nm, 0x78) ? F<void*>(nm, 0x74) : nullptr;
    return R(pl, 0x115C) ? &F<uint32_t>(pl, 0x1150) : nullptr;
}
static uint32_t culture_digest(size_t* n)
{
    uint32_t h = 2166136261u;
    auto c = designs::civ_culture();
    for (auto& e : c)
        for (uint32_t w : {e.key, e.v[0], e.v[1], e.v[2]})
            for (int i = 0; i < 4; i++)
                h = (h ^ ((w >> (8 * i)) & 0xFF)) * 16777619u;
    if (n)
        *n = c.size();
    return h;
}

std::string status()
{
    char b[640];
    size_t n = 0;
    uint32_t dg = culture_digest(&n);
    const uint32_t *cs = civ_species_key(), *ws = world_species_key(), *ch = city_hall_key();
    snprintf(b, sizeof b,
             "civedit: fix=%d active=%d guest_own=%d applies_skipped=%u picks_back=%u outfits_dropped=%u requests=%u "
             "keys_started=%u tiles=%u taken=%u failed=%u civ_sync_takes=%u pending=%zu own_open=%d offline_returns=%u "
             "unacked=%zu acked=%u\n"
             "game: culture=%zu digest=%08X civ_species=%08X/%08X world_species=%08X/%08X city_hall=%08X/%08X\n"
             "last request: tag=%08X editor=%08X design=%08X key=%08X was=%08X\n",
             (int)g_fix, (int)active(), (int)guest_own(), g_applies_skipped, g_picks_back, g_outfits_dropped, g_requests,
             g_keys_started, g_tiles, g_taken, g_take_failed, g_civ_sync_takes, g_takes.size(), (int)g_own_open,
             g_offline_returns, g_unacked.size(), g_acked, n, dg, cs ? cs[0] : 0,
             cs ? cs[1] : 0, ws ? ws[0] : 0, ws ? ws[1] : 0, ch ? ch[0] : 0, ch ? ch[1] : 0, g_req.tag, g_req.editor,
             g_req.did, g_req.key, g_req.was);
    std::string s = b;
    for (auto& d : g_done) {
        species::Key line = S ? species::design(S->my_id(), d.id) : species::Key{};
        snprintf(b, sizeof b, "  took %s design=%08X key=%08X/%08X/%08X ok=%d line=%08X\n", d.outfit ? "outfit" : "design",
                 d.id, d.key.instance, d.key.type, d.key.group, (int)d.ok, d.outfit ? 0u : line.instance);
        s += b;
    }
    return s;
}

// coop.civedit [status | new <hex design id> [edit]]: `new` is the planner's own message for a palette tile's "new
// design" (or "edit") button (0x4A314E6, what the tile's buttons post), on the open planner: the editor request of
// the game's own path (tests, when the tile's button is not found)
static int command(void*, int argc, const char* const* argv, SkOut* out)
{
    std::vector<std::string> a(argv + 1, argv + argc);
    if (a.empty() || a[0] == "status") {
        H->out_printf(out, "%s", status().c_str());
        return 0;
    }
    if (a[0] == "new" && a.size() >= 2) {
        void* planner = a_Planner ? *(void**)a_Planner : nullptr;
        if (!R(planner, 0x210) || !F<void*>(planner, 0x40) || !o_PlannerMsg) {
            H->out_printf(out, "no open planner\n");
            return 1;
        }
        uint32_t data[4] = {(uint32_t)strtoul(a[1].c_str(), nullptr, 16), 0, a.size() > 2 && a[2] == "edit" ? 1u : 0u, 0};
        int r = h_PlannerMsg(planner, kMsgNewDesign, data);
        H->out_printf(out, "planner message %08X for design %08X -> %d\n", kMsgNewDesign, data[0], r);
        return 0;
    }
    H->out_printf(out, "usage: coop.civedit [status | new <hex design id> [edit]]\n");
    return 1;
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    a_SetDesign = H->sym("SporeCoop::Civ_SetDesign");
    a_Planner = H->sym("SporeCoop::Planner_Instance");
    if (uintptr_t a = H->sym("SporeCoop::Planner_ApplyDesign"))
        H->hook_create(M, (void*)a, (void*)h_Apply, (void**)&o_Apply);
    if (uintptr_t a = H->sym("SporeCoop::Planner_TileDesign"))
        H->hook_create(M, (void*)a, (void*)h_Tile, (void**)&o_Tile);
    if (uintptr_t a = H->sym("SporeCoop::CommunityEditor_HandleMessage"))
        H->hook_create(M, (void*)a, (void*)h_PlannerMsg, (void**)&o_PlannerMsg);
    if (uintptr_t a = H->sym("SporeCoop::GameCiv_HandleMessage"))
        H->hook_create(M, (void*)a, (void*)h_CivMsg, (void**)&o_CivMsg);
    if (uintptr_t a = H->sym("Simulator::cGameModeManager::SubmitEditorRequest"))
        H->hook_create(M, (void*)a, (void*)h_Submit, (void**)&o_Submit);
    H->command_add(M, "coop.civedit",
                   "coop.civedit [status | new <hex design id> [edit]] - a guest's own Civ editors (WP-C3, BUG-1275)", command,
                   nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace civedit
