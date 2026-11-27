// Creature Rivals runtime module (WP-R3 of 0.8.0, BUG-1547, docs/11-rivals.md 11.3.2, 11.4-11.9).
#include "creature_rivals.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "actor_scope.h"
#include "call_guard.h"
#include "creations.h"
#include "fixes.h"
#include "game.h"
#include "history.h"
#include "lifecycle.h"
#include "modes.h"
#include "npc.h"
#include "own_progress.h"
#include "parts.h"
#include "player_life.h"
#include "rivals.h"
#include "settings.h"
#include "social.h"
#include "species_line_core.h"
#include "sporekit_game.hpp"
#include "wire/rivals_faction.h"

namespace creature_rivals {

using namespace skg;
using namespace creature_rivals_logic;

static const SkHost* H = nullptr;
static SkMod* M = nullptr;
static Ctx C;

// Master feature switch for WP-R3 (BUG-1547): Creature Rivals mode.
// Off (SPORE_FIX_OFF=rivals.creature): Creature stage behaves as Together even when stage mode is Rivals.
static bool g_creature_rivals_fix = true;
COOP_FIX("rivals.creature", "BUG-1547", g_creature_rivals_fix,
         "rivals: Creature Rivals mode (own nest/herd, perspective swap, per-player NPC relations, influence, war PvP, home nest protection, race & draft)");

static bool g_habitable_nest_fix = true;
COOP_FIX("rivals.creature_habitable_nest", "BUG-1583", g_habitable_nest_fix,
         "rivals: Creature Rivals places guests at populated same-landmass nests with nearby wild species and ensures family members spawn");

static bool g_pvp_symmetry_fix = true;
COOP_FIX("rivals.creature_pvp_symmetry", "BUG-1583", g_pvp_symmetry_fix,
         "rivals: symmetric Creature PvP hostility, cursor targeting, and War / Friendly Fire damage rules for both host and guest");

static bool g_starter_guard_fix = true;
COOP_FIX("rivals.creature_starter_guard", "BUG-1583", g_starter_guard_fix,
         "rivals: prevent Creature Rivals guests from auto-spawning as Civ-armored or outfitted creatures at brain level 0");

// Resolved game symbols (all verified in data/symbols/overrides_dc04aee5.sym)
static uintptr_t a_JoinNestHerd = 0;
static uintptr_t a_HerdNestPos = 0;
static uintptr_t a_HerdSetEnabled = 0;
static uintptr_t a_HerdSetBestFriends = 0;
static uintptr_t a_CreatureSetSpecies = 0;
static uintptr_t a_CreatureSetSpeciesProfile = 0;
static uintptr_t a_MakeExtinct = 0;
static uintptr_t a_StrategyGet = 0;
static uintptr_t a_StageEndAdvance = 0;
static uintptr_t a_ConsequenceTrait = 0;
static uintptr_t a_MsgMgrGet = 0;
static uintptr_t a_CreatureMinimapPtr = 0;
static uintptr_t a_MinimapAdd = 0;
static uintptr_t a_MinimapMove = 0;
static uintptr_t a_MinimapFind = 0;
static uintptr_t a_MinimapRemove = 0;
static uintptr_t a_AbilityMode = 0;
static uintptr_t a_CursorKind = 0;
static uintptr_t a_SocialMode = 0;

// Hook trampolines (GetRelationship 0x00BA40E0 is chained through tribe.cpp's h_SpecRelGet)
typedef void(SK_CDECL* MakeExtinct_t)(void*);
static MakeExtinct_t o_MakeExtinct = nullptr;
typedef int(SK_CDECL* CursorKind_t)(void*);
static CursorKind_t o_CursorKind = nullptr;
typedef int(SK_THISCALL* SocialMode_t)(void*);
static SocialMode_t o_SocialMode = nullptr;

// Call guard to avoid re-entrancy inside GetRelationship / MakeExtinct hooks
static bool g_in_rel_hook = false;
CALL_GUARD("creature_rivals.rel_hook", g_in_rel_hook);

// ---------------- State ----------------
struct PlayerNest {
    bool assigned = false;
    bool species_swapped = false;
    bool perspective_applied = false;
    game::LiveRef herd;
    net::Vec3 nest_pos{};
    uint32_t species_key[3] = {0, 0, 0};
    uint32_t orig_species_inst = 0;
    double last_spawn_at = -1e9;
};

struct GuestProgress {
    bool known = false;
    double last_at = -1e9;
    uint8_t brain_level = 0;
    float dna = 0.0f;
    float evo_bar = 0.0f;
    uint16_t parts_total = 0;
    std::vector<PartKey> parts;
};

static PlayerNest g_nests[rivals::kMaxPlayers];
static PerPlayerSpeciesTables g_tables;
static std::map<uint32_t, std::array<uint32_t, 3>> g_known_wild_keys; // species_inst -> full ResourceKey
static std::map<uint16_t, uint8_t> g_proxy_family_owner;              // guest: proxy_id -> owner player (0..3)
static GuestProgress g_guest_prog[rivals::kMaxPlayers];
static std::vector<PartKey> g_draft_keys; // index (item_id - 1) -> parts::Key
static uint32_t g_last_world_epoch = 0;
static rivals::BaseDistance g_last_assigned_dist = rivals::BaseDistance::Middle;

// Timers & transition state
static double g_next_assign_scan = 0.0;
static double g_next_assign_tx = 0.0;
static double g_next_rel_sync = 0.0;
static double g_next_prog_tx = 0.0;
static double g_next_map_sync = 0.0;
static double g_next_hostility_sync = 0.0;
static uint32_t g_starter_checked_epoch = 0;
static std::map<uint32_t, std::string> g_species_names;
static bool g_race_end_handled = false;
static bool g_auto_transition_sent = false;
static double g_transition_ready_at = 0.0;
static bool g_map_queued = false;
static uint32_t g_map_blip_keys[rivals::kMaxPlayers]; // addresses are unique minimap blip keys
static void* g_last_minimap = nullptr;
static bool g_map_has[rivals::kMaxPlayers] = {};

// Diagnostics counters
static uint32_t g_assigns_tx = 0, g_assigns_rx = 0;
static uint32_t g_rels_tx = 0, g_rels_rx = 0;
static uint32_t g_persp_swaps = 0, g_herd_swaps = 0;
static uint32_t g_inf_earned = 0, g_inf_actions = 0, g_inf_buys = 0;
static uint32_t g_pvp_blocked = 0, g_pvp_allowed = 0;
static uint32_t g_home_extinct_blocked = 0;
static uint32_t g_draft_unlocked = 0, g_draft_payouts = 0;
static uint32_t g_wins_detected = 0, g_transitions_triggered = 0;
static uint32_t g_family_spawns = 0, g_starter_guards = 0, g_cursor_overrides = 0;
static std::vector<SpeciesEntry> g_guest_last_table; // guest: last received personal species table

static bool connected() { return C.sess && C.sess->connected(); }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
static uint8_t my_id() { return C.sess ? C.sess->my_id() : 0; }
static bool ru() { return C.russian && C.russian(); }

bool enabled() { return g_creature_rivals_fix; }

bool active()
{
    return g_creature_rivals_fix && connected() && game::in_creature_stage() &&
           rivals::stage_is_rivals(rivals::Stage::Creature);
}

// WP-081-5 (BUG-1566, consumed by world.cpp): true when rival guest `peer` is currently assigned and
// playing in this Creature Rivals stage (live puppet or recent kSkProgress heartbeat), so post-editor
// host shares can be skipped without hiding real menu/other-stage recovery shares.
bool peer_recently_active(uint8_t peer, double max_age_s)
{
    if (!is_host() || !active() || !rivals::valid_player(peer) || peer == 0)
        return false;
    if (!g_nests[peer].assigned)
        return false;
    if (C.puppet_of && C.puppet_of(peer) != nullptr)
        return true;
    if (g_guest_prog[peer].known && H && (H->time_seconds() - g_guest_prog[peer].last_at) <= max_age_s)
        return true;
    return false;
}

static void reset_state()
{
    for (int i = 0; i < rivals::kMaxPlayers; i++) {
        g_nests[i] = PlayerNest{};
        g_guest_prog[i] = GuestProgress{};
        g_map_has[i] = false;
    }
    g_tables.reset();
    g_known_wild_keys.clear();
    g_proxy_family_owner.clear();
    g_draft_keys.clear();
    g_guest_last_table.clear();
    g_last_world_epoch = 0;
    g_starter_checked_epoch = 0;
    g_species_names.clear();
    g_next_assign_scan = g_next_assign_tx = g_next_rel_sync = g_next_prog_tx = g_next_map_sync = g_next_hostility_sync = 0.0;
    g_race_end_handled = false;
    g_auto_transition_sent = false;
    g_transition_ready_at = 0.0;
    g_last_minimap = nullptr;
}

SESSION_STATE(
    "creature_rivals.session", nullptr, [] { reset_state(); },
    []() -> size_t {
        size_t n = g_known_wild_keys.size() + g_proxy_family_owner.size() + g_draft_keys.size() +
                   g_guest_last_table.size();
        for (int i = 0; i < rivals::kMaxPlayers; i++)
            n += g_nests[i].assigned ? 1 : 0;
        return n;
    });

PLAYER_STATE(
    "creature_rivals.player",
    [](uint8_t id) {
        if (rivals::valid_player(id) && id != 0) {
            g_nests[id] = PlayerNest{};
            g_guest_prog[id] = GuestProgress{};
            g_tables.set_home_species(id, 0);
        }
    },
    [](uint8_t id) -> size_t {
        return (rivals::valid_player(id) && g_nests[id].assigned) ? 1 : 0;
    });

// ---------------- Helpers: Herds, Species, Ownership ----------------
static bool herd_nest_pos(void* herd, net::Vec3& out)
{
    if (!herd || !a_HerdNestPos || !readable(herd, 0x220))
        return false;
    if (!game::herd_nest(herd))
        return false;
    const float* p = thiscall<const float*>(a_HerdNestPos, herd);
    if (!p || !readable(p, 12) || !finite3(p[0], p[1], p[2]))
        return false;
    out = {p[0], p[1], p[2]};
    return true;
}

static bool species_key_of_profile(void* sp, uint32_t out[3])
{
    if (!sp || !readable((uint8_t*)sp + 0x504, 12))
        return false;
    out[0] = field<uint32_t>(sp, 0x504);
    out[1] = field<uint32_t>(sp, 0x508);
    out[2] = field<uint32_t>(sp, 0x50C);
    return out[0] != 0;
}

static bool is_epic_herd(void* herd)
{
    if (!herd || !readable(herd, 0x90))
        return false;
    uint32_t arch = field<uint32_t>(herd, 0x88);
    return arch == 0xFC811218u || arch == 0x95511253u;
}

static void* find_herd_near(const net::Vec3& pos, float max_dist = 8.0f)
{
    struct Q {
        net::Vec3 pos;
        float best;
        void* found;
    } q{pos, max_dist, nullptr};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (noun != game::kHerdNoun || !game::listed(o, game::kHerdNoun) || !readable(o, 0x220))
                return true;
            net::Vec3 np{};
            if (!herd_nest_pos(o, np))
                return true;
            float d = net::dist(np, q->pos);
            if (d < q->best) {
                q->best = d;
                q->found = o;
            }
            return true;
        },
        &q);
    return q.found;
}

uint8_t herd_owner(void* herd)
{
    if (!herd || !active())
        return 255;
    for (uint8_t i = 0; i < rivals::kMaxPlayers; i++) {
        if (!g_nests[i].assigned)
            continue;
        if (void* h = game::live(g_nests[i].herd); h && h == herd)
            return i;
        net::Vec3 np{};
        if (herd_nest_pos(herd, np) && net::dist(np, g_nests[i].nest_pos) < 5.0f)
            return i;
    }
    return 255;
}

static bool is_hex8_label(const std::string& s)
{
    if (s.size() != 8)
        return false;
    for (unsigned char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    return true;
}

static std::string lookup_species_name(uint32_t inst)
{
    if (inst == 0)
        return {};
    if (auto it = g_species_names.find(inst); it != g_species_names.end())
        return it->second;
    std::string found;
    auto check_inst = [&](uint32_t target) {
        if (target == 0 || !found.empty())
            return;
        for (const auto& o : creations::options(creations::kCreature)) {
            if (o.key.instance == target && !o.name.empty() && !is_hex8_label(o.name)) {
                found = o.name;
                return;
            }
        }
    };
    check_inst(inst);
    if (found.empty()) {
        creations::Key hk = creations::host_species({inst, 0x2B978C46u, 0x40626200u});
        if (hk.instance && hk.instance != inst)
            check_inst(hk.instance);
    }
    if (found.empty() && C.sess) {
        for (uint8_t p = 0; p < rivals::kMaxPlayers; p++) {
            creations::Key pk = creations::player_key(p, creations::kCreature);
            creations::Key po = creations::player_orig(p, creations::kCreature);
            if (pk.instance == inst || po.instance == inst) {
                check_inst(po.instance);
                check_inst(pk.instance);
            }
        }
    }
    if (!found.empty())
        g_species_names[inst] = found;
    return found;
}

static bool arc_crosses_water(const net::Vec3& a, const net::Vec3& b)
{
    float water_r = game::water_radius();
    if (!std::isfinite(water_r) || water_r <= 1.0f)
        return false;
    float la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    float lb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    if (la <= 10.0f || lb <= 10.0f)
        return false;
    net::Vec3 ua{a.x / la, a.y / la, a.z / la};
    net::Vec3 ub{b.x / lb, b.y / lb, b.z / lb};
    float r_mid = 0.5f * (la + lb);
    std::vector<float> samples;
    samples.reserve(12);
    for (int i = 1; i <= 12; i++) {
        float t = (float)i / 13.0f;
        net::Vec3 d{ua.x * (1.0f - t) + ub.x * t, ua.y * (1.0f - t) + ub.y * t, ua.z * (1.0f - t) + ub.z * t};
        float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dl <= 1e-4f)
            continue;
        net::Vec3 p{d.x / dl * r_mid, d.y / dl * r_mid, d.z / dl * r_mid};
        samples.push_back(game::ground_radius(p));
    }
    return has_water_on_samples(water_r, samples);
}

static bool species_matches_player_home(uint8_t pid, uint32_t sp_inst)
{
    if (sp_inst == 0 || !rivals::valid_player(pid) || !g_nests[pid].assigned)
        return false;
    if (g_nests[pid].species_key[0] == sp_inst)
        return true;
    if (is_guest() && g_nests[pid].species_key[0] != 0) {
        creations::Key lk = creations::local_species(
            {g_nests[pid].species_key[0], g_nests[pid].species_key[1] ? g_nests[pid].species_key[1] : 0x2B978C46u,
             g_nests[pid].species_key[2] ? g_nests[pid].species_key[2] : 0x40626200u});
        if (lk.instance == sp_inst)
            return true;
    }
    return false;
}

uint8_t creature_family_owner(void* creature)
{
    if (!creature || !active() || !readable(creature, game::kHerdOff + 4))
        return 255;
    if (creature == game::avatar() || game::is_puppet(creature))
        return 255;
    if (is_guest()) {
        int pid = npc::proxy_id(creature);
        if (pid >= 0) {
            auto it = g_proxy_family_owner.find((uint16_t)pid);
            if (it != g_proxy_family_owner.end() && rivals::valid_player(it->second))
                return it->second;
        }
    }
    void* h = game::herd_of(creature);
    if (h) {
        uint8_t ho = herd_owner(h);
        if (ho != 255)
            return ho;
    }
    // BUG-1583 fallback: when a rival's family creature sits in a temporary herd or before proxy trailer arrives,
    // match its species if it uniquely identifies one assigned player's home species.
    if (g_pvp_symmetry_fix) {
        uint32_t sp = game::species_instance(creature);
        if (sp != 0) {
            uint8_t matched = 255;
            int count = 0;
            for (uint8_t p = 0; p < rivals::kMaxPlayers; p++) {
                if (species_matches_player_home(p, sp)) {
                    matched = p;
                    count++;
                }
            }
            if (count == 1)
                return matched;
        }
    }
    return 255;
}

void* player_herd(uint8_t player_id)
{
    if (!active() || !rivals::valid_player(player_id) || !g_nests[player_id].assigned)
        return nullptr;
    void* ah = game::avatar_herd();
    bool is_rival = (player_id != my_id());
    if (void* h = game::live(g_nests[player_id].herd)) {
        if (!is_rival || h != ah)
            return h;
    }
    if (finite3(g_nests[player_id].nest_pos.x, g_nests[player_id].nest_pos.y, g_nests[player_id].nest_pos.z)) {
        if (void* h = find_herd_near(g_nests[player_id].nest_pos, 10.0f)) {
            if (!is_rival || h != ah) {
                g_nests[player_id].herd = game::live_ref(h, game::kHerdNoun);
                return h;
            }
        }
    }
    return nullptr;
}

void note_proxy_family_owner(uint16_t proxy_id, uint8_t owner)
{
    if (rivals::valid_player(owner))
        g_proxy_family_owner[proxy_id] = owner;
    else
        g_proxy_family_owner.erase(proxy_id);
}

void forget_proxy_family_owner(uint16_t proxy_id) { g_proxy_family_owner.erase(proxy_id); }

bool player_allied_with_species(uint8_t player_id, uint32_t species_inst)
{
    if (!active() || !rivals::valid_player(player_id) || species_inst == 0)
        return false;
    if (is_player_home_species(species_inst) || g_tables.is_extinct(species_inst))
        return false;
    return is_allied_rel(g_tables.get_relation(player_id, species_inst));
}

uint32_t player_species_id(uint8_t player_id)
{
    if (!active() || !rivals::valid_player(player_id))
        return 0;
    if (g_nests[player_id].assigned && g_nests[player_id].species_key[0] != 0)
        return g_nests[player_id].species_key[0];
    if (g_tables.home_species[player_id] != 0)
        return g_tables.home_species[player_id];
    if (player_id == my_id()) {
        if (void* av = game::avatar())
            return game::species_instance(av);
    }
    return 0;
}

int32_t player_species_relation(uint8_t player_id, uint32_t species_inst)
{
    if (!active() || !rivals::valid_player(player_id) || species_inst == 0)
        return kRelNone;
    return g_tables.get_relation(player_id, species_inst);
}

bool is_species_extinct(uint32_t species_inst)
{
    return active() && species_inst != 0 && g_tables.is_extinct(species_inst);
}

static uint8_t creature_player_owner(void* creature)
{
    if (!creature || !active())
        return 255;
    void* av = game::avatar();
    if (creature == av)
        return my_id();
    if (C.sess && C.puppet_of) {
        for (const auto& p : C.sess->players()) {
            if (p.id != my_id() && C.puppet_of(p.id) == creature)
                return p.id;
        }
    }
    return creature_family_owner(creature);
}

bool is_player_home_species(uint32_t species_inst)
{
    if (!active() || species_inst == 0)
        return false;
    if (g_tables.is_home_species(species_inst))
        return true;
    for (int i = 0; i < rivals::kMaxPlayers; i++) {
        if (g_nests[i].assigned && g_nests[i].species_key[0] == species_inst)
            return true;
    }
    void* av = game::avatar();
    if (av && game::species_instance(av) == species_inst)
        return true;
    return false;
}

static void refresh_known_wild_species()
{
    struct Scan {
        std::map<uint32_t, std::array<uint32_t, 3>> keys;
        std::map<uint32_t, bool> any_alive;
        std::map<uint32_t, bool> any_extinct;
    } scan;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* s = (Scan*)u;
            if (noun != game::kHerdNoun || !game::listed(o, game::kHerdNoun) || !readable(o, 0x220))
                return true;
            if (!game::herd_nest(o) || is_epic_herd(o))
                return true;
            void* sp = game::herd_species(o);
            uint32_t k[3] = {};
            if (!species_key_of_profile(sp, k) || is_player_home_species(k[0]))
                return true;
            s->keys[k[0]] = {k[0], k[1], k[2]};
            if (field<uint8_t>(o, 0x190) != 0) // cHerd::mbExtinction (+0x190)
                s->any_extinct[k[0]] = true;
            else
                s->any_alive[k[0]] = true;
            return true;
        },
        &scan);
    for (const auto& [inst, arr] : scan.keys) {
        g_known_wild_keys[inst] = arr;
        if (is_host() && scan.any_extinct[inst] && !scan.any_alive[inst] && !g_tables.is_extinct(inst)) {
            uint32_t k[3] = {arr[0], arr[1], arr[2]};
            g_tables.mark_extinct(k);
        }
    }
}

static bool lookup_wild_key(uint32_t inst, uint32_t out[3])
{
    if (inst == 0)
        return false;
    if (auto it = g_known_wild_keys.find(inst); it != g_known_wild_keys.end()) {
        out[0] = it->second[0];
        out[1] = it->second[1];
        out[2] = it->second[2];
        return true;
    }
    // Check tables
    for (int p = 0; p < rivals::kMaxPlayers; p++) {
        auto it = g_tables.by_player[p].find(inst);
        if (it != g_tables.by_player[p].end() && it->second.key[0] == inst) {
            out[0] = it->second.key[0];
            out[1] = it->second.key[1];
            out[2] = it->second.key[2];
            return true;
        }
    }
    // Fallback: standard Creature ResourceKey (type 0x2B978C46, group 0x40626200)
    out[0] = inst;
    out[1] = 0x2B978C46u;
    out[2] = 0x40626200u;
    return true;
}

// Ensure an assigned rival player herd is never permanently stuck at 0 live members (BUG-1583).
static void ensure_herd_populated(void* herd, void* sp, uint8_t pid)
{
    if (!g_habitable_nest_fix || !is_host() || !herd || !sp || !rivals::valid_player(pid) || !readable(herd, 0x220))
        return;
    double now = H ? H->time_seconds() : 0.0;
    if (now - g_nests[pid].last_spawn_at < 10.0)
        return;
    void** mb = field<void**>(herd, 0x40);
    void** me = field<void**>(herd, 0x44);
    int count = (mb && me > mb && (me - mb) <= 64 && readable(mb, (size_t)(me - mb) * sizeof(void*)))
                    ? (int)(me - mb)
                    : 0;
    int alive_members = 0;
    void* av = game::avatar();
    for (int i = 0; i < count; i++) {
        void* m = mb[i];
        if (m && m != av && !game::is_puppet(m) && game::listed(m, 0x18EB45Eu) && !game::is_dead(m))
            alive_members++;
    }
    if (alive_members > 0)
        return;
    net::Vec3 np{};
    if (!herd_nest_pos(herd, np))
        return;
    float rlen = std::sqrt(np.x * np.x + np.y * np.y + np.z * np.z);
    if (rlen <= 10.0f)
        return;
    net::Vec3 up{np.x / rlen, np.y / rlen, np.z / rlen};
    net::Vec3 ref = std::fabs(up.y) < 0.9f ? net::Vec3{0, 1, 0} : net::Vec3{1, 0, 0};
    net::Vec3 tx{up.y * ref.z - up.z * ref.y, up.z * ref.x - up.x * ref.z, up.x * ref.y - up.y * ref.x};
    float tlen = std::sqrt(tx.x * tx.x + tx.y * tx.y + tx.z * tx.z);
    if (tlen <= 1e-4f)
        return;
    tx = {tx.x / tlen, tx.y / tlen, tx.z / tlen};
    net::Vec3 ty{up.y * tx.z - up.z * tx.y, up.z * tx.x - up.x * tx.z, up.x * tx.y - up.y * tx.x};

    g_nests[pid].last_spawn_at = now;
    int spawned = 0;
    for (int i = 0; i < 3; i++) {
        float ang = (float)i * 2.0943951f; // 120 degrees apart around nest
        float ca = std::cos(ang) * 4.5f;
        float sa = std::sin(ang) * 4.5f;
        net::Vec3 dst{np.x + tx.x * ca + ty.x * sa, np.y + tx.y * ca + ty.y * sa, np.z + tx.z * ca + ty.z * sa};
        float gr = game::ground_radius(dst);
        float dl = std::sqrt(dst.x * dst.x + dst.y * dst.y + dst.z * dst.z);
        if (gr > 10.0f && dl > 10.0f) {
            float s = (gr + 0.3f) / dl;
            dst = {dst.x * s, dst.y * s, dst.z * s};
        }
        if (game::spawn_creature(dst, sp, herd)) {
            spawned++;
            g_family_spawns++;
        }
    }
    if (spawned > 0 && H && M)
        H->log(M, SK_INFO, "creature_rivals: populated empty guest herd %p (player %u) with %d family member(s) (BUG-1583)",
               herd, (unsigned)pid, spawned);
}

// ---------------- R2: Host Nest & Family Herd Takeover ----------------
static void swap_herd_species(void* herd, void* sp, uint8_t pid)
{
    if (!herd || !sp || !readable(herd, 0x220))
        return;
    // Set herd's species profile (raw non-owning pointer verified in CreateHerd 0xB2395C)
    field<void*>(herd, 0xA4) = sp;
    // Keep herd alive, non-extinct, enabled, with preserved family size >= 3 (R7)
    field<uint8_t>(herd, 0x190) = 0; // mbExtinct = 0
    if (void* ns = game::herd_nest(herd); ns && readable(ns, 0x124))
        field<uint8_t>(ns, 0x120) = 0; // nest not extinct
    if (field<int32_t>(herd, 0xF4) < 3)
        field<int32_t>(herd, 0xF4) = 3;
    if (a_HerdSetEnabled && !field<uint8_t>(herd, 0x218))
        thiscall<void>(a_HerdSetEnabled, herd, true);
    // Swap existing live members in herd+0x40..+0x44 to guest's species
    void** mb = field<void**>(herd, 0x40);
    void** me = field<void**>(herd, 0x44);
    int count = (mb && me > mb && (me - mb) <= 64 && readable(mb, (size_t)(me - mb) * sizeof(void*)))
                    ? (int)(me - mb)
                    : 0;
    void* av = game::avatar();
    for (int i = 0; i < count; i++) {
        void* m = mb[i];
        if (!m || m == av || game::is_puppet(m) || !game::listed(m, 0x18EB45Eu))
            continue;
        if (game::species_profile(m) == sp)
            continue;
        if (a_CreatureSetSpecies)
            thiscall<bool>(a_CreatureSetSpecies, m, sp, 0, true);
        else if (a_CreatureSetSpeciesProfile)
            thiscall<void>(a_CreatureSetSpeciesProfile, m, sp);
    }
    ensure_herd_populated(herd, sp, pid);
    uint32_t sk[3] = {};
    if (species_key_of_profile(sp, sk)) {
        g_nests[pid].species_key[0] = sk[0];
        g_nests[pid].species_key[1] = sk[1];
        g_nests[pid].species_key[2] = sk[2];
        g_tables.set_home_species(pid, sk[0]);
    }
    g_nests[pid].species_swapped = true;
    g_herd_swaps++;
    if (H && M)
        H->log(M, SK_INFO, "creature_rivals: swapped herd %p (player %u, %d members) to species %08X", herd,
               (unsigned)pid, count, g_nests[pid].species_key[0]);
}

static void host_broadcast_assignments()
{
    if (!is_host() || !active())
        return;
    std::vector<AssignEntry> entries;
    for (uint8_t i = 0; i < rivals::kMaxPlayers; i++) {
        if (!g_nests[i].assigned)
            continue;
        AssignEntry e{};
        e.player_id = i;
        e.nest_x = g_nests[i].nest_pos.x;
        e.nest_y = g_nests[i].nest_pos.y;
        e.nest_z = g_nests[i].nest_pos.z;
        e.species_key[0] = g_nests[i].species_key[0];
        e.species_key[1] = g_nests[i].species_key[1];
        e.species_key[2] = g_nests[i].species_key[2];
        e.flags = 1u | (g_nests[i].species_swapped ? 2u : 0u);
        entries.push_back(e);
    }
    if (entries.empty())
        return;
    net::Writer w;
    write_assign(w, entries);
    rivals::broadcast_faction(rivals::Stage::Creature, kSkAssign, w);
    g_assigns_tx++;
}

static bool host_has_unassigned_player()
{
    if (!C.sess)
        return false;
    void* ah = game::avatar_herd();
    if (!g_nests[0].assigned || (ah && game::live(g_nests[0].herd) != ah))
        return true;
    for (const auto& p : C.sess->players()) {
        if (p.id == 0 || !rivals::valid_player(p.id))
            continue;
        if (!g_nests[p.id].assigned || !game::live(g_nests[p.id].herd))
            return true;
    }
    return false;
}

static void host_assign_nests(bool force_reassign = false)
{
    if (!is_host() || !active())
        return;
    void* av = game::avatar();
    void* ah = game::avatar_herd();
    if (!av || !ah)
        return;

    // 1. Record host's home nest (player 0), including species/nest updates after host Creature Editor or migration
    net::Vec3 hpos{};
    if (!herd_nest_pos(ah, hpos))
        return;
    game::Key hk = game::species_key(av);
    bool changed = false;
    bool host_updated = !g_nests[0].assigned || force_reassign || game::live(g_nests[0].herd) != ah ||
                        (hk.instance != 0 && g_nests[0].species_key[0] != hk.instance) ||
                        net::dist(g_nests[0].nest_pos, hpos) > 5.0f;
    if (host_updated) {
        g_nests[0].assigned = true;
        g_nests[0].species_swapped = true;
        g_nests[0].perspective_applied = true;
        g_nests[0].herd = game::live_ref(ah, game::kHerdNoun);
        g_nests[0].nest_pos = hpos;
        g_nests[0].species_key[0] = hk.instance;
        g_nests[0].species_key[1] = hk.type;
        g_nests[0].species_key[2] = hk.group;
        g_tables.set_home_species(0, hk.instance);
        changed = true;
    }

    // 1b. Re-attach already-assigned guest herds by nest position before scanning, so host editor/migration never moves a guest's nest
    if (!force_reassign) {
        for (const auto& p : C.sess->players()) {
            uint8_t pid = p.id;
            if (pid == 0 || !rivals::valid_player(pid) || !g_nests[pid].assigned || game::live(g_nests[pid].herd))
                continue;
            if (void* existing = find_herd_near(g_nests[pid].nest_pos, 10.0f); existing && existing != ah)
                g_nests[pid].herd = game::live_ref(existing, game::kHerdNoun);
        }
    }

    // 2. Scan all planet nest herds ONLY when force_reassign or at least one connected guest needs a nest pick
    bool need_scan = force_reassign;
    if (!need_scan) {
        for (const auto& p : C.sess->players()) {
            if (p.id != 0 && rivals::valid_player(p.id) &&
                (!g_nests[p.id].assigned || !game::live(g_nests[p.id].herd))) {
                need_scan = true;
                break;
            }
        }
    }

    rivals::BaseDistance pref = rivals::base_distance();
    g_last_assigned_dist = pref;

    if (need_scan) {
        struct CandWrap {
            NestCandidate nc;
            void* ptr;
        };
        struct ScanCtx {
            std::vector<CandWrap>* wraps;
            net::Vec3 hpos;
            bool check_water;
        };
        std::vector<CandWrap> wraps;
        std::vector<NestCandidate> cands;
        ScanCtx sctx{&wraps, hpos, g_habitable_nest_fix};
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* ctx = (ScanCtx*)u;
                if (noun != game::kHerdNoun || !game::listed(o, game::kHerdNoun) || !readable(o, 0x220))
                    return true;
                if (!game::herd_nest(o) || !field<uint32_t>(o, 0x88)) // must have real nest & archetype
                    return true;
                net::Vec3 np{};
                if (!herd_nest_pos(o, np))
                    return true;
                void* sp = game::herd_species(o);
                uint32_t sk[3] = {};
                species_key_of_profile(sp, sk);
                void** mb = field<void**>(o, 0x40);
                void** me = field<void**>(o, 0x44);
                int mcnt = (mb && me >= mb && (me - mb) <= 64) ? (int)(me - mb) : 0;
                NestCandidate nc{};
                nc.id = game::object_serial(o);
                if (nc.id == 0)
                    nc.id = (uint32_t)(uintptr_t)o;
                nc.x = np.x;
                nc.y = np.y;
                nc.z = np.z;
                nc.species_inst = sk[0];
                nc.is_avatar_herd = field<uint8_t>(o, 0x84) != 0;
                nc.has_nest = true;
                nc.is_epic = is_epic_herd(o);
                nc.is_extinct = field<uint8_t>(o, 0x190) != 0;
                nc.members = mcnt;
                nc.water_between_host = ctx->check_water ? arc_crosses_water(ctx->hpos, np) : false;
                ctx->wraps->push_back({nc, o});
                return true;
            },
            &sctx);

        NestCandidate host_nc{};
        host_nc.id = game::object_serial(ah);
        host_nc.x = hpos.x;
        host_nc.y = hpos.y;
        host_nc.z = hpos.z;
        host_nc.is_avatar_herd = true;
        host_nc.has_nest = true;
        for (const auto& w : wraps)
            cands.push_back(w.nc);

        std::vector<uint32_t> taken;
        if (host_nc.id)
            taken.push_back(host_nc.id);
        if (!force_reassign) {
            for (int i = 1; i < rivals::kMaxPlayers; i++) {
                if (g_nests[i].assigned) {
                    if (void* h = game::live(g_nests[i].herd))
                        taken.push_back(game::object_serial(h));
                }
            }
        }

        for (const auto& p : C.sess->players()) {
            uint8_t pid = p.id;
            if (pid == 0 || !rivals::valid_player(pid))
                continue;
            bool need_pick = force_reassign || !g_nests[pid].assigned || !game::live(g_nests[pid].herd);
            if (need_pick) {
                uint32_t chosen_id = select_nest_for_guest(host_nc, cands, taken, pref);
                if (chosen_id != 0) {
                    for (const auto& w : wraps) {
                        if (w.nc.id == chosen_id) {
                            g_nests[pid] = PlayerNest{};
                            g_nests[pid].assigned = true;
                            g_nests[pid].herd = game::live_ref(w.ptr, game::kHerdNoun);
                            g_nests[pid].nest_pos = {w.nc.x, w.nc.y, w.nc.z};
                            g_nests[pid].orig_species_inst = w.nc.species_inst;
                            g_nests[pid].species_key[0] = w.nc.species_inst;
                            taken.push_back(chosen_id);
                            changed = true;
                            if (H && M)
                                H->log(M, SK_INFO,
                                       "creature_rivals: assigned nest herd %p at (%.1f %.1f %.1f, dist=%.1f, members=%d, water=%d, pref=%s) to player %u",
                                       w.ptr, w.nc.x, w.nc.y, w.nc.z, net::dist(g_nests[pid].nest_pos, hpos),
                                       w.nc.members, (int)w.nc.water_between_host, rivals::distance_name(pref),
                                       (unsigned)pid);
                            break;
                        }
                    }
                }
            }
        }
    }

    // 3. Lightweight species swap & R7 non-extinction invariant on already-assigned guest herds
    for (const auto& p : C.sess->players()) {
        uint8_t pid = p.id;
        if (pid == 0 || !rivals::valid_player(pid) || !g_nests[pid].assigned)
            continue;
        void* herd = game::live(g_nests[pid].herd);
        void* gsp = creations::player_species(pid);
        if (herd && gsp && game::herd_species(herd) != gsp) {
            swap_herd_species(herd, gsp, pid);
            changed = true;
        } else if (herd && gsp && g_nests[pid].species_key[0] == g_nests[pid].orig_species_inst) {
            uint32_t sk[3] = {};
            if (species_key_of_profile(gsp, sk)) {
                g_nests[pid].species_key[0] = sk[0];
                g_nests[pid].species_key[1] = sk[1];
                g_nests[pid].species_key[2] = sk[2];
                g_nests[pid].species_swapped = true;
                g_tables.set_home_species(pid, sk[0]);
                changed = true;
            }
        }
        // Enforce R7 home nest non-extinction invariant every assign pass
        if (herd && readable(herd, 0x220)) {
            if (field<uint8_t>(herd, 0x190) != 0)
                field<uint8_t>(herd, 0x190) = 0;
            if (void* ns = game::herd_nest(herd); ns && readable(ns, 0x124) && field<uint8_t>(ns, 0x120) != 0)
                field<uint8_t>(ns, 0x120) = 0;
            if (field<int32_t>(herd, 0xF4) < 3)
                field<int32_t>(herd, 0xF4) = 3;
            void* eff_sp = gsp ? gsp : game::herd_species(herd);
            if (eff_sp)
                ensure_herd_populated(herd, eff_sp, pid);
        }
    }

    if (changed)
        host_broadcast_assignments();
}

// ---------------- R1: Guest Perspective Swap ----------------
static void guest_apply_perspective()
{
    if (!is_guest() || !active())
        return;
    uint8_t me = my_id();
    if (!rivals::valid_player(me) || !g_nests[me].assigned)
        return;
    void* av = game::avatar();
    if (!av || !a_JoinNestHerd)
        return;

    void* target_herd = game::live(g_nests[me].herd);
    if (!target_herd) {
        target_herd = find_herd_near(g_nests[me].nest_pos, 10.0f);
        if (target_herd)
            g_nests[me].herd = game::live_ref(target_herd, game::kHerdNoun);
    }
    if (!target_herd || !readable(target_herd, 0x220))
        return;
    void* target_nest = game::herd_nest(target_herd);
    if (!target_nest)
        return;

    // Pin brain range [0, 4] so CreatureGame_NestHerdFor(0) ALWAYS picks this herd for minimap & respawn (R1)
    field<int32_t>(target_herd, 0x120) = 0;
    field<int32_t>(target_herd, 0x124) = kMaxBrainLevel;
    field<uint8_t>(target_herd, 0x190) = 0; // never extinct
    if (void* sp = game::species_profile(av); sp && game::herd_species(target_herd) != sp)
        field<void*>(target_herd, 0xA4) = sp;

    void* cur_herd = game::avatar_herd();
    if (cur_herd != target_herd || game::herd_of(av) != target_herd) {
        cdecl_call<bool>(a_JoinNestHerd, target_nest);
        // Move guest avatar near its own nest on first perspective swap (offset 4 m along tangent, grounded)
        net::Vec3 np = g_nests[me].nest_pos;
        float rlen = std::sqrt(np.x * np.x + np.y * np.y + np.z * np.z);
        if (rlen > 10.0f) {
            net::Vec3 up{np.x / rlen, np.y / rlen, np.z / rlen};
            net::Vec3 ref = std::fabs(up.y) < 0.9f ? net::Vec3{0, 1, 0} : net::Vec3{1, 0, 0};
            net::Vec3 tx{up.y * ref.z - up.z * ref.y, up.z * ref.x - up.x * ref.z, up.x * ref.y - up.y * ref.x};
            float tlen = std::sqrt(tx.x * tx.x + tx.y * tx.y + tx.z * tx.z);
            if (tlen > 1e-4f) {
                net::Vec3 dst{np.x + tx.x / tlen * 5.0f, np.y + tx.y / tlen * 5.0f, np.z + tx.z / tlen * 5.0f};
                float gr = game::ground_radius(dst);
                float dlen = std::sqrt(dst.x * dst.x + dst.y * dst.y + dst.z * dst.z);
                if (gr > 10.0f && dlen > 10.0f) {
                    float s = (gr + 0.5f) / dlen;
                    dst = {dst.x * s, dst.y * s, dst.z * s};
                }
                game::teleport(av, dst, game::orientation(av));
            }
        }
        g_nests[me].perspective_applied = true;
        g_persp_swaps++;
        if (H && M)
            H->log(M, SK_INFO, "creature_rivals: guest perspective swapped to own nest herd %p at (%.1f %.1f %.1f)",
                   target_herd, np.x, np.y, np.z);
        if (C.notice)
            C.notice(ru() ? "Ваше гнездо и стая готовы на новом месте!" : "Your rival nest and family are ready!");
    } else {
        g_nests[me].perspective_applied = true;
    }
}

// ---------------- Minimap Rival Nest Blips (outside the frame) ----------------
struct BlipDesc {
    uint32_t argb;
    float pos[3];
    uint32_t flags;
    void* obj;
    uint32_t z[3];
};
static_assert(sizeof(BlipDesc) == 0x24, "BlipDesc");

static uint32_t nest_blip_key(uint8_t pid) { return (uint32_t)(uintptr_t)&g_map_blip_keys[pid]; }

static void* creature_minimap_now()
{
    if (!a_CreatureMinimapPtr || !readable((void*)a_CreatureMinimapPtr, 4))
        return nullptr;
    void* m = *(void**)a_CreatureMinimapPtr;
    return m && readable(m, 0x9A8) ? m : nullptr;
}

static void minimap_task(void*)
{
    g_map_queued = false;
    if (!a_MinimapAdd || !a_MinimapMove || !a_MinimapFind || !a_MinimapRemove)
        return;
    void* m = creature_minimap_now();
    if (m != g_last_minimap) {
        g_last_minimap = m;
        memset(g_map_has, 0, sizeof g_map_has);
    }
    if (!m)
        return;
    bool show = active();
    uint8_t me = my_id();
    for (uint8_t pid = 0; pid < rivals::kMaxPlayers; pid++) {
        uint32_t key = nest_blip_key(pid);
        bool want = show && pid != me && g_nests[pid].assigned &&
                    finite3(g_nests[pid].nest_pos.x, g_nests[pid].nest_pos.y, g_nests[pid].nest_pos.z);
        const BlipDesc* cur = thiscall<const BlipDesc*>(a_MinimapFind, m, key);
        if (!want) {
            if (cur)
                thiscall<void>(a_MinimapRemove, m, key, 1, 0);
            g_map_has[pid] = false;
            continue;
        }
        rivals::Relation rel = rivals::relation(me, pid);
        uint32_t col = rel == rivals::Relation::War ? 0xFFFF3838u
                       : (rel == rivals::Relation::Ally ? 0xFF4CE86Au : 0xFFFFA838u);
        uint32_t flags = 0x20u; // nest marker
        float pos[3] = {g_nests[pid].nest_pos.x, g_nests[pid].nest_pos.y, g_nests[pid].nest_pos.z};
        if (cur && (cur->argb != col || cur->flags != flags)) {
            thiscall<void>(a_MinimapRemove, m, key, 1, 0);
            cur = nullptr;
        }
        if (cur) {
            thiscall<bool>(a_MinimapMove, m, key, (const float*)pos, false);
        } else {
            BlipDesc nd{};
            nd.argb = col;
            nd.pos[0] = pos[0];
            nd.pos[1] = pos[1];
            nd.pos[2] = pos[2];
            nd.flags = flags;
            thiscall<bool>(a_MinimapAdd, m, key, &nd, 0u, false);
        }
        g_map_has[pid] = true;
    }
}

// ---------------- R4: Per-Player Wild Species Relations Sync ----------------
static void host_send_rels_to(uint8_t pid)
{
    if (!is_host() || !active() || pid == 0 || !rivals::valid_player(pid))
        return;
    auto entries = g_tables.entries_for(pid);
    net::Writer w;
    write_npc_rels(w, entries);
    if (rivals::send_faction_to(pid, rivals::Stage::Creature, kSkNpcRels, w))
        g_rels_tx++;
}

static void host_sync_native_to_player0()
{
    if (!is_host() || !active())
        return;
    auto native = social::read_native_table();
    std::vector<SpeciesEntry> conv;
    conv.reserve(native.size());
    for (const auto& r : native) {
        SpeciesEntry e{};
        e.key[0] = r.key[0];
        e.key[1] = r.key[1];
        e.key[2] = r.key[2];
        e.relation = r.relation;
        e.x = r.x;
        e.impressed = r.impressed;
        e.killed = r.killed;
        e.extinct = g_tables.is_extinct(e.key[0]);
        conv.push_back(e);
        if (e.key[0] && !is_player_home_species(e.key[0]))
            g_known_wild_keys[e.key[0]] = {e.key[0], e.key[1], e.key[2]};
    }
    g_tables.sync_from_host_native(conv);
}

static void host_write_player0_entry(uint32_t inst)
{
    auto it = g_tables.by_player[0].find(inst);
    if (it == g_tables.by_player[0].end())
        return;
    const auto& e = it->second;
    social::write_native_record(e.key, e.relation, e.x, e.impressed, e.killed);
}

// Hooked from social.cpp h_RelEvent on host when creature_rivals::active().
// Returns true if creature_rivals handled/swallowed the event (do NOT call o_RelEvent on host's native table).
bool handle_host_rel_event(int event, const uint32_t key[3], void* creature, uint8_t actor)
{
    if (!is_host() || !active() || !key || key[0] == 0)
        return false;
    // R7: home species of ANY player can NEVER go extinct or suffer wild-table kill events
    if (is_player_home_species(key[0]) && (event == 0 || event == 5 || event == 2)) {
        g_home_extinct_blocked++;
        return true;
    }
    // Host's own native social / combat events (actor == kNone or 0)
    if (actor == actor::kNone || actor == 0) {
        if (event == 0 || event == 5) {
            uint32_t ext_need = 3;
            if (creature && readable(creature, 0xE88)) {
                void* tun = field<void*>(creature, 0xE84);
                if (tun && readable((uint8_t*)tun + 0x400, 4))
                    ext_need = std::max(1u, field<uint32_t>(tun, 0x400));
            }
            auto ko = g_tables.record_kill(0, key, ext_need);
            if (ko.became_extinct) {
                for (const auto& p : C.sess->players())
                    if (p.id != 0)
                        host_send_rels_to(p.id);
            }
        }
        return false; // let host's native table apply host's own act
    }
    // Guest's act on host (actor >= 1): update ONLY that guest's table!
    if (!rivals::valid_player(actor))
        return true;
    if (event == 0 || event == 5) {
        uint32_t ext_need = 3;
        if (creature && readable(creature, 0xE88)) {
            void* tun = field<void*>(creature, 0xE84);
            if (tun && readable((uint8_t*)tun + 0x400, 4))
                ext_need = std::max(1u, field<uint32_t>(tun, 0x400));
        }
        auto ko = g_tables.record_kill(actor, key, ext_need);
        if (ko.blocked_home) {
            g_home_extinct_blocked++;
            return true;
        }
        host_send_rels_to(actor);
        if (ko.became_extinct) {
            // Shared extinction: broadcast to all guests AND let host native game mark extinct
            for (const auto& p : C.sess->players())
                if (p.id != 0)
                    host_send_rels_to(p.id);
            host_write_player0_entry(key[0]);
            return false;
        }
        return true; // do NOT pollute host's own table with guest's non-extinct kill
    }
    if (event == 2) {
        // Guest attacked wild creature: lower guest's own per-player wild relation without polluting host's table
        if (g_tables.record_attack(actor, key))
            host_send_rels_to(actor);
        return true;
    }
    return false;
}

bool on_guest_impress(uint8_t player, const uint32_t key[3], void* npc, float dna)
{
    if (!is_host() || !active() || !rivals::valid_player(player) || !key || key[0] == 0)
        return false;
    uint32_t ally_need = 3, friendly_need = 2;
    if (npc && readable(npc, 0xE88)) {
        void* tun = field<void*>(npc, 0xE84);
        if (tun && readable((uint8_t*)tun + 0x408, 8)) {
            uint32_t a = field<uint32_t>(tun, 0x40C);
            uint32_t f = field<uint32_t>(tun, 0x408);
            if (a >= 1 && a <= 10)
                ally_need = a;
            if (f >= 1 && f <= 10)
                friendly_need = f;
        }
    }
    auto res = g_tables.record_impress(player, key, ally_need, friendly_need);
    if (res.changed) {
        int32_t inf = kInfPerImpress + (res.became_allied ? kInfPerAlly : 0);
        rivals::grant_influence(player, inf, res.became_allied ? "species allied" : "species impressed");
        g_inf_earned += (uint32_t)inf;
        if (res.became_allied) {
            rivals::add_chronicle_points(player, rivals::kPtsNpcFaction, "species allied");
            // BUG-1557: social.cpp skips a_RelEvent(3) in Creature Rivals so credit the species-ally social bonus here
            history::social_win(player, 25.0f);
        }
        host_send_rels_to(player);
    }
    if (dna > 0.0f)
        history::social_win(player, dna);
    return true;
}

void on_host_impress(const uint32_t key[3], bool became_allied)
{
    if (!is_host() || !active() || !key || key[0] == 0 || is_player_home_species(key[0]))
        return;
    int32_t inf = kInfPerImpress + (became_allied ? kInfPerAlly : 0);
    rivals::grant_influence(0, inf, became_allied ? "species allied" : "species impressed");
    g_inf_earned += (uint32_t)inf;
    if (became_allied)
        rivals::add_chronicle_points(0, rivals::kPtsNpcFaction, "species allied");
}

// ---------------- War PvP & R7 Home Protection Hooks ----------------
bool allow_pvp_damage(uint8_t attacker, uint8_t victim, bool is_family)
{
    if (!active())
        return false;
    TargetKind tk = is_family ? TargetKind::PlayerFamily : TargetKind::PlayerAvatar;
    bool ff = g_pvp_symmetry_fix ? settings::on(settings::kFriendlyFire) : false;
    bool ok = allow_creature_damage(true, ff, attacker, victim, tk, rivals::at_war(attacker, victim));
    if (ok)
        g_pvp_allowed++;
    else
        g_pvp_blocked++;
    return ok;
}

bool block_shared_parts() { return active(); }
bool use_own_progress() { return active(); }
bool bypass_nest_family_match() { return active(); }

// ---------------- Bug #2 (BUG-1583): Guest Starter Creature Tier Guard ----------------
static bool inspect_species_profile_stats(void* sp, uint32_t inst, species::CreatureStageStats& out)
{
    out = species::CreatureStageStats{};
    out.instance = inst;
    if (!sp || !readable((uint8_t*)sp + 0x56C, 4))
        return false;
    float bhp = field<float>(sp, 0x56C);
    if (std::isfinite(bhp) && bhp >= 0.0f && bhp <= 500.0f)
        out.bonus_hp = bhp;
    out.total_hp = 10.0f + out.bonus_hp;
    if (readable((uint8_t*)sp + 0x6D4, 8)) {
        void** a0 = field<void**>(sp, 0x6D4);
        void** a1 = field<void**>(sp, 0x6D8);
        int na = (a0 && a1 > a0 && (a1 - a0) <= 64) ? (int)(a1 - a0) : 0;
        if (na > 0 && readable(a0, (size_t)na * sizeof(void*))) {
            for (int i = 0; i < na; i++) {
                uint8_t* ab = (uint8_t*)a0[i];
                if (!ab || !readable(ab, 0x40))
                    continue;
                uint32_t ab_type = *(uint32_t*)(ab + 8);
                // Bite (0x1E), Charge (0x1F), Strike (0x20), Spit (0x21)
                if (ab_type == 0x1Eu || ab_type == 0x1Fu || ab_type == 0x20u || ab_type == 0x21u) {
                    float lvl = *(float*)(ab + 0x3C);
                    if (std::isfinite(lvl) && lvl > out.max_combat_level && lvl <= 10.0f)
                        out.max_combat_level = lvl;
                }
            }
        }
    }
    return true;
}

static void guest_guard_starter_species()
{
    if (!g_starter_guard_fix || !is_guest() || !active())
        return;
    uint32_t epoch = game::world_epoch();
    if (epoch == 0 || g_starter_checked_epoch == epoch)
        return;
    void* av = game::avatar();
    if (!av)
        return;
    if (game::brain_level() > 0) {
        g_starter_checked_epoch = epoch; // Already evolved past brain level 0: never interfere
        return;
    }

    float host_hp = 10.0f;
    if (C.puppet_of) {
        if (void* hp = C.puppet_of(0)) {
            float m = game::max_health(hp);
            if (std::isfinite(m) && m >= 10.0f && m <= 200.0f)
                host_hp = m;
        }
    }

    creations::Option cur_opt = creations::mine(creations::kCreature);
    void* av_sp = game::species_profile(av);
    uint32_t av_inst = game::species_instance(av);

    species::CreatureStageStats cur_stats{};
    bool have_stats = false;
    if (cur_opt.key.instance != 0) {
        if (void* sp = game::species_by_key({cur_opt.key.instance, cur_opt.key.type, cur_opt.key.group}))
            have_stats = inspect_species_profile_stats(sp, cur_opt.key.instance, cur_stats);
    }
    if (!have_stats && av_sp) {
        have_stats = inspect_species_profile_stats(av_sp, av_inst, cur_stats);
    }
    if (have_stats) {
        float live_max = game::max_health(av);
        if (std::isfinite(live_max) && live_max > cur_stats.total_hp && live_max <= 500.0f)
            cur_stats.total_hp = live_max;
    }

    g_starter_checked_epoch = epoch;
    if (!have_stats || !species::is_overpowered_or_outfitted_starter(cur_stats, host_hp))
        return;

    // Auto-selected creation exceeds starting Creature-stage tier (e.g. Civ-armored creature with 20-26 HP / Strike 5).
    // Prefer a clean starting Creature-stage creation from My Creations if one is available, else use Host's starter.
    std::vector<species::CreatureStageStats> cands;
    std::map<uint32_t, creations::Key> key_by_inst;
    for (const auto& o : creations::options(creations::kCreature)) {
        if (o.key.instance == 0 || o.key.instance == cur_stats.instance)
            continue;
        if (!creations::key_known(o.key))
            continue;
        void* sp = game::species_by_key({o.key.instance, o.key.type, o.key.group});
        if (!sp)
            continue;
        species::CreatureStageStats cs{};
        if (inspect_species_profile_stats(sp, o.key.instance, cs)) {
            cands.push_back(cs);
            key_by_inst[o.key.instance] = o.key;
        }
    }
    uint32_t clean_inst = species::select_clean_creature_starter(cands, host_hp);
    if (clean_inst != 0 && key_by_inst.count(clean_inst)) {
        creations::choose(creations::kCreature, key_by_inst[clean_inst]);
    } else {
        creations::choose(creations::kCreature, creations::Key{});
    }
    g_starter_guards++;
    if (H && M)
        H->log(M, SK_INFO,
               "creature_rivals: filtered high-tier starter creature %08X (hp=%.1f bonus=%.1f combat=%.1f, host_hp=%.1f) -> %08X (BUG-1583)",
               cur_stats.instance, cur_stats.total_hp, cur_stats.bonus_hp, cur_stats.max_combat_level, host_hp,
               clean_inst);
    if (C.notice)
        C.notice(ru() ? "Высокоуровневое существо заменено на стартовый вид этапа Существо."
                      : "High-tier outfitted creature replaced with a starting Creature-stage species.");
}

// ---------------- Bug #3 (BUG-1583): Symmetric PvP Hostility, Herd Attitude & Cursor Hooks ----------------
static void sync_rival_hostility()
{
    if (!g_pvp_symmetry_fix || !active() || !C.sess)
        return;
    uint8_t me = my_id();
    bool ff = settings::on(settings::kFriendlyFire);
    uint32_t my_sp = player_species_id(me);

    for (uint8_t p = 0; p < rivals::kMaxPlayers; p++) {
        if (p == me || !g_nests[p].assigned)
            continue;
        int32_t want_rel = resolve_player_pair_relation(me, p, rivals::at_war(me, p), rivals::allied(me, p), ff);
        bool hostile = is_hostile_rel(want_rel);
        bool allied = is_allied_rel(want_rel);

        // Update cSpeciesRelationshipManager native table for rival's species (only when distinct from our own species).
        uint32_t sp_inst = g_nests[p].species_key[0];
        if (sp_inst != 0 && sp_inst != my_sp) {
            uint32_t rk[3] = {sp_inst, g_nests[p].species_key[1] ? g_nests[p].species_key[1] : 0x2B978C46u,
                              g_nests[p].species_key[2] ? g_nests[p].species_key[2] : 0x40626200u};
            social::write_native_record(rk, want_rel, 0, 0, 0);
            if (is_guest()) {
                creations::Key lk = creations::local_species({rk[0], rk[1], rk[2]});
                if (lk.instance != 0 && lk.instance != rk[0] && lk.instance != my_sp) {
                    uint32_t lrk[3] = {lk.instance, lk.type, lk.group};
                    social::write_native_record(lrk, want_rel, 0, 0, 0);
                }
            }
        }

        // Keep rival herd's mbBestFriends (+0x191) and members' impressed flag synchronized with War / FF.
        if (void* rh = player_herd(p); rh && readable(rh, 0x220)) {
            bool cur_bf = field<uint8_t>(rh, 0x191) != 0;
            if (cur_bf != allied) {
                if (a_HerdSetBestFriends)
                    thiscall<void>(a_HerdSetBestFriends, rh, (int)allied);
                else
                    field<uint8_t>(rh, 0x191) = allied ? 1 : 0;
            }
            if (hostile) {
                void** mb = field<void**>(rh, 0x40);
                void** me_end = field<void**>(rh, 0x44);
                int cnt = (mb && me_end > mb && (me_end - mb) <= 64 &&
                           readable(mb, (size_t)(me_end - mb) * sizeof(void*)))
                              ? (int)(me_end - mb)
                              : 0;
                for (int i = 0; i < cnt; i++) {
                    void* m = mb[i];
                    if (m && game::listed(m, 0x18EB45Eu) && game::impressed(m))
                        game::set_impressed(m, false);
                }
            }
        }
    }
}

static uint8_t puppet_player_owner(void* obj)
{
    if (!obj || !C.sess || !C.puppet_of)
        return 255;
    uint8_t me = my_id();
    for (const auto& p : C.sess->players()) {
        if (p.id != me && C.puppet_of(p.id) == obj)
            return p.id;
    }
    return 255;
}

static int SK_CDECL h_CursorKind(void* obj)
{
    int native = o_CursorKind(obj);
    if (!g_pvp_symmetry_fix || !connected() || !game::in_creature_stage() || !game::on_main() || !obj)
        return native;
    if (!game::listed(obj, 0x18EB45Eu) || game::is_dead(obj) || obj == game::avatar())
        return native;

    bool crivals = active();
    uint8_t me = my_id();
    uint8_t target_owner = 255;
    TargetKind kind = TargetKind::WildNpc;

    uint8_t pup_owner = puppet_player_owner(obj);
    if (pup_owner != 255) {
        target_owner = pup_owner;
        kind = TargetKind::PlayerAvatar;
    } else if (crivals) {
        uint8_t fam = creature_family_owner(obj);
        if (fam != 255) {
            target_owner = fam;
            kind = TargetKind::PlayerFamily;
        }
    }
    if (target_owner == 255)
        return native;

    bool combat_stance = a_AbilityMode && cdecl_call<int>(a_AbilityMode) == 1;
    bool ff = settings::on(settings::kFriendlyFire);
    bool at_war = crivals && rivals::at_war(me, target_owner);
    int ov = evaluate_pvp_cursor_override(crivals, ff, me, target_owner, kind, at_war, combat_stance, native);
    if (ov >= 0 && ov != native) {
        g_cursor_overrides++;
        return ov;
    }
    return native;
}

static int SK_THISCALL h_SocialMode(void* c)
{
    if (g_pvp_symmetry_fix && active() && game::in_creature_stage() && game::on_main() && c && c != game::avatar()) {
        uint8_t me = my_id();
        uint8_t owner = creature_player_owner(c);
        if (owner != 255 && owner != me) {
            if (rivals::at_war(me, owner) || settings::on(settings::kFriendlyFire))
                return 0; // No social game with a hostile rival or rival's family
        }
    }
    return o_SocialMode(c);
}

// ---------------- Game Hooks: GetRelationship (R4, chained from tribe.cpp) & SpeciesRel_MakeExtinct (R7) ----------------
bool override_relationship(void* a, void* b, int& rel_out)
{
    if (g_in_rel_hook || !active() || !game::on_main())
        return false;
    callguard::Hold hold(g_in_rel_hook);

    // Only substitute game::avatar() when exactly one creature is null and the non-null side is not our own avatar;
    // never substitute for (nullptr, nullptr) species-only lookups (e.g. egg-steal cursor / friendly-nest check).
    void* av = g_pvp_symmetry_fix ? game::avatar() : nullptr;
    bool substituted = false;
    void* ea = a;
    void* eb = b;
    if (av && (a == nullptr) != (b == nullptr)) {
        if (!ea && eb != av) {
            ea = av;
            substituted = true;
        } else if (!eb && ea != av) {
            eb = av;
            substituted = true;
        }
    }
    if (ea && eb && readable(ea, game::kHerdOff + 4) && readable(eb, game::kHerdOff + 4)) {
        uint8_t pa = creature_player_owner(ea);
        uint8_t pb = creature_player_owner(eb);
        if (pa != 255 && pb != 255 && (!substituted || pa != pb)) {
            bool ff = g_pvp_symmetry_fix && settings::on(settings::kFriendlyFire);
            rel_out = resolve_player_pair_relation(pa, pb, rivals::at_war(pa, pb), rivals::allied(pa, pb), ff);
            return true;
        }
        // Host AI: wild NPC evaluating relation against a guest's puppet uses that guest's own species table!
        if (is_host() && (pa != 255) != (pb != 255)) {
            uint8_t guest_pid = pa != 255 ? pa : pb;
            void* wild_c = pa != 255 ? eb : ea;
            if (guest_pid >= 1 && rivals::valid_player(guest_pid)) {
                uint32_t sp_inst = game::species_instance(wild_c);
                int32_t rel = g_tables.get_relation(guest_pid, sp_inst);
                if (rel != kRelNone) {
                    rel_out = rel;
                    return true;
                }
            }
        }
    }
    return false;
}

static void SK_CDECL h_MakeExtinct(void* creature)
{
    uint32_t key[3] = {0, 0, 0};
    bool have_key = false;
    if (active() && creature && readable(creature, game::kSpeciesKeyOff + 12)) {
        key[0] = field<uint32_t>(creature, game::kSpeciesKeyOff);
        key[1] = field<uint32_t>(creature, game::kSpeciesKeyOff + 4);
        key[2] = field<uint32_t>(creature, game::kSpeciesKeyOff + 8);
        have_key = (key[0] != 0);
        if (have_key && block_extinction(true, is_player_home_species(key[0]))) {
            g_home_extinct_blocked++;
            if (H && M)
                H->log(M, SK_INFO, "creature_rivals: blocked extinction of player home species %08X (R7)", key[0]);
            return;
        }
    }
    o_MakeExtinct(creature);
    if (is_host() && active() && have_key && !is_player_home_species(key[0]) && !g_tables.is_extinct(key[0])) {
        g_tables.mark_extinct(key);
        for (const auto& p : C.sess->players())
            if (p.id != 0)
                host_send_rels_to(p.id);
    }
}

// ---------------- Rivals Callbacks (Influence, Draft, Stage End, F9 UI) ----------------
static bool on_npc_influence(rivals::Stage stage, rivals::InfluenceAction action, uint8_t actor, uint32_t npc_id,
                             uint8_t rival_id)
{
    if (stage != rivals::Stage::Creature || !is_host() || !active())
        return false;
    if (npc_id == 0 || is_player_home_species(npc_id) || g_tables.is_extinct(npc_id))
        return false;
    uint32_t key[3] = {};
    if (!lookup_wild_key(npc_id, key))
        return false;

    if (action == rivals::InfluenceAction::Protect) {
        if (!rivals::valid_player(actor))
            return false;
        host_sync_native_to_player0();
        if (!is_allied_rel(g_tables.get_relation(actor, npc_id)))
            return false;
        g_inf_actions++;
        return true;
    }
    if (action == rivals::InfluenceAction::TurnAgainst) {
        if (!rivals::valid_player(rival_id) || rival_id == actor)
            return false;
        host_sync_native_to_player0();
        if (!g_tables.apply_turn_against(rival_id, key))
            return false;
        g_inf_actions++;
        if (rival_id == 0)
            host_write_player0_entry(key[0]);
        else
            host_send_rels_to(rival_id);
        return true;
    }
    if (action == rivals::InfluenceAction::SmoothOver) {
        if (!rivals::valid_player(actor))
            return false;
        host_sync_native_to_player0();
        if (!g_tables.apply_smooth_over(actor, key))
            return false;
        g_inf_actions++;
        if (actor == 0)
            host_write_player0_entry(key[0]);
        else
            host_send_rels_to(actor);
        return true;
    }
    return false;
}

static bool on_buy_influence(uint8_t actor, int32_t price, int32_t /*gain*/)
{
    if (!is_host() || !active() || price <= 0)
        return false;
    if (actor == 0) {
        if (game::dna() + 0.01f < (float)price)
            return false;
        game::dna_add_raw(-(float)price);
        g_inf_buys++;
        return true;
    }
    if (!rivals::valid_player(actor) || !g_guest_prog[actor].known)
        return false;
    if (g_guest_prog[actor].dna + 0.01f < (float)price)
        return false;
    g_guest_prog[actor].dna -= (float)price;
    net::Writer w;
    w.f32((float)price);
    rivals::send_faction_to(actor, rivals::Stage::Creature, kSkChargeDna, w);
    g_inf_buys++;
    return true;
}

static void on_stage_race_end(uint8_t winner)
{
    if (!is_host() || !active() || !rivals::valid_player(winner))
        return;
    g_race_end_handled = true;
    std::vector<PartKey> winner_parts;
    std::set<PartKey> baseline;
    if (winner == 0) {
        int u = 0, a = 0;
        uint32_t h = 0;
        parts::census(u, h, a, nullptr, &winner_parts);
        // Baseline = union of guests' known parts (prefer parts guests lack)
        for (int p = 1; p < rivals::kMaxPlayers; p++)
            for (const auto& k : g_guest_prog[p].parts)
                baseline.insert(k);
    } else {
        winner_parts = g_guest_prog[winner].parts;
        std::vector<PartKey> host_parts;
        int u = 0, a = 0;
        uint32_t h = 0;
        if (parts::census(u, h, a, nullptr, &host_parts))
            baseline.insert(host_parts.begin(), host_parts.end());
    }
    std::vector<rivals::Draft::Item> items;
    build_creature_draft_items(winner_parts, baseline, items, g_draft_keys);
    if (items.empty()) {
        // Fallback placeholder if winner had 0 queryable parts
        items.push_back({1, 1, "Creature Part Bonus"});
        g_draft_keys.clear();
        g_draft_keys.push_back({0, 0});
    }
    if (rivals::start_draft(winner, items)) {
        if (H && M)
            H->log(M, SK_INFO, "creature_rivals: started Creature part draft for winner %u (%zu items)",
                   (unsigned)winner, items.size());
    }
}

static void on_draft_pick(uint8_t actor, uint32_t item_id, uint8_t /*kind*/, const std::string& /*label*/)
{
    if (!is_host() || !active() || item_id == 0 || item_id > g_draft_keys.size())
        return;
    PartKey pk = g_draft_keys[item_id - 1];
    if (pk.first == 0 && pk.second == 0)
        return;
    if (actor == 0) {
        if (parts::unlock_key(pk))
            g_draft_unlocked++;
    } else if (rivals::valid_player(actor)) {
        DraftApplyMsg dm{kDraftOpUnlockPart, pk.first, pk.second, 0};
        net::Writer w;
        write_draft_apply(w, dm);
        rivals::send_faction_to(actor, rivals::Stage::Creature, kSkDraftApply, w);
    }
}

static std::vector<rivals::UiRow> on_extra_ui_rows()
{
    std::vector<rivals::UiRow> rows;
    if (!active())
        return rows;
    uint32_t kGold = 0xFFF2D479u, kCyan = 0xFF7BE8F2u;
    uint8_t me = my_id();
    net::Vec3 my_pos{};
    bool have_my_pos = false;
    if (void* av = game::avatar()) {
        my_pos = game::position(av);
        have_my_pos = finite3(my_pos.x, my_pos.y, my_pos.z);
    } else if (g_nests[me].assigned) {
        my_pos = g_nests[me].nest_pos;
        have_my_pos = finite3(my_pos.x, my_pos.y, my_pos.z);
    }
    const auto& race = rivals::race_state();
    rows.push_back({ru() ? "Соперники: Существо (гнёзда и виды)" : "Creature Rivals (nests & wild species)", "",
                    nullptr, kGold});
    for (uint8_t p = 0; p < rivals::kMaxPlayers; p++) {
        if (!g_nests[p].assigned)
            continue;
        std::string nm = C.sess ? C.sess->name_of(p) : ("P" + std::to_string(p));
        std::string sp_name = lookup_species_name(g_nests[p].species_key[0]);
        float dist_m = have_my_pos ? net::dist(my_pos, g_nests[p].nest_pos) : -1.0f;
        int brain_lv = -1;
        if (p == me && game::avatar())
            brain_lv = std::max(0, game::brain_level());
        else if (race.players[p].present)
            brain_lv = (int)std::lround(race.players[p].primary);
        std::string text = format_player_nest_row(nm, p == me, sp_name, dist_m, brain_lv, ru());
        rows.push_back({std::move(text), "", nullptr, p == me ? kCyan : 0});
    }
    return rows;
}

static std::vector<rivals::NpcUiEntry> on_npc_ui_list(uint8_t viewer)
{
    std::vector<rivals::NpcUiEntry> out;
    if (!active() || !rivals::valid_player(viewer))
        return out;
    if (is_host())
        host_sync_native_to_player0();
    std::vector<uint32_t> discovered;
    discovered.reserve(g_known_wild_keys.size());
    std::map<uint32_t, std::string> names;
    for (const auto& [inst, arr] : g_known_wild_keys) {
        (void)arr;
        discovered.push_back(inst);
        if (std::string nm = lookup_species_name(inst); !nm.empty())
            names[inst] = std::move(nm);
    }
    auto rows = build_wild_npc_ui_list(g_tables, viewer, discovered, names.empty() ? nullptr : &names, ru());
    out.reserve(rows.size());
    for (auto& r : rows) {
        rivals::NpcUiEntry e{};
        e.npc_id = r.npc_id;
        e.label = std::move(r.label);
        e.allied = r.allied;
        e.hostile = r.hostile;
        e.can_turn = r.can_turn;
        e.can_protect = r.can_protect;
        e.can_smooth = r.can_smooth;
        out.push_back(std::move(e));
    }
    return out;
}

// ---------------- MSG_RIVALS_FACTION (Stage::Creature) Handler ----------------
static void on_faction_msg(uint8_t from, uint8_t subkind, net::Reader& r)
{
    if (!g_creature_rivals_fix)
        return;
    if (subkind == kSkAssign && is_guest() && from == 0) {
        std::vector<AssignEntry> entries;
        if (!read_assign(r, entries))
            return;
        g_assigns_rx++;
        for (const auto& e : entries) {
            bool moved = !g_nests[e.player_id].assigned ||
                         net::dist(g_nests[e.player_id].nest_pos, {e.nest_x, e.nest_y, e.nest_z}) > 5.0f;
            g_nests[e.player_id].assigned = (e.flags & 1u) != 0;
            g_nests[e.player_id].species_swapped = (e.flags & 2u) != 0;
            g_nests[e.player_id].nest_pos = {e.nest_x, e.nest_y, e.nest_z};
            g_nests[e.player_id].species_key[0] = e.species_key[0];
            g_nests[e.player_id].species_key[1] = e.species_key[1];
            g_nests[e.player_id].species_key[2] = e.species_key[2];
            g_tables.set_home_species(e.player_id, e.species_key[0]);
            if (moved) {
                g_nests[e.player_id].herd = {};
                g_nests[e.player_id].perspective_applied = false;
            }
        }
        guest_apply_perspective();
        return;
    }
    if (subkind == kSkNpcRels && is_guest() && from == 0) {
        std::vector<SpeciesEntry> entries;
        if (!read_npc_rels(r, entries))
            return;
        g_rels_rx++;
        g_guest_last_table = entries;
        uint8_t me = my_id();
        if (rivals::valid_player(me)) {
            g_tables.by_player[me].clear();
            for (const auto& e : entries) {
                if (e.key[0] == 0)
                    continue;
                if (e.extinct)
                    g_tables.extinct_species.insert(e.key[0]);
                g_tables.by_player[me][e.key[0]] = e;
                if (!is_player_home_species(e.key[0]))
                    g_known_wild_keys[e.key[0]] = {e.key[0], e.key[1], e.key[2]};
            }
        }
        std::vector<social::SpeciesRec> conv;
        conv.reserve(entries.size());
        for (const auto& e : entries)
            conv.push_back({{e.key[0], e.key[1], e.key[2]}, e.relation, e.x, e.impressed, e.killed});
        social::apply_external_table(conv);
        return;
    }
    if (subkind == kSkProgress && is_host() && from != 0 && rivals::valid_player(from)) {
        ProgressMsg pm;
        if (!read_progress(r, pm))
            return;
        g_guest_prog[from].known = true;
        g_guest_prog[from].last_at = H ? H->time_seconds() : 0.0;
        g_guest_prog[from].brain_level = pm.brain_level;
        g_guest_prog[from].dna = pm.dna;
        g_guest_prog[from].evo_bar = pm.evo_bar;
        g_guest_prog[from].parts_total = pm.parts_total;
        g_guest_prog[from].parts = std::move(pm.parts);
        return;
    }
    if (subkind == kSkDraftApply && is_guest() && from == 0) {
        DraftApplyMsg dm;
        if (!read_draft_apply(r, dm))
            return;
        if (dm.op == kDraftOpUnlockPart && (dm.lo != 0 || dm.hi != 0)) {
            if (parts::unlock_key({dm.lo, dm.hi})) {
                g_draft_unlocked++;
                if (C.feed)
                    C.feed(ru() ? "Скопированная часть драфта разблокирована!" : "Copied draft creature part unlocked!");
            }
        } else if (dm.op == kDraftOpPayoutDna && dm.amount > 0) {
            game::dna_add_raw((float)dm.amount);
            g_draft_payouts++;
        }
        return;
    }
    if (subkind == kSkChargeDna && is_guest() && from == 0) {
        float cost = r.f32();
        if (r.ok() && std::isfinite(cost) && cost > 0.0f && cost <= 100000.0f) {
            game::dna_add_raw(-cost);
            g_inf_buys++;
        }
        return;
    }
}

// ---------------- R3 Native Creature -> Tribe Transition ----------------
static void native_transition_task(void*)
{
    if (!game::in_creature_stage() || !game::avatar())
        return;
    void* st = a_StrategyGet ? cdecl_call<void*>(a_StrategyGet) : nullptr;
    if (!st || !readable((uint8_t*)st + 0xE0, 4))
        return;
    // Verified sequence from Timeline Advance button (0x00D2C703, button 0x06455B55)
    field<uint8_t>(st, 0xDE) = 1;
    void* av = game::avatar();
    if (av)
        game::set_health(av, game::max_health(av));
    if (a_ConsequenceTrait) {
        uint32_t trait = cdecl_call<uint32_t>(a_ConsequenceTrait);
        if (void* nm = game::noun_manager(); nm && readable((uint8_t*)nm + 0x74, 4)) {
            void* pl = field<void*>(nm, 0x74);
            if (pl && readable((uint8_t*)pl + 0x1100, 4)) {
                field<uint32_t>(pl, 0x1100) = trait;
                history::host_sync_now(); // BUG-1557: decide guests' own Creature cards before Tribe loads
            }
        }
    }
    if (a_StageEndAdvance)
        cdecl_call<void>(a_StageEndAdvance);
    if (a_MsgMgrGet) {
        if (void* mm = cdecl_call<void*>(a_MsgMgrGet)) {
            struct {
                uint32_t a, b, c, d, e;
            } msg{0, 1, 0, 0, 0};
            vcall<void>(mm, 0x14 / 4, 0x05120262u, (void*)&msg, (void*)nullptr);
        }
    }
    g_transitions_triggered++;
    if (H && M)
        H->log(M, SK_INFO, "creature_rivals: triggered native Creature -> Tribe stage-end transition (R3)");
}

bool trigger_native_tribe_transition()
{
    if (!g_creature_rivals_fix || !is_host() || !game::in_creature_stage() || !game::avatar())
        return false;
    H->run_outside_frame(native_transition_task, nullptr);
    return true;
}

// ---------------- Frame Tick ----------------
void tick(double now)
{
    if (!g_creature_rivals_fix || !connected())
        return;
    if (!active()) {
        if (g_last_minimap && !g_map_queued && now >= g_next_map_sync) {
            g_next_map_sync = now + 1.0;
            g_map_queued = true;
            H->run_outside_frame(minimap_task, nullptr);
        }
        return;
    }

    uint32_t epoch = game::world_epoch();
    if (epoch != g_last_world_epoch) {
        g_last_world_epoch = epoch;
        g_next_assign_scan = 0.0;
        for (int i = 0; i < rivals::kMaxPlayers; i++) {
            g_nests[i].herd = {};
            g_nests[i].perspective_applied = false;
        }
    }

    // Minimap rival nest blips (1 Hz outside the frame)
    if (!g_map_queued && now >= g_next_map_sync) {
        g_next_map_sync = now + 1.0;
        g_map_queued = true;
        H->run_outside_frame(minimap_task, nullptr);
    }

    // Symmetric War / Friendly Fire hostility sync on both host and guest (1 Hz, BUG-1583)
    if (now >= g_next_hostility_sync) {
        g_next_hostility_sync = now + 1.0;
        sync_rival_hostility();
    }

    if (is_host()) {
        // R2: Assign nests & swap herd species (throttled: scan immediately on distance change or first unassigned,
        // otherwise >= 1.0 s cooldown; lightweight maintenance when all connected players already have live nests).
        bool dist_changed = (g_last_assigned_dist != rivals::base_distance());
        if (dist_changed || now >= g_next_assign_scan) {
            bool any_unassigned = dist_changed || host_has_unassigned_player();
            if (any_unassigned || now >= g_next_assign_scan) {
                host_assign_nests(dist_changed);
                g_next_assign_scan = now + 1.0;
            }
        }
        if (now >= g_next_assign_tx) {
            g_next_assign_tx = now + 5.0;
            host_broadcast_assignments();
        }

        // R4: Sync host native species table & push per-player tables to guests (every 1.0 s)
        if (now >= g_next_rel_sync) {
            g_next_rel_sync = now + 1.0;
            refresh_known_wild_species();
            host_sync_native_to_player0();
            for (const auto& p : C.sess->players())
                if (p.id != 0)
                    host_send_rels_to(p.id);
        }

        // R3: Update Race progress & check brain-level win condition (1 Hz)
        if (now >= g_next_prog_tx) {
            g_next_prog_tx = now + 1.0;
            // Host (player 0)
            int h_brain = std::max(0, game::brain_level());
            rivals::PlayerProgress hp{};
            hp.player_id = 0;
            hp.present = true;
            hp.primary = (float)h_brain;
            hp.secondary = game::dna();
            g_tables.count_stats(0, hp.stat_a, hp.stat_b);
            hp.extra = (float)kMaxBrainLevel;
            rivals::set_player_progress(0, hp);
            if (reached_tribe_threshold(h_brain) && rivals::race_state().players[0].finish_rank == 0) {
                if (rivals::report_stage_complete(0) > 0)
                    g_wins_detected++;
            }
            // Guests
            for (const auto& p : C.sess->players()) {
                if (p.id == 0 || !rivals::valid_player(p.id) || !g_guest_prog[p.id].known)
                    continue;
                const auto& gp = g_guest_prog[p.id];
                rivals::PlayerProgress pp{};
                pp.player_id = p.id;
                pp.present = true;
                pp.primary = (float)gp.brain_level;
                pp.secondary = gp.dna;
                g_tables.count_stats(p.id, pp.stat_a, pp.stat_b);
                pp.extra = (float)kMaxBrainLevel;
                rivals::set_player_progress(p.id, pp);
                if (reached_tribe_threshold((int)gp.brain_level) && rivals::race_state().players[p.id].finish_rank == 0) {
                    if (rivals::report_stage_complete(p.id) > 0)
                        g_wins_detected++;
                }
            }
        }

        // R3: Automatic Creature -> Tribe transition once catch-up and draft have both finished
        const auto& race = rivals::race_state();
        const auto& draft = rivals::draft_state();
        if (race.winner == 255 || race.stage != rivals::Stage::Creature) {
            g_race_end_handled = false;
            g_auto_transition_sent = false;
            g_transition_ready_at = 0.0;
        } else if (g_race_end_handled && race.stage_finished && !race.catchup_active && !draft.active &&
                   !g_auto_transition_sent) {
            if (g_transition_ready_at <= 0.0)
                g_transition_ready_at = now;
            if (should_auto_trigger_tribe_transition(true, true, race.stage_finished, race.catchup_active, draft.active,
                                                     race.winner, g_race_end_handled, g_auto_transition_sent,
                                                     game::cinematic_playing(), g_transition_ready_at, now)) {
                if (trigger_native_tribe_transition())
                    g_auto_transition_sent = true;
            }
        } else {
            g_transition_ready_at = 0.0;
        }
    } else if (is_guest()) {
        // Guard against auto-spawning as a Civ-armored / outfitted creature at brain level 0 (BUG-1583)
        guest_guard_starter_species();

        // R1: Ensure guest perspective is applied once avatar & assignment exist
        if (!g_nests[my_id()].perspective_applied && g_nests[my_id()].assigned)
            guest_apply_perspective();

        // Discover wild species on guest planet for F9 Influence UI (1 Hz)
        if (now >= g_next_rel_sync) {
            g_next_rel_sync = now + 1.0;
            refresh_known_wild_species();
        }

        // R3: Send local brain level, own DNA, and collected parts to host (1 Hz)
        if (now >= g_next_prog_tx && game::avatar()) {
            g_next_prog_tx = now + 1.0;
            ProgressMsg pm{};
            pm.brain_level = (uint8_t)std::clamp(game::brain_level(), 0, 10);
            pm.dna = game::dna();
            float bar = game::evolution_bar();
            pm.evo_bar = std::isfinite(bar) ? bar : 0.0f;
            int u = 0, a = 0;
            uint32_t h = 0;
            std::vector<PartKey> mine;
            if (parts::census(u, h, a, nullptr, &mine)) {
                pm.parts_total = (uint16_t)std::min(u, 65535);
                std::sort(mine.begin(), mine.end());
                if (mine.size() > kMaxReportedParts)
                    mine.resize(kMaxReportedParts);
                pm.parts = std::move(mine);
            }
            net::Writer w;
            write_progress(w, pm);
            rivals::send_faction_to(0, rivals::Stage::Creature, kSkProgress, w);
        }
    }
}

// ---------------- Diagnostics & Console Command: coop.crivals ----------------
std::string status_summary()
{
    char b[512];
    uint8_t me = my_id();
    void* av = game::avatar();
    void* ah = game::avatar_herd();
    net::Vec3 ah_pos{};
    bool has_ah = ah && herd_nest_pos(ah, ah_pos);
    snprintf(b, sizeof b,
             "crivals: fix=%d active=%d role=%s me=%u dist=%s assign_tx=%u assign_rx=%u rel_tx=%u rel_rx=%u "
             "persp_swaps=%u herd_swaps=%u fam_spawns=%u starter_guards=%u cursor_ov=%u inf_earned=%u inf_acts=%u inf_buys=%u pvp_allow=%u pvp_block=%u "
             "home_ext_block=%u wins=%u draft_unlock=%u draft_pay=%u trans=%u\n"
             "own_nest: assigned=%d swapped=%d persp=%d target=(%.1f %.1f %.1f) avatar_herd=%p avatar_nest=(%.1f %.1f %.1f) "
             "brain=%d dna=%.0f\n",
             (int)g_creature_rivals_fix, (int)active(), is_host() ? "host" : (is_guest() ? "guest" : "none"),
             (unsigned)me, rivals::distance_name(rivals::base_distance()), g_assigns_tx, g_assigns_rx, g_rels_tx,
             g_rels_rx, g_persp_swaps, g_herd_swaps, g_family_spawns, g_starter_guards, g_cursor_overrides,
             g_inf_earned, g_inf_actions, g_inf_buys, g_pvp_allowed, g_pvp_blocked, g_home_extinct_blocked,
             g_wins_detected, g_draft_unlocked, g_draft_payouts, g_transitions_triggered, (int)g_nests[me].assigned,
             (int)g_nests[me].species_swapped, (int)g_nests[me].perspective_applied, g_nests[me].nest_pos.x,
             g_nests[me].nest_pos.y, g_nests[me].nest_pos.z, ah, has_ah ? ah_pos.x : 0.f, has_ah ? ah_pos.y : 0.f,
             has_ah ? ah_pos.z : 0.f, av ? game::brain_level() : -1, av ? game::dna() : 0.f);
    return b;
}

int command(void*, int argc, const char* const* argv, SkOut* out)
{
    auto say = [&](const std::string& s) {
        if (H && out)
            H->out_printf(out, "%s", s.c_str());
    };
    if (!g_creature_rivals_fix) {
        say("crivals: disabled (rivals.creature is off)\n");
        return 0;
    }
    std::string sub = argc >= 2 ? argv[1] : "status";
    if (sub == "status") {
        say(status_summary());
        return 0;
    }
    if (sub == "nests") {
        char b[256];
        net::Vec3 h0 = g_nests[0].nest_pos;
        for (uint8_t p = 0; p < rivals::kMaxPlayers; p++) {
            if (!g_nests[p].assigned)
                continue;
            void* h = game::live(g_nests[p].herd);
            int mcnt = 0;
            int extinct = 0;
            int av_own = 0;
            if (h && readable(h, 0x220)) {
                void** mb = field<void**>(h, 0x40);
                void** me = field<void**>(h, 0x44);
                mcnt = (mb && me >= mb && (me - mb) <= 64) ? (int)(me - mb) : 0;
                extinct = field<uint8_t>(h, 0x190);
                av_own = field<uint8_t>(h, 0x84);
            }
            snprintf(b, sizeof b,
                     "nest p=%u pos=(%.1f %.1f %.1f) dist_from_host=%.1f sp=%08X orig=%08X swapped=%d herd=%p members=%d av_own=%d extinct=%d\n",
                     (unsigned)p, g_nests[p].nest_pos.x, g_nests[p].nest_pos.y, g_nests[p].nest_pos.z,
                     net::dist(g_nests[p].nest_pos, h0), g_nests[p].species_key[0], g_nests[p].orig_species_inst,
                     (int)g_nests[p].species_swapped, h, mcnt, av_own, extinct);
            say(b);
        }
        return 0;
    }
    if (sub == "rels") {
        uint8_t pid = argc >= 3 ? (uint8_t)atoi(argv[2]) : my_id();
        char b[160];
        if (is_host() && rivals::valid_player(pid)) {
            host_sync_native_to_player0();
            auto list = g_tables.entries_for(pid);
            snprintf(b, sizeof b, "crivals rels p=%u count=%zu extinct_global=%zu\n", (unsigned)pid, list.size(),
                     g_tables.extinct_species.size());
            say(b);
            for (const auto& e : list) {
                snprintf(b, sizeof b, "  sp=%08X rel=%d imp=%u kill=%u ext=%d\n", e.key[0], (int)e.relation, e.impressed,
                         e.killed, (int)e.extinct);
                say(b);
            }
        } else {
            auto native = social::read_native_table();
            snprintf(b, sizeof b, "crivals rels local count=%zu rx_count=%zu\n", native.size(),
                     g_guest_last_table.size());
            say(b);
            for (const auto& r : native) {
                snprintf(b, sizeof b, "  sp=%08X rel=%d imp=%u kill=%u ext=%d\n", r.key[0], (int)r.relation, r.impressed,
                         r.killed, (int)g_tables.is_extinct(r.key[0]));
                say(b);
            }
        }
        return 0;
    }
    if (sub == "reassign" && is_host()) {
        host_assign_nests(true);
        say("reassigned\n");
        return 0;
    }
    if (sub == "impress" && argc >= 4 && is_host()) {
        uint8_t pid = (uint8_t)atoi(argv[2]);
        uint32_t inst = (uint32_t)strtoul(argv[3], nullptr, 16);
        uint32_t k[3] = {};
        lookup_wild_key(inst, k);
        if (pid == 0) {
            auto res = g_tables.record_impress(0, k);
            if (res.changed) {
                host_write_player0_entry(k[0]);
                on_host_impress(k, res.became_allied);
            }
            say(res.changed ? "ok\n" : "no change\n");
        } else {
            bool ok = on_guest_impress(pid, k, nullptr, 10.0f);
            say(ok ? "ok\n" : "error\n");
        }
        return 0;
    }
    if (sub == "kill" && argc >= 4 && is_host()) {
        uint8_t pid = (uint8_t)atoi(argv[2]);
        uint32_t inst = (uint32_t)strtoul(argv[3], nullptr, 16);
        uint32_t k[3] = {};
        lookup_wild_key(inst, k);
        auto ko = g_tables.record_kill(pid, k);
        if (ko.blocked_home) {
            g_home_extinct_blocked++;
            say("blocked: home species protected\n");
            return 0;
        }
        if (pid == 0)
            host_write_player0_entry(k[0]);
        else
            host_send_rels_to(pid);
        if (ko.became_extinct) {
            host_write_player0_entry(k[0]);
            for (const auto& p : C.sess->players())
                if (p.id != 0)
                    host_send_rels_to(p.id);
            say("extinct (shared)\n");
        } else {
            say(ko.changed ? "ok\n" : "no change\n");
        }
        return 0;
    }
    if (sub == "brain" && argc >= 3) {
        int lv = atoi(argv[2]);
        bool ok = game::set_brain_level(lv);
        say(ok ? ("brain=" + std::to_string(game::brain_level()) + "\n") : "error\n");
        return 0;
    }
    if (sub == "dna" && argc >= 3) {
        float v = (float)atof(argv[2]);
        game::dna_set_raw(v);
        say("dna=" + std::to_string((int)game::dna()) + "\n");
        return 0;
    }
    if (sub == "familyhit" && argc >= 4 && is_host()) {
        uint8_t target_pid = (uint8_t)atoi(argv[2]);
        float dmg = (float)atof(argv[3]);
        if (!rivals::valid_player(target_pid) || !g_nests[target_pid].assigned) {
            say("error: no assigned nest\n");
            return 0;
        }
        void* herd = game::live(g_nests[target_pid].herd);
        void* member = nullptr;
        if (herd && readable(herd, 0x220)) {
            void** mb = field<void**>(herd, 0x40);
            void** me = field<void**>(herd, 0x44);
            int cnt = (mb && me > mb && (me - mb) <= 64 && readable(mb, (size_t)(me - mb) * sizeof(void*)))
                          ? (int)(me - mb)
                          : 0;
            for (int i = 0; i < cnt; i++) {
                if (mb[i] && game::listed(mb[i], 0x18EB45Eu) && !game::is_dead(mb[i]) && mb[i] != game::avatar()) {
                    member = mb[i];
                    break;
                }
            }
        }
        if (!member) {
            say("error: no live family member\n");
            return 0;
        }
        float hp0 = game::health(member);
        game::hit(member, dmg, 2, game::avatar());
        float hp1 = game::health(member);
        char b[128];
        snprintf(b, sizeof b, "familyhit p=%u member=%p hp %.1f -> %.1f\n", (unsigned)target_pid, member, hp0, hp1);
        say(b);
        return 0;
    }
    if (sub == "transition" && is_host()) {
        say(trigger_native_tribe_transition() ? "transition queued\n" : "error\n");
        return 0;
    }
    say("usage: coop.crivals [status | nests | rels [pid] | reassign | impress <pid> <hex> | kill <pid> <hex> | "
        "brain <0..4> | dna <val> | familyhit <pid> <dmg> | transition]\n");
    return 0;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    reset_state();

    a_JoinNestHerd = H->sym("SporeCoop::CreatureGame_JoinNestHerd");
    a_HerdNestPos = H->sym("SporeCoop::Herd_NestPosition");
    a_HerdSetEnabled = H->sym("Simulator::cHerd::SetEnabled");
    a_HerdSetBestFriends = H->sym("SporeCoop::cHerd_SetBestFriends");
    a_CreatureSetSpecies = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    a_CreatureSetSpeciesProfile = H->sym("SporeCoop::CreatureBase_SetSpeciesProfile");
    a_MakeExtinct = H->sym("SporeCoop::SpeciesRel_MakeExtinct");
    a_StrategyGet = H->sym("App::cCreatureModeStrategy::Get");
    a_StageEndAdvance = H->sym("SporeCoop::Creature_StageEndAdvance");
    a_ConsequenceTrait = H->sym("SporeCoop::Creature_GetConsequenceTrait");
    a_MsgMgrGet = H->sym("App::IMessageManager::Get");
    a_CreatureMinimapPtr = H->sym("SporeCoop::CreatureMinimap_Ptr");
    a_MinimapAdd = H->sym("SporeCoop::Minimap_AddBlip");
    a_MinimapMove = H->sym("SporeCoop::Minimap_MoveBlip");
    a_MinimapFind = H->sym("SporeCoop::Minimap_FindBlip");
    a_MinimapRemove = H->sym("SporeCoop::Minimap_RemoveBlip");
    a_AbilityMode = H->sym("Simulator::cCreatureGameData::GetAbilityMode");
    a_CursorKind = H->sym("SporeCoop::CreatureCursorKind");
    a_SocialMode = H->sym("SporeCoop::CreatureSocialMode");

    if (a_MakeExtinct)
        H->hook_create(M, (void*)a_MakeExtinct, (void*)h_MakeExtinct, (void**)&o_MakeExtinct);
    if (a_CursorKind)
        H->hook_create(M, (void*)a_CursorKind, (void*)h_CursorKind, (void**)&o_CursorKind);
    if (a_SocialMode)
        H->hook_create(M, (void*)a_SocialMode, (void*)h_SocialMode, (void**)&o_SocialMode);

    rivals::register_faction_handler(rivals::Stage::Creature, on_faction_msg);
    rivals::register_npc_influence_callback(rivals::Stage::Creature, on_npc_influence);
    rivals::StageHooks hk;
    hk.on_buy_influence = on_buy_influence;
    hk.on_draft_pick = on_draft_pick;
    hk.on_stage_race_end = on_stage_race_end;
    hk.on_extra_ui_rows = on_extra_ui_rows;
    hk.on_npc_ui_list = on_npc_ui_list;
    rivals::register_stage_hooks(rivals::Stage::Creature, std::move(hk));

    H->command_add(M, "coop.crivals",
                   "coop.crivals [status | nests | rels [pid] | reassign | impress <pid> <hex> | kill <pid> <hex> | "
                   "brain <0..4> | dna <val> | familyhit <pid> <dmg> | transition] - Creature Rivals diagnostics and controls (BUG-1547)",
                   command, nullptr, SK_CMD_MAIN_THREAD);
    H->log(M, SK_INFO, "creature_rivals: initialized (rivals.creature=%d)", (int)g_creature_rivals_fix);
}

} // namespace creature_rivals
