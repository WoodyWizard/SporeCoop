#include "npc.h"
#include "view_centres.h"
#include "modes.h"
#include "tribe.h"
#include "teamplay.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>

#include "sporekit_game.hpp"

#include "creations.h"
#include "creature_hp.h"
#include "social.h"
#include "driver.h"
#include "game.h"
#include "net/validate.h"
#include "npc_ids.h"
#include "world.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "fields.h"
#include "gamefixes.h"
#include "dna_ledger.h"
#include "trace.h"

namespace npc {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_enabled = true;

static constexpr float kRelevance = 110.f;  // m around a guest
static constexpr float kHysteresis = 20.f;  // keep until kRelevance + this
static constexpr double kStateInterval = 0.1;
static constexpr float kMaxHit = 10000.f; // a guest's reported hit (MSG_DAMAGE_REQ) is clamped to 0..this
static constexpr float kRtsRelevance = 250.f; // tribe stage: m around the player tribe

// NPC creatures are shared in the creature stage and, around the player tribe, in the tribe stage
static bool npc_stage() { return game::in_creature_stage() || game::mode_id() == 0x1654C02; }
static bool rts_stage() { return game::mode_id() == 0x1654C02; }
// BUG-0291: in the Tribe stage the host sent a guest only the animals within kRtsRelevance of the
// player tribe; a guest looking farther away saw none there (its own are removed by the update
// filter). Now also those within kRelevance of the guest's view centre (its pointer,
// view_centres.h); the game keeps them alive there (npc.view_centres_tribe).
// `coop.fix npc.rts_views off`: around the tribe only (0.7.3.x).
static bool g_rts_views_fix = true;
COOP_FIX("npc.rts_views", "BUG-0291", g_rts_views_fix, "tribe: the host sends a guest the animals around the guest's own view too");

// ------------------------------------------------------------------ host side
struct HostNpc {
    uint16_t id;
    double last_seen;
    uint32_t species = 0; // what the object was when it got the id (BUG-0044)
    bool dead = false;
    uint64_t seen_bits = 0; // diagnostics: bit i = updated by the game i frames ago (coop.npc list)
    uint64_t vel_bits = 0;  // diagnostics: bit i = the game's velocity over 0.3 m/s i frames ago
    net::Vec3 pos_hist[64] = {}; // diagnostics: the position of the last 64 frames (ring)
    uint8_t hist_at = 0;
    bool centre_said = false; // BUG-0339 diagnostics: logged once per stay at the planet's centre
    uint32_t bhv_anim = 0;    // BUG-0464: the last behaviour animation its BT started
};
static std::unordered_map<void*, HostNpc> g_host_npcs;  // creature -> id
static std::unordered_map<uint16_t, void*> g_host_by_id;
static std::map<uint8_t, std::set<uint16_t>> g_known;  // guest -> net ids it mirrors
static uint16_t g_next_id = 1;
static double g_last_state = 0;
static double g_frozen_since = 0;                       // host: no creature updates since then
static std::unordered_set<void*> g_seen_now;            // creatures alive this frame
static bool g_reuse_fix = true; // coop.npc reusefix off: a reused creature object keeps its id (BUG-0044)
static bool g_id_fix = true;    // coop.npc idfix off: ids wrap over ids in use (BUG-0023)
// BUG-0169: a proxy that stays more than 2.5 m off for 3 s is put in place whatever the host's
// NPC reports as its speed (coop.npc stallfix off: the old driving)
static bool g_stall_fix = true;
COOP_FIX("npc.reuse", "BUG-0044", g_reuse_fix, "creature: a reused creature object gets a new NPC id");
COOP_FIX("npc.id", "BUG-0023", g_id_fix, "creature: NPC ids never wrap over ids in use");
COOP_FIX("npc.stall", "BUG-0169", g_stall_fix, "creature: a proxy 2.5 m off for 3 s is put in place");
// BUG-0305: proxies of animals that run side by side (a herd fleeing a Tribe hunt) push each other off
// the path on a guest and cannot run faster than the animal: one trailed 2-4 m behind the host's on
// every guest at once. A proxy more than 1.5 m off for 0.5 s without closing in is put in place.
// `coop.fix npc.trail off`: the driving before (0.7.3.x).
// BUG-0281: in the Creature stage a proxy given a MoveTo again and again (the host's NPC runs on) hardly
// walked (own speed 0-3 m/s while the NPC ran at 4) and our pulls towards the path were undone by its
// locomotion (5-10 % kept): running NPCs were 3-6 m off on a 15 fps guest. While the host's NPC moves
// its proxy is now placed on the interpolated host path every frame with the host's velocity (the
// game animates from it); standing, the driving (walk, nudge, stuck/stall/trail) works as before.
// `coop.fix npc.kinematic off`: always the driving.
static bool g_kinematic_fix = true;
static bool g_kinematic_sync_fix = true;
COOP_FIX("npc.kinematic_sync", "BUG-0334", g_kinematic_sync_fix, "creature: a running NPC's proxy gets its health, death and impressed flag too");
COOP_FIX("npc.kinematic", "BUG-0281", g_kinematic_fix, "creature: the proxy of a running NPC follows the host's path exactly");
static bool g_trail_fix = true;
COOP_FIX("npc.trail", "BUG-0305", g_trail_fix, "creature, tribe: a proxy 1.5 m off its path for 0.5 s is put in place");
// BUG-0361: npc.kinematic (BUG-0281) teleported a moving NPC's proxy onto the host's path every frame.
// A teleport (vt 0x44) sets the teleport mark +0xB5D, which restarts the animation's walk gait: the
// proxies of walking and running NPCs slid over the ground in the idle pose (walk gait on in 4 % of
// their moving frames on the guest's screen, coop_npc_walk.py). Now, as for the partner's puppet
// (BUG-0360, creature.puppet_walk), the game walks the proxy (the host NPC's speed, a MoveTo 12 m
// ahead on its path given again only when the direction turns, at most every 0.3 s, at least every
// 1.5 s) and game::place (teleport, then +0xB5D = 0) puts it on the interpolated path every frame;
// an error left from standing is closed at kWalkCatchUp faster than the NPC. The end of a walk is
// a MoveTo to the stop point. `coop.fix npc.walk off`: the teleport of npc.kinematic (0.7.3.3).
static bool g_walk_fix = true;
COOP_FIX("npc.walk", "BUG-0361", g_walk_fix, "creature: a walking or running NPC's proxy walks on its path instead of sliding in the idle pose");
// Tribe: the moving animals' proxies were driven by drive::step (a MoveTo again every 0.1 s, pulls, the
// stall / trail teleports): in 0.1 s windows of a moving host animal they stood in 4 % and moved at its
// speed in only 77 %, with 0.8-2.3 m jumps (coop_npc_walk.py, TRACK_FIXTURE=tribe). They are walked and
// placed as in the Creature stage now, at the guest's own ground height (BUG-0329).
// `coop.fix npc.walk_tribe off`: the driving (0.7.3.3).
// BUG-0394: an NPC of the host can move without reporting a speed (carried by an animation, a hit): its proxy
// was driven as standing. `coop.fix npc.path`: off, the proxy walks only with a reported speed.
static bool g_path_fix = true;
COOP_FIX("npc.path", "BUG-0394", g_path_fix, "creature, tribe: an NPC's proxy also walks where the NPC moves without a speed (moves carried by an animation)");
// tests (BUG-0394): `coop.npctrack novel 1`: the host's NPC states without their speed (as a move carried by an
// animation reports none)
static bool g_npc_novel = false;
// BUG-0399: in Tribe, right after a guest joined, some host animals' proxies (made on the ground) were
// 0.75-1.7 m under the guest's ground for 1-30 s, walked and standing (driven at their own height, BUG-0329;
// how they got there is inferred only, docs/re/tribe.md#tribe-proxy-ground). Their walk is placed onto this
// game's ground now (WalkPlace::ground), drive::step puts a standing one found under the ground onto it, and a
// new proxy is made on the ground (driver.h out_of_ground): no need for the game to ground them.
// `coop.fix npc.proxy_ground off`: at its own height only (0.7.3.4).
COOP_FIX("npc.proxy_ground", "BUG-0399", drive::g_out_of_ground_fix, "tribe: an animal's proxy is never left under this game's ground (made there, kept there standing)");
// BUG-0406: placed exactly on the host's path, a proxy walking downhill is a few cm over the ground after the
// game's next step and starts to fall (+0x137 = 0, the fall animation): 51 fall starts per 100 s of walking on
// a slope of the creature fixture, the host's own NPCs 0.9 (npc_walk_slope). As the partner's puppet
// (BUG-0366, creature.puppet_ground) its walk is placed 5 cm into this game's ground (WalkPlace::ground).
// `coop.fix npc.walk_ground off`: on the host's path exactly.
static bool g_walk_ground_fix = true;
COOP_FIX("npc.walk_ground", "BUG-0406", g_walk_ground_fix, "creature: a walking NPC's proxy is placed onto the ground, so it does not keep starting to fall downhill");
static bool g_walk_tribe_fix = true;
COOP_FIX("npc.walk_tribe", "BUG-0361", g_walk_tribe_fix, "tribe: a walking or running animal's proxy walks on its path instead of moving in steps");
// tests (coop.npctrack variant, drive::walk_place): 0 the fix; 1 place only (no walk goal); 2 walk goal +
// teleport with the mark
static int g_walk_variant = 0;
// 0.7.2.x fixes kept for the tests (off: the code before them)
static bool g_stale_fix = true, g_world_despawn_fix = true, g_death_fix = true;
COOP_FIX("npc.stale_proxy", "BUG-0009", g_stale_fix, "creature: a guest drops a proxy the host stopped sending states for");
COOP_FIX("npc.world_despawn", "BUG-0009", g_world_despawn_fix, "creature: the host despawns its NPCs of a world it left at the guests");
COOP_FIX("npc.play_death", "BUG-0015", g_death_fix, "creature: a guest's proxy of a killed NPC plays the death");
void set_stall_fix(bool on) { g_stall_fix = on; }
// a killed proxy plays the game's death (BUG-0015); coop.npc deathfix off (= npc.play_death off): it
// does not (tests: seeds the 0-health mirror that stands alive, BUG-0148)
void set_death_fix(bool on) { g_death_fix = on; }
static uint32_t g_reused = 0;   // host: creature objects that became another NPC (new id)
void set_reuse_fix(bool on) { g_reuse_fix = on; }
void set_id_fix(bool on) { g_id_fix = on; }
void debug_next_id(uint16_t id) { g_next_id = id; }

// ------------------------------------------------------------------ guest side
struct Proxy {
    void* c = nullptr;
    game::Key key{};
    uint8_t flags = 0;
    float health = 0, max_health = 0;
    bool killed = false;
    int fail = 0; // 1 species, 2 herd, 3 create
    bool nospecies_said = false; // BUG-0300 diagnostics: logged once
    bool kinematic = false;      // BUG-0281: placed on the host's path this frame
    double manual_until = 0;     // tests (BUG-0281): `coop.npc pwalk` drives it, the sync does not
    int manual_mode = 0;         // bits: 1 MoveTo again every 0.1 s, 2 set_position to itself, 4 set_orientation
    net::Vec3 manual_to{};
    float manual_speed = 0;
    double manual_last = 0;
    double last_rx = 0; // local time of its last spawn / state from the host
    net::SnapshotBuffer buf;
    drive::State drv;
    float walk_sp = 0;      // BUG-0361: the host NPC's speed while it last moved
    bool ever = false;      // BUG-0383: an object was made for it once (a body the guest's game removed stays gone)
    double made_at = 0;     // BUG-0399 (diagnostics): when its creature was made and the host's
    float made_agl = 0;     // position's height over this game's ground then (m)
    uint32_t tuning = 0;    // BUG-0462: the host NPC's archetype tuning key (spawn trailer), 0 unknown
    uint32_t tuning_set = 0; // the key last given to its creature
    double anim_fix_at = 0;  // BUG-0464: when the snapshot last played the host's animation on it
    double anim_rx_at = 0;   // BUG-0464: when the host's last start of an animation of it came
};
static std::map<uint16_t, Proxy> g_proxies;
// BUG-0383: a guest made no proxy for an NPC that was dead when it first came into the guest's range (killed
// while the guest was more than 110 m away, killed by another NPC before its proxy was made, a body that lay
// there when the guest joined): the host's player saw the body, the guest saw nothing. Now the proxy of a body
// is made too and plays the death where the host's body lies, unless the guest's game had made and removed
// it already (eaten, decayed). `coop.fix npc.dead_proxy off`: no proxy for a body (0.7.3.3).
static bool g_dead_proxy_fix = true;
COOP_FIX("npc.dead_proxy", "BUG-0383", g_dead_proxy_fix, "creature: a guest shows the bodies of NPCs that were dead before it came near");
// BUG-0300: a guest in a 4-player campaign had a proxy in its list (states arriving) whose creature
// was no creature of its world (not in the census, sync-check "missing on the guest") for 30 s and
// more, while the host's NPC stood 90 m from it. Cause not found. Once a second the guest now checks
// that every spawned proxy's object is still a creature of the noun list: one that is not was freed
// behind our back (driving it wrote into freed memory) and is made again; the log names it.
// `coop.fix npc.proxy_listed off`: no check (0.7.3.1).
static bool g_listed_fix = true;
COOP_FIX("npc.proxy_listed", "BUG-0300", g_listed_fix, "creature: a guest makes a proxy again whose creature left the world unnoticed");
static uint32_t g_proxy_unlisted = 0, g_proxy_nospecies = 0;
static double g_listed_at = 0;
static std::unordered_map<void*, uint16_t> g_proxy_of;
static std::map<uint64_t, void*> g_herds; // species instance/group -> herd for proxies
// the invariant monitor's damage line (definition at the end of the file)
static void log_hit(uint8_t victim, void* attacker, float amount, const char* how, bool from_host = false);
// guest: the game's own herd of each species (seen on the local NPCs the update filter removes).
// Proxies go into it: Create then gives them the herd's archetype tuning (+0xE84) like any NPC of
// the nest (social games, epic flag, DNA worth need it; docs/re/creature.md 8), and the species
// still has its creatures where the game counts them
static std::map<uint64_t, void*> g_local_herds;
static uint32_t g_proxies_in_nest = 0;
static uint64_t herd_key(const game::Key& k) { return ((uint64_t)k.group << 32) | k.instance; }
static std::vector<void*> g_kill;          // local NPCs to remove (guest)
static uint32_t g_proxy_lost = 0;
static double g_last_states_rx = -100; // guest: the last NPC state batch from the host
static uint32_t g_stale_dropped = 0;   // guest: proxies the host stopped talking about (ghosts)
static uint32_t g_deaths_played = 0; // guest: proxy deaths replayed (Die + animation)
static uint32_t g_removed_local = 0, g_hits_sent = 0, g_hits_applied = 0, g_damage_in = 0, g_kills_credited = 0;

static bool g_host_away = false; // guest: the host is in the main menu, our own game runs the NPCs
static uint32_t g_away_proxies = 0;
static uint32_t g_local_alive = 0; // guest: own NPCs that ran Update last frame (host away: the game's)
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

bool is_proxy(void* c) { return g_proxy_of.count(c) != 0; }
static bool g_strays_on = true; // tests: remove_saved_strays on/off
COOP_FIX("npc.strays", "BUG-0008", g_strays_on, "creature: creatures without tuning in herds like ours (saved puppets) are removed");
void set_strays(bool on) { g_strays_on = on; }
void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }

// called from inside the game's creature update loop: keep it cheap
static bool update_filter(void* c)
{
    if (!g_enabled || !is_guest() || !npc_stage())
        return true;
    if (game::is_puppet(c) || c == game::avatar())
        return true;
    if (g_host_away) // the host left the world: the guest plays on alone with its own NPCs
        return true;
    if (void* h = game::herd_of(c))
        if (h != game::avatar_herd())
            g_local_herds[herd_key(game::species_key(c))] = h;
    g_kill.push_back(c); // a local NPC: the host owns NPCs
    return false;
}

// The creature AI knows one player: NPCTickAI and its behaviour tree notice, chase, attack and
// flee only the avatar (cGameNounManager +0x54). Aggressive NPCs walked past a partner's puppet and
// went for the host (player report 2026-10-25: "aggressive enemies didn't target my friend",
// docs/re/creature.md 14). On the host an NPC nearer to a partner than to the host's avatar ticks
// with that partner's puppet as the player; its attacks on the puppet go to the partner through the
// damage filter (MSG_DAMAGE), like any NPC hit on a puppet.
static bool g_partner_targets = true;
COOP_FIX("npc.partners", "BUG-0016", g_partner_targets, "creature: the host's NPCs notice, chase and attack a partner's puppet");
static constexpr float kPartnerAiRange = 80.f; // m: NPCs farther from a partner than this ignore it
static std::unordered_map<void*, void*> g_ai_target; // host: NPC -> the puppet it treats as the player
static uint32_t g_ai_partner_picks = 0;
void set_partner_targets(bool on) { g_partner_targets = on; }

static void* ai_target(void* c)
{
    if (!g_enabled || !g_partner_targets || !is_host() || !game::in_creature_stage())
        return nullptr;
    void* av = game::avatar();
    auto prev = g_ai_target.find(c);
    void* was = prev != g_ai_target.end() ? prev->second : nullptr;
    void* pick = nullptr;
    // the pack and the avatar's family follow and defend the host's avatar
    if (av && !game::is_dead(c) && !game::pack_member(c) && game::herd_of(c) != game::avatar_herd()) {
        net::Vec3 np = game::position(c);
        // an NPC far from every partner keeps the avatar (its notice and chase ranges are tens of m)
        float best = std::min(net::dist(np, game::position(av)), kPartnerAiRange);
        for (auto& p : C.sess->players()) {
            if (p.id == 0)
                continue;
            void* pup = C.puppet_of ? C.puppet_of(p.id) : nullptr;
            net::Vec3 gp;
            if (!pup || !game::is_puppet(pup) || !game::object_alive(pup) || !C.player_pos(p.id, gp) ||
                (C.player_dead && C.player_dead(p.id)))
                continue;
            // 3 m either way: an NPC between the two players does not switch every frame
            float d = net::dist(np, game::position(pup)) + (pup == was ? -3.f : 3.f);
            if (d < best) {
                best = d;
                pick = pup;
            }
        }
    }
    if (pick) {
        if (pick != was)
            g_ai_partner_picks++;
        g_ai_target[c] = pick;
    } else if (prev != g_ai_target.end())
        g_ai_target.erase(prev);
    return pick;
}

// ------------------------------------------------------------------ BUG-0461: replicated fields
// NpcSpawn.scale was sent and never read: every proxy kept the size its own game gave it (a random
// size per individual of the species, and a grown-up's for a young one: babies were shown as alphas).
// The proxies are one kind of the field replication (fields.h): the host reads every NPC's scale, the
// guest sets it on its proxy whenever the proxy's own differs (made later, grown by the game).
// `coop.fix npc.scale off`: proxies keep their own size (0.7.3.6).
static bool g_scale_fix = true;
COOP_FIX("npc.scale", "BUG-0461", g_scale_fix, "creature: NPC proxies get the host NPC's size (young ones small)");
static const fields::Field kNpcFields[] = {fields::kScale};

static void npc_each(void (*cb)(uint16_t, void*, void*), void* u)
{
    for (void* c : g_seen_now) {
        auto it = g_host_npcs.find(c);
        if (it != g_host_npcs.end() && !game::is_destroyed(c))
            cb(it->second.id, c, u);
    }
}

static void* npc_mirror(uint16_t id)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end() || !it->second.c || game::is_destroyed(it->second.c))
        return nullptr;
    return it->second.c;
}

// ------------------------------------------------------------------ BUG-0487: resync of unknown ids (G5)
// The host sends an NPC's spawn once per guest (g_known); a guest that has no proxy of an id it gets
// states for (its proxy dropped as stale while the host still counts it as known, a spawn that came
// while the guest was in another world, a world change on the guest) dropped those states for good: a
// host NPC near the guest without a proxy, or a proxy whose death / health never came. Now the guest
// asks the host for the spawn of every id it gets states for and does not know (at most once per id in
// kResyncEvery s), and the host sends it again. `coop.fix npc.resync off`: states of unknown ids dropped.
static bool g_resync_fix = true;
COOP_FIX("npc.resync", "BUG-0487", g_resync_fix, "creature: a guest asks the host again for an NPC it gets states of and has no proxy of");
constexpr double kResyncEvery = 2.0;
static std::map<uint16_t, double> g_unknown_ids; // guest: id -> when it was asked for last (0: not yet)
static double g_resync_at = 0;
static uint32_t g_resync_asked = 0, g_resync_sent = 0, g_forgotten = 0;
static void destroy_proxy(uint16_t id);

static void resync_tick(double now)
{
    if (!g_resync_fix || g_unknown_ids.empty() || now - g_resync_at < 0.5)
        return;
    g_resync_at = now;
    std::vector<uint16_t> ask;
    for (auto it = g_unknown_ids.begin(); it != g_unknown_ids.end();) {
        if (g_proxies.count(it->first)) { // the spawn came
            it = g_unknown_ids.erase(it);
            continue;
        }
        if (now - it->second >= kResyncEvery && ask.size() < 200) {
            ask.push_back(it->first);
            it->second = now;
        }
        ++it;
    }
    if (ask.empty())
        return;
    net::Writer w;
    w.u8((uint8_t)ask.size());
    for (uint16_t id : ask)
        w.u16(id);
    C.sess->send_reliable_to(0, net::MSG_NPC_RESYNC, w);
    g_resync_asked += (uint32_t)ask.size();
}

// guest (tests, BUG-0487): forget the proxy of `id` without telling the host, as a lost spawn would
std::string debug_forget(uint16_t id)
{
    if (!g_proxies.count(id))
        return "no proxy " + std::to_string(id) + "\n";
    destroy_proxy(id);
    g_forgotten++;
    return "forgot proxy " + std::to_string(id) + " (the host still counts it as known)\n";
}

std::string resync_status()
{
    char b[200];
    snprintf(b, sizeof b, "npc resync: fix=%d unknown=%zu asked=%u spawns_resent=%u forgotten=%u\n", g_resync_fix ? 1 : 0,
             g_unknown_ids.size(), g_resync_asked, g_resync_sent, g_forgotten);
    return b;
}

static int c_npctrack(void*, int argc, const char* const* argv, SkOut* out);
static void on_behaviour_anim(void* c, uint32_t anim, int how, int a, int b);

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    game::set_update_filter(update_filter);
    game::set_ai_target_picker(ai_target);
    game::set_behaviour_anim_listener(on_behaviour_anim); // BUG-0464
    fields::add_kind({fields::K_NPC, "npc", &g_scale_fix, kNpcFields, 1, npc_each, npc_mirror, nullptr}); // BUG-0461
    H->command_add(M, "coop.npctrack", "coop.npctrack start [radius m]|stop|count|dump [from] [n]|walk <id> x y z <m/s> - every frame: the guest's NPC proxies (tests, BUG-0361)",
                   c_npctrack, nullptr, SK_CMD_MAIN_THREAD);
}

// BUG-0462: an NPC's archetype tuning (+0xE84) decides its relation to the player when its species has no
// record (GetRelationship 0xBA40E0 -> tuning +0x390), and the social games and the avatar-event handler read
// it. Create takes it from the species profile (+0x58C archetype, +0x5F4 variant) through the species
// manager's tuning map (0x4E00C0): a proxy of a species the guest's game knows differently (lane 9: 06805133)
// was made without tuning, so its relation came from the diet / size rule (1, hostile, where the host's NPC
// had 3). The tuning is a node of that map: its key is the dword before it. The host sends the key after
// the spawn; the guest gives the proxy the same tuning (SetTuning 0xC0C2E0) when it has another or none.
// `coop.fix npc.tuning off`: the proxy keeps the tuning its own Create picked.
static bool g_tuning_fix = true;
COOP_FIX("npc.tuning", "BUG-0462", g_tuning_fix, "creature: an animal on the guest has the host's archetype (its relation, social game and behaviour data)");
static uint32_t g_tunings_set = 0, g_tunings_missing = 0;

static uint32_t tuning_key(void* c)
{
    uint8_t* t = c ? *(uint8_t**)((uint8_t*)c + 0xE84) : nullptr;
    return t && skg::readable(t - 4, 8) ? *(uint32_t*)(t - 4) : 0;
}

static void* tuning_of_key(uint32_t key)
{
    static uintptr_t get = H->sym("Editors::cSpeciesManager::Get"), find = H->sym("SporeCoop::cSpeciesManager_GetTuning");
    void* mgr = get && find && key ? skg::cdecl_call<void*>(get) : nullptr;
    return mgr ? skg::thiscall<void*>(find, mgr, key, 0) : nullptr;
}

static void proxy_tuning(Proxy& px)
{
    if (!g_tuning_fix || !px.c || !px.tuning || px.tuning_set == px.tuning)
        return;
    px.tuning_set = px.tuning;
    if (tuning_key(px.c) == px.tuning)
        return;
    static uintptr_t set = H->sym("SporeCoop::cCreatureBase_SetTuning");
    void* t = tuning_of_key(px.tuning);
    if (!t || !set) {
        g_tunings_missing++;
        return;
    }
    skg::thiscall<void>(set, px.c, t);
    g_tunings_set++;
}

// tests: a proxy without tuning, as Create makes one in a herd of ours (the species' nest herd not found yet:
// lane 9, the first proxies of species 06805133 after a join); the fix gives it the host's again
std::string debug_untune(uint16_t id)
{
    static uintptr_t set = H->sym("SporeCoop::cCreatureBase_SetTuning");
    auto it = g_proxies.find(id);
    if (it == g_proxies.end() || !it->second.c || !set)
        return "no proxy " + std::to_string(id) + "\n";
    skg::thiscall<void>(set, it->second.c, (void*)nullptr);
    it->second.tuning_set = 0;
    return "proxy " + std::to_string(id) + " has no tuning now\n";
}

std::string tuning_status()
{
    char b[120];
    snprintf(b, sizeof b, "npc tuning: fix=%d set=%u missing=%u\n", (int)g_tuning_fix, g_tunings_set, g_tunings_missing);
    return b;
}

// BUG-0464: the guest's proxies showed the host NPC's animation in 7-9 % of the samples (coop_npc_anims.py): a
// proxy has no AI (NPCTickAI is skipped for puppets), and the behaviour tree's nodes start their animations
// (the carnivores' and herbivores' idle node 0xD8DC30: B31EB92C, 731EA3EA, 0519E154 ...; eating, social
// answers, fights) with SporeCoop::CreaturePlayAnim, which nothing relayed (the relay tried before hooked
// cCreatureBase::PlayAnimation, the idle refresh's other path). Now the host reports every CreaturePlayAnim
// of its NPCs outside the idle refresh 0xC148B0 (game.cpp) and sends them once a frame to the guests that
// mirror the NPC (MSG_NPC_ANIM); the guest plays the same call on the proxy. The proxy's own idle / walk
// choice stays the game's: cCreatureAnimal::Update asks the idle refresh only when no main animation is left,
// so a played behaviour animation runs to its end there as on the host (docs/re/creature.md#creature-bt-anims).
// `coop.fix npc.anims off`: nothing sent, nothing played (0.7.3.4).
static bool g_anims_fix = true;
COOP_FIX("npc.anims", "BUG-0464", g_anims_fix, "creature: a guest sees the host's animals eat, look around, fight and answer as on the host");
struct NpcAnim {
    uint16_t id;
    uint32_t anim;
    uint8_t how; // 0 CreaturePlayAnim, 1 PlayAnimation, 2 PlayAnimationTo
    uint8_t a;
    int32_t b;
};
static std::vector<NpcAnim> g_anim_out; // host: this frame's behaviour animations of its NPCs
static uint32_t g_anims_sent = 0, g_anims_dropped = 0, g_anims_rx = 0, g_anims_played = 0, g_anims_noproxy = 0,
                g_anims_failed = 0, g_anims_snap_sent = 0, g_anims_snap_rx = 0, g_anims_snap_played = 0;
static double g_anim_snap_at = 0;
static float g_anim_snap_quiet = 1.5f; // guest: no snapshot play this soon after a start (s); tests: coop.npc animquiet

static void on_behaviour_anim(void* c, uint32_t anim, int how, int a, int b)
{
    if (!g_anims_fix || !is_host() || !npc_stage())
        return;
    auto it = g_host_npcs.find(c);
    if (it == g_host_npcs.end() || it->second.dead)
        return;
    it->second.bhv_anim = anim;
    if (g_anim_out.size() >= 200) {
        g_anims_dropped++;
        return;
    }
    g_anim_out.push_back({it->second.id, anim, (uint8_t)how, (uint8_t)(a & 0xFF), (int32_t)b});
}

// host, once a frame after the states: the guest gets the animations of the NPCs it mirrors
static void send_anims(uint8_t guest, const std::set<uint16_t>& known)
{
    std::vector<const NpcAnim*> v;
    for (auto& e : g_anim_out)
        if (known.count(e.id))
            v.push_back(&e);
    const size_t per = (net::kMaxPacket - 64) / 12;
    for (size_t i = 0; i < v.size(); i += per) {
        size_t n = std::min(per, v.size() - i);
        net::Writer w;
        w.u8(0); // started now
        w.u8((uint8_t)n);
        for (size_t j = 0; j < n; j++) {
            w.u16(v[i + j]->id);
            w.u32(v[i + j]->anim);
            w.u8(v[i + j]->how);
            w.u8(v[i + j]->a);
            w.u32((uint32_t)v[i + j]->b);
        }
        C.sess->send_unreliable_to(guest, net::MSG_NPC_ANIM, w);
        g_anims_sent += (uint32_t)n;
    }
}

// host, once a second: the animation each NPC the guest mirrors plays now, when it is one its BT started (or
// it is dead: the body's animation). A proxy made after the start, a lost message, a proxy whose animation was
// cut (the driving's nudges) shows it again within a second.
static void send_anim_snapshot(uint8_t guest, const std::set<uint16_t>& known)
{
    std::vector<std::pair<uint16_t, uint32_t>> v;
    for (uint16_t id : known) {
        auto b = g_host_by_id.find(id);
        if (b == g_host_by_id.end() || !g_seen_now.count(b->second))
            continue;
        auto hn = g_host_npcs.find(b->second);
        uint32_t cur = 0;
        if (hn == g_host_npcs.end() || !game::drawn_state(b->second, nullptr, &cur) || !cur)
            continue;
        if (cur == hn->second.bhv_anim || game::is_dead(b->second))
            v.push_back({id, cur});
    }
    const size_t per = (net::kMaxPacket - 64) / 6;
    for (size_t i = 0; i < v.size(); i += per) {
        size_t n = std::min(per, v.size() - i);
        net::Writer w;
        w.u8(1); // what plays now
        w.u8((uint8_t)n);
        for (size_t j = 0; j < n; j++) {
            w.u16(v[i + j].first);
            w.u32(v[i + j].second);
        }
        C.sess->send_unreliable_to(guest, net::MSG_NPC_ANIM, w);
        g_anims_snap_sent += (uint32_t)n;
    }
}

void set_anim_quiet(float s) { g_anim_snap_quiet = s; }

std::string anims_status()
{
    uint32_t seen = 0, idle = 0;
    game::behaviour_anim_counts(&seen, &idle);
    char b[256];
    snprintf(b, sizeof b, "npc anims: fix=%d seen=%u idle=%u sent=%u dropped=%u rx=%u played=%u noproxy=%u failed=%u "
             "snap_sent=%u snap_rx=%u snap_played=%u\n",
             (int)g_anims_fix, seen, idle, g_anims_sent, g_anims_dropped, g_anims_rx, g_anims_played, g_anims_noproxy,
             g_anims_failed, g_anims_snap_sent, g_anims_snap_rx, g_anims_snap_played);
    return b;
}

// tests (BUG-0464): the animation each NPC plays now, as the game holds it (vt 0x58 GetCurrentAnimation):
// host: its NPCs by net id, guest: the proxies
std::string current_anims()
{
    std::string out;
    char b[80];
    auto line = [&](int id, void* c) {
        uint32_t a = 0;
        net::Vec3 m;
        if (c && game::creature_listed(c) && game::drawn_state(c, &m, &a)) {
            snprintf(b, sizeof b, "cur %d anim=%08X%s\n", id, a, game::is_dead(c) ? " DEAD" : "");
            out += b;
        }
    };
    if (is_host())
        for (auto& [id, c] : g_host_by_id)
            line(id, c);
    else
        for (auto& [id, px] : g_proxies)
            line(id, px.c);
    return out;
}

static net::NpcState host_state(uint16_t id, void* c)
{
    net::NpcState s;
    s.net_id = id;
    s.pos = game::position(c);
    s.rot = game::orientation(c);
    s.vel = game::velocity(c);
    s.health = game::health(c);
    s.flags = (game::is_dead(c) ? net::NF_DEAD : 0) | (game::impressed(c) ? net::NF_IMPRESSED : 0);
    return s;
}

static void send_spawn(uint8_t guest, uint16_t id, void* c)
{
    net::NpcSpawn sp;
    sp.net_id = id;
    game::Key k = game::species_key(c);
    // one of our own creations (another creation of the guest may have its key): its records first
    creations::host_species_out(guest, {k.instance, k.type, k.group});
    sp.key[0] = k.instance;
    sp.key[1] = k.type;
    sp.key[2] = k.group;
    sp.scale = game::scale(c);
    void* av = game::avatar();
    sp.flags = (game::is_dead(c) ? net::NF_DEAD : 0) |
               (av && game::herd_of(c) && game::herd_of(c) == game::herd_of(av) ? net::NF_FAMILY : 0);
    sp.pos = game::position(c);
    sp.rot = game::orientation(c);
    sp.health = game::health(c);
    sp.max_health = game::max_health(c);
    net::Writer w;
    sp.write(w);
    w.u32(tuning_key(c)); // BUG-0462 trailer: older SporeCoop ignores it
    C.sess->send_reliable_to(guest, net::MSG_NPC_SPAWN, w);
}

static void send_despawn(uint8_t guest, uint16_t id)
{
    net::Writer w;
    w.u16(id);
    w.u8(0);
    C.sess->send_reliable_to(guest, net::MSG_NPC_DESPAWN, w);
}

// host: net id `id` of creature object `c` is over: the guests that mirror it drop it
static void retire(uint16_t id, void* c)
{
    for (auto& [g, ks] : g_known)
        if (ks.erase(id))
            send_despawn(g, id);
    auto b = g_host_by_id.find(id);
    if (b != g_host_by_id.end() && b->second == c)
        g_host_by_id.erase(b);
    g_ai_target.erase(c);
    teamplay::forget_target(c);
}

// host: tracked NPCs that did not update this frame and are no creature in the world any more: freed
// (not in the noun list) or back in the creature pool (still listed, no herd, at (0 0 0)). Their ids
// go now. Fields are read only of objects still in the noun list (a freed one may be any memory).
static bool g_pool_fix = true;
COOP_FIX("npc.pooled", "BUG-0274", g_pool_fix, "creature: an NPC the game puts back into its pool is despawned at once");
static uint32_t g_pooled_retired = 0;
static void retire_pooled()
{
    struct Acc {
        std::unordered_set<void*> unseen, listed;
    } a;
    for (auto& [c, hn] : g_host_npcs)
        if (!g_seen_now.count(c))
            a.unseen.insert(c);
    if (a.unseen.empty())
        return;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* a = (Acc*)u;
            if (noun == 0x18EB45Eu && a->unseen.count(o) && game::object_alive(o))
                a->listed.insert(o);
            return true;
        },
        &a);
    for (void* c : a.unseen) {
        bool gone = !a.listed.count(c);
        if (!gone) {
            net::Vec3 p = game::position(c);
            gone = !game::herd_of(c) || (p.x == 0.f && p.y == 0.f && p.z == 0.f);
        }
        if (!gone)
            continue;
        auto it = g_host_npcs.find(c);
        retire(it->second.id, c);
        g_host_npcs.erase(it);
        g_pooled_retired++;
    }
}

// tests (BUG-0305): `coop.npc dash <id> <m> <m/s> <s> [w]` runs host NPC #id back and forth along
// a zigzag on the ground (4 legs over <m>, <w> m to either side, at <m/s>, 0.6 s standing at each
// end) for <s> seconds, like a wounded animal of a Tribe hunt. Position and velocity are written
// after the game's update of the frame.
struct Dash {
    uint16_t id = 0;
    net::Vec3 a, dir, side;
    float len = 0, speed = 0, width = 0;
    double t0 = 0, until = 0;
    uint16_t mate = 0; // a second NPC on the same path, `off` m to the side (a herd that flees together)
    float off = 0;
};
static Dash g_dash;
static net::Vec3 dash_point(int k) // corner k (0..4) of the zigzag
{
    float fw = g_dash.len * k / 4.f, sw = (k == 0 || k == 4) ? 0.f : (k & 1 ? g_dash.width : -g_dash.width);
    return {g_dash.a.x + g_dash.dir.x * fw + g_dash.side.x * sw, g_dash.a.y + g_dash.dir.y * fw + g_dash.side.y * sw,
            g_dash.a.z + g_dash.dir.z * fw + g_dash.side.z * sw};
}
static void dash_tick(double now)
{
    if (!g_dash.id || now > g_dash.until) {
        g_dash.id = 0;
        return;
    }
    float leg[4], total = 0;
    for (int k = 0; k < 4; k++)
        total += leg[k] = net::dist(dash_point(k), dash_point(k + 1));
    double run = total / g_dash.speed, cycle = 2 * (run + 0.6);
    double t = std::fmod(now - g_dash.t0, cycle);
    float d; // metres along the path from corner 0
    bool moving = true;
    if (t < run)
        d = (float)(t * g_dash.speed);
    else if (t < run + 0.6) {
        d = total;
        moving = false;
    } else if (t < 2 * run + 0.6)
        d = total - (float)((t - run - 0.6) * g_dash.speed);
    else {
        d = 0;
        moving = false;
    }
    bool back = t >= run + 0.6;
    int k = 0;
    while (k < 3 && d > leg[k]) {
        d -= leg[k];
        k++;
    }
    net::Vec3 p0 = dash_point(k), p1 = dash_point(k + 1);
    float f = leg[k] > 0 ? std::min(d / leg[k], 1.f) : 0.f;
    net::Vec3 q{p0.x + (p1.x - p0.x) * f, p0.y + (p1.y - p0.y) * f, p0.z + (p1.z - p0.z) * f};
    net::Vec3 v{};
    if (moving && leg[k] > 0) {
        float sc = g_dash.speed / leg[k] * (back ? -1.f : 1.f);
        v = {(p1.x - p0.x) * sc, (p1.y - p0.y) * sc, (p1.z - p0.z) * sc};
    }
    for (int m = 0; m < 2; m++) {
        auto it = g_host_by_id.find(m ? g_dash.mate : g_dash.id);
        if ((m && !g_dash.mate) || it == g_host_by_id.end() || !g_seen_now.count(it->second))
            continue;
        void* c = it->second;
        float o = m ? g_dash.off : 0.f;
        net::Vec3 w{q.x + g_dash.side.x * o, q.y + g_dash.side.y * o, q.z + g_dash.side.z * o};
        float r = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z), gr = game::ground_radius(w);
        if (r > 1 && gr > 1)
            w = {w.x / r * gr, w.y / r * gr, w.z / r * gr};
        game::set_position(c, w);
        *(net::Vec3*)((uint8_t*)c + game::kVelOff) = v;
    }
}
// tests (BUG-0339): host NPC #id goes back to the game's creature pool, as the relevance pass does it
// (creature vt 0xC8, thiscall(c, 1): push 1 at 0xACE88C in Relevance_Update)
std::string debug_pool(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    if (!is_host() || it == g_host_by_id.end() || !g_seen_now.count(it->second))
        return "no such host npc";
    void* c = it->second;
    skg::vcall<void>(c, 0xC8 / 4, 1);
    net::Vec3 p = game::position(c);
    char b[120];
    snprintf(b, sizeof b, "npc %u pooled: now at (%.1f %.1f %.1f) herd %p", id, p.x, p.y, p.z, game::herd_of(c));
    return b;
}
// tests (BUG-0281): the guest's proxy #id walks to p at speed with the game's locomotion for secs (no sync)
std::string debug_pwalk(uint16_t id, const net::Vec3& p, float speed, float secs, int mode)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end() || !it->second.c)
        return "no such spawned proxy";
    it->second.manual_until = H->time_seconds() + secs;
    it->second.manual_mode = mode;
    it->second.manual_to = p;
    it->second.manual_speed = speed;
    game::set_speed(it->second.c, speed);
    game::move_to(it->second.c, p, 0.3f);
    net::Vec3 c = game::position(it->second.c);
    char b[160];
    snprintf(b, sizeof b, "proxy %u at (%.1f %.1f %.1f) walks to (%.1f %.1f %.1f) at %.1f m/s for %.0f s", id, c.x, c.y, c.z,
             p.x, p.y, p.z, speed, secs);
    return b;
}
// tests (BUG-0399): the guest's proxy #id put <m> under this game's ground once (where the monitor found
// proxies after joins); the sync drives it on from there
std::string debug_sink(uint16_t id, float m, bool mark)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end() || !it->second.c)
        return "no such spawned proxy";
    void* c = it->second.c;
    net::Vec3 p = game::position(c);
    float r = drive::speed(p), g = game::ground_radius(p);
    if (r < 1 || g <= 0)
        return "no ground";
    float k = (g - m) / r;
    net::Vec3 q{p.x * k, p.y * k, p.z * k};
    // without the teleport mark (+0xB5D), as walk_place places: with it the game set the creature back onto
    // its terrain at once (fix off, standing, 6 of 6)
    if (mark)
        game::teleport(c, q, game::orientation(c));
    else
        game::place(c, q, game::orientation(c));
    char b[160];
    snprintf(b, sizeof b, "proxy %u put %.2f m under the ground at (%.1f %.1f %.1f): now %.2f m over it", id, m, q.x, q.y,
             q.z, drive::over_ground(game::position(c)));
    return b;
}
std::string debug_dash(uint16_t id, float len, float speed, float secs, float width, uint16_t mate, float off)
{
    void* c = host_npc(id);
    if (!is_host() || !c || len <= 0 || speed <= 0)
        return "no such host npc";
    net::Vec3 p = game::position(c);
    float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    net::Vec3 up = r > 1 ? net::Vec3{p.x / r, p.y / r, p.z / r} : net::Vec3{0, 0, 1};
    net::Vec3 a = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
    float al = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    a = {a.x / al, a.y / al, a.z / al};
    net::Vec3 b{up.y * a.z - up.z * a.y, up.z * a.x - up.x * a.z, up.x * a.y - up.y * a.x}; // up x a
    void* mc = host_npc(mate);
    g_dash = {id, p, a, b, len, speed, width, H->time_seconds(), H->time_seconds() + secs, mc ? mate : (uint16_t)0, off};
    char t[192];
    snprintf(t, sizeof t, "npc %u dashes %.1f m (zigzag %.1f m) at %.1f m/s for %.0f s from (%.1f %.1f %.1f), mate %u %.1f m beside", id,
             len, width, speed, secs, p.x, p.y, p.z, g_dash.mate, off);
    return t;
}

// ---- coop.npctrack (BUG-0361): what a guest shows of the host's NPCs, frame by frame, as coop.track
// does for the partner's puppet (BUG-0360). One row per proxy within the radius of the guest's avatar
// per frame: the position the game drew (the proxy's position before this frame's driving: the mod's
// frame runs at Present, after the drawing), the interpolated host state (the target), the game's
// speed, the animation and its walk gait, whether it is on the screen.
struct NpcTrackRow {
    float t;
    uint16_t id;
    uint8_t how;   // 0 driven (drive::step), 1 placed (npc.kinematic), 2 killed, 3 walked (npc.walk)
    uint8_t flags; // the host state's NF_*
    uint16_t tp;   // the driver's teleports so far
    net::Vec3 d, p, tgt;
    net::Vec3 m; // the model's position (AnimatedCreature +0x04): where a corpse's body lies
    float gv, rv, da; // the game's speed, the host's speed, the distance to the guest's avatar
    uint32_t anim;
    int16_t gait = -1, gait2 = -1;
    int8_t moving = -1, speed_state = -1;
    int8_t vis = 0;
    float sx = 0, sy = 0;
};
static std::deque<NpcTrackRow> g_ntrack;
static bool g_ntrack_on = false;
static double g_ntrack_t0 = 0;
static float g_ntrack_r = 40.f;
static constexpr size_t kNtrackMax = 200000;

// the guest's avatar, in Tribe the player tribe (no avatar there)
static bool ntrack_centre(net::Vec3& c)
{
    if (void* av = game::avatar()) {
        c = game::position(av);
        return true;
    }
    return rts_stage() && tribe::center(c);
}

static void ntrack_push(uint16_t id, void* c, uint8_t how, uint8_t flags, uint16_t tp, const net::Vec3& drawn,
                        const net::Vec3& tgt, float rv, double now)
{
    net::Vec3 ctr;
    if (!g_ntrack_on || !c || !ntrack_centre(ctr))
        return;
    NpcTrackRow x{};
    x.p = game::position(c);
    x.da = net::dist(x.p, ctr);
    if (x.da > g_ntrack_r)
        return;
    x.t = (float)(now - g_ntrack_t0);
    x.id = id;
    x.how = how;
    x.flags = flags;
    x.tp = tp;
    x.d = drawn;
    x.tgt = tgt;
    x.gv = drive::speed(game::velocity(c));
    x.rv = rv;
    game::drawn_state(c, &x.m, &x.anim);
    int g = -1, g2 = -1, mv = -1, ss = -1;
    if (game::gait_state(c, &g, &g2, &mv, &ss)) {
        x.gait = (int16_t)g;
        x.gait2 = (int16_t)g2;
        x.moving = (int8_t)mv;
        x.speed_state = (int8_t)ss;
    }
    float w[3] = {drawn.x, drawn.y, drawn.z}, sc[2] = {0, 0};
    x.vis = H->world_to_screen(w, sc) ? 1 : 0;
    x.sx = sc[0];
    x.sy = sc[1];
    g_ntrack.push_back(x);
    while (g_ntrack.size() > kNtrackMax)
        g_ntrack.pop_front();
}

static void ntrack_row(uint16_t id, const Proxy& px, const net::PlayerState& s, const net::Vec3& drawn, uint8_t how,
                       double now, float rv)
{
    ntrack_push(id, px.c, how, s.flags, (uint16_t)px.drv.teleports, drawn, s.pos, rv, now);
}

static int c_npctrack(void*, int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "count";
    if (!strcmp(a, "start")) {
        g_ntrack.clear();
        g_ntrack_on = true;
        g_ntrack_t0 = H->time_seconds();
        if (argc > 2)
            g_ntrack_r = (float)atof(argv[2]);
    } else if (!strcmp(a, "stop"))
        g_ntrack_on = false;
    else if (!strcmp(a, "dump")) {
        // dump [from] [n] (how: 0 driven, 1 placed, 2 killed, 3 walked, 4 an NPC of the host itself): t id how flags tp dx dy dz px py pz tx ty tz gv rv da anim gait gait2 moving ss vis sx sy mx my mz
        size_t from = argc > 2 ? (size_t)atol(argv[2]) : 0, n = argc > 3 ? (size_t)atol(argv[3]) : 2000;
        for (size_t i = from; i < g_ntrack.size() && i < from + n; i++) {
            const auto& x = g_ntrack[i];
            H->out_printf(out, "%.4f %u %u %u %u %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.2f %.2f %.1f %08X %d %d %d %d %d %.0f %.0f %.3f %.3f %.3f\n",
                          x.t, x.id, x.how, x.flags, x.tp, x.d.x, x.d.y, x.d.z, x.p.x, x.p.y, x.p.z, x.tgt.x, x.tgt.y,
                          x.tgt.z, x.gv, x.rv, x.da, x.anim, x.gait, x.gait2, x.moving, x.speed_state, x.vis, x.sx, x.sy, x.m.x,
                          x.m.y, x.m.z);
        }
        return 0;
    } else if (!strcmp(a, "objects")) {
        // objects [r] [noun hex]: every game object within r m of the centre: noun, its position, its height over
        // the ground (cPlanetModel::GetHeightAt), alive (tests: nest eggs, corpses; player report 2026-11-01)
        net::Vec3 ctr{};
        if (!ntrack_centre(ctr)) {
            H->out_printf(out, "no centre\n");
            return 1;
        }
        struct U {
            net::Vec3 c;
            float r;
            uint32_t noun;
            SkOut* out;
            int n;
        } u{ctr, argc > 2 ? (float)atof(argv[2]) : 30.f, argc > 3 ? (uint32_t)strtoul(argv[3], nullptr, 16) : 0u, out, 0};
        game::for_each_object(
            [](void* o, uint32_t noun, void* p) {
                auto* u = (U*)p;
                net::Vec3 q;
                if ((u->noun && noun != u->noun) || !game::object_position(o, q) || net::dist(q, u->c) > u->r || u->n >= 400)
                    return true;
                float rad = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), gr = game::ground_radius(q);
                H->out_printf(u->out, "obj %p noun=%08X pos=(%.2f %.2f %.2f) over_ground=%.2f alive=%d dist=%.1f\n", o, noun, q.x,
                              q.y, q.z, gr > 1 ? rad - gr : -999.f, (int)game::object_alive(o), net::dist(q, u->c));
                u->n++;
                return true;
            },
            &u);
        return 0;
    } else if (!strcmp(a, "novel")) {
        if (argc > 2)
            g_npc_novel = atoi(argv[2]) != 0;
        H->out_printf(out, "npc novel %d\n", (int)g_npc_novel);
        return 0;
    } else if (!strcmp(a, "variant") && argc > 2) { // tests (BUG-0361): how npc.walk moves a proxy
        g_walk_variant = atoi(argv[2]);
        H->out_printf(out, "npc.walk variant %d (0 walk goal + place, 1 place only, 2 walk goal + teleport, 3 no teleport when the place was lost)\n", g_walk_variant);
        return 0;
    } else if (!strcmp(a, "walk") && argc > 6) {
        // host (tests): the game walks host NPC #id to x y z at <m/s> (its own locomotion, MoveTo)
        void* c = host_npc((uint16_t)atoi(argv[2]));
        if (!is_host() || !c) {
            H->out_printf(out, "no such host npc\n");
            return 1;
        }
        net::Vec3 to{(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])};
        game::set_speed(c, (float)atof(argv[6]));
        game::move_to(c, to, 0.5f);
        net::Vec3 p = game::position(c);
        H->out_printf(out, "npc %s at (%.1f %.1f %.1f) walks to (%.1f %.1f %.1f) at %s m/s\n", argv[2], p.x, p.y, p.z,
                      to.x, to.y, to.z, argv[6]);
        return 0;
    }
    net::Vec3 ctr{};
    bool have = ntrack_centre(ctr);
    H->out_printf(out, "npctrack %s samples=%u r=%.0f centre=%s(%.1f %.1f %.1f)\n", g_ntrack_on ? "on" : "off",
                  (unsigned)g_ntrack.size(), g_ntrack_r, have ? "" : "none", ctr.x, ctr.y, ctr.z);
    return 0;
}

// BUG-0512: the host's pack (+0xB58 & 0x100) and family (the avatar's herd) take the host's fights as theirs:
// a partner's puppet the host targeted (a click sets the avatar's +0xE7C to the puppet), or one that got into
// a pack member's "attacked" stimuli, became the pack's target and the pack chased and bit the partner on both
// screens (npc.party_peace, BUG-0374, only drops the damage). With npc.pack_spares_partners the host clears, for
// every pack or family member the game updates, a target (+0xE7C, a combatant: inside the creature) on a
// partner's puppet with the creature's own SetTarget (vt 0x84, (null, 1, 0): the BUG-0453 guard ends an attack
// node left without a target) and its stimuli naming the puppet. Off: 0.7.3.6.
static bool g_pack_spares_fix = true;
COOP_FIX("npc.pack_spares_partners", "BUG-0512", g_pack_spares_fix, "creature: the host's pack and family never take a partner as their target");
static uint32_t g_pack_targets_cleared = 0, g_pack_stimuli_cleared = 0;
constexpr size_t kTargetOff = 0xE7C, kCreatureSpan = 0x1700;

static void* partner_puppet_at(const void* sub)
{
    for (void* p : game::puppets())
        if (sub >= p && (const uint8_t*)sub < (const uint8_t*)p + kCreatureSpan)
            return p;
    return nullptr;
}

static void pack_spares_partners(const std::vector<void*>& creatures)
{
    if (!g_pack_spares_fix || !game::in_creature_stage() || game::puppets().empty())
        return;
    // the pack (cGameNounManager +0x5C..+0x60, it may not be in the update list) and the creatures updated
    std::vector<void*> party;
    if (void* nm = game::noun_manager())
        for (void** it = skg::field<void**>(nm, 0x5C), **e = skg::field<void**>(nm, 0x60); it && it < e && it - skg::field<void**>(nm, 0x5C) < 32; ++it)
            if (*it && skg::readable(*it, 0x1680))
                party.push_back(*it);
    for (void* c : creatures)
        if (std::find(party.begin(), party.end(), c) == party.end())
            party.push_back(c);
    for (void* c : party) {
        if (game::is_puppet(c) || c == game::avatar() || !skg::readable(c, kTargetOff + 4) || !creature_hp::host_party(c) ||
            game::is_dead(c))
            continue;
        if (void* pup = partner_puppet_at(skg::field<void*>(c, kTargetOff))) {
            skg::vcall<void>(c, 0x84 / 4, (void*)nullptr, 1, 0);
            if (++g_pack_targets_cleared <= 8 || g_pack_targets_cleared % 100 == 0)
                H->log(M, SK_INFO, "npc: the host's pack member %p had the partner's puppet %p as its target: cleared (BUG-0512, %u)",
                       c, pup, g_pack_targets_cleared);
        }
        for (void* pup : game::puppets())
            g_pack_stimuli_cleared += gamefixes::clear_stimuli_from(c, pup);
    }
}

static void host_tick(double now, const std::vector<void*>& creatures)
{
    void* av = game::avatar();
    net::Vec3 center;
    bool rts = rts_stage() && C.stage_center && C.stage_center(center);
    if (!av && !rts)
        return;
    // no creature updated this frame: the simulation stands still (pause menu, the death caption
    // waiting for OK, a cinematic). Not a reason to forget them: the guests lost every NPC after 2 s
    // and got them again when the host went on (play_creature.py 2026-10-23)
    if (creatures.empty()) {
        g_frozen_since = g_frozen_since > 0 ? g_frozen_since : now;
        return;
    }
    if (g_frozen_since > 0) { // the frozen time does not count as unseen
        double gap = now - g_frozen_since;
        for (auto& [c, hn] : g_host_npcs)
            hn.last_seen += gap;
        g_frozen_since = 0;
    }
    g_seen_now.clear();
    for (auto& [c, hn] : g_host_npcs)
        hn.seen_bits <<= 1;
    for (void* c : creatures) {
        if (c == av || game::is_puppet(c) || game::is_destroyed(c))
            continue;
        uint32_t species = game::species_instance(c);
        bool dead = game::is_dead(c);
        auto it = g_host_npcs.find(c);
        if (it != g_host_npcs.end() && g_reuse_fix &&
            npc_object_reused(it->second.species, it->second.dead, species, dead)) {
            // the game freed this NPC and built another one in the same object (BUG-0044): the
            // guests drop the old one, the new one gets a new id and a spawn with its species
            retire(it->second.id, c);
            g_host_npcs.erase(it);
            it = g_host_npcs.end();
            g_reused++;
        }
        if (it == g_host_npcs.end()) {
            uint16_t id = alloc_npc_id(
                g_next_id, [](uint16_t i) { return g_host_by_id.count(i) != 0; }, g_id_fix);
            if (!id) // 65534 NPCs at once: not in this world
                continue;
            g_host_npcs[c] = {id, now, species, dead};
            g_host_by_id[id] = c;
        } else {
            it->second.last_seen = now;
            it->second.dead = dead;
        }
        {
            HostNpc& hn = g_host_npcs[c];
            hn.seen_bits |= 1;
            hn.vel_bits = (hn.vel_bits << 1) | (drive::speed(game::velocity(c)) > 0.3f ? 1u : 0u);
            hn.hist_at = (uint8_t)((hn.hist_at + 1) & 63);
            hn.pos_hist[hn.hist_at] = game::position(c);
        }
        g_seen_now.insert(c);
        {   // BUG-0339 diagnostics: an NPC the game still updates at the planet's centre (pool place)
            HostNpc& hn = g_host_npcs[c];
            net::Vec3 p = hn.pos_hist[hn.hist_at];
            bool centre = p.x * p.x + p.y * p.y + p.z * p.z < 2500.f;
            if (centre && !hn.centre_said) {
                net::Vec3 q = hn.pos_hist[(hn.hist_at + 63) & 63];
                H->log(M, SK_WARN, "npc %u (species %08X) is updated at the planet's centre: herd %p, %.0f m from where it "
                       "was a frame before, dead=%d", hn.id, hn.species, game::herd_of(c), net::dist(p, q), (int)hn.dead);
                hn.centre_said = true;
            } else if (!centre)
                hn.centre_said = false;
        }
    }
    pack_spares_partners(creatures); // BUG-0512
    // creatures the game took out of the world (BUG-0274): the creature stage keeps NPCs only around
    // the host's avatar; one farther away goes back to the creature pool (herd cleared, position
    // (0 0 0), no more updates) and the nest spawns it again when it misses a member. It used to keep
    // its id for the 2 s below, so a guest near it kept the proxy where it stood (504 m from the
    // host's object at the planet's centre in the census) and got it again with the next respawn.
    if (g_pool_fix && g_seen_now.size() < g_host_npcs.size())
        retire_pooled();
    if (g_ntrack_on) // coop.npctrack on the host: its own NPCs (the reference for the guest's proxies)
        for (void* c : g_seen_now) {
            net::Vec3 p = game::position(c);
            ntrack_push(g_host_npcs[c].id, c, 4, game::is_dead(c) ? net::NF_DEAD : 0, 0, p, p,
                        drive::speed(game::velocity(c)), now);
        }
    dash_tick(now);
    // forget creatures that stopped updating (destroyed / unloaded)
    for (auto it = g_host_npcs.begin(); it != g_host_npcs.end();) {
        if (now - it->second.last_seen > 2.0) {
            retire(it->second.id, it->first);
            it = g_host_npcs.erase(it);
        } else
            ++it;
    }
    bool send_states = now - g_last_state >= kStateInterval;
    if (send_states)
        g_last_state = now;
    const bool send_snap = now - g_anim_snap_at >= 1.0; // BUG-0464
    if (send_snap)
        g_anim_snap_at = now;
    for (auto& p : C.sess->players()) {
        if (p.id == 0)
            continue;
        auto& known = g_known[p.id];
        net::Vec3 gp;
        float relevance = kRelevance;
        if (C.in_place && !C.in_place(p.id)) { // another world (BUG-0130): its proxies go, sent again later
            for (uint16_t id : known)
                send_despawn(p.id, id);
            known.clear();
            continue;
        }
        net::Vec3 view;
        bool has_view = false;
        if (rts) {
            gp = center;
            relevance = kRtsRelevance;
            has_view = g_rts_views_fix && views::of(p.id, game::mode_id(), now, view);
        } else if (!C.player_pos(p.id, gp)) {
            for (uint16_t id : known)
                send_despawn(p.id, id);
            known.clear();
            continue;
        }
        std::vector<net::NpcState> batch;
        for (void* c : g_seen_now) {
            uint16_t id = g_host_npcs[c].id;
            net::Vec3 cp = game::position(c);
            float d = net::dist(cp, gp);
            float dv = has_view ? net::dist(cp, view) : 1e9f;
            bool k = known.count(id) != 0;
            if (!k && (d < relevance || dv < kRelevance)) {
                send_spawn(p.id, id, c);
                known.insert(id);
                k = true;
            } else if (k && d > relevance + kHysteresis && dv > kRelevance + kHysteresis) {
                send_despawn(p.id, id);
                known.erase(id);
                k = false;
            }
            if (k && send_states)
                batch.push_back(host_state(id, c));
        }
        // unreliable batches that fit in one packet
        size_t per = (net::kMaxPacket - 64) / net::NpcState::kSize;
        for (size_t i = 0; i < batch.size(); i += per) {
            net::Writer w;
            w.u32(net::now_ms());
            size_t n = std::min(per, batch.size() - i);
            w.u8((uint8_t)n);
            for (size_t j = 0; j < n; j++)
                batch[i + j].write(w);
            C.sess->send_unreliable_to(p.id, net::MSG_NPC_STATE, w);
        }
        if (g_anims_fix && !g_anim_out.empty())
            send_anims(p.id, known); // BUG-0464
        if (g_anims_fix && send_snap)
            send_anim_snapshot(p.id, known);
    }
    g_anim_out.clear();
    for (auto it = g_known.begin(); it != g_known.end();) { // guests that left
        bool present = false;
        for (auto& p : C.sess->players())
            if (p.id == it->first)
                present = true;
        it = present ? std::next(it) : g_known.erase(it);
    }
}

static void* proxy_herd(const game::Key& k, uint8_t flags, const net::Vec3& pos, void* species)
{
    void* species_nest = species;
    if (flags & net::NF_FAMILY)
        if (void* h = game::avatar_herd())
            return h;
    // the nest of the species in this world (from the save): by the host's key. A creation of the
    // host's own has a key of its own here (creations::local_species), its nests the host's key
    creations::Key hkey = creations::host_species({k.instance, k.type, k.group});
    if (hkey.instance != k.instance)
        if (void* sp = game::species_by_key({hkey.instance, hkey.type, hkey.group}))
            species_nest = sp;
    uint64_t hk = herd_key({hkey.instance, hkey.type, hkey.group});
    auto lh = g_local_herds.find(hk);
    if (lh != g_local_herds.end()) {
        if (game::object_alive(lh->second)) {
            g_proxies_in_nest++;
            return lh->second;
        }
        g_local_herds.erase(lh);
    }
    // not seen on a local NPC yet (proxies made before the game's NPCs ran an update): the nest
    // herd of the species among the game objects
    struct Find {
        void* species;
        void* found;
    } f{species_nest, nullptr};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* f = (Find*)u;
            if (noun != game::kHerdNoun || !game::object_alive(o) || game::herd_species(o) != f->species ||
                !game::herd_nest(o))
                return true;
            f->found = o;
            return false;
        },
        &f);
    if (f.found) {
        g_local_herds[hk] = f.found;
        g_proxies_in_nest++;
        return f.found;
    }
    auto it = g_herds.find(hk);
    if (it != g_herds.end())
        return it->second;
    void* h = game::create_herd(pos, species);
    if (h)
        g_herds[hk] = h;
    return h;
}

static void destroy_proxy(uint16_t id)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end())
        return;
    if (it->second.c) {
        g_proxy_of.erase(it->second.c);
        if (game::is_destroyed(it->second.c)) // the game removed it already: only forget it
            game::puppet_unregister(it->second.c);
        else
            game::destroy_creature(it->second.c);
    }
    g_proxies.erase(it);
}

// BUG-0300 (see g_listed_fix): reads the noun list only; an object that is not in it is never touched
static void check_listed()
{
    struct Acc {
        std::unordered_set<void*> listed;
    } a;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun == 0x18EB45Eu && game::object_alive(o))
                ((Acc*)u)->listed.insert(o);
            return true;
        },
        &a);
    if (a.listed.empty()) // no list (between worlds): nothing to compare with
        return;
    for (auto& [id, px] : g_proxies) {
        if (!px.c)
            continue;
        if (!a.listed.count(px.c)) {
            g_proxy_unlisted++;
            H->log(M, SK_WARN, "npc %u: the proxy's creature %p is no creature of this world any more (key %08X, %s)%s", id,
                   px.c, px.key.instance, px.killed ? "killed" : "alive",
                   g_listed_fix ? ": made again" : " (npc.proxy_listed off: kept)");
            if (!g_listed_fix)
                continue;
            g_proxy_of.erase(px.c);
            game::puppet_unregister(px.c);
            px.c = nullptr;
            px.drv = drive::State{};
            continue;
        }
        if (!game::species_key(px.c).instance) { // listed without a species: not placed in the world
            if (!px.nospecies_said)
                H->log(M, SK_WARN, "npc %u: the proxy's creature %p has no species (key %08X, herd %p)", id, px.c,
                       px.key.instance, game::herd_of(px.c));
            px.nospecies_said = true;
            g_proxy_nospecies++;
        }
    }
}

static void guest_tick(double now)
{
    void* av = game::avatar();
    // remove local NPCs collected by the update filter
    std::sort(g_kill.begin(), g_kill.end());
    g_kill.erase(std::unique(g_kill.begin(), g_kill.end()), g_kill.end());
    for (void* c : g_kill)
        if (c != av && !game::is_puppet(c) && !game::is_destroyed(c) && !game::destroy_pending(c)) {
            game::destroy_creature(c);
            g_removed_local++;
        }
    g_kill.clear();
    if (!av && !rts_stage()) // the tribe stage has no avatar
        return;
    // the first frames of a world: the update filter meets the game's own NPCs and learns their
    // herds (g_local_herds) before the proxies are made
    // (with the world counter: the avatar of a reloaded world may have the old address, BUG-0119)
    static void* s_av = nullptr;
    static uint32_t s_epoch = 0;
    static double s_av_t0 = 0;
    uint32_t epoch = game::world_epoch();
    if (av != s_av || epoch != s_epoch) {
        s_av = av;
        s_epoch = epoch;
        s_av_t0 = now;
    }
    if (now - s_av_t0 < 1.0)
        return;
    // A proxy the host stopped sending states for while it sends them for the others is not an
    // NPC of the host any more (its id belongs to a world the host has left: before 2026-10-25 the
    // host renumbered its NPCs after its editor or a load without a despawn, and the guest kept
    // the old ones for good: a second, living copy of every NPC, a kill of the host's left it
    // standing). A host that stands still (pause menu) sends no states at all: nothing is dropped.
    if (g_stale_fix && now - g_last_states_rx < 0.5) {
        std::vector<uint16_t> stale;
        for (auto& [id, px] : g_proxies)
            if (now - px.last_rx > 5.0)
                stale.push_back(id);
        for (uint16_t id : stale) {
            destroy_proxy(id);
            g_stale_dropped++;
        }
    }
    if (now - g_listed_at >= 1.0 && game::in_creature_stage()) { // (Creature only: the case seen; Tribe untouched)
        g_listed_at = now;
        check_listed();
    }
    for (auto& [id, px] : g_proxies) {
        net::PlayerState s;
        if (!px.buf.sample(now, s))
            continue;
        const float rv_rep = drive::speed(s.vel); // (coop.npctrack: the speed the host reported)
        if (g_npc_novel)
            s.vel = {0, 0, 0};
        // the guest's own game destroyed the proxy (its tribe killed it, a cleanup): forget the
        // object at once (it is freed later; it was driven every frame after that) and make a new
        // one unless the host says the NPC is dead. NPCs vanished from guests this way in a 4-player
        // tribe run (coverage 26-48 % for 4 minutes).
        if (px.c && game::is_destroyed(px.c)) {
            g_proxy_of.erase(px.c);
            game::puppet_unregister(px.c);
            px.c = nullptr;
            px.drv = drive::State{};
            g_proxy_lost++;
        }
        if (!px.c && (s.flags & net::NF_DEAD) && (px.ever || !g_dead_proxy_fix))
            continue;
        if (!px.c) {
            void* species = game::species_by_key(px.key);
            void* herd = species ? proxy_herd(px.key, px.flags, s.pos, species) : nullptr;
            int fail = !species ? 1 : !herd ? 2 : 0;
            if (!fail) {
                px.c = game::spawn_creature(s.pos, species, herd);
                if (!px.c)
                    fail = 3;
            }
            if (fail) {
                // a species of the host's world this game lacks (the host's own creations and its
                // Sporepedia downloads populate its planet): the host sends it, the next try works
                if (fail == 1)
                    creations::want_species({px.key.instance, px.key.type, px.key.group});
                if (px.fail != fail)
                    H->log(M, SK_WARN, "npc %u: proxy spawn failed (%s) key=%08X/%08X/%08X", id,
                           fail == 1 ? "unknown species" : fail == 2 ? "no herd" : "Create failed", px.key.instance,
                           px.key.type, px.key.group);
                px.fail = fail;
                continue;
            }
            game::puppet_register(px.c); // no local AI
            g_proxy_of[px.c] = id;
            px.ever = true;
            px.tuning_set = 0; // (a new creature: its own Create's tuning)
            px.made_at = now;
            px.made_agl = drive::over_ground(s.pos);
            // BUG-0399: on this game's ground (Tribe: the ground can lie higher than the host's)
            game::teleport(px.c, rts_stage() ? drive::out_of_ground(s.pos) : s.pos, s.rot);
        }
        proxy_tuning(px);
        if (now < px.manual_until) {
            if (px.c && (px.manual_mode & 1) && now - px.manual_last > 0.1) {
                game::set_speed(px.c, px.manual_speed);
                game::move_to(px.c, px.manual_to, 0.3f);
                px.manual_last = now;
            }
            if (px.c && (px.manual_mode & 2))
                game::set_position(px.c, game::position(px.c));
            if (px.c && (px.manual_mode & 4))
                game::set_orientation(px.c, game::orientation(px.c));
            continue;
        }
        const net::Vec3 drawn = game::position(px.c); // where the game drew it (before this frame's driving)
        bool placed = false, walked = false;
        const bool rts = rts_stage();
        const bool walking = g_walk_fix || rts;
        // BUG-0361: the last 0.1 s of a walk is interpolated towards a state that stands (velocity 0): a
        // walked proxy goes on with the speed of its path to the stop point instead of standing there
        net::Vec3 vel = s.vel;
        float walk_speed = -1.f;
        drive::path_update(px.drv, s.pos, now);
        {
            const drive::PathTrack& pt = px.drv.path;
            if (walking && px.kinematic && drive::speed(vel) <= 0.3f) {
                // not faster than it walked: at a stop the target can jump back up to 1 m (the extrapolation
                // past the newest state corrected), which made a 37 m/s "path" and a 0.8 m jump of the proxy
                float ps = drive::speed(pt.v);
                if (ps > 0.5f) {
                    float k = px.walk_sp > 0.3f && ps > px.walk_sp ? px.walk_sp / ps : 1.f;
                    vel = {pt.v.x * k, pt.v.y * k, pt.v.z * k};
                }
            } else if (walking && g_path_fix && drive::speed(vel) <= 0.3f) {
                // BUG-0394: a host NPC that moves without a speed of its own (2.2 m/s at speed 0): driven as
                // standing, its pulls were undone by its locomotion, it fell 2.7 m behind and was teleported
                vel = drive::path_velocity(px.drv);
            } else if (drive::speed(vel) > 0.3f) {
                px.walk_sp = drive::speed(vel);
                // a host NPC that reports its speed but moves slower (pushed by its herd, against a rock, turning):
                // the game walks the proxy at the path's speed, or its walk between two placings runs ahead
                // (Tribe: 0.6-1.4x the path in 77 % of the 0.1 s windows where the path was slower than the speed)
                float ps = std::max(pt.sp_ema * 1.1f, 0.5f);
                if (walking && ps < px.walk_sp)
                    walk_speed = ps; // (only the walk: scaling the placing too made it trail at every start, creature_npc_dash)
            }
        }
        if (!px.killed && ((g_kinematic_fix && game::in_creature_stage()) || (g_walk_tribe_fix && rts)) &&
            drive::speed(vel) > 0.3f && net::dist(game::position(px.c), s.pos) < 8.f) {
            // BUG-0281: a running NPC's proxy follows the host's path itself (see g_kinematic_fix)
            if (walking) { // BUG-0361: the game walks it, placed on the path
                if (!px.kinematic)
                    px.drv.kinematic = false; // a new walk: its first goal at once
                drive::WalkPlace o;
                o.own_height = rts;     // BUG-0329
                // BUG-0399: onto this game's ground, not only at its own height (proxies 1.3-1.7 m under after joins)
                o.ground = rts ? drive::g_out_of_ground_fix : g_walk_ground_fix; // BUG-0406 (Creature)
                o.walk_speed = walk_speed;
                o.snap = 2.5f;
                o.keep_lost = true;
                o.variant = g_walk_variant;
                drive::walk_place(px.c, s.pos, s.rot, vel, px.drv, now, o);
                walked = true;
            } else {
                if (!px.kinematic)
                    game::stop(px.c);
                game::teleport(px.c, s.pos, s.rot);
                *(net::Vec3*)((uint8_t*)px.c + game::kVelOff) = s.vel;
                px.drv.last_step = now;
            }
            px.kinematic = true;
            placed = true;
            // BUG-0334: health, death and the impressed flag below are synced for a running NPC too
            // (0.7.3.2's first npc.kinematic went on to the next proxy here: a fleeing NPC kept its
            // old health on the guest and did not die there until it stopped).
            // `coop.fix npc.kinematic_sync off`: that first version.
            if (!g_kinematic_sync_fix) {
                ntrack_row(id, px, s, drawn, walked ? 3 : 1, now, rv_rep);
                continue;
            }
        }
        if (!placed && px.kinematic) { // it stands again: the driving takes over from here
            px.kinematic = false;
            if (px.drv.kinematic) // walked (npc.walk): a MoveTo to where it stops, as the partner's puppet
                drive::walk_end(px.c, s.pos, px.drv, now);
            else
                *(net::Vec3*)((uint8_t*)px.c + game::kVelOff) = {0, 0, 0};
        }
        if (placed) {
            // placed on the host's path this frame: no driving
        } else if (!px.killed) {
            const uint32_t gr = px.drv.grounded;
            drive::step(px.c, s.pos, s.rot, s.vel, px.drv, now, 0, true, 8.f, g_stall_fix, g_trail_fix, rts_stage(), // own height in Tribe (BUG-0329)
                        rts_stage() && drive::g_out_of_ground_fix); // and never under the ground (BUG-0399)
            if (px.drv.grounded != gr && px.drv.grounded <= 3) // (diagnostics: the fix at work, the first times)
                H->log(M, SK_INFO, "npc: proxy %u (species %08X) was %.2f m under this game's ground, made %.1f s ago at %.2f m "
                       "over it: put onto it (npc.proxy_ground)", id, px.key.instance, -px.drv.grounded_from, now - px.made_at,
                       px.made_agl);
        }
        else { // a corpse lies where the host has it: the guest's dead proxy shot off into the sky
               // (~2.5 km/s away from the planet; driven, it stayed ~80 m up: play_tribe.py)
            *(net::Vec3*)((uint8_t*)px.c + game::kVelOff) = {0, 0, 0};
            game::teleport(px.c, s.pos, s.rot);
        }
        if ((s.flags & net::NF_DEAD) && !px.killed) {
            px.killed = true;
            // play the death locally with a just-lethal hit: the knockback grows with the damage
            // (1e6 threw the corpse ~150 m up, it hung 15-80 m away from the host's corpse for
            // the rest of its life: play_creature.py 2026-10-23)
            game::apply_damage(px.c, std::max(game::health(px.c), 0.f) + 0.5f, 0, nullptr);
            // the death itself is the behaviour tree's (0xD85B80): a proxy has no AI and stood on
            // with 0 health, alive for the game (not edible, "still alive" for the guest; player
            // report 2026-10-25, docs/re/creature.md 14)
            if (g_death_fix) {
                game::play_death(px.c);
                g_deaths_played++;
            }
        } else if (!px.killed && s.health > 0)
            game::set_health(px.c, s.health);
        if (!px.killed) // the host's NPC was impressed (by anybody): the proxy shows it too
            game::set_impressed(px.c, (s.flags & net::NF_IMPRESSED) != 0);
        ntrack_row(id, px, s, drawn, px.killed ? 2 : walked ? 3 : placed ? 1 : 0, now, rv_rep);
    }
}

// Our creatures (the partner's puppet, NPC proxies) live in herds of our own (CreateHerd(.., 0, 0, 0,
// 0): no archetype, no nest, not the avatar's) and have no archetype tuning (+0xE84). A save made
// while they existed keeps them: loaded again (after a restart of the game) such a puppet is a
// plain game creature of the partner's species, without tuning and with its AI on, and the first
// attack on it read (+0xE84)->+0x32C in CreatureStageEventHandler (0xD48634: the host crashed 3
// times after killing "these guys", 2026-10-25; docs/re/creature.md 4). Every such creature that is
// not one of ours now is removed (also from saves that already hold them). Herds of the game always
// have an archetype (137 herds on Scalinus, none without), so a creature of the game is never one.
static uint32_t g_strays_removed = 0;
static double g_strays_at = 0;

static bool own_kind_herd(void* h)
{
    return h && !IsBadReadPtr(h, 0x21C) && game::object_alive(h) && game::noun_id(h) == game::kHerdNoun &&
           *(uint32_t*)((uint8_t*)h + 0x88) == 0 && !game::herd_nest(h) && !*(uint8_t*)((uint8_t*)h + 0x84);
}

static void remove_saved_strays(double now)
{
    if (!g_strays_on || now - g_strays_at < 1.0)
        return;
    g_strays_at = now;
    struct Acc {
        std::vector<void*> v;
        void* av;
    } a{{}, game::avatar()};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* a = (Acc*)u;
            if (noun != 0x18EB45Eu || o == a->av || !game::object_alive(o) || IsBadReadPtr(o, 0x1680) ||
                *(void**)((uint8_t*)o + 0xE84) || !game::species_profile(o) || game::is_puppet(o) || is_proxy(o) ||
                game::destroy_pending(o))
                return true;
            if (own_kind_herd(game::herd_of(o)))
                a->v.push_back(o);
            return true;
        },
        &a);
    for (void* c : a.v) {
        H->log(M, SK_INFO, "removed a creature of species %08X without tuning in a herd like ours (from a save made "
               "while a partner's puppet existed)", game::species_key(c).instance);
        game::destroy_creature(c);
        g_strays_removed++;
    }
}

// A respawn (0xD40FA0) or a hatch puts the avatar back into the nest as a hatchling whose behaviour
// tree waits (tree 0x9413EDEB) for a family member of the nest: that NPC's tree walks over, plays the
// greeting with it and makes it grow up (BT node 0xD64A30, state 9: 0xC04F30(1), 0xD52DF0(1)). A
// guest has no family of its own (the update filter removes its local NPCs, the host's mirrored NPCs
// have no AI), so after a death the guest stayed a hatchling for good: half the health, no control,
// the hatch pose with the egg shell on both screens (player report 2026-10-25, docs/re/creature.md
// 13). The guest does what the greeting does once its hatchling has waited a moment; the host only
// when its own family did not come for much longer than it takes (~13 s), e.g. when the greeter
// picked was a partner's puppet (puppets run no AI either).
static bool g_hatch_on = true; // tests: hatch help on/off
COOP_FIX("npc.hatch", "BUG-0014", g_hatch_on, "creature: a waiting hatchling grows up without its family's greeting");
static double g_hatch_wait = 0, g_hatch_last = 0;
static uint32_t g_hatch_helped = 0;
void set_hatch_help(bool on) { g_hatch_on = on; }
constexpr double kGuestHatchWait = 4.0, kHostHatchWait = 40.0;

static void hatch_tick(double now, bool simulating)
{
    double dt = now - g_hatch_last;
    g_hatch_last = now;
    void* av = game::avatar();
    if (!g_hatch_on || !C.sess || !C.sess->connected() || !game::in_creature_stage() || !av || game::is_dead(av) ||
        !game::hatchling_waiting(av)) {
        g_hatch_wait = 0;
        return;
    }
    if (!simulating || world::cinematic_playing() || dt > 1.0) // paused, a cinematic, a caption, a long frame
        return;
    g_hatch_wait += dt;
    bool no_family = is_guest() && g_enabled && !g_host_away; // the guest's own NPCs are removed
    if (g_hatch_wait < (no_family ? kGuestHatchWait : kHostHatchWait))
        return;
    g_hatch_wait = 0;
    if (game::grow_up(av)) {
        g_hatch_helped++;
        H->log(M, SK_INFO, "creature: the hatchling waited for its family %s: it grows up now",
               no_family ? "(a guest has none of its own)" : "too long");
    }
}

// BUG-0180: when the host leaves to the main menu the guest drops the host's frozen proxies and its
// own game runs the planet again, but the nests had lost their members to the update filter and
// brought them back one at a time (0-1 own NPCs near the guest 30 s later, coop_exit_menu.py). Every
// living proxy in a nest herd of the game is now replaced by a creature of the game of the same
// species in the same herd at the same place (cCreatureAnimal::Create, as the nest does), so the
// guest keeps the animals it saw. Proxies in herds of our own (no nest) are only removed, as before.
// `coop.fix npc.away_locals off`: the proxies just go (0.7.3.1).
static bool g_away_locals_fix = true;
COOP_FIX("npc.away_locals", "BUG-0180", g_away_locals_fix, "creature: the host leaves: a guest's proxies become NPCs of its own game");
static uint32_t g_away_locals = 0;
// what the guest showed of the host's NPCs (the last non-empty set, every 0.5 s): the host's world
// change (its menu) despawns them all a few seconds before the guest learns that the host is away
struct AwaySnap {
    game::Key key;
    net::Vec3 pos;
    net::Quat rot;
};
static std::vector<AwaySnap> g_away_snap;
static double g_away_snap_at = -100;
static void away_snapshot(double now)
{
    static double s_last = -100;
    if (now - s_last < 0.5)
        return;
    s_last = now;
    std::vector<AwaySnap> v;
    for (auto& [id, px] : g_proxies) {
        if (!px.c || px.killed || !game::object_alive(px.c) || game::is_dead(px.c) || game::destroy_pending(px.c))
            continue;
        void* h = game::herd_of(px.c);
        if (!h || !game::herd_nest(h)) // a herd of our own: no nest of the game to go on in
            continue;
        v.push_back({px.key, game::position(px.c), game::orientation(px.c)});
    }
    if (v.empty()) // the host's world change took them (its menu): keep the last ones seen
        return;
    g_away_snap.swap(v);
    g_away_snap_at = now;
}
static void* nest_herd_of(void* species)
{
    struct Find {
        void* species;
        void* found;
    } f{species, nullptr};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* f = (Find*)u;
            if (noun != game::kHerdNoun || !game::object_alive(o) || game::herd_species(o) != f->species ||
                !game::herd_nest(o))
                return true;
            f->found = o;
            return false;
        },
        &f);
    return f.found;
}
static void away_locals(double now)
{
    unsigned made = 0, skipped = 0;
    if (now - g_away_snap_at > 15.0) // the host's NPCs went long ago (another world, a load)
        skipped = (unsigned)g_away_snap.size();
    else
        for (auto& sn : g_away_snap) {
            void* species = game::species_by_key(sn.key);
            void* h = species ? nest_herd_of(species) : nullptr;
            void* c = h ? game::spawn_creature(sn.pos, species, h) : nullptr;
            if (!c) {
                skipped++;
                continue;
            }
            game::teleport(c, sn.pos, sn.rot);
            made++;
        }
    g_away_snap.clear();
    g_away_locals += made;
    H->log(M, SK_INFO, "creature: the host left: %u of its NPCs shown here go on as NPCs of this game (%u not)", made,
           skipped);
}

static void tick_frame(double now)
{
    const auto& creatures = game::creatures_updated(); // must run every frame
    game::swap_guard_tick();                            // outside every NPC tick here
    game::destroy_deferred_tick();                      // creatures removed while a cinematic played
    if (game::in_creature_stage())
        remove_saved_strays(now);
    hatch_tick(now, !creatures.empty());
    // creature stage only: adventure creatures are scripted by the adventure on every machine
    if (!g_enabled || !C.sess || !npc_stage()) {
        g_anim_out.clear();
        return;
    }
    if (is_host()) {
        host_tick(now, creatures);
        g_anim_out.clear(); // (host_tick returns early in some frames: never send an old frame's)
    } else if (is_guest()) {
        uint32_t n = 0;
        for (void* c : creatures)
            n += !game::is_puppet(c) && c != game::avatar();
        g_local_alive = n;
        bool away = C.host_away && C.host_away();
        if (away && !g_host_away) { // the host's NPCs stand frozen and nothing removes them: go
            std::vector<uint16_t> ids;
            for (auto& [id, px] : g_proxies)
                ids.push_back(id);
            g_away_proxies += (uint32_t)ids.size();
            if (g_away_locals_fix)
                away_locals(now);
            for (uint16_t id : ids)
                destroy_proxy(id);
            g_herds.clear();
        }
        g_host_away = away;
        if (!away) {
            guest_tick(now);
            resync_tick(now); // BUG-0487
            if (g_away_locals_fix && game::in_creature_stage())
                away_snapshot(now);
        }
    }
}

// BUG-0082: g_kill holds pointers of this frame's update only. Every frame that does not reach
// guest_tick (the frame the host goes away, NPC sync off, another stage) drops them: they used to
// wait, possibly minutes, until the host was back, and guest_tick then destroyed objects the game
// had freed or reused meanwhile. `coop.npc killfix off` keeps the old behaviour (tests).
static bool g_kill_clear = true;
COOP_FIX_UNSAFE("npc.kill", "BUG-0082", g_kill_clear, "creature: the guest's kill list is cleared every frame (coop.npc killfix)");
void set_kill_clear(bool on) { g_kill_clear = on; }

void tick(double now)
{
    tick_frame(now);
    if (g_kill_clear)
        g_kill.clear(); // guest_tick empties it itself; anything left here is from a skipped frame
}

void debug_kill_hold()
{
    // tests: what the update filter collects on the frame the host goes away (the avatar is a safe
    // stand-in: guest_tick never destroys it)
    if (void* av = game::avatar())
        g_kill.push_back(av);
}

void on_world_change()
{
    // the host's NPCs of the old world are gone and the new ones get new ids: the guests drop the
    // old ones (they mirrored them for good before 2026-10-25: a living copy of every NPC next to
    // the new one after each editor or load of the host)
    if (is_host() && g_world_despawn_fix)
        for (auto& [g, ks] : g_known)
            for (uint16_t id : ks)
                send_despawn(g, id);
    for (auto& [id, px] : g_proxies) {
        if (px.c)
            game::puppet_unregister(px.c);
        px.c = nullptr;
        px.killed = false;
        px.drv = drive::State{};
    }
    g_proxy_of.clear();
    g_unknown_ids.clear();
    g_herds.clear();
    g_local_herds.clear();
    g_kill.clear();
    g_host_npcs.clear();
    g_host_by_id.clear();
    g_ai_target.clear();
    teamplay::reset();
    for (auto& [g, ks] : g_known)
        ks.clear(); // host re-sends spawns
}

// BUG-0125: host_tick forgets what a guest mirrors only while the simulation runs; a guest that
// left and joined again (the same id) while the host was paused kept the old set and got no spawns
static bool g_known_fix = true;
COOP_FIX("npc.known", "BUG-0125", g_known_fix, "creature: a guest that left forgets its known NPCs while the host is paused too");
void set_known_fix(bool on) { g_known_fix = on; }
void on_player_leave(uint8_t id)
{
    if (g_known_fix)
        g_known.erase(id);
}

void on_session_end()
{
    std::vector<uint16_t> ids;
    for (auto& [id, px] : g_proxies)
        ids.push_back(id);
    for (uint16_t id : ids)
        destroy_proxy(id);
    g_herds.clear();
    g_local_herds.clear();
    g_host_npcs.clear();
    g_host_by_id.clear();
    g_known.clear();
    g_kill.clear();
    g_ai_target.clear();
    g_unknown_ids.clear();
    teamplay::reset();
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    double now = H->time_seconds();
    switch (type) {
    case net::MSG_NPC_SPAWN: {
        net::NpcSpawn sp;
        if (!is_guest() || !sp.read(r))
            return;
        destroy_proxy(sp.net_id);
        Proxy& px = g_proxies[sp.net_id];
        // the host's own creations have keys of their own here (creations::local_species)
        creations::Key lk = creations::local_species({sp.key[0], sp.key[1], sp.key[2]});
        px.key = {lk.instance, lk.type, lk.group};
        px.flags = sp.flags;
        px.health = sp.health;
        px.max_health = sp.max_health;
        px.tuning = r.left() >= 4 ? r.u32() : 0; // BUG-0462 trailer
        net::PlayerState s;
        // the spawn carries no host time: older than any state. The guest's own clock here made
        // every state older than this snapshot whenever the guest's clock was ahead of the host's
        // (another PC, another Wine bottle): they were all dropped and the NPC stood still
        s.time_ms = 0;
        s.pos = sp.pos;
        s.rot = sp.rot;
        s.flags = sp.flags;
        s.health = sp.health;
        px.buf.delay = 0.15;
        px.buf.push(s, now);
        px.last_rx = now;
        break;
    }
    case net::MSG_NPC_DESPAWN: {
        if (!is_guest())
            return;
        uint16_t id = r.u16();
        destroy_proxy(id);
        break;
    }
    case net::MSG_NPC_STATE: {
        if (!is_guest())
            return;
        uint32_t t = r.u32();
        uint8_t n = r.u8();
        bool whole = r.ok() && n > 0;
        for (int i = 0; i < n && r.ok(); i++) {
            net::NpcState st;
            if (!st.read(r)) {
                whole = false;
                break;
            }
            auto it = g_proxies.find(st.net_id);
            if (it == g_proxies.end()) {
                if (g_resync_fix && !g_unknown_ids.count(st.net_id) && g_unknown_ids.size() < 4000)
                    g_unknown_ids[st.net_id] = 0; // BUG-0487: ask for its spawn (resync_tick)
                continue;
            }
            net::PlayerState s;
            s.time_ms = t;
            s.pos = st.pos;
            s.rot = st.rot;
            s.vel = st.vel;
            s.health = st.health;
            s.flags = st.flags;
            it->second.buf.push(s, now);
            it->second.last_rx = now;
        }
        if (whole) // the host sends states (an empty or cut batch says nothing about stale proxies)
            g_last_states_rx = now;
        break;
    }
    case net::MSG_NPC_ANIM: { // guest: the host's NPCs' behaviour animations (BUG-0464)
        if (!is_guest())
            return;
        uint8_t kind = r.u8();
        uint8_t n = r.u8();
        if (kind == 1) { // what the host's NPCs play now: the proxy plays it too when it plays another
            for (int i = 0; i < n && r.ok(); i++) {
                uint16_t id = r.u16();
                uint32_t anim = r.u32();
                if (!r.ok())
                    break;
                g_anims_snap_rx++;
                auto it = g_proxies.find(id);
                if (!g_anims_fix || it == g_proxies.end())
                    continue;
                Proxy& px = it->second;
                uint32_t cur = 0;
                if (!px.c || game::is_destroyed(px.c) || now - px.anim_fix_at < 2.0 || now - px.anim_rx_at < g_anim_snap_quiet ||
                    !game::drawn_state(px.c, nullptr, &cur) || cur == anim)
                    continue;
                px.anim_fix_at = now; // (at most every 2 s: never restarts an animation that will not take)
                if (game::play_behaviour_anim(px.c, anim, 0, 1, -1))
                    g_anims_snap_played++;
            }
            break;
        }
        if (kind != 0)
            return;
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            uint32_t anim = r.u32();
            uint8_t how = r.u8();
            uint8_t a = r.u8();
            int32_t b = (int32_t)r.u32();
            if (!r.ok())
                break;
            g_anims_rx++;
            if (!g_anims_fix)
                continue;
            auto it = g_proxies.find(id);
            void* c = it == g_proxies.end() ? nullptr : it->second.c;
            if (!c || it->second.killed || game::is_destroyed(c) || game::is_dead(c)) {
                g_anims_noproxy++;
                continue;
            }
            it->second.anim_rx_at = now;
            if (game::play_behaviour_anim(c, anim, how, a, b))
                g_anims_played++;
            else
                g_anims_failed++;
        }
        break;
    }
    case net::MSG_NPC_RESYNC: { // host: a guest gets states of NPCs it has no spawn of (BUG-0487)
        if (!is_host() || !g_resync_fix)
            return;
        uint8_t n = r.u8();
        auto& known = g_known[from];
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            auto it = g_host_by_id.find(id);
            // only what this guest mirrors (the host sends it states): others come with the relevance rule
            if (!r.ok() || it == g_host_by_id.end() || !known.count(id) || !g_seen_now.count(it->second))
                continue;
            send_spawn(from, id, it->second);
            g_resync_sent++;
        }
        break;
    }
    case net::MSG_DAMAGE_REQ: { // host: a guest hit one of our NPCs
        net::DamageReq q;
        if (!is_host() || !q.read(r))
            return;
        auto it = g_host_by_id.find(q.net_id);
        if (it == g_host_by_id.end() || !g_seen_now.count(it->second))
            return;
        void* npc = it->second;
        if (creature_hp::drop_partner_hit(npc)) // BUG-0374: the host's pack / family (npc.party_peace)
            break;
        void* attacker = C.puppet_of(from);
        float dmg = teamplay::on_player_hit(npc, from, net::sane_damage(q.damage, kMaxHit));
        // kill credit: the game pays DNA, goal progress and the kill popup only for a kill by the
        // player's own avatar (TakeDamage 0xC07A78: attacker spatial vt 0x58, docs/re/creature.md 2).
        // A guest's puppet is not it, so a guest's kill paid nothing. The pack shares its progress:
        // the lethal hit lands as the host avatar's (one hit, the target dies, no aggro to move).
        void* av = game::avatar();
        if (av && !game::is_dead(npc) && game::health(npc) > 0 && game::health(npc) <= dmg) {
            attacker = av;
            g_kills_credited++;
            dna_ledger::g_credit.kill(from, H->time_seconds()); // the DNA it pays is that guest's hunt
        }
        game::apply_damage(npc, dmg, q.type, attacker);
        g_hits_applied++;
        break;
    }
    case net::MSG_DAMAGE: { // guest: the host says we were hit
        net::Damage d;
        if (!is_guest() || !d.read(r) || d.player != C.sess->my_id())
            return;
        void* av = game::avatar();
        if (!av)
            return;
        void* attacker = nullptr;
        auto it = g_proxies.find(d.attacker);
        if (it != g_proxies.end())
            attacker = it->second.c;
        float dmg = teamplay::on_own_damage(av, d.damage);
        // (the host's family flag is all a guest knows of the host's party; the host's line has the pack)
        log_hit(C.sess->my_id(), attacker, dmg, "from the host", true);
        game::apply_damage(av, dmg, d.type, attacker);
        g_damage_in++;
        break;
    }
    }
}

bool damage_filter(void* c, float& damage, int type, void* attacker)
{
    if (!g_enabled || !C.sess || !C.sess->connected())
        return true;
    void* av = game::avatar();
    if (is_guest()) {
        auto it = g_proxy_of.find(c);
        if (it != g_proxy_of.end()) { // hit on a host NPC: the host applies it
            if (attacker == av && social::drop_social_hit(it->second))
                return false; // BUG-0021: the social stance does not attack
            if (attacker == av) {
                net::DamageReq q;
                q.net_id = it->second;
                q.damage = damage;
                q.type = type;
                net::Writer w;
                q.write(w);
                C.sess->send_reliable_to(0, net::MSG_DAMAGE_REQ, w);
                g_hits_sent++;
            }
            return false;
        }
        if (game::is_puppet(c))
            return false; // no friendly fire on other players
        if (c == av) {
            damage = teamplay::on_own_damage(av, damage);
            log_hit(C.sess->my_id(), attacker, damage, "");
        }
        return true;
    }
    if (is_host()) {
        if (game::mode_id() == modes::kTribe)
            tribe::note_animal_hit(c, attacker);
        if (c == av) {
            damage = teamplay::on_own_damage(av, damage);
            log_hit(0, attacker, damage, "");
            return true;
        }
        if (!game::is_puppet(c)) {
            if (attacker == av)
                damage = teamplay::on_player_hit(c, 0, damage);
            return true;
        }
        // a guest's puppet was hit: forward real NPC hits to that guest
        if (creature_hp::drop_party_hit(attacker)) { // BUG-0374: never from the host's pack / family
            for (auto& p : C.sess->players())
                if (p.id != 0 && C.puppet_of(p.id) == c)
                    log_hit(p.id, attacker, 0, "dropped (npc.party_peace)");
            return false;
        }
        for (auto& p : C.sess->players()) {
            if (p.id == 0 || C.puppet_of(p.id) != c)
                continue;
            if (attacker && attacker != av && !game::is_puppet(attacker)) {
                net::Damage d;
                d.player = p.id;
                d.damage = damage;
                d.type = type;
                auto it = g_host_npcs.find(attacker);
                d.attacker = it != g_host_npcs.end() ? it->second.id : 0xFFFF;
                net::Writer w;
                d.write(w);
                C.sess->send_reliable_to(p.id, net::MSG_DAMAGE, w);
                log_hit(p.id, attacker, damage, "forwarded");
            }
        }
        return false; // puppets mirror the guest's own health
    }
    return true;
}

std::string trace_line(const net::Vec3& me, float radius)
{
    unsigned n_near = 0, total = 0, dead = 0;
    float nearest = -1;
    auto count = [&](const net::Vec3& p, bool is_dead) {
        total++;
        float d = net::dist(p, me);
        if (d > radius)
            return;
        n_near++;
        dead += is_dead;
        if (nearest < 0 || d < nearest)
            nearest = d;
    };
    if (is_host()) {
        for (auto& [c, hn] : g_host_npcs)
            if (g_seen_now.count(c)) // alive this frame: its position is readable
                count(game::position(c), hn.dead);
    } else {
        for (auto& [id, px] : g_proxies)
            if (!px.buf.empty())
                count(px.buf.latest().pos, px.killed);
    }
    char b[160];
    snprintf(b, sizeof b, "npcs %s: %u within %.0f m (%u dead), nearest %.0f m, %u in all", is_host() ? "simulated" : "mirrored",
             n_near, radius, dead, nearest, total);
    return b;
}

std::string status()
{
    char b[768];
    if (is_host()) {
        size_t known = 0;
        for (auto& [g, ks] : g_known)
            known += ks.size();
        size_t on_partner = 0;
        for (auto& [c, pup] : g_ai_target)
            on_partner += g_seen_now.count(c) != 0;
        snprintf(b, sizeof b, "npc(host): tracked=%u alive=%u mirrored=%u hits_applied=%u guest_kills=%u strays_removed=%u "
                 "hatch_helped=%u partner_targets=%d on_partner=%u partner_picks=%u ai_swaps=%u swaps_restored=%u ledger_restored=%u reused=%u hunt_targets=%u pooled=%u\n",
                 (unsigned)g_host_npcs.size(), (unsigned)g_seen_now.size(), (unsigned)known, g_hits_applied,
                 g_kills_credited, g_strays_removed, g_hatch_helped, (int)g_partner_targets, (unsigned)on_partner,
                 g_ai_partner_picks, game::ai_swaps(), game::swaps_restored(), game::swap_ledger_restored(), g_reused, (unsigned)teamplay::hit_targets(), g_pooled_retired);
        char b2[128]; // BUG-0512
        snprintf(b2, sizeof b2, "npc(host) pack: partner_targets_cleared=%u partner_stimuli_cleared=%u\n", g_pack_targets_cleared,
                 g_pack_stimuli_cleared);
        return std::string(b) + b2 + game::destroy_deferred_status() + "\n";
    } else {
        unsigned spawned = 0;
        for (auto& [id, px] : g_proxies)
            spawned += px.c != nullptr;
        snprintf(b, sizeof b, "npc(guest): proxies=%u spawned=%u local_removed=%u hits_sent=%u damage_in=%u lost=%u host_away=%d "
                 "away_dropped=%u local_alive=%u in_nest=%u nests=%u stale_dropped=%u strays_removed=%u hatch_helped=%u "
                 "deaths_played=%u kill_held=%u away_locals=%u unlisted=%u nospecies=%u\n",
                 (unsigned)g_proxies.size(), spawned, g_removed_local, g_hits_sent, g_damage_in, g_proxy_lost, (int)g_host_away,
                 g_away_proxies, g_local_alive, g_proxies_in_nest, (unsigned)g_local_herds.size(), g_stale_dropped,
                 g_strays_removed, g_hatch_helped, g_deaths_played, (unsigned)g_kill.size(), g_away_locals, g_proxy_unlisted,
                 g_proxy_nospecies);
    }
    return b + game::destroy_deferred_status() + "\n";
}

} // namespace npc

namespace npc {

// a fingerprint of a creature's body as the game built it: its abilities (count, type and level of
// each; they come from the creation's parts). Two creations under one key give different prints
// (tests: the guest must build the host's creation, not its own of the same key)
static uint32_t body_print(void* c)
{
    if (!c || IsBadReadPtr(c, 0x1680))
        return 0;
    int n = skg::vcall<int>(c, 0xB0 / 4);
    uint32_t h = 0x811C9DC5u ^ (uint32_t)n;
    for (int i = 0; i < n && i < 64; i++) {
        uint8_t* a = skg::vcall<uint8_t*>(c, 0xB4 / 4, i);
        uint32_t v[2] = {a && !IsBadReadPtr(a, 0x14) ? *(uint32_t*)(a + 8) : 0u, a && !IsBadReadPtr(a, 0x14) ? *(uint32_t*)(a + 0xC) : 0u};
        for (int j = 0; j < 8; j++)
            h = (h ^ ((uint8_t*)v)[j]) * 0x01000193u;
    }
    return h;
}

std::string list()
{
    std::string out;
    char b[640];
    void* av = game::avatar();
    net::Vec3 ap = av ? game::position(av) : net::Vec3{};
    if (is_host()) {
        double now = H->time_seconds();
        for (auto& [c, hn] : g_host_npcs) {
            if (!g_seen_now.count(c)) { // maybe freed already: no field reads
                snprintf(b, sizeof b, "  #%u %p not updated for %d ms\n", hn.id, c, (int)((now - hn.last_seen) * 1000));
                out += b;
                continue;
            }
            net::Vec3 p = game::position(c);
            // " DEAD" right after dist: tests match "dist=N( DEAD)?"
            snprintf(b, sizeof b, "  #%u %p species=%08X hp=%.2f dist=%.1f%s pos=(%.1f %.1f %.1f) spd=%.1f vel=(%.1f %.1f %.1f) upd=%d/64 velfr=%d/64 moved64=%.1f%s%s%s body=%08X impressed=%d agl=%.2f\n", hn.id,
                     c, game::species_key(c).instance, game::health(c), net::dist(p, ap), game::is_dead(c) ? " DEAD" : "",
                     p.x, p.y, p.z, drive::speed(game::velocity(c)), game::velocity(c).x, game::velocity(c).y, game::velocity(c).z,
                     __builtin_popcountll(hn.seen_bits), __builtin_popcountll(hn.vel_bits),
                     net::dist(hn.pos_hist[hn.hist_at], hn.pos_hist[(hn.hist_at + 1) & 63]), game::pack_member(c) ? " pack" : "",
                     // BUG-0390: the avatar's herd (family): a partner's hit on it is dropped (npc.party_peace)
                     game::herd_of(c) && game::herd_of(c) == game::avatar_herd() ? " family" : "",
                     g_ai_target.count(c) ? " on_partner" : "", body_print(c), (int)game::impressed(c), drive::over_ground(p));
            out += b;
        }
    } else {
        double now = H->time_seconds();
        for (auto& [k, h] : g_local_herds) {
            snprintf(b, sizeof b, "  nest %08X/%08X herd=%p alive=%d noun=%08X\n", (uint32_t)k, (uint32_t)(k >> 32), h,
                     (int)game::object_alive(h), game::noun_id(h));
            out += b;
        }
        for (auto& [id, px] : g_proxies) {
            net::PlayerState st;
            bool have = px.buf.sample(now, st);
            float err = px.c && have ? net::dist(game::position(px.c), st.pos) : -1.f;
            net::Vec3 at = px.c ? game::position(px.c) : net::Vec3{};
            snprintf(b, sizeof b, "  #%u species=%08X %s hp=%.2f dist=%.1f err=%.2f age=%.2f%s want=(%.1f %.1f %.1f) at=(%.1f %.1f %.1f) "
                     "agl=%.2f want_agl=%.2f made_agl=%.2f grounded=%u rx=%d/s spd=%.1f vel=(%.1f %.1f %.1f) own=%.1f tp=%u stall=%u trail=%u nudge=%u/%u puppet=%d ptr=%p herd=%p body=%08X"
                     " game_dead=%d impressed=%d\n", // (the game's own flags of the proxy, tests: BUG-0383, w33)
                     id, px.key.instance, px.c ? "spawned" : "pending", px.c ? game::health(px.c) : 0.f,
                     px.c ? net::dist(at, ap) : -1.f, err, px.buf.age(now),
                     px.killed ? " DEAD" : "", st.pos.x, st.pos.y, st.pos.z, at.x, at.y, at.z, px.c ? drive::over_ground(at) : 0.f,
                     have ? drive::over_ground(st.pos) : 0.f, px.made_agl, px.drv.grounded, px.buf.received_in(1.0), drive::speed(st.vel), st.vel.x, st.vel.y, st.vel.z,
                     px.c ? drive::speed(game::velocity(px.c)) : 0.f, px.drv.teleports, px.drv.stalls, px.drv.trails, px.drv.nudges_kept,
                     px.drv.nudges, px.c ? (int)game::is_puppet(px.c) : -1, px.c, px.c ? game::herd_of(px.c) : nullptr, body_print(px.c),
                     px.c ? (int)game::is_dead(px.c) : -1, px.c ? (int)game::impressed(px.c) : -1);
            out += b;
        }
    }
    return out;
}

// debug: the living creature objects (all, or those of one species, or only those without archetype
// tuning +0xE84): ours (puppets, proxies) and any other (a creature brought back by a save made while
// ours existed, docs/re/creature.md 4)
std::string creatures(bool untuned_only, uint32_t species)
{
    struct Acc {
        std::string out;
        unsigned n = 0, untuned = 0, ours = 0, idle = 0;
        void* av;
        bool untuned_only;
        uint32_t species;
    } a{"", 0, 0, 0, 0, game::avatar(), untuned_only, species};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* a = (Acc*)u;
            if (noun != 0x18EB45Eu || !game::object_alive(o) || IsBadReadPtr(o, 0x1680))
                return true;
            void* e84 = *(void**)((uint8_t*)o + 0xE84);
            if (!game::species_profile(o)) { // a creature object without a species: not in the world (pool)
                a->idle++;
                return true;
            }
            if ((a->untuned_only && e84) || (a->species && game::species_key(o).instance != a->species))
                return true;
            void* h = game::herd_of(o);
            bool ours = game::is_puppet(o) || g_proxy_of.count(o);
            bool hr = h && !IsBadReadPtr(h, 0x21C);
            char b[352];
            net::Vec3 p = game::position(o);
            snprintf(b, sizeof b, "  creature %p species=%08X prof=%p e84=%p herd=%p arch=%08X nest=%p flags=%08X body=%08X%s%s%s%s pos=(%.1f %.1f %.1f)\n", o,
                     game::species_key(o).instance, game::species_profile(o), e84, h, hr ? *(uint32_t*)((uint8_t*)h + 0x88) : 0u,
                     hr ? game::herd_nest(h) : nullptr, *(uint32_t*)((uint8_t*)o + 0xB58), body_print(o), ours ? " ours" : "",
                     o == a->av ? " avatar" : "", game::is_dead(o) ? " DEAD" : "", e84 ? "" : " untuned", p.x, p.y, p.z);
            a->out += b;
            a->n++;
            a->untuned += !e84;
            a->ours += ours && !e84;
            return true;
        },
        &a);
    char b[160];
    snprintf(b, sizeof b, "creatures %u: untuned %u (ours %u, other %u); %u without a species\n", a.n, a.untuned,
             a.ours, a.untuned - a.ours, a.idle);
    return b + a.out;
}

// debug: the herds (noun 0x01BE418E) without an archetype (ours: CreateHerd(.., 0, 0, 0, 0)) with
// species profile, target size, enabled flag, nest; the game's own herds are only counted
std::string herds(bool all)
{
    struct Acc {
        std::string out;
        unsigned all = 0, plain = 0;
        bool every;
        net::Vec3 at;
    } a;
    a.every = all;
    void* av = game::avatar();
    a.at = av ? game::position(av) : net::Vec3{};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun != game::kHerdNoun || !game::object_alive(o) || IsBadReadPtr(o, 0x21C))
                return true;
            auto* a = (Acc*)u;
            a->all++;
            if (a->every) { // every herd: archetype, species key, home and its distance from the avatar
                auto* home = (float*)((uint8_t*)o + 0x34);
                void* sp = game::herd_species(o);
                uint32_t inst = sp && !IsBadReadPtr(sp, 0x10) ? *(uint32_t*)((uint8_t*)sp + 0xC) : 0;
                // enabled (+0x218), members (+0x40 / +0x44) and how many of them stand in the world
                // (not in the creature pool at the planet's centre): BUG-0280 tests in Tribe
                int members = 0, in_world = 0, near_home = 0;
                void** m0 = *(void***)((uint8_t*)o + 0x40);
                void** m1 = *(void***)((uint8_t*)o + 0x44);
                if (m0 && m1 > m0 && m1 - m0 < 64 && !IsBadReadPtr(m0, (size_t)((uint8_t*)m1 - (uint8_t*)m0)))
                    for (void** m = m0; m != m1; ++m) {
                        members++;
                        if (*m && game::object_alive(*m) && net::dist(game::position(*m), net::Vec3{}) > 50.f) {
                            in_world++;
                            near_home += net::dist(game::position(*m), net::Vec3{home[0], home[1], home[2]}) < 100.f;
                        }
                    }
                char b[260];
                snprintf(b, sizeof b, "  herd %p arch=%08X species=%p/%08X nest=%p dist=%.0f en=%d members=%d in_world=%d "
                         "near_home=%d home=(%.1f %.1f %.1f)\n", o,
                         *(uint32_t*)((uint8_t*)o + 0x88), sp, inst, game::herd_nest(o),
                         net::dist(net::Vec3{home[0], home[1], home[2]}, a->at), (int)*(uint8_t*)((uint8_t*)o + 0x218),
                         members, in_world, near_home, home[0], home[1], home[2]);
                a->out += b;
                return true;
            }
            if (*(uint32_t*)((uint8_t*)o + 0x88))
                return true;
            a->plain++;
            char b[220];
            void* sp = game::herd_species(o); // a cSpeciesProfile: its key at +0x504
            snprintf(b, sizeof b, "  herd %p species=%p key=%08X arch=%08X target=%d enabled=%d nest=%p avatar_owned=%d\n", o,
                     sp, sp && !IsBadReadPtr(sp, 0x510) ? *(uint32_t*)((uint8_t*)sp + 0x504) : 0u,
                     *(uint32_t*)((uint8_t*)o + 0x88), *(int32_t*)((uint8_t*)o + 0xF4),
                     *(uint8_t*)((uint8_t*)o + 0x218), game::herd_nest(o), *(uint8_t*)((uint8_t*)o + 0x84));
            a->out += b;
            return true;
        },
        &a);
    char b[96];
    snprintf(b, sizeof b, "herds %u, without an archetype %u\n", a.all, a.plain);
    return b + a.out;
}

static bool g_test_hit = false;
bool test_hit_now() { return g_test_hit; }

bool debug_hit(uint16_t id, float damage)
{
    auto it = g_proxies.find(id);
    if (it == g_proxies.end() || !it->second.c || !game::avatar())
        return false;
    g_test_hit = true; // (social.no_bite lets a test's hit pass in the social stance)
    game::hit(it->second.c, damage, 0, game::avatar());
    g_test_hit = false;
    return true;
}

} // namespace npc

namespace npc {
void* proxy(uint16_t id)
{
    auto it = g_proxies.find(id);
    return it == g_proxies.end() ? nullptr : it->second.c;
}

void* host_npc(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    return it != g_host_by_id.end() && g_seen_now.count(it->second) && game::object_alive(it->second) ? it->second
                                                                                                     : nullptr;
}

int host_id(void* c)
{
    auto it = g_host_npcs.find(c);
    return it == g_host_npcs.end() ? -1 : it->second.id;
}

int proxy_id(void* c)
{
    auto it = g_proxy_of.find(c);
    return it == g_proxy_of.end() ? -1 : it->second;
}

int proxy_id_of_ai(void* ai)
{
    for (auto& [c, id] : g_proxy_of)
        if (c && *(void**)((uint8_t*)c + 0xB4C) == ai) // cCreatureAnimal +0xB4C: the AI block
            return id;
    return -1;
}

// host: the pack recruits NPC #id, as the end of a social game with an ally does (tests)
bool debug_recruit(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    if (!is_host() || it == g_host_by_id.end() || !g_seen_now.count(it->second))
        return false;
    static uintptr_t get = H->sym("SporeCoop::cPosseSimulator_Get"), add = H->sym("SporeCoop::cPosseSimulator_AddMember");
    void* posse = get && add ? skg::cdecl_call<void*>(get) : nullptr;
    return posse && skg::thiscall<bool>(add, posse, it->second);
}

// host: an NPC of the given species (crt instance) a few metres from the avatar, in a herd of its
// own (tests: a species only the host's profile has, like a creation of its My Creations)
void* debug_spawn(uint32_t instance)
{
    void* av = game::avatar();
    void* sp = av ? game::species_by_key({instance, 0x2B978C46u, 0x40626200u}) : nullptr;
    if (!sp)
        return nullptr;
    net::Vec3 p = game::position(av);
    p.x += 4.f;
    void* herd = game::create_herd(p, sp);
    return herd ? game::spawn_creature(p, sp, herd) : nullptr;
}

// host (tests, BUG-0044): dead NPC #id's object becomes a living creature of its species again, the
// state host_tick meets when the game builds a new creature in a freed one (cCreatureAnimal::Create
// with its reuse flag takes a free slot of the creature pool whose object had the same species
// profile, 0xAD2020). A natural reuse could not be forced: DestroyInstance on a corpse, then up to 8
// Create(reuse) of its species over 8 frames, never returned the freed object (2026-10-28, lane 5).
// So the object itself is made alive again: the death flag cleared, full health.
std::string debug_reuse(uint16_t id)
{
    void* c = host_npc(id);
    if (!is_host() || !c)
        return "not the host or no such npc";
    char b[160];
    snprintf(b, sizeof b, "npc #%u %p species %08X dead=%d: alive again (as a new creature in its object)", id, c,
             game::species_instance(c), (int)game::is_dead(c));
    *((uint8_t*)c + game::kDeadOff) = 0;
    game::set_health(c, game::max_health(c));
    return b;
}

// host (tests): an epic creature (archetype 0xFC811218, isEpic: always hostile to the player,
// docs/re/tribe.md E) `dist` m from player `player`'s creature (0: the host's avatar), of the species
// of the nearest NPC (everybody has its model)
void* debug_spawn_epic(uint8_t player, float dist, uint32_t arch, uint32_t species)
{
    void* av = game::avatar();
    void* who = player ? (C.puppet_of ? C.puppet_of(player) : nullptr) : av;
    if (!is_host() || !av || !who)
        return nullptr;
    net::Vec3 at = game::position(who);
    void* prof = nullptr;
    float bd = 1e9f;
    for (auto& [c, hn] : g_host_npcs) {
        if (!g_seen_now.count(c) || game::is_dead(c) || game::herd_of(c) == game::avatar_herd() || !game::species_profile(c))
            continue;
        float d = net::dist(game::position(c), at);
        if (d < bd) {
            bd = d;
            prof = game::species_profile(c);
        }
    }
    if (species) // a given species (crt instance), e.g. the one of an epic of the planet
        prof = game::species_by_key({species, 0x2B978C46u, 0x40626200u});
    if (!prof)
        return nullptr;
    float r = std::sqrt(at.x * at.x + at.y * at.y + at.z * at.z);
    net::Vec3 up = r > 1 ? net::Vec3{at.x / r, at.y / r, at.z / r} : net::Vec3{0, 0, 1};
    net::Vec3 side = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
    float sl = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
    net::Vec3 p{at.x + side.x / sl * dist + up.x, at.y + side.y / sl * dist + up.y, at.z + side.z / sl * dist + up.z};
    static uintptr_t herd_fn = H->sym("Simulator::cGameNounManager::CreateHerd"),
                     create = H->sym("Simulator::cCreatureAnimal::Create");
    void* nm = game::noun_manager();
    float pp[3] = {p.x, p.y, p.z};
    void* herd = nm && herd_fn && create ? skg::thiscall<void*>(herd_fn, nm, pp, prof, 0, false, 0, false) : nullptr;
    if (!herd)
        return nullptr;
    *(uint32_t*)((uint8_t*)herd + 0x88) = arch;  // e.g. 0x95511253 / 0xFC811218: isEpic = 1
    *(uint32_t*)((uint8_t*)herd + 0xF0) = 0;     // generation
    // as an epic herd of the planet has them (runtime 2026-10-25): archetype group 2, territory 50 m,
    // personality 9 (without these the spawned epic wandered and never attacked anybody)
    *(uint32_t*)((uint8_t*)herd + 0x8C) = 2;
    *(float*)((uint8_t*)herd + 0x108) = 50.f;
    *(uint32_t*)((uint8_t*)herd + 0x15C) = 9;
    void* c = skg::cdecl_call<void*>(create, pp, prof, 1, herd, false, false);
    *(int32_t*)((uint8_t*)herd + 0xF4) = 0; // no more members
    static uintptr_t enable = H->sym("Simulator::cHerd::SetEnabled");
    if (enable && c)
        skg::thiscall<void>(enable, herd, true); // CreateHerd leaves it disabled in the creature stage
    return c;
}

bool debug_host_hit(uint16_t id, float damage, float push)
{
    auto it = g_host_by_id.find(id);
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !game::avatar())
        return false;
    if (push > 0) { // BUG-0383 (tests): a hit that throws it, away from the avatar and up (as a charge does)
        net::Vec3 a = game::position(game::avatar()), b = game::position(it->second);
        float r = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
        net::Vec3 up = r > 1 ? net::Vec3{b.x / r, b.y / r, b.z / r} : net::Vec3{0, 0, 1};
        net::Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
        float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        d = dl > 0.1f ? net::Vec3{d.x / dl, d.y / dl, d.z / dl} : up;
        game::hit_push(it->second, damage, 0, game::avatar(),
                       {(d.x + up.x * 0.5f) * push, (d.y + up.y * 0.5f) * push, (d.z + up.z * 0.5f) * push});
        return true;
    }
    game::hit(it->second, damage, 0, game::avatar()); // through the damage filter
    return true;
}

// tests: the message an attack of the avatar on host NPC `id` posts (FUN_00C02B40, the creature's
// attack vt 0xB8: ExecuteAction(0xD335362C, {attacker, target, 0}) to every creature-stage
// listener, CreatureStageEventHandler among them; docs/re/creature.md 4)
bool debug_attack_msg(uint16_t id)
{
    auto it = g_host_by_id.find(id);
    void* av = game::avatar();
    static uintptr_t get = H->sym("App::cCreatureModeStrategy::Get"),
                     exec = H->sym("App::cCreatureModeStrategy::ExecuteAction");
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !av || !get || !exec)
        return false;
    void* strat = skg::cdecl_call<void*>(get);
    if (!strat)
        return false;
    void* msg[3] = {av, it->second, nullptr};
    skg::thiscall<void>(exec, strat, 0xD335362Cu, (void*)msg);
    return true;
}

bool debug_hurt(uint8_t player, uint16_t id, float damage)
{
    auto it = g_host_by_id.find(id);
    void* pup = C.puppet_of(player);
    if (it == g_host_by_id.end() || !g_seen_now.count(it->second) || !pup)
        return false;
    game::hit(pup, damage, 0, it->second);
    return true;
}
std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [c, h] : g_host_npcs) // updated this frame: an unloaded one may be freed already
            if (g_seen_now.count(c) && game::object_alive(c) && !game::is_dead(c)) // living ones only
                v.push_back({"n" + std::to_string(h.id), game::position(c), game::orientation(c)});
    } else {
        for (auto& [id, px] : g_proxies)
            if (px.c && game::object_alive(px.c) && !px.killed)
                v.push_back({"n" + std::to_string(id), game::position(px.c), game::orientation(px.c)});
    }
    return v;
}

} // namespace npc

// ---------------------------------------------------------------- the invariant monitor
// docs/06-testing.md#testing-monitor: what the lab checks while a test plays, also in players' logs
// (docs/REPORTING.md). Diagnostics only: nothing here changes the game.
namespace npc {

// `damage: <victim> hit by <kind> owner=<host|guest id|npc|-> posse=<0|1|?> amount=<hp>[ <how>]`, where a hit
// on a player (its avatar, or its puppet on the host) is applied, forwarded or dropped. kind: none (no
// attacker), avatar, player (a partner's puppet), proxy (guest: a host NPC's copy), pack / family (the
// host's own party, cCreatureAnimal +0xB58 & 0x100 / the avatar's herd), npc. posse=1: the attacker is
// of the hit game's player's party (BUG-0374 class); `?`: not known here (a guest knows only the host's
// family flag). posse=1 hits that land are written every time, the others rate limited (trace::ev).
static void log_hit(uint8_t victim, void* a, float amount, const char* how, bool from_host)
{
    if (!trace::on() || !C.sess)
        return;
    const char* kind = "npc";
    std::string owner = "npc";
    bool posse = false, known = true;
    void* av = game::avatar();
    if (!a) {
        kind = "none";
        owner = "-";
        known = !from_host; // the host's NPC that hit is not mirrored here
    } else if (a == av) {
        kind = "avatar";
        owner = is_host() ? "host" : std::to_string(C.sess->my_id());
    } else if (auto pi = g_proxy_of.find(a); pi != g_proxy_of.end()) {
        kind = "proxy";
        auto px = g_proxies.find(pi->second);
        posse = px != g_proxies.end() && (px->second.flags & net::NF_FAMILY);
        known = posse;
    } else if (game::is_puppet(a)) {
        kind = "player";
        owner = "-";
        for (auto& p : C.sess->players())
            if (C.puppet_of && C.puppet_of(p.id) == a)
                owner = p.id == 0 ? "host" : std::to_string(p.id);
    } else {
        bool pack = (skg::field<uint32_t>(a, game::kFlagsOff) & 0x100) != 0;
        posse = creature_hp::host_party(a);
        kind = pack ? "pack" : posse ? "family" : "npc";
    }
    char b[200];
    snprintf(b, sizeof b, "%u hit by %s owner=%s posse=%s amount=%.1f%s%s", victim, kind, owner.c_str(),
             !known ? "?" : posse ? "1" : "0", amount, *how ? " " : "", how);
    if (posse && amount > 0)
        trace::line("damage", "%s", b);
    else {
        char key[64];
        snprintf(key, sizeof key, "%u hit by %s%s", victim, kind, amount > 0 ? "" : " (none)");
        trace::ev("damage", key, "%s", b);
    }
}

struct ProxyAudit {
    double under_since = -1, off_since = -1;
    double seen = 0;
};
static std::unordered_map<uint16_t, ProxyAudit> g_audit;
static std::set<uint16_t> g_audit_under, g_audit_stuck; // since the last trace line
static double g_audit_at = 0;

void monitor_tick(double now)
{
    if (!g_enabled || !is_guest() || !npc_stage() || g_host_away || now - g_audit_at < 0.25)
        return;
    g_audit_at = now;
    for (auto& [id, px] : g_proxies) {
        auto& a = g_audit[id];
        a.seen = now;
        net::PlayerState s;
        if (!px.c || px.killed || game::is_destroyed(px.c) || !game::object_alive(px.c) ||
            game::destroy_pending(px.c) || game::is_dead(px.c) || !px.buf.sample(now, s) || (s.flags & net::NF_DEAD)) {
            a.under_since = a.off_since = -1;
            continue;
        }
        net::Vec3 p = game::position(px.c);
        float r = drive::speed(p), rt = drive::speed(s.pos), g = game::ground_radius(p);
        bool under = g > 0 && r - g < -0.5f;
        // off: from the host's NPC taken to this proxy's distance from the centre (each game its own
        // ground: Tribe drives at its own height, BUG-0329)
        net::Vec3 t = rt > 1 && r > 1 ? net::Vec3{s.pos.x * r / rt, s.pos.y * r / rt, s.pos.z * r / rt} : s.pos;
        bool off = net::dist(p, t) > 3.f;
        a.under_since = under ? (a.under_since < 0 ? now : a.under_since) : -1;
        a.off_since = off ? (a.off_since < 0 ? now : a.off_since) : -1;
        // which one and where (players' reports too): the first of each kind per line
        if (under && now - a.under_since >= 1.0 && g_audit_under.insert(id).second && g_audit_under.size() == 1)
            trace::line("npc", "proxy %u (species %08X) %.2f m under this game's ground at (%.1f %.1f %.1f) for %.1f s, the "
                        "host's NPC %.1f m from it (%s%s), made %.1f s ago at %.2f m over the ground", id, px.key.instance,
                        g - r, p.x, p.y, p.z, now - a.under_since, net::dist(p, t),
                        px.kinematic ? "placed on the host's path" : "driven", now < px.manual_until ? ", test walk" : "",
                        now - px.made_at, px.made_agl);
        if (off && now - a.off_since >= 2.0 && g_audit_stuck.insert(id).second && g_audit_stuck.size() == 1)
            trace::line("npc", "proxy %u (species %08X) %.1f m off the host's NPC for %.1f s at (%.1f %.1f %.1f), host's speed "
                        "%.1f m/s (%s%s)", id, px.key.instance, net::dist(p, t), now - a.off_since, p.x, p.y, p.z,
                        drive::speed(s.vel), px.kinematic ? "placed on the host's path" : "driven",
                        now < px.manual_until ? ", test walk" : "");
    }
    for (auto it = g_audit.begin(); it != g_audit.end();)
        it = now - it->second.seen > 5.0 ? g_audit.erase(it) : std::next(it);
}

std::string monitor_fields()
{
    if (!is_guest() || !npc_stage())
        return "";
    char b[64];
    snprintf(b, sizeof b, " proxy_under=%u proxy_stuck=%u", (unsigned)g_audit_under.size(), (unsigned)g_audit_stuck.size());
    g_audit_under.clear();
    g_audit_stuck.clear();
    return b;
}

} // namespace npc
