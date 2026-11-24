#include "npc.h"
#include "party_difficulty.h"
#include "lifecycle.h"
#include "player_rules.h"
#include "view_centres.h"
#include "modes.h"
#include "tribe.h"
#include "wire/tribe_anim.h"
#include "settings.h"
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
#include "social_logic.h"
#include "driver.h"
#include "game.h"
#include "net/compat.h"
#include "net/validate.h"
#include "npc_ids.h"
#include "npc_logic.h"
#include "npc_proxy_rules.h"
#include "mirror_registry.h"
#include "world.h"
#include "sporekit_game.hpp"
#include "actor_scope.h"
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

// BUG-0973: the Civilization stage's wild creatures (cCreatureAnimal, noun 0x18EB45E: herds and epics) ran in each
// game on its own: they fled, moved and died apart, the guest's mirror vehicles never killed one, and a guest's click
// on a creature named one the host did not have there. Now the host owns them as in the Tribe stage: the guest's own
// are removed by the update filter and it shows proxies of the host's (positions, herds, deaths, walked like the
// Tribe stage's). A Civ planet has few of them (civ_npc: 10, the civ fixture: 4), so every one goes to every guest
// in the world (kCivRelevance). `coop.fix npc.civ_wild off`: each game runs its own (0.7.13).
static bool g_civ_wild_fix = true;
COOP_FIX("npc.civ_wild", "BUG-0973", g_civ_wild_fix,
         "civ: the host owns the wild creatures, every guest shows the host's (positions, herds, deaths)");
static constexpr float kCivRelevance = 1e6f; // m: the whole planet
// The bandwidth of it: a state is 49 B (NpcState and the height trailer) ten times a second per guest; a lab civ_npc
// run had 25 wild creatures, 12.5 KB/s per guest. Most of them stand most of the time: a standing creature's state
// (no speed, the same place, flags and health as the last one sent to that guest) goes once a second only (the
// guest drops a proxy without states after 5 s). `coop.fix npc.civ_quiet off`: every 0.1 s.
static bool g_civ_quiet_fix = true;
COOP_FIX("npc.civ_quiet", "BUG-0973", g_civ_quiet_fix, "civ: a standing wild creature's state goes to the guests once a second");
struct CivSent {
    net::Vec3 pos;
    uint8_t flags;
    float health;
    double at;
};
static std::map<uint8_t, std::unordered_map<uint16_t, CivSent>> g_civ_sent; // host: guest -> id -> last state sent
static uint32_t g_civ_quiet_skipped;
// BUG-1216: the Civ game takes wild creatures back into its pool and gives the objects out again (the relevance pass,
// docs/re/civ.md#civ-wild-sync), several first at an unplaced point with z = 0 and a moment later at their new herd
// (docs/re/civ.md#civ-wild-reused-frozen): the host's states of one NPC jump hundreds of metres. A guest's proxy
// buffer blended across that step (jump_speed 0): its target slid along the chord through the planet (59-115 m under
// the ground, 80-205 m from the host's creature: the campaign's "n apart", lab 20261117-122701), and the proxy, walked
// after it at the path's speed, flew in a straight line 10-80 m over the guest's ground for seconds (the `drawn X m
// over this game's ground` lines, 27 in a minute of civ_wild_sync). Now a Civ proxy's buffer takes a step faster than
// modes' puppet jump speed (40 m/s) as a jump (BUG-0499's rule for the partners' puppets): the target is at the new
// place at once and the proxy is put there. `coop.fix npc.civ_jump off`: blended (0.7.14).
// The same jump also went into the proxy's path velocity (drive::path_velocity: a standing host creature's proxy is
// walked at its path's speed, BUG-0394), 1000+ m/s for 0.2 s: the walk went the whole way in a few frames, in a
// straight line over valleys. So a Civ target that jumps more than kCivJumpM in one frame puts the proxy there (onto
// this game's ground) with a fresh path, as the partners' puppets (modes.cpp kJumpM, BUG-0499).
static bool g_civ_jump_fix = true;
COOP_FIX("npc.civ_jump", "BUG-1216", g_civ_jump_fix, "civ: a wild creature the game moves elsewhere is shown there at once, not flying there through the air");
constexpr float kCivJumpM = 10.f; // m of the target in one frame (a lead jump up to smooth_max 8 m is smoothed before)
static uint32_t g_civ_jumps;
// BUG-1228: a guest's copy of a host creature that stands, on steep ground of the guest (a slope the host's creature
// does not stand on: the final check's guest ground lay 10 m higher there), was slid down by its game (velocity
// 19-26 m/s), pulled back up by the nudge and trail-teleported onto its target every ~2.5 s: a player saw it slide and
// pop back. Now a standing copy that its game moves more than kSlideM in one frame (no creature stands still that way
// on its own) is held from then on while its remote stands: the game's next creature and physics update may move it
// towards its target, not away (game::place_hold, still: no teleport mark, no velocity). `coop.fix npc.slope_hold
// off`: the game slides it (0.7.14).
static bool g_slope_hold_fix = true;
COOP_FIX("npc.slope_hold", "BUG-1228", g_slope_hold_fix,
         "a copy of a standing wild creature on steep ground stands still, not sliding down and jumping back up");
constexpr float kSlideM = 0.1f; // m its game moved a standing copy in one frame (6 m/s at 60 fps)
static uint32_t g_slope_slides; // standing copies found moved by their game (held from then on with the switch)
LIFECYCLE_STATE("npc.civ_sent", lifecycle::kSession | lifecycle::kWorld, &g_civ_quiet_fix, [] { g_civ_sent.clear(); },
                []() -> size_t { return g_civ_sent.size(); });
static bool civ_stage() { return g_civ_wild_fix && game::mode_id() == modes::kCiv; }
// NPC creatures are shared in the creature stage and, around the player tribe, in the tribe stage (and in Civ)
static bool npc_stage() { return game::in_creature_stage() || game::mode_id() == 0x1654C02 || civ_stage(); }
static bool rts_stage() { return game::mode_id() == 0x1654C02 || civ_stage(); }
// BUG-0291: in the Tribe stage the host sent a guest only the animals within kRtsRelevance of the
// player tribe; a guest looking farther away saw none there (its own are removed by the update
// filter). Now also those within kRelevance of the guest's view centre (its pointer,
// view_centres.h); the game keeps them alive there (BUG-0280).

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
// C1 (BUG-1140, docs/01-architecture.md#arch-mirror-registry): the id <-> creature tables of both sides are
// mirror::Registry objects: one entry per net id with its creature (pointer, cGameData::mID serial, noun) and the
// stage's data; the reverse index (creature -> id) is the registry's, so the two directions cannot drift. Liveness is
// game::alive (the game's object list of the frame and the serial). What stays here: the species / dead reuse rule
// (BUG-0044, BUG-1100), retire_pooled (BUG-0274), the 5 s stale drop and its thaw (BUG-0611), herds, the driving.
// (The hand-kept maps of 0.7.14 and their switch npc.registry went in 0.7.15 after the stage's tests passed with it.)
static constexpr uint32_t kCreatureNoun = 0x18EB45Eu; // cCreatureAnimal
static bool ref_alive(const mirror::Ref& r) { return game::alive(game::LiveRef{r.p, r.serial, r.noun}); }
using HostReg = mirror::Registry<uint16_t, HostNpc>;
static HostReg g_hreg("npc.host", ref_alive);
// the host entry of creature `c` (a pointer compare: safe on a freed one), null when it has none
static HostNpc* hn_of(const void* c)
{
    uint16_t id;
    HostReg::Entry* e = g_hreg.id_of(c, id) ? g_hreg.find(id) : nullptr;
    return e ? &e->data : nullptr;
}
// the creature of host id `id` (no liveness check), null when the id is not given
static void* hn_obj(uint16_t id)
{
    HostReg::Entry* e = g_hreg.find(id);
    return e ? e->ref.p : nullptr;
}
static bool hn_used(uint16_t id) { return g_hreg.has(id); }
static size_t hn_count() { return g_hreg.size(); }
// for (auto [c, hn] : hn_walk()): every host entry (an entry forgotten during the walk must not be touched after it)
struct HnRow {
    void* c;
    HostNpc& hn;
};
static std::vector<HnRow> hn_walk()
{
    std::vector<HnRow> v;
    v.reserve(g_hreg.size());
    g_hreg.for_each([&](HostReg::Entry& e) { v.push_back({e.ref.p, e.data}); });
    return v;
}
// the entry of id goes (no game call; retire() told the guests)
static void hn_forget(uint16_t id) { g_hreg.erase(id); }
static void hn_clear() { g_hreg.clear(); }
static std::map<uint8_t, std::set<uint16_t>> g_known;  // guest -> net ids it mirrors
static uint16_t g_next_id = 1;
static double g_last_state = 0;
static double g_frozen_since = 0;                       // host: no creature updates since then
static std::unordered_set<void*> g_seen_now;            // creatures alive this frame
// CR-2 (BUG-1428): a guest's MSG_DAMAGE_REQ / MSG_NPC_RESYNC is read in frame_receive, before this frame's host pass
// rebuilt g_seen_now (and with no pass at all while the host has no avatar or is in its editor): the creature the id
// names can be one the host's game removed since (a decayed or eaten body, a far animal culled). hn_obj has no liveness
// check, so the hit went to TakeDamage on a freed creature (BUG-0453 class) and the resync read it. On: the handlers
// ask host_npc (the entry's object listed now with its serial, as the social handlers do). Off: 0.7.16.
static bool g_msg_live_fix = true;
COOP_FIX("npc.msg_live", "BUG-1428", g_msg_live_fix,
         "creature: a guest's hit on an animal the host's game just removed is dropped, not applied to the freed creature");
static double g_test_pass_hold_until = 0; // tests (BUG-1428): no host pass until then, as in the host's editor
static uint32_t g_msg_gone_refused = 0;   // guests' hits / resyncs refused: the NPC is not alive on the host now
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
// BUG-0281: in the Creature stage a proxy given a MoveTo again and again (the host's NPC runs on) hardly
// walked (own speed 0-3 m/s while the NPC ran at 4) and our pulls towards the path were undone by its
// locomotion (5-10 % kept): running NPCs were 3-6 m off on a 15 fps guest. While the host's NPC moves
// its proxy is now placed on the interpolated host path every frame with the host's velocity (the
// game animates from it); standing, the driving (walk, nudge, stuck/stall/trail) works as before.
// BUG-0361: BUG-0281's placement teleported a moving NPC's proxy onto the host's path every frame.
// A teleport (vt 0x44) sets the teleport mark +0xB5D, which restarts the animation's walk gait: the
// proxies of walking and running NPCs slid over the ground in the idle pose (walk gait on in 4 % of
// their moving frames on the guest's screen, coop_npc_walk.py). Now, as for the partner's puppet
// (BUG-0360), the game walks the proxy (the host NPC's speed, a MoveTo 12 m
// ahead on its path given again only when the direction turns, at most every 0.3 s, at least every
// 1.5 s) and game::place (teleport, then +0xB5D = 0) puts it on the interpolated path every frame;
// an error left from standing is closed at kWalkCatchUp faster than the NPC. The end of a walk is
// a MoveTo to the stop point.
// Tribe: the moving animals' proxies were driven by drive::step (a MoveTo again every 0.1 s, pulls, the
// stall / trail teleports): in 0.1 s windows of a moving host animal they stood in 4 % and moved at its
// speed in only 77 %, with 0.8-2.3 m jumps (coop_npc_walk.py, TRACK_FIXTURE=tribe). They are walked and
// placed as in the Creature stage now, at the guest's own ground height (BUG-0329).
// BUG-0394: an NPC of the host can move without reporting a speed (carried by an animation, a hit): its proxy
// also walks with the speed of its path.
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
// BUG-0752: the same for a standing proxy in the Creature stage. Two proxies of the host's world species made 0.5 m apart
// after the host's evolution editor: the game separated them and pressed one 0.5-1.3 m into the terrain, and a standing
// proxy is only nudged towards its target (drive::step without `ground`), so it stayed there for over a minute (the
// monitor's proxies rule, campaign_ct lab 20261108-072547 / -075905-4 of rel/0.7.11). `coop.fix npc.proxy_ground_creature
// off`: Tribe only (0.7.10).
static bool g_proxy_ground_creature = true;
COOP_FIX("npc.proxy_ground_creature", "BUG-0752", g_proxy_ground_creature, "creature: an animal's standing proxy pressed into the ground is put back onto it");
// BUG-0406: placed exactly on the host's path, a proxy walking downhill is a few cm over the ground after the
// game's next step and starts to fall (+0x137 = 0, the fall animation): 51 fall starts per 100 s of walking on
// a slope of the creature fixture, the host's own NPCs 0.9 (npc_walk_slope). As the partner's puppet
// (BUG-0366, creature.puppet_ground) its walk is placed 5 cm into this game's ground (WalkPlace::ground).
// `coop.fix npc.walk_ground off`: on the host's path exactly.
static bool g_walk_ground_fix = true;
COOP_FIX("npc.walk_ground", "BUG-0406", g_walk_ground_fix, "creature: a walking NPC's proxy is placed onto the ground, so it does not keep starting to fall downhill");
// BUG-1360: px.walk_sp (the speed the host NPC reported while it last walked) clamps a walked proxy's path velocity in
// the frames without a reported speed only within that walk (npc_logic::walk_velocity); off: kept across walks, an
// unreported walk is clamped to an earlier walk's speed (0.3 m/s: the game plays the standing idle, 0.7.16.1)
static bool g_walk_gait_fix = true;
COOP_FIX("npc.walk_gait", "BUG-1360", g_walk_gait_fix, "creature: an NPC's copy that walks without a reported speed walks at its path's speed with the walk animation, not slowed to a standing idle by an earlier walk's speed");
// BUG-0503: walk_place closes a proxy's error at (speed + catch_up) x the frame time, the frame time capped at
// WalkPlace::max_dt (0.1 s). A guest at 3-5 fps (0.2-0.5 s frames under load, or a weak PC) closed 0.7 m per frame
// of a host NPC that walked 1-2 m: the placed proxy fell 0.2-0.4 m further behind every frame, 3-8 m off for 2-11 s
// until the NPC stood (the monitor's `proxies` rule in play_tribe / long_tribe). The Tribe mirrors take 0.4
// (BUG-0365): a proxy's cap is 0.4 s too (BUG-0503). BUG-0590: the Creature stage's proxies lagged the
// same way (4 fps guest: 2.5-5.9 s over 3 m, placed p90 2.7-3.6 m): the same cap on every stage.
// BUG-0550 / BUG-0425: an NPC off its ground on the host (a flier 9-14 m up, a jump, thrown by a hit: the game's
// grounded byte +0x137 = 0) was drawn on the ground on a guest (sync-check `n apart` 9-14 m, `want_agl=10.56
// made_agl=0`): its proxy was walked and the guest's game put the grounded proxy back onto its terrain. Now
// the host sends NF_AIR and its NPC's height over the host's terrain (the batch's agl trailer) and the
// guest places such a proxy at that height over its own terrain, not grounded (drive::air_place, as the partner's
// puppet in a jump, BUG-0376). On its ground the proxy stays on the guest's ground as before.
// BUG-0440: an animal proxy placed with the teleport mark every frame (walk_place held: its error grew) was moved back
// by the guest's game in its update at 6-7 m/s (lab 20261113-191751 chaos_tribe#4: pushed back 6.9 m/s, the teleport
// itself taken), so it crept at 0-3 m/s 3-8 m behind its walking host animal for seconds (the monitor's proxies gate).
// Now such a placing is put back after the game's creature update and its physics update whenever they moved it away
// from its target (game::place_hold). `coop.fix npc.proxy_hold off`: placed only (0.7.13).
static bool g_proxy_hold_fix = true;
COOP_FIX("npc.proxy_hold", "BUG-0440", g_proxy_hold_fix, "tribe: a walking animal's copy that its game holds back is kept on the host animal's path");
// BUG-1357: the host sends NF_AIR for a body that is not grounded and over 0.15 m off its terrain either way
// (drive::body_airborne), so also for an NPC pressed under the host's ground: a far NPC the host's game does not
// simulate (outside its 60 m) stood 0.7-0.8 m under the host's terrain at (137.7 -480.2 -79.3) for 13 s in
// migration_heal (final check 20261120-041304) and nest_follow (BUG-1208); the guest air-placed its proxy at that
// negative height over its own ground (at_sender_agl), the monitor's proxies rule saw it under the ground. Now a state
// in the air whose height is under the host's ground (agl < -kUnderTol) is placed as standing / walking on this
// game's ground (npc_rules::place host_under; drive::step's `ground` lifts it). `coop.fix npc.air_under off`: air-placed
// at the negative height (0.7.15).
static bool g_air_under_fix = true;
COOP_FIX("npc.air_under", "BUG-1357", g_air_under_fix, "creature: an animal under the host's ground is shown on this game's ground, not air-placed under it");
// BUG-1094 (`coop.fix npc.air_catch`): the first placings in the air put a proxy on its target at once: a walked proxy
// that trailed it 0.9 m stepped 0.94 m in one 15 ms frame at the throw's start, and the lead's target itself leaps
// 1-1.5 m there (the newest state moved on at the throw's 9 m/s; lab 20261113-235647 #2, 20261114-000525 #3: 1.07 m
// in 19 ms). The drawn proxy is moved toward its target at no more than its speed x kAirCatchK + kAirCatchMps per
// second (the game adds the written velocity x the frame after the placing: allowed for); farther than kAirCatchMax m
// it is placed on the target. Off: placed on the target in every frame.
static bool g_air_catch_fix = true;
COOP_FIX("npc.air_catch", "BUG-1094", g_air_catch_fix, "creature, tribe: an animal's copy that is thrown goes into the air from where it is drawn, no step of a metre at the throw");
constexpr float kAirCatchK = 1.2f, kAirCatchMps = 8.f, kAirCatchMax = 3.f;
static uint32_t g_air_catches = 0;
// BUG-0710 (class BUG-0419): NpcSpawn.max_health was sent and never applied: a proxy had its species' adult max on the
// guest (a baby NPC: max 5 on the host, 10 on the guest; detector Creature:max_hp, 20 of 23 comparisons). Off: 0.7.10.
static bool g_max_hp_fix = true;
COOP_FIX("npc.max_health", "BUG-0710", g_max_hp_fix, "creature: an animal's maximum health on the guest is the host's (babies, elders)");
static uint32_t g_max_hp_applied;
// BUG-0850: 97392cd1 (BUG-0710) put forget_puppet into destroy_proxy's unbraced else, so destroy_creature ran for a
// proxy the guest's game had destroyed already too: a second cGameNounManager::DestroyInstance of it (0.7.11, 0.7.11.1).
// Off: that second destroy again.
static bool g_destroy_once_fix = true;
COOP_FIX_UNSAFE("npc.destroy_once", "BUG-0850", g_destroy_once_fix, "creature, tribe: an animal the guest's game removed itself is not destroyed a second time when its proxy goes");
// tests (BUG-0550): `coop.npc lift <id> <m>`: the host's NPC <m> over its ground, every frame for <s> s
static uint16_t g_lift_id = 0;
static float g_lift_m = 0;
static double g_lift_until = 0;
// tests (BUG-1228): coop.npctrack put <id> x y z <secs>: the host's NPC held at that point of its ground for secs
static uint16_t g_pin_id = 0;
static net::Vec3 g_pin_at{};
static double g_pin_until = 0;
// tests (coop.npctrack variant, drive::walk_place): 0 the fix; 1 place only (no walk goal); 2 walk goal +
// teleport with the mark
static int g_walk_variant = 0;
// 0.7.2.x fixes kept for the tests (off: the code before them)
static bool g_stale_fix = true, g_world_despawn_fix = true, g_death_fix = true;
COOP_FIX("npc.stale_proxy", "BUG-0009", g_stale_fix, "creature: a guest drops a proxy the host stopped sending states for");
COOP_FIX("npc.world_despawn", "BUG-0009", g_world_despawn_fix, "creature: the host despawns its NPCs of a world it left at the guests");
COOP_FIX("npc.play_death", "BUG-0015", g_death_fix, "creature: a guest's proxy of a killed NPC plays the death");
void set_stall_fix(bool on) { g_stall_fix = on; }

// BUG-0562 (docs/re/creature.md#creature-nest-mound-proxy): a game simulates the creatures far from its own camera
// without the collision objects (a nest mound): the host's NPC of a nest the host's camera is not at stands on the
// plain terrain, 0.4-0.7 m inside the mound, and the guest's proxy of it was drawn there, in the nest (the guest's
// camera is at the nest: its mound is solid, and the game does not lift a creature put that far under). Now
// a standing proxy's target under this game's surface (cPlanetModel_SurfacePoint: the mound) by more
// than kMoundMin and less than kMoundMax, where that surface is over the plain terrain, is put kMoundSink under the
// surface (the game sets it onto it).
static uint32_t g_mound_lifts, g_mound_targets;
static net::Vec3 proxy_on_mound(const net::Vec3& p)
{
    if (!game::in_creature_stage())
        return p;
    float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), sr = game::surface_radius(p), g = game::ground_radius(p);
    float k = npc_rules::mound_lift(r, sr, g); // (D3: npc_proxy_rules.h)
    if (k == 1.f)
        return p;
    return {p.x * k, p.y * k, p.z * k};
}
// a killed proxy plays the game's death (BUG-0015); coop.npc deathfix off (= npc.play_death off): it
// does not (tests: seeds the 0-health mirror that stands alive, BUG-0148)
void set_death_fix(bool on) { g_death_fix = on; }
static uint32_t g_reused = 0;   // host: creature objects that became another NPC (new id)
void set_reuse_fix(bool on) { g_reuse_fix = on; }
void set_id_fix(bool on) { g_id_fix = on; }
void debug_next_id(uint16_t id) { g_next_id = id; }

// ------------------------------------------------------------------ guest side
// BUG-1293 (found by the Lineages fork, PR #196, its BUG-9231): a proxy whose species key (our own key for a host's
// creation) builds nothing here is built from the host's key. `coop.fix npc.species_fallback off`: it waits (0.7.15)
static bool g_species_fallback = true;
COOP_FIX("npc.species_fallback", "BUG-1293", g_species_fallback,
         "npc: a host creature whose species the guest knows under its own key but cannot build there is built from the host's key");
static unsigned g_species_fallbacks;

struct Proxy {
    // (its creature is the registry entry's ref: pointer, the serial when the proxy made it (BUG-0842), noun; C1)
    game::Key key{};
    uint8_t flags = 0;
    float health = 0, max_health = 0;
    bool killed = false;
    bool host_pack = false; // BUG-0556: the host's NPC is in the host's pack (NF_PACK of its latest state)
    bool flock_fx = false;  // BUG-0556: the summoned flock's hearts effect started on the proxy
    int fail = 0; // 1 species, 2 herd, 3 create
    bool nospecies_said = false; // BUG-0300 diagnostics: logged once
    bool fallback_logged = false; // BUG-1293: built from the host's key (logged once)
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
    net::Vec3 diag_want{};  // BUG-0363 (coop.bodyrec diag): the target of the last driving
    bool diag_have = false;
    bool ever = false;      // BUG-0383: an object was made for it once (a body the guest's game removed stays gone)
    double made_at = 0;     // BUG-0399 (diagnostics): when its creature was made and the host's
    float made_agl = 0;     // position's height over this game's ground then (m)
    double high_logged = -1e9; // BUG-1216 diagnostics: the last "drawn high over the ground" line of this proxy
    net::Vec3 rest_at{};       // BUG-1228: where the last frame left the standing copy (after its driving)
    bool rest_have = false;
    bool slope_held = false;   // BUG-1228: its game slid it while its remote stood: held until the remote moves
    uint32_t tuning = 0;    // BUG-0462: the host NPC's archetype tuning key (spawn trailer), 0 unknown
    uint32_t tuning_set = 0; // the key last given to its creature
    double anim_fix_at = 0;  // BUG-0464: when the snapshot last played the host's animation on it
    double anim_rx_at = 0;   // BUG-0464: when the host's last start of an animation of it came
    double push_until = 0;   // tests (BUG-0440, `coop.npc pushback`): its game's update moves it back until then
    float push_mps = 0;
    bool air = false;        // BUG-0550: placed in the air (the host's NPC off its ground) last frame
    uint32_t airs = 0;       // BUG-0550 (coop.npc list): frames placed in the air
    float meat_seen = -1e9f; // BUG-0557: the body's meat (+0xB84) as last read or set here (-1e9: not a body yet)
};
// C1 (BUG-1140): the proxies by net id, each with the creature made for it (its serial; ref.p null: none yet, made
// in guest_tick). The registry's reverse index answers is_proxy / proxy_id (pointer compares).
using ProxyReg = mirror::Registry<uint16_t, Proxy>;
using PxE = ProxyReg::Entry;
static ProxyReg g_px("npc.proxies", ref_alive);
static PxE* px_find(uint16_t id) { return g_px.find(id); }
// the entry of `id`, made empty when there is none
static PxE& px_make(uint16_t id) { return g_px.at(id); }
// BUG-1228: the standing copy's slope hold ends (its remote walks, it is placed or put elsewhere)
static void slope_release(void* c, Proxy& px)
{
    if (px.slope_held && c)
        game::place_release(c);
    px.rest_have = px.slope_held = false;
}
static bool px_has(uint16_t id) { return g_px.has(id); }
static size_t px_count() { return g_px.size(); }
static bool px_empty() { return px_count() == 0; }
// every entry, in the order of the ids (the walks of 0.7.14 went by id); an entry erased during the walk must not be
// touched after it
static std::vector<PxE*> px_list()
{
    std::vector<PxE*> v;
    v.reserve(g_px.size());
    g_px.for_each([&](PxE& e) { v.push_back(&e); });
    std::sort(v.begin(), v.end(), [](const PxE* a, const PxE* b) { return a->id < b->id; });
    return v;
}
// for (auto [id, pe, px] : px_walk()): every proxy with its entry, by id
struct PxRow {
    uint16_t id;
    PxE& pe;
    Proxy& px;
};
static std::vector<PxRow> px_walk()
{
    std::vector<PxRow> v;
    for (PxE* e : px_list())
        v.push_back({e->id, *e, e->data});
    return v;
}
// the id of the proxy whose creature is `c` (a pointer compare: safe on a freed one)
static bool px_id_of(const void* c, uint16_t& id)
{
    return c && g_px.id_of(c, id);
}
static uint32_t g_px_rebound = 0; // a new creature at the address another entry still named (that entry let go)
// `c` is the creature made for proxy `e` now (with `serial`, its cGameData::mID)
static void px_bind(PxE& e, void* c, uint32_t serial)
{
    uint16_t other;
    if (g_px.id_of(c, other) && other != e.id) { // its creature is gone (this one was just made there)
        g_px.detach(other);
        g_px_rebound++;
    }
    g_px.bind(e.id, {c, serial, kCreatureNoun}, true);
}
// the proxy has no creature any more (no game call)
static void px_detach(PxE& e) { g_px.detach(e.id); }
static void px_erase(uint16_t id) { g_px.erase(id); }
// tests (BUG-0931, coop.npc ghost): the entry names `p` (no creature) as if its creature had been freed behind the mod
static void px_rename(PxE& e, void* p)
{
    uint32_t serial = e.ref.serial;
    px_detach(e);
    g_px.bind(e.id, {p, serial, kCreatureNoun}, true);
}
// BUG-0383: a guest made no proxy for an NPC that was dead when it first came into the guest's range (killed
// while the guest was more than 110 m away, killed by another NPC before its proxy was made, a body that lay
// there when the guest joined): the host's player saw the body, the guest saw nothing. Now the proxy of a body
// is made too and plays the death where the host's body lies, unless the guest's game had made and removed
// it already (eaten, decayed).
// BUG-0557 / BUG-0383: eating a body is each game's own act (the eat node 0xD70FD0 takes meat +0xB84 off the body in
// the eater's game): a body the guest's creature ate stayed whole in the host's game and the host's body (eaten by the
// host, its pack) stayed whole on the guest; a body eaten out in one game lay there in the other with all its meat
// (lab 20261105-225954: 100 -> -31 in the eater's game, 100 in the other). The meat of a body is now one value of the
// party: the host's game holds it (the guest's bites of its proxy go to the host and come off the host's body) and the
// guest's proxy of a body shows the host's meat (the lower of the two: a body only loses meat).
// `coop.fix npc.body_meat off`: each game eats its own bodies (0.7.8).
static bool g_meat_fix = true;
COOP_FIX("npc.body_meat", "BUG-0557", g_meat_fix, "creature: a body eaten by one player is eaten as far on the other's screen");
static constexpr float kMeatStep = 0.5f; // meat units: a change smaller than this is not sent
static std::map<uint8_t, std::map<uint16_t, std::pair<float, double>>> g_meat_sent; // host: guest -> id -> (meat, when)
static std::map<uint16_t, float> g_meat_eaten; // guest: what its creature ate of each proxy, not sent yet
static double g_meat_out_at = 0;
static uint32_t g_meat_rx = 0, g_meat_set = 0, g_meat_bites_tx = 0, g_meat_bites_rx = 0;
static float body_meat(void* c) { return c && skg::readable(c, 0xB88) ? *(float*)((uint8_t*)c + 0xB84) : 0.f; }
static void set_body_meat(void* c, float m)
{
    if (c && skg::readable(c, 0xB88))
        *(float*)((uint8_t*)c + 0xB84) = m;
}
// BUG-0300: a guest in a 4-player campaign had a proxy in its list (states arriving) whose creature
// was no creature of its world (not in the census, sync-check "missing on the guest") for 30 s and
// more, while the host's NPC stood 90 m from it. Cause not found. Once a second the guest now checks
// that every spawned proxy's object is still a creature of the noun list: one that is not was freed
// behind our back (driving it wrote into freed memory) and is made again; the log names it.
// `coop.fix npc.proxy_listed off`: no check (0.7.3.1).
static bool g_listed_fix = true;
static uint8_t* g_ghost; // tests (coop.npc ghost, BUG-0931): a zeroed block that is no creature
COOP_FIX("npc.proxy_listed", "BUG-0300", g_listed_fix, "creature: a guest makes a proxy again whose creature left the world unnoticed");
static uint32_t g_proxy_unlisted = 0, g_proxy_nospecies = 0;
static double g_listed_at = 0;
static std::map<uint64_t, void*> g_herds; // species instance/group -> herd for proxies
// the invariant monitor's damage line (definition at the end of the file)
static void log_hit(uint8_t victim, void* attacker, float amount, const char* how, bool from_host = false);
// guest: the game's own herd of each species (seen on the local NPCs the update filter removes).
// Proxies go into it: Create then gives them the herd's archetype tuning (+0xE84) like any NPC of
// the nest (social games, epic flag, DNA worth need it; docs/re/creature.md 8), and the species
// still has its creatures where the game counts them
static std::map<uint64_t, void*> g_local_herds;
static std::unordered_map<void*, uint32_t> g_herd_serial; // BUG-0843: herd -> its cGameData::mID when cached
static uint32_t g_proxies_in_nest = 0;
static uint64_t herd_key(const game::Key& k) { return ((uint64_t)k.group << 32) | k.instance; }
static std::vector<void*> g_kill;          // local NPCs to remove (guest)
static uint32_t g_proxy_lost = 0;
static double g_last_states_rx = -100; // guest: the last NPC state batch from the host
// BUG-0611: a proxy is stale after 5 s without its state only while the host sends states. After a time
// without any (the host's game stood still under a caption, the death caption, the pause menu) every proxy's
// last state was that old, and the first batch after it dropped every proxy it did not list: the guest lost
// 24 of 25 host NPCs 2 s after the brain upgrade caption that followed another caption (transition_net, lab
// 20261101-145528 of feat/w33-tests-session), and the host, standing still again, counted them as known and
// sent nothing. Now the 5 s count from when the states flow again (g_states_flow_since).
static double g_states_flow_since = -100; // guest: the first batch after a time without any
static uint32_t g_thaw_kept = 0;          // guest: stale drops the thaw rule held back (tests)
// tests (BUG-0611): `coop.npc rxkeep <n> <s>` (guest): for s seconds each MSG_NPC_STATE batch keeps only its
// first n NPCs, as when the other packet of a batch split in two (23 NPCs a packet) is lost
static int g_rxkeep_n = 0;
static double g_rxkeep_until = 0;
static uint32_t g_rxkeep_cut = 0;
// tests (BUG-0894): `coop.npc nanvel <s> [id]` (host): for s seconds the NPC states sent carry a NaN velocity (one NPC
// or all), as a host game with a NaN velocity of its own (the BUG-0292 class) would send them
static double g_nanvel_until = 0;
static int g_nanvel_id = -1;
static bool g_nanvel_air = false; // and marked in the air (NF_AIR: a thrown NPC, the guest's air_place writes the velocity)
static uint32_t g_nanvel_sent = 0;
std::string debug_nanvel(float secs, int id, bool air)
{
    g_nanvel_air = air;
    g_nanvel_until = H->time_seconds() + secs;
    g_nanvel_id = id;
    char b[96];
    snprintf(b, sizeof b, "nanvel %.1f s for %s (%u sent so far)\n", secs, id < 0 ? "every NPC" : "one NPC", g_nanvel_sent);
    return b;
}
std::string debug_rxkeep(int n, float secs)
{
    g_rxkeep_n = n < 0 ? 0 : n;
    g_rxkeep_until = H->time_seconds() + secs;
    char b[96];
    snprintf(b, sizeof b, "rxkeep %d NPCs a state batch for %.1f s\n", g_rxkeep_n, secs);
    return b;
}
static uint32_t g_stale_dropped = 0;   // guest: proxies the host stopped talking about (ghosts)
static uint32_t g_deaths_played = 0; // guest: proxy deaths replayed (Die + animation)
static uint32_t g_removed_local = 0, g_hits_sent = 0, g_hits_applied = 0, g_damage_in = 0, g_kills_credited = 0;

static bool g_host_away = false; // guest: the host is in the main menu, our own game runs the NPCs
// BUG-1174 (owner decision 2 of docs/plan-0.7.15.md, C2: an editor visit is the host away, not a world change): the
// host's editor ended the guests' proxies twice: the guest's away edge destroyed them (its own NPCs took the planet
// over, 4 s into the host's editor), and the host, whose host pass does not run outside its stage, retired every NPC
// 2 s after the editor (their last update was the editor's length ago) and spawned them again under new ids. Now the
// guest holds its proxies (frozen, no states) while the host is in its editor and the host's pass does not count the
// time without a pass as unseen. `coop.fix npc.editor_hold off`: destroyed and made again (0.7.14).
static bool g_editor_hold_fix = true;
COOP_FIX("npc.editor_hold", "BUG-1174", g_editor_hold_fix,
         "creature: the animals on a guest's screen stay (the same, standing) while the host is in its editor");
static bool g_editor_held = false; // guest: holding the proxies now
static uint32_t g_editor_holds = 0, g_host_gaps = 0;
static double g_host_pass_at = 0; // host: when host_tick last passed its avatar check
static uint32_t g_away_proxies = 0;
static uint32_t g_local_alive = 0; // guest: own NPCs that ran Update last frame (host away: the game's)
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

bool is_proxy(void* c)
{
    uint16_t id;
    return px_id_of(c, id);
}
static bool g_strays_on = true; // tests: remove_saved_strays on/off
COOP_FIX("npc.strays", "BUG-0008", g_strays_on, "creature: creatures without tuning in herds like ours (saved puppets) are removed");
void set_strays(bool on) { g_strays_on = on; }
void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }

// called from inside the game's creature update loop: keep it cheap
// BUG-0556: the guest's proxies of a host's summoned flock show the event's hearts as the host's do
static constexpr uint32_t kFlockFx = 0x6A17B05A;
static uintptr_t a_StartFx2, a_StopFx;

// BUG-0412: a guest that leaves the session plays on in its copy of the host's world, and a guest that
// joins while the host is not in that world (its editor, a load) plays on alone too. Two holes left it
// on a planet without creatures: (1) the leave destroyed every proxy and made none of them NPCs of the
// guest's game (away_locals ran only when the host went away); (2) at a (re)join the update filter
// removed the guest's own NPCs from the first frame, before host_here / host_away knew where the host
// was (the away state needs 4 s of the host in its editor), and nothing brought them back. Now the
// leave hands the proxies over to the guest's game like the host's going away (in the next frame, the
// same world only), and a guest that mirrors nothing keeps its own NPCs until the host plays its world.

static bool update_filter(void* c)
{
    if (!g_enabled || !is_guest() || !npc_stage())
        return true;
    if (game::is_puppet(c) || c == game::avatar())
        return true;
    // the host left the world: the guest plays on alone with its own NPCs; BUG-0412: the host is not here yet
    // (D3: npc_proxy_rules.h; host_here is asked only when the host is not away and nothing is mirrored)
    const bool empty = px_empty();
    if (npc_rules::keep_locals(g_host_away, empty, C.host_here != nullptr, !g_host_away && empty && C.host_here && C.host_here()))
        return true;
    if (void* h = game::herd_of(c))
        if (h != game::avatar_herd()) {
            void*& slot = g_local_herds[herd_key(game::species_key(c))];
            if (slot != h || !g_herd_serial.count(h)) // (a live NPC's herd, in its update: its serial is the herd's)
                g_herd_serial[h] = game::object_serial(h);
            slot = h;
        }
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
// BUG-1283 (found by the Lineages fork, its BUG-9115): the alpha mark of the host's nest alphas on the guest (below, in
// the state's apply). `coop.fix npc.alpha off`: a proxy keeps the bit its own game gave it (0.7.15).
static bool g_alpha_fix = true;
COOP_FIX("npc.alpha", "BUG-1283", g_alpha_fix, "creature: a guest sees the host's nest alphas as alphas (the card, the extra social round)");
// the host's build sends NF_ALPHA (npc 1.2 or newer: 0.7.16, net/compat.h kNpcMinorWorldEvents); asked once a second
static bool host_sends_alpha()
{
    static double at = -1e9;
    static bool yes = false;
    double now = H->time_seconds();
    if (now - at < 1.0)
        return yes;
    at = now;
    bool has = false;
    std::vector<net::FamilyRev> fam;
    yes = C.sess && C.sess->host_families(has, fam) && net::compat::host_relays_world_events(fam, has); // guest: the host's WELCOME
    return yes;
}
static constexpr float kPartnerAiRange = 80.f; // m: NPCs farther from a partner than this ignore it
static std::unordered_map<void*, void*> g_ai_target; // host: NPC -> the puppet it treats as the player
static uint32_t g_ai_partner_picks = 0;
void set_partner_targets(bool on) { g_partner_targets = on; }

// BUG-1256: Creature stage guest pack recruitment
static social_logic::PackOwners g_pack_owners; // host: guest-owned pack members (npc_id -> player 1..7)
static bool g_pack_dirty = false;
static uint32_t g_guest_pack_recruited = 0, g_guest_pack_released = 0, g_guest_pack_dismissed = 0;
static std::map<uint16_t, uint8_t> g_guest_pack_owners; // guest: host-synced pack owners
static std::unordered_set<uint16_t> g_guest_my_pack;     // guest: proxy ids added to local mPosseMembers + CreatureUI
static std::unordered_map<uint16_t, void*> g_guest_pack_ptr; // guest: proxy ptr registered in local posse

static uintptr_t a_PosseGet, a_PosseAdd, a_PosseRemove, a_PosseCount, a_PosseMax;
static uintptr_t a_NmAddPosse, a_NmRemovePosse, a_StrategyGet, a_UiAddPosse, a_UiRemovePosse;
using PosseRemove_t = void(SK_THISCALL*)(void*, void*, uint32_t, uint32_t);
static PosseRemove_t o_PosseRemove;

static void* creature_ui()
{
    if (!a_StrategyGet || !game::in_creature_stage())
        return nullptr;
    uint8_t* st = skg::cdecl_call<uint8_t*>(a_StrategyGet);
    if (!st || IsBadReadPtr(st + 0x68, 4))
        return nullptr;
    void* ui = *(void**)(st + 0x68);
    return ui && !IsBadReadPtr(ui, 0x80) ? ui : nullptr;
}

static void guest_pack_untrack_id(uint16_t id, bool remove_from_game)
{
    auto it = g_guest_pack_ptr.find(id);
    if (it == g_guest_pack_ptr.end())
        return;
    void* c = it->second;
    g_guest_pack_ptr.erase(it);
    g_guest_my_pack.erase(id);
    if (!remove_from_game || !c || !game::in_creature_stage() || !game::creature_listed(c))
        return;
    if (void* ui = creature_ui(); ui && a_UiRemovePosse)
        skg::thiscall<void>(a_UiRemovePosse, ui, c);
    if (void* nm = game::noun_manager(); nm && a_NmRemovePosse)
        skg::thiscall<void>(a_NmRemovePosse, nm, c);
}

static void guest_pack_untrack_ptr(void* c)
{
    if (!c || g_guest_pack_ptr.empty())
        return;
    for (auto it = g_guest_pack_ptr.begin(); it != g_guest_pack_ptr.end(); ++it) {
        if (it->second == c) {
            uint16_t id = it->first;
            guest_pack_untrack_id(id, true);
            return;
        }
    }
}

static void SK_THISCALL h_PosseRemove(void* posse, void* c, uint32_t a, uint32_t b)
{
    if (is_guest() && social::g_guest_recruit_fix && c && game::in_creature_stage()) {
        int id = proxy_id(c);
        if (id >= 0 && g_guest_my_pack.count((uint16_t)id)) {
            guest_pack_untrack_id((uint16_t)id, true);
            g_guest_pack_dismissed++;
            social::send_dismiss((uint16_t)id);
            return;
        }
    }
    o_PosseRemove(posse, c, a, b);
}

static void host_pack_remove_creature(void* c)
{
    if (!c || !game::in_creature_stage() || !game::creature_listed(c))
        return;
    void* posse = a_PosseGet ? skg::cdecl_call<void*>(a_PosseGet) : nullptr;
    if (posse && o_PosseRemove && game::pack_member(c))
        o_PosseRemove(posse, c, 1, 0);
    if (!IsBadWritePtr((uint8_t*)c + 0xE68, 1))
        *(uint8_t*)((uint8_t*)c + 0xE68) = 0;
}

static void host_pack_prune()
{
    if (!is_host() || g_pack_owners.owners.empty())
        return;
    std::vector<uint16_t> gone;
    for (auto& [id, owner] : g_pack_owners.owners) {
        void* c = host_npc(id);
        if (!c || game::is_dead(c) || !game::pack_member(c))
            gone.push_back(id);
    }
    for (uint16_t id : gone) {
        g_pack_owners.remove(id);
        g_pack_dirty = true;
    }
}

static void guest_pack_sync()
{
    if (!is_guest() || !C.sess || !game::in_creature_stage())
        return;
    if (!social::g_guest_recruit_fix) {
        std::vector<uint16_t> ids(g_guest_my_pack.begin(), g_guest_my_pack.end());
        for (uint16_t id : ids)
            guest_pack_untrack_id(id, true);
        return;
    }
    uint8_t my = C.sess->my_id();
    std::vector<uint16_t> drop;
    for (uint16_t id : g_guest_my_pack) {
        auto ow = g_guest_pack_owners.find(id);
        void* px = proxy(id);
        auto pi = g_guest_pack_ptr.find(id);
        if (ow == g_guest_pack_owners.end() || ow->second != my || !px || game::is_dead(px) ||
            pi == g_guest_pack_ptr.end() || pi->second != px)
            drop.push_back(id);
    }
    for (uint16_t id : drop)
        guest_pack_untrack_id(id, true);

    void* nm = game::noun_manager();
    void* ui = creature_ui();
    for (auto& [id, owner] : g_guest_pack_owners) {
        if (owner != my || g_guest_my_pack.count(id))
            continue;
        void* px = proxy(id);
        if (!px || game::is_dead(px) || !game::creature_listed(px))
            continue;
        if (nm && a_NmAddPosse)
            skg::thiscall<void>(a_NmAddPosse, nm, px);
        if (ui && a_UiAddPosse)
            skg::thiscall<void>(a_UiAddPosse, ui, px);
        g_guest_my_pack.insert(id);
        g_guest_pack_ptr[id] = px;
    }
}

static void* ai_target(void* c)
{
    if (!g_enabled || !is_host() || !game::in_creature_stage())
        return nullptr;
    // BUG-1256: guest-recruited pack members follow their owner guest's live puppet even if
    // creature.npcs_target_partners is off; host's own pack members keep following the host's avatar.
    if (game::pack_member(c)) {
        int hid = host_id(c);
        uint8_t owner = hid >= 0 ? g_pack_owners.owner_of((uint16_t)hid) : social_logic::kNoOwner;
        if (social::g_guest_recruit_fix && owner >= 1 && owner < 8) {
            void* pup = C.puppet_live ? C.puppet_live(owner) : (C.puppet_of ? C.puppet_of(owner) : nullptr);
            if (pup && !game::is_dead(pup) && (!C.player_dead || !C.player_dead(owner))) {
                g_ai_target[c] = pup;
                return pup;
            }
        }
        g_ai_target.erase(c);
        return nullptr;
    }
    // (the session setting creature.npcs_target_partners, coop.ini npcs_target_partners its default; the fix switch
    // and `coop.npc partners off` stay the test seams)
    if (!g_partner_targets || !settings::on(settings::kNpcTargets))
        return nullptr;
    void* av = game::avatar();
    auto prev = g_ai_target.find(c);
    void* was = prev != g_ai_target.end() ? prev->second : nullptr;
    void* pick = nullptr;
    // the pack and the avatar's family follow and defend the host's avatar
    if (av && !game::is_dead(c) && game::herd_of(c) != game::avatar_herd()) {
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
// BUG-0710: the host NPC's maximum health follows it after the spawn (a baby that grows up: 5 -> 10)
static float read_max_hp(void* c) { return game::max_health(c); }
static void apply_max_hp(void* c, float v)
{
    if (!g_max_hp_fix || !(v > 0) || !std::isfinite(v))
        return;
    uint16_t id;
    PxE* pe = px_id_of(c, id) ? px_find(id) : nullptr;
    if (!pe)
        return;
    pe->data.max_health = v;
    if (!pe->data.killed && creature_hp::drive_puppet(c, game::health(c), v))
        g_max_hp_applied++;
}
static const fields::Field kNpcFields[] = {fields::kScale, {fields::F_MAX_HP, "max_hp", read_max_hp, apply_max_hp, 0.01f}};

static void npc_each(void (*cb)(uint16_t, void*, void*), void* u)
{
    for (void* c : g_seen_now) {
        HostNpc* it = hn_of(c);
        // CR-2 (BUG-1428): g_seen_now is the last host pass's (none while the host has no avatar or is in its editor):
        // a creature the game removed since is not read (its destroyed byte is freed memory then)
        if (it && (g_msg_live_fix ? ref_alive(g_hreg.find(it->id)->ref) : !game::is_destroyed(c)))
            cb(it->id, c, u);
    }
}

static void* handler_c(const PxE& pe);
static void* npc_mirror(uint16_t id)
{
    auto it = px_find(id);
    if (it == nullptr || !it->ref.p || game::is_destroyed(it->ref.p))
        return nullptr;
    return handler_c(*it); // BUG-0805
}

// ------------------------------------------------------------------ BUG-0487: resync of unknown ids (G5)
// The host sends an NPC's spawn once per guest (g_known); a guest that has no proxy of an id it gets
// states for (its proxy dropped as stale while the host still counts it as known, a spawn that came
// while the guest was in another world, a world change on the guest) dropped those states for good: a
// host NPC near the guest without a proxy, or a proxy whose death / health never came. Now the guest
// asks the host for the spawn of every id it gets states for and does not know (at most once per id in
// kResyncEvery s), and the host sends it again.
constexpr double kResyncEvery = 2.0;
static std::map<uint16_t, double> g_unknown_ids; // guest: id -> when it was asked for last (0: not yet)
static double g_resync_at = 0;
static uint32_t g_resync_asked = 0, g_resync_sent = 0, g_forgotten = 0;
static void destroy_proxy(uint16_t id);

static void resync_tick(double now)
{
    if (g_unknown_ids.empty() || now - g_resync_at < 0.5)
        return;
    g_resync_at = now;
    std::vector<uint16_t> ask;
    for (auto it = g_unknown_ids.begin(); it != g_unknown_ids.end();) {
        if (px_has(it->first)) { // the spawn came
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
    if (!px_has(id))
        return "no proxy " + std::to_string(id) + "\n";
    destroy_proxy(id);
    g_forgotten++;
    return "forgot proxy " + std::to_string(id) + " (the host still counts it as known)\n";
}

// guest (tests, BUG-0931): the proxy of `id` loses its creature behind the mod's back and its pointer names memory
// that is no creature (a zeroed block: a null vtable, as the freed block of the report), as when the game removed
// and freed it between two checks
std::string debug_ghost(uint16_t id)
{
    auto it = px_find(id);
    if (it == nullptr || !it->ref.p || game::is_destroyed(it->ref.p))
        return "no live proxy " + std::to_string(id) + "\n";
    if (!g_ghost)
        g_ghost = (uint8_t*)VirtualAlloc(nullptr, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_ghost)
        return "no memory\n";
    memset(g_ghost, 0, 0x4000);
    void* real = it->ref.p;
    px_rename(*it, g_ghost);
    creature_hp::forget_puppet(real);
    game::destroy_creature(real);
    char b[120];
    snprintf(b, sizeof b, "proxy %u: its creature %p destroyed, the proxy now names %p (no creature)\n", (unsigned)id, real, (void*)g_ghost);
    return b;
}

std::string debug_gamekill(uint16_t id)
{
    auto it = px_find(id);
    if (it == nullptr || !it->ref.p || game::is_destroyed(it->ref.p))
        return "no live proxy " + std::to_string(id) + "\n";
    void* c = it->ref.p;
    if (!gamefixes::count_destroys(c))
        return "no DestroyInstance hook: cannot count\n";
    game::destroy_untracked(c); // the game's own destroy: is_destroyed(c) from now on
    bool gone = game::is_destroyed(c);
    destroy_proxy(id);
    unsigned n = gamefixes::destroys_counted();
    gamefixes::count_destroys(nullptr);
    char b[160];
    snprintf(b, sizeof b, "gamekill proxy %u: destroyed_by_game=%d destroy_calls=%u fix=%d\n", (unsigned)id, (int)gone, n,
             (int)g_destroy_once_fix);
    return b;
}

std::string resync_status()
{
    char b[200];
    snprintf(b, sizeof b, "npc resync: unknown=%zu asked=%u spawns_resent=%u forgotten=%u\n",
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
    a_StartFx2 = H->sym("Simulator::cCreatureBase::StartOrStopEffectFromPool2");
    a_StopFx = H->sym("Simulator::cCreatureBase::StopEffectFromPools");
    a_PosseGet = H->sym("SporeCoop::cPosseSimulator_Get");
    a_PosseAdd = H->sym("SporeCoop::cPosseSimulator_AddMember");
    a_PosseRemove = H->sym("SporeCoop::cPosseSimulator_RemoveMember");
    a_PosseCount = H->sym("SporeCoop::cPosseSimulator_GetMemberCount");
    a_PosseMax = H->sym("SporeCoop::cPosseSimulator_GetMaxMembers");
    a_NmAddPosse = H->sym("SporeCoop::cGameNounManager_AddPosseMember");
    a_NmRemovePosse = H->sym("SporeCoop::cGameNounManager_RemovePosseMember");
    a_StrategyGet = H->sym("App::cCreatureModeStrategy::Get");
    a_UiAddPosse = H->sym("SporeCoop::CreatureUI_AddPosseCard");
    a_UiRemovePosse = H->sym("SporeCoop::CreatureUI_RemovePosseCard");
    if (a_PosseRemove)
        H->hook_create(M, (void*)a_PosseRemove, (void*)h_PosseRemove, (void**)&o_PosseRemove);
    game::set_behaviour_anim_listener(on_behaviour_anim); // BUG-0464
    fields::add_kind({fields::K_NPC, "npc", nullptr, kNpcFields, 2, npc_each, npc_mirror, nullptr}); // BUG-0461
    H->command_add(M, "coop.npctrack", "coop.npctrack start [radius m [x y z]]|stop|count|dump [from] [n]|walk <id> x y z <m/s>|put <id> x y z [secs]|shove <id> <m> x y z <m/s> - every frame: the guest's NPC proxies (tests, BUG-0361)",
                   c_npctrack, nullptr, SK_CMD_MAIN_THREAD);
}

// BUG-0462: an NPC's archetype tuning (+0xE84) decides its relation to the player when its species has no
// record (GetRelationship 0xBA40E0 -> tuning +0x390), and the social games and the avatar-event handler read
// it. Create takes it from the species profile (+0x58C archetype, +0x5F4 variant) through the species
// manager's tuning map (0x4E00C0): a proxy of a species the guest's game knows differently (lane 9: 06805133)
// was made without tuning, so its relation came from the diet / size rule (1, hostile, where the host's NPC
// had 3). The tuning is a node of that map: its key is the dword before it. The host sends the key after
// the spawn; the guest gives the proxy the same tuning (SetTuning 0xC0C2E0) when it has another or none.
static uint32_t g_tunings_set = 0, g_tunings_missing = 0, g_puppet_tunings_set = 0; // (puppets: BUG-0722)

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

static void proxy_tuning(PxE& pe)
{
    Proxy& px = pe.data;
    if (!pe.ref.p || !px.tuning || px.tuning_set == px.tuning)
        return;
    px.tuning_set = px.tuning;
    if (tuning_key(pe.ref.p) == px.tuning)
        return;
    static uintptr_t set = H->sym("SporeCoop::cCreatureBase_SetTuning");
    void* t = tuning_of_key(px.tuning);
    if (!t || !set) {
        g_tunings_missing++;
        return;
    }
    skg::thiscall<void>(set, pe.ref.p, t);
    g_tunings_set++;
}

// BUG-0669 (docs/re/tribe.md#tribe-epic-steps): the footsteps of an animated creature play (0xA01000 ->
// FUN_009A6BD0) while its animation blends a gait in (+0xFAC of the animation's owner) or always while it moves when
// its animation's epic flag (AnimatedCreature +0x17C -> +0x2D5) is set; the game sets that flag from the tuning's
// isEpic (+0x388) only when the creature's model is set up (0xC21D50 -> 0xA04C00). A proxy gets the host's tuning
// later (proxy_tuning: SetTuning does not touch the animation), so the guest's copy of the host's epic walked
// silently (tribe fixture: the host's game played 35-52 footsteps of its epic 0654E5CB a minute, the guest's none).
// The proxy's animation flag follows its tuning now, while the host's NPC walks: a proxy that stands still turns a
// little every frame under the mod's facing (its animation's turn +0x166C up to 1 rad/s where the host's was 0), which
// passes the footsteps' gate with the flag (first try: 256 footsteps on the guest against the host's 38 a minute).
// `coop.fix npc.epic_steps off`: as Create left it (0.7.9).
static bool g_epic_steps_fix = true;
COOP_FIX("npc.epic_steps", "BUG-0669", g_epic_steps_fix,
         "creature: the guest hears the footsteps of the host's epic creatures (their animation's epic flag)");
static uint32_t g_epic_steps_set = 0;
static void proxy_epic_steps(void* c, bool moving)
{
    static uintptr_t set = H->sym("SporeCoop::AnimatedCreature_SetEpicFlag");
    if (!g_epic_steps_fix || !c || !set || !skg::readable((uint8_t*)c + 0xB54, 4) || !skg::readable((uint8_t*)c + 0xE84, 4))
        return;
    uint8_t* t = *(uint8_t**)((uint8_t*)c + 0xE84);
    uint8_t* ac = *(uint8_t**)((uint8_t*)c + 0xB54);
    if (!ac || !skg::readable(ac + 0x17C, 4))
        return;
    uint8_t* cid = *(uint8_t**)(ac + 0x17C);
    if (!cid || !skg::readable(cid + 0x2D5, 1))
        return;
    uint8_t want = moving && t && skg::readable(t + 0x388, 1) && t[0x388] ? 1 : 0;
    if (cid[0x2D5] == want)
        return;
    skg::thiscall<void>(set, ac, (uint32_t)want);
    g_epic_steps_set++;
}

// tests: a proxy without tuning, as Create makes one in a herd of ours (the species' nest herd not found yet:
// lane 9, the first proxies of species 06805133 after a join); the fix gives it the host's again
std::string debug_untune(uint16_t id)
{
    static uintptr_t set = H->sym("SporeCoop::cCreatureBase_SetTuning");
    auto it = px_find(id);
    if (it == nullptr || !it->ref.p || !set)
        return "no proxy " + std::to_string(id) + "\n";
    skg::thiscall<void>(set, it->ref.p, (void*)nullptr);
    it->data.tuning_set = 0;
    // BUG-0669: and its animation's epic flag as the model setup of a creature without tuning leaves it (0)
    static uintptr_t epic = H->sym("SporeCoop::AnimatedCreature_SetEpicFlag");
    uint8_t* ac = skg::readable((uint8_t*)it->ref.p + 0xB54, 4) ? *(uint8_t**)((uint8_t*)it->ref.p + 0xB54) : nullptr;
    if (epic && ac && skg::readable(ac + 0x17C, 4) && *(void**)(ac + 0x17C))
        skg::thiscall<void>(epic, ac, 0u);
    return "proxy " + std::to_string(id) + " has no tuning now\n";
}

std::string tuning_status()
{
    char b[120];
    snprintf(b, sizeof b, "npc tuning: set=%u missing=%u puppets_set=%u epic_steps=%u\n", g_tunings_set, g_tunings_missing, g_puppet_tunings_set,
             g_epic_steps_set);
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
    tribe::on_behaviour_anim(c, anim, how, a, b); // BUG-0569: a member's (its own checks)
    if (!is_host() || !npc_stage())
        return;
    HostNpc* it = hn_of(c);
    if (!it || it->dead)
        return;
    it->bhv_anim = anim;
    if (g_anim_out.size() >= 200) {
        g_anims_dropped++;
        return;
    }
    g_anim_out.push_back({it->id, anim, (uint8_t)how, (uint8_t)(a & 0xFF), (int32_t)b});
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
        void* b = hn_obj(id);
        if (!b || !g_seen_now.count(b))
            continue;
        HostNpc* hn = hn_of(b);
        uint32_t cur = 0;
        if (!hn || !game::drawn_state(b, nullptr, &cur) || !cur)
            continue;
        if (cur == hn->bhv_anim || game::is_dead(b))
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
    snprintf(b, sizeof b, "npc anims: seen=%u idle=%u sent=%u dropped=%u rx=%u played=%u noproxy=%u failed=%u "
             "snap_sent=%u snap_rx=%u snap_played=%u\n",
             seen, idle, g_anims_sent, g_anims_dropped, g_anims_rx, g_anims_played, g_anims_noproxy,
             g_anims_failed, g_anims_snap_sent, g_anims_snap_rx, g_anims_snap_played);
    return b;
}

// tests (BUG-0464): the animation each NPC plays now, as the game holds it (vt 0x58 GetCurrentAnimation):
// host: its NPCs by net id, guest: the proxies
std::string current_anims()
{
    std::string out;
    char b[200];
    auto line = [&](int id, void* c) {
        uint32_t a = 0;
        net::Vec3 m;
        if (c && game::creature_listed(c) && game::drawn_state(c, &m, &a)) {
            snprintf(b, sizeof b, "cur %d anim=%08X%s\n", id, a, game::is_dead(c) ? " DEAD" : "");
            out += b;
        }
    };
    if (is_host())
        for (auto [c, hn] : hn_walk())
            line(hn.id, c);
    else
        for (auto [id, pe, px] : px_walk())
            line(id, pe.ref.p);
    // BUG-0891: the own avatar and the partners' puppets the same way (`cur avatar` / `cur p<id>`): what a social
    // ability plays on each game
    auto named = [&](const char* name, void* c) {
        uint32_t a = 0;
        net::Vec3 m;
        if (c && game::creature_listed(c) && game::drawn_state(c, &m, &a)) {
            // BUG-1241 (tests): the creature's party bits here: the pack (+0xB58 & 0x100), the avatar's herd (family)
            // BUG-1241 / BUG-1252 (tests): the ability it performs now (+0xE8C) and its animation, its food meter
            // (+0xBB8 the meter's change a second, +0xCC0 the mood 0xC02F50 gives the thought bubble from)
            snprintf(b, sizeof b, "cur %s anim=%08X pack=%d family=%d ability=%d ability_anim=%08X hunger=%.1f hunger_delta=%.3f "
                     "mood=%d\n", name, a, (int)game::pack_member(c),
                     (int)(game::herd_of(c) && game::herd_of(c) == game::avatar_herd()), (int)game::ability_in_use(c),
                     game::ability_anim_in_use(c), game::hunger(c), *(float*)((uint8_t*)c + 0xBB8),
                     *(int32_t*)((uint8_t*)c + 0xCC0));
            out += b;
        }
    };
    named("avatar", game::avatar());
    if (C.sess && C.puppet_of)
        for (auto& p : C.sess->players())
            if (p.id != C.sess->my_id()) {
                char nm[8];
                snprintf(nm, sizeof nm, "p%u", (unsigned)p.id);
                named(nm, C.puppet_of(p.id));
            }
    return out;
}

static void dash_velocity(uint16_t id, const net::Vec3& pos, net::Vec3& vel); // BUG-0753 (tests)
static net::NpcState host_state(uint16_t id, void* c)
{
    net::NpcState s;
    s.net_id = id;
    s.pos = game::position(c);
    s.rot = game::orientation(c);
    s.vel = game::velocity(c);
    // BUG-0753 (tools.dash_after_send): a dashing NPC moved by the test's write moves at the dash's velocity (the game's own
    // velocity after its update is its locomotion's, not the written path's), when the game kept the write (the mate's
    // is not kept: then its own)
    dash_velocity(id, s.pos, s.vel);
    s.health = game::health(c);
    s.flags = (!game::is_dead(c) && drive::body_airborne(c) ? net::NF_AIR : 0) | // BUG-0550
              (game::is_dead(c) ? net::NF_DEAD : 0) | (game::impressed(c) ? net::NF_IMPRESSED : 0) |
              (game::pack_member(c) ? net::NF_PACK : 0) | // BUG-0556: the host's pack (a Summon Flock) is the party's
              (game::alpha(c) ? net::NF_ALPHA : 0) |      // BUG-1283: the nest's alpha
              (game::herd_of(c) && skg::readable(game::herd_of(c), 0x194) && skg::field<uint8_t>(game::herd_of(c), 0x192)
                   ? net::NF_SUMMONED
                   : 0);
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
               (av && game::herd_of(c) && game::herd_of(c) == game::herd_of(av) ? net::NF_FAMILY : 0) |
               (game::alpha(c) ? net::NF_ALPHA : 0); // BUG-1283
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
    g_ai_target.erase(c);
    teamplay::forget_target(c);
}

// host: tracked NPCs that did not update this frame and are no creature in the world any more: freed
// (not in the noun list) or back in the creature pool (still listed, no herd, at (0 0 0)). Their ids
// go now. Fields are read only of objects still in the noun list (a freed one may be any memory).
static uint32_t g_pooled_retired = 0;
static void retire_pooled()
{
    // C1: the entry's object is the one the id was given to and in the game's list now (game::alive)
    std::vector<std::pair<uint16_t, void*>> gone;
    g_hreg.for_each([&](HostReg::Entry& e) {
        void* c = e.ref.p;
        if (!c || g_seen_now.count(c))
            return;
        bool g = !ref_alive(e.ref);
        if (!g) {
            net::Vec3 p = game::position(c);
            g = !game::herd_of(c) || (p.x == 0.f && p.y == 0.f && p.z == 0.f);
        }
        if (g)
            gone.push_back({e.id, c});
    });
    for (auto& [id, c] : gone) {
        retire(id, c);
        g_hreg.erase(id);
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
    float vel_k = 1.f; // BUG-0440 (tests, `coop.npc dashto ... <k>`): the velocity written and sent, times k
    // BUG-0753: what the last dash_tick wrote (index 0 the dasher, 1 the mate), for host_state's velocity
    net::Vec3 w[2], v[2];
    bool wrote[2] = {false, false};
};
static Dash g_dash;
static net::Vec3 dash_point(int k) // corner k (0..4) of the zigzag
{
    float fw = g_dash.len * k / 4.f, sw = (k == 0 || k == 4) ? 0.f : (k & 1 ? g_dash.width : -g_dash.width);
    return {g_dash.a.x + g_dash.dir.x * fw + g_dash.side.x * sw, g_dash.a.y + g_dash.dir.y * fw + g_dash.side.y * sw,
            g_dash.a.z + g_dash.dir.z * fw + g_dash.side.z * sw};
}
// BUG-0753 (tests, `coop.fix tools.dash_after_send`): `coop.npc dash` moves its NPCs (set_position) after the game's
// update; called before the states went out, the states carried the test's own write. The game does not keep that write
// for the mate (on the host the mate stayed 7-9 m from the dashing NPC while its states said 1.2 m beside:
// creature_npc_dash's "one NPC 3.6-12 m off for the whole run, raw lead too", lab 20261108-092241 rows of
// fix/w46-g4-tests). After the send the states are what the game has, as in play. Off: before the send (the test as it was).
static bool g_dash_after_send = true;
COOP_FIX("tools.dash_after_send", "BUG-0753", g_dash_after_send, "tests: coop.npc dash moves its NPCs after the states went out (the states are the game's own positions)");
static void dash_velocity(uint16_t id, const net::Vec3& pos, net::Vec3& vel)
{
    if (g_dash_after_send && g_dash.id)
        for (int m = 0; m < 2; m++)
            if (g_dash.wrote[m] && id == (m ? g_dash.mate : g_dash.id) &&
                net::dist(pos, g_dash.w[m]) < 0.5f + drive::speed(g_dash.v[m]) * 0.1f)
                vel = g_dash.v[m];
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
        float sc = g_dash.speed / leg[k] * (back ? -1.f : 1.f) * g_dash.vel_k; // (vel_k: BUG-0440 tests)
        v = {(p1.x - p0.x) * sc, (p1.y - p0.y) * sc, (p1.z - p0.z) * sc};
    }
    for (int m = 0; m < 2; m++) {
        void* it = hn_obj(m ? g_dash.mate : g_dash.id);
        if ((m && !g_dash.mate) || !it || !g_seen_now.count(it))
            continue;
        void* c = it;
        float o = m ? g_dash.off : 0.f;
        net::Vec3 w{q.x + g_dash.side.x * o, q.y + g_dash.side.y * o, q.z + g_dash.side.z * o};
        float r = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z), gr = game::ground_radius(w);
        if (r > 1 && gr > 1)
            w = {w.x / r * gr, w.y / r * gr, w.z / r * gr};
        game::set_position(c, w);
        *(net::Vec3*)((uint8_t*)c + game::kVelOff) = v;
        g_dash.w[m] = w;
        g_dash.v[m] = v;
        g_dash.wrote[m] = true;
    }
}
// tests (BUG-0440): `coop.npc raised <r> [x y z]`: the nests (cNest 0x52AA6122) within r m of the avatar (or the point) and how far this
// game's surface with its collision objects (cPlanetModel_SurfacePoint: a nest mound, a rock) lies over the plain
// terrain (GetHeightAt) at their centre and 2 / 4 / 6 / 8 m from it
std::string debug_raised(float rad, const net::Vec3* at)
{
    void* av = game::avatar();
    if (!av && !at)
        return "no avatar";
    struct U {
        net::Vec3 a;
        float r;
        std::string s;
        int n;
    } u{at ? *at : game::position(av), rad, "", 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* up) {
            auto* u = (U*)up;
            net::Vec3 q;
            if (noun != 0x52AA6122u || u->n >= 12 || !game::object_position(o, q) || net::dist(q, u->a) > u->r)
                return true;
            float qr = drive::speed(q);
            net::Vec3 n{q.x / qr, q.y / qr, q.z / qr};
            net::Vec3 e = std::fabs(n.x) < 0.9f ? net::Vec3{0, -n.z, n.y} : net::Vec3{-n.z, 0, n.x};
            float el = drive::speed(e);
            e = {e.x / el, e.y / el, e.z / el};
            char b[160];
            snprintf(b, sizeof b, "nest (%.1f %.1f %.1f) %.1f m away, raised:", q.x, q.y, q.z, net::dist(q, u->a));
            u->s += b;
            for (int m = 0; m <= 8; m += 2) {
                net::Vec3 w{q.x + e.x * m, q.y + e.y * m, q.z + e.z * m};
                snprintf(b, sizeof b, " %d m %.2f", m, game::surface_radius(w) - game::ground_radius(w));
                u->s += b;
            }
            u->s += "\n";
            u->n++;
            return true;
        },
        &u);
    return u.s.empty() ? "no nest" : u.s;
}
// tests (BUG-0440): `coop.npc bhv <id> <anim hex>` (host): NPC #id plays a behaviour animation as its BT does
// (SporeCoop::CreaturePlayAnim), reported to the relay as the BT's own starts are
std::string debug_bhv(uint16_t id, uint32_t anim)
{
    void* c = host_npc(id);
    if (!is_host() || !c)
        return "no such host npc";
    bool ok = game::play_behaviour_anim(c, anim, 0, 1, -1);
    if (ok)
        on_behaviour_anim(c, anim, 0, 1, -1);
    char b[96];
    snprintf(b, sizeof b, "npc %u plays %08X: %s", id, anim, ok ? "started" : "not played");
    return b;
}
// tests (BUG-0440): `coop.npc pushback <id> <m/s> <s>` (guest): while the proxy is walked, the game's update moves it
// back (away from its target) at m/s for s seconds: the hold seen in the reports, planted
std::string debug_pushback(uint16_t id, float mps, float secs)
{
    auto it = px_find(id);
    if (it == nullptr || !it->ref.p)
        return "no such spawned proxy";
    it->data.push_until = H->time_seconds() + secs;
    it->data.push_mps = mps;
    char b[96];
    snprintf(b, sizeof b, "proxy %u pushed back at %.1f m/s for %.0f s in its game's updates", id, mps, secs);
    return b;
}
// tests (BUG-0339): host NPC #id goes back to the game's creature pool, as the relevance pass does it
// (creature vt 0xC8, thiscall(c, 1): push 1 at 0xACE88C in Relevance_Update)
std::string debug_pool(uint16_t id)
{
    void* it = hn_obj(id);
    if (!is_host() || !it || !g_seen_now.count(it))
        return "no such host npc";
    void* c = it;
    skg::vcall<void>(c, 0xC8 / 4, 1);
    net::Vec3 p = game::position(c);
    char b[120];
    snprintf(b, sizeof b, "npc %u pooled: now at (%.1f %.1f %.1f) herd %p", id, p.x, p.y, p.z, game::herd_of(c));
    return b;
}
// tests (BUG-1428): the host's game removes NPC #id (its own DestroyInstance, no bookkeeping of ours) and the host's
// pass does not run for secs (as in its editor or without an avatar): the entry and g_seen_now still name it
std::string debug_gone(uint16_t id, float secs)
{
    void* c = host_npc(id);
    if (!is_host() || !c)
        return "no such host npc";
    g_test_pass_hold_until = H->time_seconds() + std::clamp(secs, 0.f, 30.f);
    game::destroy_untracked(c);
    char b[120];
    snprintf(b, sizeof b, "npc %u removed by the game; host pass held %.1f s; alive now %d", id, secs,
             host_npc(id) != nullptr ? 1 : 0);
    return b;
}
// tests (BUG-0281): the guest's proxy #id walks to p at speed with the game's locomotion for secs (no sync)
std::string debug_pwalk(uint16_t id, const net::Vec3& p, float speed, float secs, int mode)
{
    auto it = px_find(id);
    if (it == nullptr || !it->ref.p)
        return "no such spawned proxy";
    it->data.manual_until = H->time_seconds() + secs;
    it->data.manual_mode = mode;
    it->data.manual_to = p;
    it->data.manual_speed = speed;
    game::set_speed(it->ref.p, speed);
    game::move_to(it->ref.p, p, 0.3f);
    net::Vec3 c = game::position(it->ref.p);
    char b[160];
    snprintf(b, sizeof b, "proxy %u at (%.1f %.1f %.1f) walks to (%.1f %.1f %.1f) at %.1f m/s for %.0f s", id, c.x, c.y, c.z,
             p.x, p.y, p.z, speed, secs);
    return b;
}
// tests (BUG-0399): the guest's proxy #id put <m> under this game's ground once (where the monitor found
// proxies after joins); the sync drives it on from there
std::string debug_lift(uint16_t id, float m, float secs)
{
    if (!is_host() || !host_npc(id))
        return "no such host npc";
    g_lift_id = id;
    g_lift_m = m;
    g_lift_until = H->time_seconds() + secs;
    char b[96];
    snprintf(b, sizeof b, "npc %u held %.2f m over its ground for %.1f s", id, m, secs);
    return b;
}

std::string debug_sink(uint16_t id, float m, bool mark)
{
    auto it = px_find(id);
    if (it == nullptr || !it->ref.p)
        return "no such spawned proxy";
    void* c = it->ref.p;
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
std::string debug_dash(uint16_t id, float len, float speed, float secs, float width, uint16_t mate, float off, const net::Vec3* to, float vel_k)
{
    void* c = host_npc(id);
    if (!is_host() || !c || (len <= 0 && !to) || speed <= 0)
        return "no such host npc";
    net::Vec3 p = game::position(c);
    float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    net::Vec3 up = r > 1 ? net::Vec3{p.x / r, p.y / r, p.z / r} : net::Vec3{0, 0, 1};
    net::Vec3 a = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
    if (to) { // BUG-0440 (`coop.npc dashto`): straight towards a point and back (the zigzag's legs on one line)
        net::Vec3 d{to->x - p.x, to->y - p.y, to->z - p.z};
        float k = d.x * up.x + d.y * up.y + d.z * up.z;
        a = {d.x - up.x * k, d.y - up.y * k, d.z - up.z * k};
        len = drive::speed(a);
        if (len < 1.f)
            return "the point is too near";
        width = 0;
    }
    float al = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    a = {a.x / al, a.y / al, a.z / al};
    net::Vec3 b{up.y * a.z - up.z * a.y, up.z * a.x - up.x * a.z, up.x * a.y - up.y * a.x}; // up x a
    void* mc = host_npc(mate);
    g_dash = {id, p, a, b, len, speed, width, H->time_seconds(), H->time_seconds() + secs, mc ? mate : (uint16_t)0, off};
    g_dash.vel_k = vel_k;
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
    uint8_t how;   // 0 driven (drive::step), 1 placed (BUG-0281), 2 killed, 3 walked (BUG-0361)
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
    float ag = 0; // BUG-0550: the drawn position's height over this game's terrain (m)
    float mag = 0; // and the model's (AnimatedCreature +0x04)
    int32_t lm = -1; // BUG-0409: the locomotion mode (+0x2B0: 0 normal, 3 thrown by a hit, 4 abducted)
    int8_t gr = -1;  // the grounded byte (+0x137)
    // BUG-1357 (test of npc_air_under): the host state's height over the HOST's terrain as this guest received it (the
    // agl trailer, interpolated), 99 = none (an older host, the host's own rows). The host's own rows cannot show an
    // NPC pressed under its ground: its game puts it back onto the terrain in its update before it draws it
    float sa = 99.f;
    // BUG-1360 diagnostics (a walked proxy playing the standing idle 02481DE5 on a straight 30 fps run): the locomotion's
    // own request, read without a call (docs/re/creature.md, the fork's `?` offsets: spatial +0x1E0 desired speed, the
    // current movement request creature +0x2C8 destination / +0x310 mode, 1 MoveTo, 0 Stop); -1 = not readable
    float dsp = -1.f;  // desired speed (creature +0xC0 +0x1E0)
    int8_t mmode = -1; // movement request mode (creature +0x310)
    float gdist = -1.f; // m from the drawn position to the request's destination (creature +0x2C8)
};
static std::deque<NpcTrackRow> g_ntrack;
static bool g_ntrack_on = false;
static double g_ntrack_t0 = 0;
static float g_ntrack_r = 40.f;
static constexpr size_t kNtrackMax = 200000;

// the guest's avatar, in Tribe the player tribe (no avatar there)
static net::Vec3 g_ntrack_c{}; // tests (BUG-1228): `start <r> x y z` measures around this point (Civ: no avatar)
static bool g_ntrack_c_set = false;
static bool ntrack_centre(net::Vec3& c)
{
    if (g_ntrack_c_set) {
        c = g_ntrack_c;
        return true;
    }
    if (void* av = game::avatar()) {
        c = game::position(av);
        return true;
    }
    return rts_stage() && tribe::center(c);
}

static void ntrack_push(uint16_t id, void* c, uint8_t how, uint8_t flags, uint16_t tp, const net::Vec3& drawn,
                        const net::Vec3& tgt, float rv, double now, float sa = 99.f)
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
    x.ag = drive::over_ground(drawn);
    x.mag = drive::over_ground(x.m);
    x.lm = *(int32_t*)((uint8_t*)c + game::kLocoModeOff);
    x.gr = (int8_t)*((uint8_t*)c + 0x137);
    x.sa = sa;
    if (skg::readable(c, 0x318)) {
        const uint8_t* b = (const uint8_t*)c;
        x.dsp = *(const float*)(b + game::kSpatialOff + 0x1E0);
        x.mmode = (int8_t)*(const int32_t*)(b + 0x310);
        x.gdist = net::dist(drawn, *(const net::Vec3*)(b + 0x2C8));
    }
    g_ntrack.push_back(x);
    while (g_ntrack.size() > kNtrackMax)
        g_ntrack.pop_front();
}

static void ntrack_row(uint16_t id, const PxE& pe, const net::PlayerState& s, const net::Vec3& drawn, uint8_t how,
                       double now, float rv)
{
    ntrack_push(id, pe.ref.p, how, s.flags, (uint16_t)pe.data.drv.teleports, drawn, s.pos, rv, now,
                net::agl_known(s.agl) ? s.agl : 99.f);
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
        g_ntrack_c_set = argc > 5;
        if (g_ntrack_c_set)
            g_ntrack_c = {(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])};
    } else if (!strcmp(a, "stop"))
        g_ntrack_on = false;
    else if (!strcmp(a, "dump")) {
        // dump [from] [n] (how: 0 driven, 1 placed, 2 killed, 3 walked, 4 an NPC of the host itself, 5 in the air): t id how flags tp dx dy dz px py pz tx ty tz gv rv da anim gait gait2 moving ss vis sx sy mx my mz ag mag lm gr sa dsp mmode gdist
        size_t from = argc > 2 ? (size_t)atol(argv[2]) : 0, n = argc > 3 ? (size_t)atol(argv[3]) : 2000;
        for (size_t i = from; i < g_ntrack.size() && i < from + n; i++) {
            const auto& x = g_ntrack[i];
            H->out_printf(out, "%.4f %u %u %u %u %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.2f %.2f %.1f %08X %d %d %d %d %d %.0f %.0f %.3f %.3f %.3f %.3f %.3f %d %d %.3f %.2f %d %.2f\n",
                          x.t, x.id, x.how, x.flags, x.tp, x.d.x, x.d.y, x.d.z, x.p.x, x.p.y, x.p.z, x.tgt.x, x.tgt.y,
                          x.tgt.z, x.gv, x.rv, x.da, x.anim, x.gait, x.gait2, x.moving, x.speed_state, x.vis, x.sx, x.sy, x.m.x,
                          x.m.y, x.m.z, x.ag, x.mag, x.lm, x.gr, x.sa, x.dsp, (int)x.mmode, x.gdist);
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
        if (argc > 2) {
            g_npc_novel = atoi(argv[2]) != 0;
            if (g_walk_gait_fix) // BUG-1360: no proxy keeps a reported walk's speed into the test's unreported walks
                for (auto [id, pe, px] : px_walk())
                    px.walk_sp = 0;
        }
        H->out_printf(out, "npc novel %d\n", (int)g_npc_novel);
        return 0;
    } else if (!strcmp(a, "walksp") && argc > 2) { // tests (BUG-1360): every proxy as if its last reported walk ran at V m/s
        const float v = (float)atof(argv[2]);
        int n = 0;
        for (auto [id, pe, px] : px_walk()) {
            px.walk_sp = v;
            n++;
        }
        H->out_printf(out, "npc walksp %.2f on %d proxies\n", v, n);
        return 0;
    } else if (!strcmp(a, "variant") && argc > 2) { // tests (BUG-0361): how a walked proxy moves
        g_walk_variant = atoi(argv[2]);
        H->out_printf(out, "npc.walk variant %d (0 walk goal + place, 1 place only, 2 walk goal + teleport, 3 no teleport when the place was lost)\n", g_walk_variant);
        return 0;
    } else if (!strcmp(a, "shove") && argc > 7) {
        // host (tests, BUG-0997): host NPC #id put <m> metres to the side at once (as a hit throws it), then walked
        // on to x y z at <m/s>: shove <id> <m> x y z <m/s>
        void* c = host_npc((uint16_t)atoi(argv[2]));
        if (!is_host() || !c) {
            H->out_printf(out, "no such host npc\n");
            return 1;
        }
        net::Vec3 p = game::position(c);
        float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (l < 1.f) {
            H->out_printf(out, "no position\n");
            return 1;
        }
        net::Vec3 n{p.x / l, p.y / l, p.z / l};
        net::Vec3 ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
        net::Vec3 u{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z), m = (float)atof(argv[3]);
        net::Vec3 q{p.x + u.x / ul * m, p.y + u.y / ul * m, p.z + u.z / ul * m};
        float g = game::ground_radius(q), ql = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        if (g > 1.f && ql > 1.f)
            q = {q.x / ql * g, q.y / ql * g, q.z / ql * g};
        game::teleport(c, q, game::orientation(c));
        net::Vec3 to{(float)atof(argv[4]), (float)atof(argv[5]), (float)atof(argv[6])};
        game::set_speed(c, (float)atof(argv[7]));
        game::move_to(c, to, 0.5f);
        H->out_printf(out, "npc %s put from (%.1f %.1f %.1f) to (%.1f %.1f %.1f), walks on to (%.1f %.1f %.1f)\n", argv[2], p.x,
                      p.y, p.z, q.x, q.y, q.z, to.x, to.y, to.z);
        return 0;
    } else if (!strcmp(a, "put") && argc > 5) {
        // host (tests, BUG-1228): host NPC #id put onto this game's ground at x y z and stopped there, held there for secs
        // (put every frame, no velocity): put <id> x y z [secs]
        void* c = host_npc((uint16_t)atoi(argv[2]));
        if (!is_host() || !c) {
            H->out_printf(out, "no such host npc\n");
            return 1;
        }
        net::Vec3 q{(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])};
        float g = game::ground_radius(q), ql = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        if (g > 1.f && ql > 1.f)
            q = {q.x / ql * g, q.y / ql * g, q.z / ql * g};
        game::teleport(c, q, game::orientation(c));
        *(net::Vec3*)((uint8_t*)c + game::kVelOff) = {0, 0, 0};
        g_pin_id = (uint16_t)atoi(argv[2]);
        g_pin_at = q;
        g_pin_until = argc > 6 ? H->time_seconds() + atof(argv[6]) : 0;
        H->out_printf(out, "npc %s put at (%.2f %.2f %.2f) ground %.2f\n", argv[2], q.x, q.y, q.z, g);
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
// screens (npc.party_peace, BUG-0374, only drops the damage). Now the host clears, for
// every pack or family member the game updates, a target (+0xE7C, a combatant: inside the creature) on a
// partner's puppet with the creature's own SetTarget (vt 0x84, (null, 1, 0): the BUG-0453 guard ends an attack
// node left without a target) and its stimuli naming the puppet.
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
    if (!game::in_creature_stage() || game::puppets().empty())
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

// BUG-0413: while the host's game stands still (its death caption, a cinematic, the pause menu) no
// creature is updated and host_tick used to send nothing at all: the guests kept the proxies they had,
// but a guest that moved meanwhile (respawned at its nest while the host lay dead, walked off) never
// got the host's NPCs at its new place (sync-check "n missing on the guest" for the whole freeze).
// Every 0.5 s of a freeze the host now runs the guests' relevance on its tracked NPCs that are still
// creatures of its world (in the noun list, alive, in a herd, not at the planet's centre): newly near
// ones are spawned (their state does not change while frozen, the spawn carries it), far ones go.
static double g_frozen_send_at = 0;
// BUG-0385: a creature of a save made while a partner's puppet existed (untuned, in a herd like ours) is
// removed by remove_saved_strays once a second; host_tick gave it an NPC id and sent it in the frames
// before, the guests could not spawn its species (the host's key of a partner's creation) and asked
// for it when it was gone already: "n missing on the guest" once. Such a creature gets no id now.
// `coop.fix npc.stray_ids off`: tracked like any creature (0.7.8).
static bool g_stray_ids_fix = true;
COOP_FIX("npc.stray_ids", "BUG-0385", g_stray_ids_fix,
         "creature: a saved partner's puppet loaded with the world is never sent to the guests as an NPC");
static bool own_kind_herd(void* h);
// BUG-0673: `coop.npc spawn` (tests) builds its creature in a herd of its own (game::create_herd) without
// an archetype, so it has no tuning (+0xE84) and looks like the saved puppet of BUG-0385: npc.stray_ids
// gave it no NPC id (nest_species, creature_desync, old_species never saw it in the host's list) and the
// stray pass removed it. A creature of the console is the test's NPC and is neither. The set is kept over world
// changes (an editor round trip keeps the creature: creature_desync's companion); only tests fill it.
// `coop.fix tools.console_npc off`: treated as a saved stray (0.7.9).
static std::unordered_set<void*> g_console_npcs;
static bool g_console_npc_fix = true;
COOP_FIX("tools.console_npc", "BUG-0673", g_console_npc_fix,
         "tests: an NPC made by coop.npc spawn is an NPC of the host's world, not a saved partner's puppet");
static bool saved_stray(void* c)
{
    return c && !g_console_npcs.count(c) && !IsBadReadPtr(c, 0x1680) && !*(void**)((uint8_t*)c + 0xE84) && game::species_profile(c) &&
           !game::destroy_pending(c) && own_kind_herd(game::herd_of(c));
}
static uint32_t g_frozen_spawns = 0, g_frozen_despawns = 0, g_frozen_retired = 0;
// BUG-1089: the same retirement in the Tribe and Civ stages. host_tick returned at once there while the simulation stood
// (frozen_send is the Creature stage's relevance), so NPCs the host's game took back into its pool during a pause (Civ:
// its camera moved on, the relevance pass parks the herd at the planet's centre) kept their proxies on every guest
// for the whole pause: a herd of 14 standing on Bob's ground, 525 m from the host's objects at (0 0 0) (campaign lab
// 20261113-160426, sync-check MISMATCH; in Civ every guest gets every wild creature, no distance drops them).
// `coop.fix npc.frozen_pooled_rts off`: Tribe and Civ keep them until the host's game goes on (0.7.13).
static bool g_frozen_pooled_rts_fix = true;
COOP_FIX("npc.frozen_pooled_rts", "BUG-1089", g_frozen_pooled_rts_fix,
         "tribe, civ: an animal the host's game parks in its pool while the game stands still (a pause) is removed at the guests");
// BUG-1100: the game's pool gave the object of a tracked NPC out again as another creature while the host's game stood
// (civ_wild_pooled, 0.7.14 final check: #129 species 06538C80 became 065CBF12, 517 m from the centre): the frozen pass
// saw a live creature off the centre and kept its id, so the guest kept the old creature 690 m away. Such an object is
// retired like a pooled one (host_tick gives the new creature a new id when the game goes on).
// `coop.fix npc.frozen_reused off`: kept (0.7.14 rc).
static bool g_frozen_reused_fix = true;
COOP_FIX("npc.frozen_reused", "BUG-1100", g_frozen_reused_fix,
         "an NPC object the game reuses for another creature while the host's game stands is a new NPC at the guests");
static uint32_t g_frozen_reused;
// BUG-1216: the pool also gives a tracked object out again as a creature of the same species at another place while the
// host's game stands (the campaign's Civ segment, lab 20261117-130736: a herd of 7 moved 392-398 m during a 24 s pause
// of the host; the guests kept it where it had stood, `n: 7 apart`, states 20 s old, until the game went on). No
// creature moves while no creature is updated, so a tracked object found more than kFrozenMovedM from its last updated
// place is retired like a reused one (host_tick gives it a new id at its new place when the game goes on).
// `coop.fix npc.frozen_moved off`: kept where it was until the game goes on (0.7.14).
static bool g_frozen_moved_fix = true;
COOP_FIX("npc.frozen_moved", "BUG-1216", g_frozen_moved_fix,
         "an animal the game moves elsewhere while the host's game stands (a pause) leaves its old place at the guests too");
constexpr float kFrozenMovedM = 20.f;
static uint32_t g_frozen_moved;
// host, frozen: the tracked NPCs that are still creatures of the world in a herd off the planet's centre (with their
// positions); those that are not (freed, pooled) are retired (BUG-0339)
static std::vector<std::pair<void*, net::Vec3>> frozen_pooled_pass()
{
    struct Acc {
        std::vector<void*> live;
    } a;
    // C1: each entry's object, the one its id was given to, in the game's list now (game::alive)
    g_hreg.for_each([&](HostReg::Entry& e) {
        void* o = e.ref.p;
        if (o && ref_alive(e.ref) && !game::is_puppet(o) && o != game::avatar() && !game::is_destroyed(o))
            a.live.push_back(o);
    });
    std::vector<std::pair<void*, net::Vec3>> live;
    std::vector<void*> gone; // BUG-0339: tracked, no creature of the world any more (freed, or pooled at the centre)
    std::unordered_set<void*> listed(a.live.begin(), a.live.end());
    for (auto [c, hn] : hn_walk())
        if (!listed.count(c))
            gone.push_back(c);
    for (void* c : a.live) {
        net::Vec3 p = game::position(c);
        bool centre = p.x * p.x + p.y * p.y + p.z * p.z < 2500.f;
        // BUG-1100: pooled and given out again as another creature between two passes (the same object, another
        // species, back on the ground): host_tick's reuse check (BUG-0044) does not run in a freeze, so the id stayed
        if (!centre && g_frozen_reused_fix) {
            HostNpc* it = hn_of(c);
            if (it &&
                npc_object_reused(it->species, it->dead, game::species_instance(c), game::is_dead(c))) {
                if (g_frozen_reused < 40) // (diagnostics: what the object is now)
                    H->log(M, SK_INFO, "npc: NPC %u's object %p is another creature while the host's game stands: species "
                           "%08X -> %08X, dead %d -> %d, at (%.1f %.1f %.1f) herd %p: retired (BUG-1100)", it->id, c,
                           it->species, game::species_instance(c), (int)it->dead, (int)game::is_dead(c), p.x, p.y,
                           p.z, game::herd_of(c));
                gone.push_back(c);
                g_frozen_reused++;
                continue;
            }
        }
        if (!centre && g_frozen_moved_fix && civ_stage()) { // BUG-1216 (Civ only: where it was seen)
            HostNpc* it = hn_of(c);
            const net::Vec3 last = it ? it->pos_hist[it->hist_at] : net::Vec3{};
            if (it && (last.x != 0 || last.y != 0 || last.z != 0) && net::dist(p, last) > kFrozenMovedM) {
                if (g_frozen_moved < 40)
                    H->log(M, SK_INFO, "npc: NPC %u's object %p is %.1f m from where the game last updated it while the host's "
                           "game stands: at (%.1f %.1f %.1f) herd %p: retired (BUG-1216)", it->id, c, net::dist(p, last), p.x,
                           p.y, p.z, game::herd_of(c));
                gone.push_back(c);
                g_frozen_moved++;
                continue;
            }
        }
        if (centre)
            gone.push_back(c);
        if (!game::herd_of(c) || centre) // pooled (BUG-0274, BUG-0339)
            continue;
        live.push_back({c, p});
    }
    // BUG-0339: the game takes NPCs back into its pool (the planet's centre) also while it updates no
    // creature (the host's respawn at its nest with a caption up, lab 20261031-083425 campaign#2): host_tick's
    // retire_pooled does not run then, and they were skipped here, so a guest kept their proxies where
    // they had stood for the whole freeze (no despawn, no states: no stale drop either). They go now.
    for (void* c : gone) {
        HostNpc* it = hn_of(c);
        if (!it)
            continue;
        retire(it->id, c);
        hn_forget(it->id);
        g_frozen_retired++;
    }
    return live;
}
static void frozen_send(double now)
{
    if (now - g_frozen_send_at < 0.5)
        return;
    g_frozen_send_at = now;
    const auto live = frozen_pooled_pass();
    for (auto& p : C.sess->players()) {
        if (p.id == 0 || (C.in_place && !C.in_place(p.id)))
            continue;
        net::Vec3 gp;
        if (!C.player_pos(p.id, gp))
            continue;
        auto& known = g_known[p.id];
        for (auto& [c, cp] : live) {
            const HostNpc* h = hn_of(c);
            if (!h)
                continue;
            uint16_t id = h->id;
            float d = net::dist(cp, gp);
            bool k = known.count(id) != 0;
            if (!k && d < kRelevance) {
                send_spawn(p.id, id, c);
                known.insert(id);
                g_frozen_spawns++;
            } else if (k && d > kRelevance + kHysteresis) {
                send_despawn(p.id, id);
                known.erase(id);
                g_frozen_despawns++;
            }
        }
    }
}

static void host_tick(double now, const std::vector<void*>& creatures)
{
    void* av = game::avatar();
    net::Vec3 center;
    const bool civ = civ_stage(); // BUG-0973: no centre, the whole planet
    bool rts = civ || (rts_stage() && C.stage_center && C.stage_center(center));
    if (!av && !rts)
        return;
    if (now < g_test_pass_hold_until) // tests (BUG-1428): the host's pass does not run (its editor, no avatar)
        return;
    // BUG-1174: no host pass for a while (its editor: not a world change since C2, the NPCs and their ids stay): that
    // time does not count as unseen, or every NPC was retired 2 s after the editor and spawned again under a new id
    if (g_editor_hold_fix && g_host_pass_at > 0 && now - g_host_pass_at > 1.0) {
        const double gap = now - g_host_pass_at;
        for (auto [c, hn] : hn_walk())
            hn.last_seen += gap;
        g_host_gaps++;
    }
    g_host_pass_at = now;
    // no creature updated this frame: the simulation stands still (pause menu, the death caption
    // waiting for OK, a cinematic). Not a reason to forget them: the guests lost every NPC after 2 s
    // and got them again when the host went on (play_creature.py 2026-10-23)
    if (creatures.empty()) {
        g_frozen_since = g_frozen_since > 0 ? g_frozen_since : now;
        if (!rts && now - g_frozen_since > 0.5) // BUG-0413
            frozen_send(now);
        else if (rts && g_frozen_pooled_rts_fix && now - g_frozen_since > 0.5 && now - g_frozen_send_at >= 0.5) {
            g_frozen_send_at = now; // BUG-1089: no relevance sends (the RTS stages have none per guest), the pool only
            frozen_pooled_pass();
        }
        return;
    }
    if (g_frozen_since > 0) { // the frozen time does not count as unseen
        double gap = now - g_frozen_since;
        for (auto [c, hn] : hn_walk())
            hn.last_seen += gap;
        g_frozen_since = 0;
    }
    host_pack_prune(); // BUG-1256
    g_seen_now.clear();
    for (auto [c, hn] : hn_walk())
        hn.seen_bits <<= 1;
    for (void* c : creatures) {
        if (c == av || game::is_puppet(c) || game::is_destroyed(c))
            continue;
        uint32_t species = game::species_instance(c);
        bool dead = game::is_dead(c);
        HostNpc* it = hn_of(c);
        if (it && g_reuse_fix &&
            npc_object_reused(it->species, it->dead, species, dead)) {
            // the game freed this NPC and built another one in the same object (BUG-0044): the
            // guests drop the old one, the new one gets a new id and a spawn with its species
            retire(it->id, c);
            hn_forget(it->id);
            it = nullptr;
            g_reused++;
        }
        // BUG-0385: never an NPC of the world. BUG-0973: not in Civ, whose herds look like ours (members without an
        // archetype tuning, herds without an archetype or nest: the civ_npc herd of 7 was never tracked) and where no
        // partner's puppet exists to be saved
        if (!it && g_stray_ids_fix && !civ && saved_stray(c))
            continue;
        auto alloc = [] { return alloc_npc_id(g_next_id, [](uint16_t i) { return hn_used(i); }, g_id_fix); };
        // C1: the id of this object; another object at a known address (another serial: freed and built again
        // there) loses the old id at the guests first and gets a new one (BUG-0044's class by the serial)
        uint16_t id = 0;
        bool fresh = false;
        if (!g_hreg.adopt(mirror::Ref{c, game::object_serial(c), kCreatureNoun}, alloc,
                          [&](uint16_t old) {
                              retire(old, c);
                              g_reused++;
                          },
                          id, &fresh))
            continue; // (65534 NPCs at once: not in this world)
        it = &g_hreg.find(id)->data;
        if (fresh)
            *it = {id, now, species, dead};
        else {
            it->last_seen = now;
            it->dead = dead;
        }
        {
            HostNpc& hn = *it;
            hn.seen_bits |= 1;
            hn.vel_bits = (hn.vel_bits << 1) | (drive::speed(game::velocity(c)) > 0.3f ? 1u : 0u);
            hn.hist_at = (uint8_t)((hn.hist_at + 1) & 63);
            hn.pos_hist[hn.hist_at] = game::position(c);
        }
        g_seen_now.insert(c);
        {   // BUG-0339 diagnostics: an NPC the game still updates at the planet's centre (pool place)
            HostNpc& hn = *it;
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
    if (g_seen_now.size() < hn_count())
        retire_pooled();
    if (g_ntrack_on) // coop.npctrack on the host: its own NPCs (the reference for the guest's proxies)
        for (void* c : g_seen_now) {
            net::Vec3 p = game::position(c);
            ntrack_push(hn_of(c)->id, c, 4, game::is_dead(c) ? net::NF_DEAD : 0, 0, p, p,
                        drive::speed(game::velocity(c)), now);
        }
    if (now < g_pin_until) // tests (BUG-1228): coop.npctrack put ... <secs> (where the game drew it: in the track row above)
        if (void* c = host_npc(g_pin_id)) {
            game::teleport(c, g_pin_at, game::orientation(c));
            *(net::Vec3*)((uint8_t*)c + game::kVelOff) = net::Vec3{};
        }
    if (now < g_lift_until) // tests (BUG-0550): coop.npc lift (after the track row: that is where the game drew it)
        if (void* c = host_npc(g_lift_id)) {
            net::Vec3 p = game::position(c);
            float r = drive::speed(p), g = game::ground_radius(p);
            if (r > 1 && g > 1) {
                float k = (g + g_lift_m) / r;
                game::teleport(c, {p.x * k, p.y * k, p.z * k}, game::orientation(c));
                game::set_grounded(c, false);
                *(net::Vec3*)((uint8_t*)c + game::kVelOff) = net::Vec3{};
            }
        }
    if (!g_dash_after_send)
        dash_tick(now);
    // forget creatures that stopped updating (destroyed / unloaded)
    for (auto [c, hn] : hn_walk())
        if (now - hn.last_seen > 2.0) {
            const uint16_t id = hn.id;
            retire(id, c);
            hn_forget(id);
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
        if (civ) {
            gp = {};
            relevance = kCivRelevance;
        } else if (rts) {
            gp = center;
            relevance = kRtsRelevance;
            has_view = views::of(p.id, game::mode_id(), now, view);
        } else if (!C.player_pos(p.id, gp)) {
            for (uint16_t id : known)
                send_despawn(p.id, id);
            known.clear();
            continue;
        }
        std::vector<net::NpcState> batch;
        for (void* c : g_seen_now) {
            const HostNpc* h = hn_of(c);
            if (!h)
                continue;
            uint16_t id = h->id;
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
            if (k && send_states) {
                net::NpcState st = host_state(id, c);
                if (civ && g_civ_quiet_fix) { // BUG-0973: a standing creature's state once a second only
                    auto& q = g_civ_sent[p.id][id];
                    bool same = drive::speed(st.vel) < 0.05f && net::dist(st.pos, q.pos) < 0.05f && st.flags == q.flags &&
                                std::fabs(st.health - q.health) < 0.01f;
                    if (same && now - q.at < 1.0) {
                        g_civ_quiet_skipped++;
                        continue;
                    }
                    q = {st.pos, st.flags, st.health, now};
                }
                batch.push_back(st);
                if (H->time_seconds() < g_nanvel_until && (g_nanvel_id < 0 || g_nanvel_id == id)) { // tests (BUG-0894)
                    batch.back().vel = {NAN, NAN, NAN};
                    if (g_nanvel_air)
                        batch.back().flags |= net::NF_AIR;
                    g_nanvel_sent++;
                }
            }
        }
        // unreliable batches that fit in one packet
        // BUG-0550 trailer: n x i16, each NPC's height over the host's terrain in cm (older guests ignore it)
        size_t per = (net::kMaxPacket - 64) / (net::NpcState::kSize + 2);
        for (size_t i = 0; i < batch.size(); i += per) {
            net::Writer w;
            w.u32(net::now_ms());
            size_t n = std::min(per, batch.size() - i);
            w.u8((uint8_t)n);
            for (size_t j = 0; j < n; j++)
                batch[i + j].write(w);
            for (size_t j = 0; j < n; j++) { // BUG-0550: the agl trailer
                float a = drive::over_ground(batch[i + j].pos) * 100.f;
                w.u16((uint16_t)(int16_t)std::max(-32000.f, std::min(32000.f, std::round(a))));
            }
            C.sess->send_unreliable_to(p.id, net::MSG_NPC_STATE, w);
        }
        if (g_meat_fix && send_states) { // BUG-0557: the meat left on the bodies this guest mirrors
            auto& sent = g_meat_sent[p.id];
            std::vector<std::pair<uint16_t, float>> v;
            for (void* c : g_seen_now) {
                const HostNpc* h = hn_of(c);
                if (!h)
                    continue;
                uint16_t id = h->id;
                if (!known.count(id) || !game::is_dead(c))
                    continue;
                float m = body_meat(c);
                auto f = sent.find(id);
                if (f == sent.end() || std::fabs(f->second.first - m) >= kMeatStep || now - f->second.second >= 2.0) {
                    v.push_back({id, m});
                    sent[id] = {m, now};
                }
            }
            for (size_t i = 0; i < v.size(); i += 100) {
                size_t n = std::min<size_t>(100, v.size() - i);
                net::Writer w;
                w.u8(0);
                w.u8((uint8_t)n);
                for (size_t j = 0; j < n; j++) {
                    w.u16(v[i + j].first);
                    w.f32(v[i + j].second);
                }
                C.sess->send_unreliable_to(p.id, net::MSG_NPC_MEAT, w);
            }
        }
        if (!g_anim_out.empty())
            send_anims(p.id, known); // BUG-0464
        if (send_snap)
            send_anim_snapshot(p.id, known);
    }
    g_anim_out.clear();
    if (g_dash_after_send) // BUG-0753: the states above are the game's positions, not the test's own write
        dash_tick(now);
    for (auto it = g_known.begin(); it != g_known.end();) { // guests that left
        bool present = false;
        for (auto& p : C.sess->players())
            if (p.id == it->first)
                present = true;
        it = present ? std::next(it) : g_known.erase(it);
    }
}

// BUG-0843 (class BUG-0426): the nest herd cached per species (g_local_herds, learnt from the guest's own NPCs) and the
// herds made for proxies (g_herds) were reused while `object_alive` said so: that reads the herd's own memory, so a
// herd the game destroyed and freed (its nest gone, a migration, the world's last member of the species gone) and
// whose block a new herd took (another species' herd: CreateHerd gives the block back within frames) still passed,
// and the next proxy of the species was made into that other herd (another species' herd: its AI, its nest, its
// save). Now a cached herd is used only while it is the same object in the game's list (noun, serial) and of the
// species; else the noun list is searched again. Old (`coop.fix npc.herd_identity off`): object_alive.
static bool g_herd_identity_fix = true;
COOP_FIX("npc.herd_identity", "BUG-0843", g_herd_identity_fix, "creature: a guest's copy of a host animal is never put into another herd that the game built where the species' herd was");
static void note_herd(void* h)
{
    if (g_herd_serial.size() > 512)
        g_herd_serial.clear();
    g_herd_serial[h] = game::object_serial(h);
}
static bool herd_is(void* h, void* species)
{
    auto it = g_herd_serial.find(h);
    return it != g_herd_serial.end() && game::alive(game::LiveRef{h, it->second, game::kHerdNoun}) &&
           game::herd_species(h) == species;
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
        if (g_herd_identity_fix ? herd_is(lh->second, species_nest) : game::object_alive(lh->second)) {
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
        note_herd(f.found);
        g_proxies_in_nest++;
        return f.found;
    }
    auto it = g_herds.find(hk);
    if (it != g_herds.end()) {
        if (!g_herd_identity_fix || herd_is(it->second, species))
            return it->second;
        g_herds.erase(it);
    }
    void* h = game::create_herd(pos, species);
    if (h) {
        g_herds[hk] = h;
        note_herd(h);
    }
    return h;
}

// BUG-0842 (see check_identity)
static bool g_proxy_identity_fix = true;
COOP_FIX("npc.proxy_identity", "BUG-0842", g_proxy_identity_fix, "creature, tribe: a guest never drives or destroys another creature that the game built where a removed animal's copy was");
static uint32_t g_identity_retired;
// the proxy's creature is the object it made, now (the object list of this frame and the serial)
static bool px_alive(const PxE& pe) { return pe.ref.p && ref_alive(pe.ref); }
// ... (npc.proxy_identity off: it is set)
static bool px_live(const PxE& pe) { return pe.ref.p && (!g_proxy_identity_fix || px_alive(pe)); }
// BUG-0805 (class BUG-0426): the message handlers (the host's hit on the guest with a proxy as the attacker, the
// social prompt and result, the NPC's animations and fight effects, a body's meat) took the proxy's creature as it
// was stored: in the frame after the guest's own game removed it (or in the frames before it was freed and given to
// another creature) they handed a destroyed creature to TakeDamage (the pack's stimulus sender, BUG-0453 class), to
// CreaturePlayAnim or to an effect; guest_tick forgets such a proxy only after the messages of the frame. Now every
// handler takes the creature only while it is the live object the proxy made. Old (`coop.fix npc.handler_live off`):
// the stored pointer (with mbIsDestroyed in some of them).
static bool g_handler_live_fix = true;
COOP_FIX("npc.handler_live", "BUG-0805", g_handler_live_fix, "creature: a host's hit, a social prompt or an animal's animation is never given to a copy of an animal the guest's game has just removed");
static uint32_t g_handler_dropped;
static void* handler_c(const PxE& pe)
{
    if (!g_handler_live_fix)
        return pe.ref.p;
    if (pe.ref.p && !px_alive(pe)) {
        g_handler_dropped++;
        return nullptr;
    }
    return pe.ref.p;
}
// the creature of a proxy that goes (the entry is detached already: not a proxy any more while the game destroys it)
static void destroy_proxy_object(const mirror::Ref& r)
{
    guest_pack_untrack_ptr(r.p); // BUG-1256: never leave a destroyed proxy in mPosseMembers or CreatureUI
    // BUG-0842: not the object the proxy made (freed, another one at its address): only forget it, no call on it
    if (g_proxy_identity_fix && !ref_alive(r)) {
        game::puppet_unregister(r.p);
        g_identity_retired++;
    } else if (game::is_destroyed(r.p)) { // the game removed it already: only forget it
        game::puppet_unregister(r.p);
        if (!g_destroy_once_fix) // BUG-0850 off: 0.7.11.1, a second DestroyInstance of the destroyed proxy
            game::destroy_creature(r.p);
    } else {
        creature_hp::forget_puppet(r.p);
        game::destroy_creature(r.p);
    }
}
static void destroy_proxy(uint16_t id)
{
    auto it = px_find(id);
    if (it == nullptr)
        return;
    if (it->ref.p) {
        const mirror::Ref r = it->ref;
        px_detach(*it);
        destroy_proxy_object(r);
    }
    px_erase(id);
}

// BUG-0931: the check of BUG-0300 ran once a second and the proxies are driven in every frame: a proxy the guest's own
// game removed and freed in between (lab 20261111-071518 long_creature: two proxies when the guest's baby_game event
// started, at the end of its evolution) was driven for 0.45 s: 29 caught faults in game::teleport (a vtable of 0 in the
// freed block), each one a cut-off frame. The check now runs in every frame, right before the proxies are driven
// (pointer compares against the noun list: about 200 objects). Old (`coop.fix npc.proxy_listed_now off`): once a second.
static bool g_listed_now_fix = true;
COOP_FIX("npc.proxy_listed_now", "BUG-0931", g_listed_now_fix, "creature: a guest never moves a mirrored animal that its own game has just removed (it did for up to a second: errors caught in the mod's code, a stutter)");

// BUG-0300 (see g_listed_fix): reads the noun list only; an object that is not in it is never touched
static void check_listed()
{
    struct Acc {
        std::unordered_set<void*> listed;
    };
    static Acc a; // (kept: no allocation per frame once it has its buckets)
    a.listed.clear();
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun == 0x18EB45Eu && game::object_alive(o))
                ((Acc*)u)->listed.insert(o);
            return true;
        },
        &a);
    if (a.listed.empty()) // no list (between worlds): nothing to compare with
        return;
    for (auto [id, pe, px] : px_walk()) {
        if (!pe.ref.p)
            continue;
        if (!a.listed.count(pe.ref.p)) {
            g_proxy_unlisted++;
            H->log(M, SK_WARN, "npc %u: the proxy's creature %p is no creature of this world any more (key %08X, %s)%s", id,
                   pe.ref.p, px.key.instance, px.killed ? "killed" : "alive",
                   g_listed_fix ? ": made again" : " (npc.proxy_listed off: kept)");
            if (!g_listed_fix)
                continue;
            game::puppet_unregister(pe.ref.p);
            px_detach(pe);
            px.drv = drive::State{};
            continue;
        }
        if (!game::species_key(pe.ref.p).instance) { // listed without a species: not placed in the world
            if (!px.nospecies_said)
                H->log(M, SK_WARN, "npc %u: the proxy's creature %p has no species (key %08X, herd %p)", id, pe.ref.p,
                       px.key.instance, game::herd_of(pe.ref.p));
            px.nospecies_said = true;
            g_proxy_nospecies++;
        }
    }
}

// BUG-0757 (`coop.fix npc.place_now`, as civ.place_now BUG-0438): a walked proxy is placed where its target will be a
// frame later. The placing of this frame is what the game draws and the next frame reads back, so a proxy placed on its
// target now was always one guest frame behind the host's NPC: at 15 fps and 6.5 m/s 0.42 m (the drawn copy 0.05-0.07 s
// behind the host along the path, p50, tribe_npc_dash lab 20261108-102113 rows). Aimed ahead by the last frame's time
// (at most 0.1 s) along the target's velocity. Off: on the target (0.7.10).
static bool g_place_now_fix = true;
COOP_FIX("npc.place_now", "BUG-0757", g_place_now_fix, "creature, tribe: a walking animal's copy is placed where the host has it when the frame is drawn, not a frame behind");
// BUG-0997: with npc.place_now the walked proxy is aimed a frame ahead, and walk_place's snap (BUG-0361: teleported
// when over 2.5 m off and the host's path itself jumped this frame) compared that aim with the path point, which never
// matched for a moving proxy: a host animal thrown aside was closed at the catch-up speed (seconds) instead of at once.
// Now the snap gets this frame's path point. `coop.fix npc.place_now_snap off`: the aim (0.7.10 .. 0.7.13)
static bool g_place_now_snap_fix = true;
COOP_FIX("npc.place_now_snap", "BUG-0997", g_place_now_snap_fix, "creature, tribe: a walking animal's copy jumps with the host's animal when that is thrown aside");
// BUG-0842: a cached proxy pointer was checked by its own memory (mbIsDestroyed, BUG-0300's list check once a second,
// BUG-0931's in every frame in Creature only) and by its address: the game frees a destroyed creature some frames
// later (its last reference goes, 10-40 frames in the lab) and gives the block to the next creature of that species
// (lab: 8-11 creations). Where the mod did not look in that time (the second after an avatar or world change, the
// fork's held death caption, Tribe at any time, a proxy without states), the next frame drove the new creature as the
// host's animal, or the stale drop below destroyed it (another proxy, an animal of the guest's world). Now, before the
// stale drop and the driving, in Creature and Tribe: a proxy whose creature is not the object it made (game::listed and
// the serial) is forgotten without a call on it and made again; an empty list (between worlds) retires them all.
// Old (`coop.fix npc.proxy_identity off`): the address and the mbIsDestroyed byte, the list check in Creature only.
// (g_proxy_identity_fix, g_identity_retired: above destroy_proxy)
static double g_settle_s = 1.0, g_test_settle_until = 0; // tests (BUG-0842): `coop.npc settle <s>`
static void check_identity()
{
    for (auto [id, pe, px] : px_walk()) {
        if (!pe.ref.p)
            continue;
        if (px_alive(pe))
            continue;
        g_identity_retired++;
        H->log(M, SK_WARN, "npc %u: the proxy's creature %p is not the one it made any more (gone or another object there, "
               "key %08X): made again (npc.proxy_identity)", id, pe.ref.p, px.key.instance);
        game::puppet_unregister(pe.ref.p);
        px_detach(pe);
        px.drv = drive::State{};
        g_proxy_lost++;
    }
}
std::string debug_settle(double s)
{
    g_test_settle_until = H->time_seconds() + s;
    char b[120];
    snprintf(b, sizeof b, "npc tick waits %.1f s (as after an avatar change); identity checks so far %u\n", s, g_identity_retired);
    return b;
}

static double g_frame_dt = 0, g_frame_last = 0;
static void guest_tick(double now)
{
    g_frame_dt = g_frame_last > 0 ? std::min(std::max(now - g_frame_last, 0.0), 0.1) : 0.0; // BUG-0757
    g_frame_last = now;
    void* av = game::avatar();
    // remove local NPCs collected by the update filter
    std::sort(g_kill.begin(), g_kill.end());
    g_kill.erase(std::unique(g_kill.begin(), g_kill.end()), g_kill.end());
    for (void* c : g_kill)
        if (c != av && !game::is_puppet(c) && !game::is_destroyed(c) && !game::destroy_pending(c)) {
            creature_hp::forget_puppet(c);
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
    if (now - s_av_t0 < g_settle_s || now < g_test_settle_until)
        return;
    // BUG-0842 (npc.proxy_identity): every proxy's creature is checked against the object list and its serial first, in
    // Creature and Tribe, before the stale drop below destroys anything and before anything is driven
    if (g_proxy_identity_fix)
        check_identity();
    guest_pack_sync(); // BUG-1256: keep guest's own pack members in mPosseMembers + CreatureUI
    // A proxy the host stopped sending states for while it sends them for the others is not an
    // NPC of the host any more (its id belongs to a world the host has left: before 2026-10-25 the
    // host renumbered its NPCs after its editor or a load without a despawn, and the guest kept
    // the old ones for good: a second, living copy of every NPC, a kill of the host's left it
    // standing). A host that stands still (pause menu) sends no states at all: nothing is dropped.
    // (D3: the rule is npc_rules::stale; C1: the registry's sweep, Keep while the states thaw, BUG-0611)
    if (g_stale_fix && npc_rules::states_flow(now, g_last_states_rx)) {
        g_stale_dropped += (uint32_t)g_px.sweep(
            [&](uint16_t id) {
                const PxE* e = g_px.find(id);
                return !e || npc_rules::stale(now, e->data.last_rx, g_states_flow_since) == npc_rules::Stale::Fresh;
            },
            [&](PxE& e, bool) { // (not Fresh: Thaw or Drop)
                if (npc_rules::stale(now, e.data.last_rx, g_states_flow_since) == npc_rules::Stale::Thaw) {
                    g_thaw_kept++;
                    return mirror::Unseen::Keep;
                }
                return mirror::Unseen::Remove;
            },
            [&](PxE& e, bool) {
                const mirror::Ref r = e.ref;
                g_px.detach(e.id);
                if (r.p)
                    destroy_proxy_object(r);
            });
    }
    // BUG-0931: in every frame, before any proxy is driven (g_listed_now_fix); once a second before
    if ((g_listed_now_fix || now - g_listed_at >= 1.0) && game::in_creature_stage()) { // (Creature only: the case seen; Tribe untouched)
        g_listed_at = now;
        check_listed();
    }
    for (auto [id, pe, px] : px_walk()) {
        net::PlayerState s;
        if (!px.buf.sample(now, s))
            continue;
        px.diag_want = s.pos; // (read only by coop.bodyrec diag rows: no driving reads it)
        px.diag_have = true;
        const float rv_rep = drive::speed(s.vel); // (coop.npctrack: the speed the host reported)
        if (g_npc_novel)
            s.vel = {0, 0, 0};
        // the guest's own game destroyed the proxy (its tribe killed it, a cleanup): forget the
        // object at once (it is freed later; it was driven every frame after that) and make a new
        // one unless the host says the NPC is dead. NPCs vanished from guests this way in a 4-player
        // tribe run (coverage 26-48 % for 4 minutes).
        if (pe.ref.p && game::is_destroyed(pe.ref.p)) {
            game::puppet_unregister(pe.ref.p);
            px_detach(pe);
            px.drv = drive::State{};
            g_proxy_lost++;
        }
        if (!pe.ref.p && !npc_rules::remake((s.flags & net::NF_DEAD) != 0, px.ever)) // BUG-0383 (D3)
            continue;
        if (!pe.ref.p) {
            void* species = game::species_by_key(px.key);
            // BUG-1293 (found by the Lineages fork, its BUG-9231): our own key for a host's creation builds nothing
            // here: the host's key (registered here too when this game had nothing under it, creations.cpp)
            if (!species && g_species_fallback) {
                creations::Key hk = creations::host_species({px.key.instance, px.key.type, px.key.group});
                if (hk.instance != px.key.instance)
                    if (void* hs = game::species_by_key({hk.instance, hk.type, hk.group})) {
                        species = hs;
                        if (!px.fallback_logged) {
                            px.fallback_logged = true;
                            g_species_fallbacks++;
                            H->log(M, SK_INFO, "npc %u: species %08X builds nothing here: the host's key %08X (BUG-1293)",
                                   id, px.key.instance, hk.instance);
                        }
                    }
            }
            void* herd = species ? proxy_herd(px.key, px.flags, s.pos, species) : nullptr;
            int fail = !species ? 1 : !herd ? 2 : 0;
            void* made = nullptr;
            if (!fail) {
                made = game::spawn_creature(s.pos, species, herd);
                if (!made)
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
            game::puppet_register(made); // no local AI
            px_bind(pe, made, game::object_serial(made));
            px.ever = true;
            px.tuning_set = 0; // (a new creature: its own Create's tuning)
            px.made_at = now;
            px.made_agl = drive::over_ground(s.pos);
            px.rest_have = px.slope_held = false; // (BUG-1228: a new creature)
            // BUG-0399: on this game's ground (Tribe: the ground can lie higher than the host's)
            game::teleport(pe.ref.p, rts_stage() ? drive::out_of_ground(s.pos) : s.pos, s.rot);
        }
        proxy_tuning(pe);
        proxy_epic_steps(pe.ref.p, rv_rep > 0.3f); // BUG-0669 (the host's NPC walks)
        if (now < px.manual_until) {
            if (pe.ref.p && (px.manual_mode & 1) && now - px.manual_last > 0.1) {
                game::set_speed(pe.ref.p, px.manual_speed);
                game::move_to(pe.ref.p, px.manual_to, 0.3f);
                px.manual_last = now;
            }
            if (pe.ref.p && (px.manual_mode & 2))
                game::set_position(pe.ref.p, game::position(pe.ref.p));
            if (pe.ref.p && (px.manual_mode & 4))
                game::set_orientation(pe.ref.p, game::orientation(pe.ref.p));
            continue;
        }
        const net::Vec3 drawn = game::position(pe.ref.p); // where the game drew it (before this frame's driving)
        bool placed = false, walked = false;
        const bool rts = rts_stage();
        // BUG-0361: the last 0.1 s of a walk is interpolated towards a state that stands (velocity 0): a
        // walked proxy goes on with the speed of its path to the stop point instead of standing there
        net::Vec3 vel = s.vel;
        float walk_speed = -1.f;
        if (g_civ_jump_fix && civ_stage() && !px.killed) { // BUG-1216: the game put the host's creature elsewhere
            const bool jumped = px.drv.jump_have && net::dist(s.pos, px.drv.jump_from) > kCivJumpM;
            px.drv.jump_from = s.pos;
            px.drv.jump_have = true;
            if (jumped && net::dist(drawn, s.pos) > kCivJumpM) {
                if (px.air) {
                    px.air = false;
                    game::air_hold(pe.ref.p, false);
                }
                drive::walk_end(pe.ref.p, s.pos, px.drv, now);
                game::teleport(pe.ref.p, drive::out_of_ground(s.pos), s.rot);
                *(net::Vec3*)((uint8_t*)pe.ref.p + game::kVelOff) = {0, 0, 0};
                px.kinematic = false;
                px.drv.teleports++;
                px.drv.jumps++;
                px.drv.path = drive::PathTrack{}; // (no path velocity across the jump)
                slope_release(pe.ref.p, px); // (BUG-1228: put elsewhere)
                px.drv.track = drive::Track{};
                px.drv.last_step = now;
                px.drv.last_cmd = now;
                g_civ_jumps++;
            }
        }
        drive::path_update(px.drv, s.pos, now);
        {
            // BUG-0361: not faster than it walked (at a stop the target can jump back up to 1 m: a 37 m/s "path" and a
            // 0.8 m jump of the proxy); BUG-0394: a host NPC that moves without a speed of its own walks at its path's
            // velocity; a host NPC that reports its speed but moves slower is walked at the path's speed (Tribe:
            // 0.6-1.4x the path in 77 % of the windows); BUG-1360 (npc.walk_gait): the stop clamp belongs to one walk
            // (npc_logic::walk_velocity)
            const drive::PathTrack& pt = px.drv.path;
            npc_logic::WalkIn wi;
            wi.kinematic = px.kinematic;
            wi.vel = {vel.x, vel.y, vel.z};
            wi.path_v = {pt.v.x, pt.v.y, pt.v.z};
            wi.path_v_ema = {pt.v_ema.x, pt.v_ema.y, pt.v_ema.z};
            wi.path_sp_ema = pt.sp_ema;
            wi.walk_sp = px.walk_sp;
            const npc_logic::WalkOut wo = npc_logic::walk_velocity(wi, g_walk_gait_fix);
            vel = {wo.vel.x, wo.vel.y, wo.vel.z};
            walk_speed = wo.walk_speed;
            px.walk_sp = wo.walk_sp;
        }
        // BUG-0550: the host's NPC is off its ground: at its height over the host's terrain, over this game's, not
        // grounded (drive::air_place); not walked (the walk is ended, it walks again after the landing)
        // (D3: how it is moved this frame is npc_rules::place)
        npc_rules::PlaceIn pin;
        pin.killed = px.killed;
        pin.host_dead = (s.flags & net::NF_DEAD) != 0;
        pin.host_air = (s.flags & net::NF_AIR) != 0;
        pin.host_under = g_air_under_fix && net::agl_known(s.agl) && s.agl < -drive::kUnderTol; // BUG-1357
        pin.stage = game::in_creature_stage() || rts;
        const bool air = npc_rules::place(pin) == npc_rules::Place::Air;
        if (!air && !px.killed && !(s.flags & net::NF_DEAD)) { // BUG-0562: the host's NPC inside this game's mound
            const net::Vec3 up = proxy_on_mound(s.pos);
            if (up.x != s.pos.x || up.y != s.pos.y || up.z != s.pos.z) {
                s.pos = up;
                g_mound_targets++;
            }
        }
        if (air) {
            px.kinematic = false;
            net::Vec3 at = drive::at_sender_agl(s.pos, s.agl);
            if (g_air_catch_fix && g_frame_dt > 0) {
                // where the next drawing shows it when placed at `at` (the game moves it on by the velocity)
                const float dt = (float)g_frame_dt;
                const net::Vec3 next{at.x + s.vel.x * dt - drawn.x, at.y + s.vel.y * dt - drawn.y, at.z + s.vel.z * dt - drawn.z};
                const float len = drive::speed(next), lim = (drive::speed(s.vel) * kAirCatchK + kAirCatchMps) * dt;
                if (len > lim && net::dist(drawn, at) < kAirCatchMax) {
                    const float k = lim / len;
                    at = {drawn.x + next.x * k - s.vel.x * dt, drawn.y + next.y * k - s.vel.y * dt, drawn.z + next.z * k - s.vel.z * dt};
                    g_air_catches++;
                }
            }
            drive::air_place(pe.ref.p, at, s.rot, s.vel, px.drv, now);
            px.air = true;
            px.airs++;
            placed = true;
        } else if (px.air) {
            px.air = false;
            game::air_hold(pe.ref.p, false); // (its game lands it)
        }
        pin.speed = drive::speed(vel);
        pin.off = net::dist(game::position(pe.ref.p), s.pos);
        if (!placed && npc_rules::place(pin) == npc_rules::Place::Walk) {
            // BUG-0281: a running NPC's proxy follows the host's path itself
            // BUG-0361: the game walks it, placed on the path
            if (!px.kinematic)
                px.drv.kinematic = false; // a new walk: its first goal at once
            drive::WalkPlace o;
            o.own_height = rts;     // BUG-0329
            // BUG-0399: onto this game's ground, not only at its own height (proxies 1.3-1.7 m under after joins)
            o.ground = rts ? drive::g_out_of_ground_fix : g_walk_ground_fix; // BUG-0406 (Creature)
            o.walk_speed = walk_speed;
            o.max_dt = 0.4; // BUG-0503 (as the Tribe mirrors, BUG-0365); Creature too (BUG-0590)
            o.snap = 2.5f;
            o.keep_lost = true;
            o.catch_prop = true; // BUG-0425
            // BUG-0440 (Tribe only: in Creature, npc_walk's fleeing proxy after a hit stepped 0.8-1.1 m a few times
            // with it, labs 20261113-202612, -204146 #2; all the reported holds were Tribe)
            o.hold_after = g_proxy_hold_fix && rts;
            o.variant = g_walk_variant;
            // BUG-0757 (npc.place_now): aimed where the target is when the placing shows up, a frame later
            net::Vec3 aim = s.pos;
            if (g_place_now_fix)
                aim = {aim.x + vel.x * (float)g_frame_dt, aim.y + vel.y * (float)g_frame_dt, aim.z + vel.z * (float)g_frame_dt};
            if (g_place_now_snap_fix) { // BUG-0997: the snap compares the path point, not the aim
                o.has_path_pos = true;
                o.path_pos = s.pos;
            }
            if (now < px.push_until) { // tests (BUG-0440): the game's next update moves it back, away from its target
                net::Vec3 cp = game::position(pe.ref.p), d{cp.x - aim.x, cp.y - aim.y, cp.z - aim.z};
                float dl = drive::speed(d);
                if (dl < 0.05f) // (on its target: back along the host's path)
                    d = {-vel.x, -vel.y, -vel.z}, dl = drive::speed(vel);
                if (dl > 0.01f)
                    game::test_push(pe.ref.p, {d.x / dl, d.y / dl, d.z / dl}, px.push_mps);
            }
            drive::walk_place(pe.ref.p, aim, s.rot, vel, px.drv, now, o);
            walked = true;
            px.kinematic = true;
            placed = true;
            // BUG-0334: health, death and the impressed flag below are synced for a running NPC too
        }
        if (!placed && px.kinematic) { // it stands again: the driving takes over from here
            px.kinematic = false;
            if (px.drv.kinematic) // walked (BUG-0361): a MoveTo to where it stops, as the partner's puppet
                drive::walk_end(pe.ref.p, s.pos, px.drv, now);
            else
                *(net::Vec3*)((uint8_t*)pe.ref.p + game::kVelOff) = {0, 0, 0};
        }
        if (placed) {
            // placed on the host's path this frame: no driving
            slope_release(pe.ref.p, px); // (BUG-1228)
        } else if (!px.killed) {
            const uint32_t gr = px.drv.grounded;
            drive::step(pe.ref.p, s.pos, s.rot, s.vel, px.drv, now, 0, true, 8.f, g_stall_fix, true, rts_stage(), // own height in Tribe (BUG-0329)
                        (rts_stage() || (g_proxy_ground_creature && game::in_creature_stage())) &&
                            drive::g_out_of_ground_fix); // and never under the ground (BUG-0399, BUG-0752)
            if (drive::speed(vel) <= 0.3f) { // BUG-1228: its game slides the standing copy
                if (px.rest_have && net::dist(drawn, px.rest_at) > kSlideM && !px.slope_held) {
                    px.slope_held = true; // (counted with the switch off too: the tests' sign that a copy slid)
                    g_slope_slides++;
                }
                const net::Vec3 at = game::position(pe.ref.p);
                if (px.slope_held && g_slope_hold_fix) {
                    game::place_hold(pe.ref.p, at, game::orientation(pe.ref.p), px.drv.step_to, 0.f, true);
                    *(net::Vec3*)((uint8_t*)pe.ref.p + game::kVelOff) = {0, 0, 0};
                }
                px.rest_at = at;
                px.rest_have = true;
            } else
                slope_release(pe.ref.p, px);
            if (px.drv.grounded != gr && px.drv.grounded <= 3) // (diagnostics: the fix at work, the first times)
                H->log(M, SK_INFO, "npc: proxy %u (species %08X) was %.2f m under this game's ground, made %.1f s ago at %.2f m "
                       "over it: put onto it (npc.proxy_ground)", id, px.key.instance, -px.drv.grounded_from, now - px.made_at,
                       px.made_agl);
        }
        else { // a corpse lies where the host has it: the guest's dead proxy shot off into the sky
               // (~2.5 km/s away from the planet; driven, it stayed ~80 m up: play_tribe.py)
            slope_release(pe.ref.p, px); // (BUG-1228)
            *(net::Vec3*)((uint8_t*)pe.ref.p + game::kVelOff) = {0, 0, 0};
            game::teleport(pe.ref.p, s.pos, s.rot);
        }
        if (!px.killed && !px.air && pe.ref.p) { // BUG-0562: drawn inside this game's nest mound: onto it
            const net::Vec3 now_at = game::position(pe.ref.p), up = proxy_on_mound(now_at);
            if (up.x != now_at.x || up.y != now_at.y || up.z != now_at.z) {
                game::teleport(pe.ref.p, up, game::orientation(pe.ref.p));
                g_mound_lifts++;
            }
        }
        // BUG-1216 diagnostics: a Civ copy drawn over 2 m above this game's ground while the host's creature is on its own
        // (campaign Civ segment: two walking copies 8.2 m up for seconds; the Civ game grounds a walked copy put in the air
        // within a frame, labs 20261117-122049 / -1225xx, so something held these up): what drove it, once per 10 s
        if (civ_stage() && !px.killed && !px.air && pe.ref.p && net::agl_known(s.agl) && std::fabs(s.agl) < 0.5f &&
            now - px.high_logged > 10.0) {
            const net::Vec3 at = game::position(pe.ref.p);
            const float agl = drive::over_ground(at);
            if (agl > 2.f) {
                px.high_logged = now;
                H->log(M, SK_WARN, "npc: proxy %u (species %08X) drawn %.2f m over this game's ground, the host's %.2f m over its "
                       "own: %s, game grounded=%d dead=%d, host speed %.1f, made %.1f s ago at %.2f, tp=%u held=%d time paused=%d; "
                       "at (%.1f %.1f %.1f) r %.1f, before this frame %.2f m up, target (%.1f %.1f %.1f) r %.1f %.2f m up, age %.2f (BUG-1216)",
                       id, px.key.instance, agl, s.agl, walked ? "walked" : placed ? "placed" : "driven",
                       (int)game::grounded(pe.ref.p), (int)game::is_dead(pe.ref.p), drive::speed(s.vel), now - px.made_at,
                       px.made_agl, px.drv.teleports, (int)px.drv.held, (int)world::time_paused(), at.x, at.y, at.z,
                       drive::speed(at), drive::over_ground(drawn), s.pos.x, s.pos.y, s.pos.z, drive::speed(s.pos),
                       drive::over_ground(s.pos), px.buf.age(now));
            }
        }
        if ((s.flags & net::NF_DEAD) && !px.killed) {
            px.killed = true;
            // play the death locally with a just-lethal hit: the knockback grows with the damage
            // (1e6 threw the corpse ~150 m up, it hung 15-80 m away from the host's corpse for
            // the rest of its life: play_creature.py 2026-10-23)
            game::apply_damage(pe.ref.p, std::max(game::health(pe.ref.p), 0.f) + 0.5f, 0, nullptr);
            // the death itself is the behaviour tree's (0xD85B80): a proxy has no AI and stood on
            // with 0 health, alive for the game (not edible, "still alive" for the guest; player
            // report 2026-10-25, docs/re/creature.md 14)
            if (g_death_fix) {
                game::play_death(pe.ref.p);
                g_deaths_played++;
            }
        } else if (!px.killed && s.health > 0) {
            // BUG-0710: the host's max health of the NPC (a baby has less than an adult of its species), as the
            // partner's puppet gets the partner's (creature_hp GetMaxHp hook)
            if (g_max_hp_fix && px.max_health > 0 && creature_hp::drive_puppet(pe.ref.p, s.health, px.max_health))
                g_max_hp_applied++;
            else {
                creature_hp::forget_puppet(pe.ref.p); // (switched off in a session: the game's own max again)
                game::set_health(pe.ref.p, s.health);
            }
        }
        if (g_meat_fix && px.killed) { // BUG-0557: what this game's creature ate of the body goes to the host
            float m = body_meat(pe.ref.p);
            if (px.meat_seen > -1e8f && m < px.meat_seen - 0.001f)
                g_meat_eaten[id] += px.meat_seen - m;
            px.meat_seen = m;
        }
        if (!px.killed) // the host's NPC was impressed (by anybody): the proxy shows it too
            game::set_impressed(pe.ref.p, (s.flags & net::NF_IMPRESSED) != 0);
        // BUG-1283 (fork BUG-9115): the nest's alpha is the host's (+0xB58 bit 0x1): a proxy kept the bit of this game's
        // own herd (4 of 15 proxies differed in the fork's run), so the guest's card read the plain name and its social
        // game had one round less (or one more). A host before 0.7.16 (npc below 1.2) sends no NF_ALPHA: the proxy keeps its own bit.
        if (!px.killed && g_alpha_fix && host_sends_alpha())
            game::set_alpha(pe.ref.p, (s.flags & net::NF_ALPHA) != 0);
        px.host_pack = (s.flags & net::NF_PACK) != 0;
        // BUG-0556: a summoned flock's member wears the event's hearts (cSummonCreatureEvent update 0xD512A0)
        bool summoned = !px.killed && (s.flags & net::NF_SUMMONED);
        if (summoned != px.flock_fx && a_StartFx2 && a_StopFx) {
            if (summoned)
                skg::thiscall<void*>(a_StartFx2, pe.ref.p, 1, kFlockFx, kFlockFx);
            else
                skg::thiscall<void>(a_StopFx, pe.ref.p, kFlockFx, 0);
            px.flock_fx = summoned;
        }
        ntrack_row(id, pe, s, drawn, px.killed ? 2 : px.air ? 5 : walked ? 3 : placed ? 1 : 0, now, rv_rep);
    }
    if (g_meat_fix && !g_meat_eaten.empty() && now - g_meat_out_at >= 0.2) { // BUG-0557: the bites, 5 times a second
        g_meat_out_at = now;
        net::Writer w;
        w.u8(1);
        uint8_t n = (uint8_t)std::min<size_t>(100, g_meat_eaten.size());
        w.u8(n);
        auto it = g_meat_eaten.begin();
        for (uint8_t j = 0; j < n; j++, it = g_meat_eaten.erase(it)) {
            w.u16(it->first);
            w.f32(it->second);
            g_meat_bites_tx++;
        }
        C.sess->send_reliable_to(0, net::MSG_NPC_MEAT, w);
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
            if (noun != 0x18EB45Eu || o == a->av || g_console_npcs.count(o) || !game::object_alive(o) || IsBadReadPtr(o, 0x1680) ||
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
        creature_hp::forget_puppet(c);
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
static uint32_t g_away_snap_epoch = 0;
static bool g_was_guest = false;      // BUG-0412: this game mirrored the host's NPCs in its last frame
static bool g_leave_locals_due = false; // BUG-0412: the session ended: hand the proxies over next frame
static uint32_t g_leave_locals = 0;
static void away_snapshot(double now)
{
    static double s_last = -100;
    if (now - s_last < 0.5)
        return;
    s_last = now;
    std::vector<AwaySnap> v;
    for (auto [id, pe, px] : px_walk()) {
        if (!px_live(pe) || px.killed || !px_alive(pe) || game::is_dead(pe.ref.p) || game::destroy_pending(pe.ref.p))
            continue;
        void* h = game::herd_of(pe.ref.p);
        if (!h || !game::herd_nest(h)) // a herd of our own: no nest of the game to go on in
            continue;
        v.push_back({px.key, game::position(pe.ref.p), game::orientation(pe.ref.p)});
    }
    if (v.empty()) // the host's world change took them (its menu): keep the last ones seen
        return;
    g_away_snap.swap(v);
    g_away_snap_at = now;
    g_away_snap_epoch = game::world_epoch();
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
    g_away_snap_at = -100;
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
    if (g_leave_locals_due && !is_guest()) { // BUG-0412: the guest left; its game plays the planet on alone
        g_leave_locals_due = false;
        if (game::in_creature_stage() && game::avatar() && game::world_epoch() == g_away_snap_epoch &&
            !g_away_snap.empty()) {
            unsigned before = g_away_locals;
            away_locals(now);
            g_leave_locals += g_away_locals - before;
        }
    }
    // creature stage only: adventure creatures are scripted by the adventure on every machine
    if (!g_enabled || !C.sess || !npc_stage()) {
        g_anim_out.clear();
        return;
    }
    if (is_host()) {
        host_tick(now, creatures);
        g_anim_out.clear(); // (host_tick returns early in some frames: never send an old frame's)
    } else if (is_guest()) {
        g_was_guest = true;
        uint32_t n = 0;
        for (void* c : creatures)
            n += !game::is_puppet(c) && c != game::avatar();
        g_local_alive = n;
        bool away = C.host_away && C.host_away();
        // BUG-1174 (owner decision 2): the host in its editor is not a world change: the proxies are held (D3:
        // npc_rules::editor_hold), the guest's game does not take the planet over
        const uint32_t hm = C.host_mode ? C.host_mode() : 0u;
        const bool hold = g_editor_hold_fix && npc_rules::editor_hold(away, hm == modes::kEdit || hm == modes::kEditor,
                                                                      g_editor_held, hm == modes::kMenu || hm == modes::kLoad);
        if (hold && !g_editor_held) {
            g_editor_holds++;
            H->log(M, SK_INFO, "npc: the host is in its editor: %u proxies held (npc.editor_hold)", (unsigned)px_count());
        }
        g_editor_held = hold;
        if (hold)
            away = false; // (no states come: they stand as during a pause; the stale drop waits for the states)
        if (away && !g_host_away) { // the host's NPCs stand frozen and nothing removes them: go
            std::vector<uint16_t> ids;
            for (auto [id, pe, px] : px_walk())
                ids.push_back(id);
            g_away_proxies += (uint32_t)ids.size();
            away_locals(now);
            for (uint16_t id : ids)
                destroy_proxy(id);
            g_herds.clear();
        }
        g_host_away = away;
        if (!away) {
            guest_tick(now);
            resync_tick(now); // BUG-0487
            if (game::in_creature_stage())
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

// BUG-0722 (class BUG-0431): game code reads a creature's archetype tuning (+0xE84) without a null check (the getter
// 0xC0C300 from 0xBA43E0, ApplyEvent 0xBA4A00, CreatureSocialSuccess 0xD9BBD0, Create 0xC09B40, the direct read
// 0xC115E0; docs/re/creature.md#creature-puppet-tuning). Proxies get the host's (BUG-0462); a partner's puppet got
// one only from its herd: the puppet of a guest playing its own species (a herd without an archetype) had none on
// every other game (lab 20261108-040137: 1 of 24-25 of ours on three games, for minutes). Guards covered three
// readers only (SelectUI, the stage event handler, the AI tick, which lends the avatar's for its tick). Now every
// puppet in the Creature stage without one gets the avatar's tuning (the player archetype, what Create gives a
// creature of the player's herd), checked five times a second. `coop.fix npc.puppet_tuning off`: as before.
static bool g_puppet_tuning_fix = true;
COOP_FIX("npc.puppet_tuning", "BUG-0722", g_puppet_tuning_fix, "creature: a partner's creature always carries an archetype tuning, the player's (game code reads it without a check)");
static void puppet_tuning_tick(double now)
{
    static double last;
    if (!g_puppet_tuning_fix || now - last < 0.2 || !C.sess || !C.puppet_of || game::mode_id() != modes::kCreature)
        return;
    last = now;
    static uintptr_t set = H->sym("SporeCoop::cCreatureBase_SetTuning");
    void* av = game::avatar();
    void* t = av && skg::readable((uint8_t*)av + 0xE84, 4) ? *(void**)((uint8_t*)av + 0xE84) : nullptr;
    if (!set || !t)
        return;
    for (auto& p : C.sess->players()) {
        if (p.id == C.sess->my_id())
            continue;
        void* pup = C.puppet_of(p.id);
        if (!pup || !game::is_puppet(pup) || !game::object_alive(pup) || !skg::readable((uint8_t*)pup + 0xE84, 4) ||
            *(void**)((uint8_t*)pup + 0xE84))
            continue;
        skg::thiscall<void>(set, pup, t);
        g_puppet_tunings_set++;
        H->log(M, SK_INFO, "npc: the puppet %p of player %u had no archetype tuning: the player's given (BUG-0722)", pup, (unsigned)p.id);
    }
}

static void reuse_tick();
void tick(double now)
{
    reuse_tick();
    tick_frame(now);
    puppet_tuning_tick(now);
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
    if (is_host() && g_world_despawn_fix) {
        size_t n = 0;
        for (auto& [g, ks] : g_known)
            for (uint16_t id : ks) {
                send_despawn(g, id);
                n++;
            }
        H->log(M, SK_INFO, "npc: world change (avatar %p): %zu NPC proxies despawned at the guests, %zu NPCs forgotten",
               game::avatar(), n, hn_count());
    }
    for (auto [id, pe, px] : px_walk()) {
        if (pe.ref.p)
            game::puppet_unregister(pe.ref.p);
        px_detach(pe);
        px.killed = false;
        px.drv = drive::State{};
    }
    g_unknown_ids.clear();
    g_herds.clear();
    g_local_herds.clear();
    g_kill.clear();
    hn_clear();
    g_ai_target.clear();
    g_pack_owners.clear(); // BUG-1256: old world's creatures are already unloaded
    g_pack_dirty = false;
    g_guest_pack_owners.clear();
    g_guest_my_pack.clear();
    g_guest_pack_ptr.clear();
    teamplay::reset();
    for (auto& [g, ks] : g_known)
        ks.clear(); // host re-sends spawns
}
// C2 (BUG-1141): a world that was open changed or closed (world_id.h; not an editor visit, decision 2)
LIFECYCLE_HOOK("npc.world", lifecycle::kWorldChange, -90, [](const lifecycle::Ctx& c) { if (c.from_stage >= 0) on_world_change(); }, true);

// BUG-0125: host_tick forgets what a guest mirrors only while the simulation runs; a guest that
// left and joined again (the same id) while the host was paused kept the old set and got no spawns
static bool g_known_fix = true;
COOP_FIX("npc.known", "BUG-0125", g_known_fix, "creature: a guest that left forgets its known NPCs while the host is paused too");
void set_known_fix(bool on) { g_known_fix = on; }
void on_player_leave(uint8_t id)
{
    g_meat_sent.erase(id);
    g_civ_sent.erase(id); // BUG-0973
    if (g_known_fix)
        g_known.erase(id);
    release_player_pack(id); // BUG-1256: release this player's recruited pack members
}

void on_session_end()
{
    // BUG-1256: release every guest's recruited pack members on the host before clearing host NPCs
    if (is_host() && !g_pack_owners.owners.empty()) {
        for (uint8_t p = 1; p < 8; p++)
            release_player_pack(p);
    }
    g_pack_owners.clear();
    g_pack_dirty = false;
    g_guest_pack_owners.clear();
    // BUG-0412: a guest that leaves plays on in this world: its game takes over the NPCs it showed
    // (the snapshot of away_snapshot, at most 0.5 s old; away_locals runs next frame, outside the
    // session's update)
    if (g_was_guest && !g_host_away && game::in_creature_stage() &&
        H->time_seconds() - g_away_snap_at < 2.0)
        g_leave_locals_due = true;
    g_was_guest = false;
    g_editor_held = false; // BUG-1174
    g_host_pass_at = 0;
    std::vector<uint16_t> ids;
    for (auto [id, pe, px] : px_walk())
        ids.push_back(id);
    for (uint16_t id : ids)
        destroy_proxy(id);
    g_herds.clear();
    g_local_herds.clear();
    hn_clear();
    g_known.clear();
    g_kill.clear();
    g_ai_target.clear();
    g_unknown_ids.clear();
    teamplay::reset();
}
LIFECYCLE_HOOK("npc.session", lifecycle::kSessionEnd, -90, [](const lifecycle::Ctx&) { on_session_end(); }, true); // C2 (BUG-1141): end_session

// F3 (BUG-0768): all.friendly_fire (session, default off): a player's hit on a partner's creature hurts that partner
static bool friendly_fire() { return settings::on(settings::kFriendlyFire); }
static unsigned g_pvp_out, g_pvp_in, g_pvp_relayed; // hits on partners sent / taken by the host's avatar / relayed

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    double now = H->time_seconds();
    switch (type) {
    case net::MSG_NPC_SPAWN: {
        net::NpcSpawn sp;
        if (!is_guest() || !sp.read(r))
            return;
        destroy_proxy(sp.net_id);
        Proxy& px = px_make(sp.net_id).data;
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
        px.buf.lead = true; // BUG-0425 (drive.lead): where the host has it now
        px.buf.late_lead = true; // BUG-0674 (drive.lead_fall): a late state brought in over time, its jumps smoothed
        px.buf.smooth_max = 8.f; // (a turn known late: 3.8-4 m, lab 20261108-013741 #2, proxy 22 at 23.2 s)
        px.buf.jump_speed = g_civ_jump_fix && civ_stage() ? modes::puppet_jump_speed() : 0.f; // BUG-1216
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
        // BUG-0550: the agl trailer after the n entries (an older host sends none)
        const bool trailer = r.ok() && r.left() == (size_t)n * (net::NpcState::kSize + 2);
        std::vector<net::NpcState> sts;
        for (int i = 0; i < n && r.ok(); i++) {
            net::NpcState st;
            if (!st.read(r)) {
                whole = false;
                break;
            }
            sts.push_back(st);
        }
        std::vector<float> agls(sts.size(), net::kNoAgl);
        if (trailer && whole)
            for (size_t i = 0; i < sts.size() && r.ok(); i++)
                agls[i] = (int16_t)r.u16() / 100.f;
        if (now < g_rxkeep_until && sts.size() > (size_t)g_rxkeep_n) { // tests (BUG-0611): a lost packet of the batch
            g_rxkeep_cut += (uint32_t)(sts.size() - g_rxkeep_n);
            sts.resize(g_rxkeep_n);
        }
        for (size_t i = 0; i < sts.size(); i++) {
            const net::NpcState& st = sts[i];
            auto it = px_find(st.net_id);
            if (it == nullptr) {
                if (!g_unknown_ids.count(st.net_id) && g_unknown_ids.size() < 4000)
                    g_unknown_ids[st.net_id] = 0; // BUG-0487: ask for its spawn (resync_tick)
                continue;
            }
            net::PlayerState s;
            s.time_ms = t;
            s.pos = st.pos;
            s.rot = st.rot;
            s.vel = game::finite_vel(st.vel); // BUG-0894
            s.health = st.health;
            s.flags = st.flags;
            s.agl = agls[i];
            it->data.buf.push(s, now);
            it->data.last_rx = now;
        }
        if (whole) { // the host sends states (an empty or cut batch says nothing about stale proxies)
            if (now - g_last_states_rx > 1.0) // BUG-0611: they flow again after a time without any
                g_states_flow_since = now;
            g_last_states_rx = now;
        }
        break;
    }
    case net::MSG_NPC_MEAT: { // BUG-0557: the meat of the bodies
        if (!g_meat_fix)
            return;
        uint8_t kind = r.u8();
        uint8_t n = r.u8();
        for (uint8_t j = 0; j < n && r.ok(); j++) {
            uint16_t id = r.u16();
            float v = r.f32();
            if (!r.ok())
                break;
            if (kind == 0 && is_guest()) { // the host's meat of the body: the proxy shows it when it is less
                g_meat_rx++;
                auto f = px_find(id);
                if (f == nullptr || !handler_c(*f) || !f->data.killed || !game::is_dead(f->ref.p))
                    continue;
                PxE& pe = *f;
                Proxy& px = pe.data;
                auto e = g_meat_eaten.find(id); // (bites not sent yet are not in the host's value)
                float want = v - (e != g_meat_eaten.end() ? e->second : 0.f);
                if (want < body_meat(pe.ref.p) - 0.001f) {
                    set_body_meat(pe.ref.p, want);
                    px.meat_seen = want;
                    g_meat_set++;
                }
            } else if (kind == 1 && is_host()) { // the guest's creature ate this much of its proxy of the body
                g_meat_bites_rx++;
                void* b = hn_obj(id);
                if (b && game::creature_listed(b) && game::is_dead(b))
                    set_body_meat(b, body_meat(b) - v);
            }
        }
        break;
    }
    case net::MSG_NPC_ANIM: { // guest: the host's NPCs' behaviour animations (BUG-0464)
        if (!is_guest())
            return;
        uint8_t kind = r.u8();
        // C7 (BUG-1160): since 0.7.15 a host sends these as MSG_TRIBE_ANIM to a guest that knows it; a 0.7.14 host (and a
        // 0.7.15 host to an older guest) still sends them here
        if (wire::tribe_anim::is_tribe_kind(kind)) { // (21: BUG-1086, the host's pause; 16: BUG-1072, the members' other animation tracks; 12: BUG-0971, a hold start with its item; 14: BUG-0988, the members' animation times; 15: BUG-1069, the same for other animations; 20: BUG-0960, the host's game-over caption) // BUG-0569 / BUG-0568 / BUG-0646 / BUG-0946 / BUG-0936 / BUG-0914: the tribe members' animations, the theme and party music, the members in the air (tribe.cpp)
            tribe::on_member_anims(kind, r);
            return;
        }
        uint8_t n = r.u8();
        if (kind == 1) { // what the host's NPCs play now: the proxy plays it too when it plays another
            for (int i = 0; i < n && r.ok(); i++) {
                uint16_t id = r.u16();
                uint32_t anim = r.u32();
                if (!r.ok())
                    break;
                g_anims_snap_rx++;
                auto it = px_find(id);
                if (it == nullptr)
                    continue;
                PxE& pe = *it;
                Proxy& px = pe.data;
                uint32_t cur = 0;
                if (!px_live(pe) || game::is_destroyed(pe.ref.p) || now - px.anim_fix_at < 2.0 || now - px.anim_rx_at < g_anim_snap_quiet ||
                    !game::drawn_state(pe.ref.p, nullptr, &cur) || cur == anim)
                    continue;
                px.anim_fix_at = now; // (at most every 2 s: never restarts an animation that will not take)
                if (game::play_behaviour_anim(pe.ref.p, anim, 0, 1, -1))
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
            auto it = px_find(id);
            void* c = it == nullptr ? nullptr : handler_c(*it); // BUG-0805
            if (!c || it->data.killed || game::is_destroyed(c) || game::is_dead(c)) {
                g_anims_noproxy++;
                continue;
            }
            it->data.anim_rx_at = now;
            if (game::play_behaviour_anim(c, anim, how, a, b))
                g_anims_played++;
            else
                g_anims_failed++;
        }
        break;
    }
    case net::MSG_NPC_RESYNC: { // host: a guest gets states of NPCs it has no spawn of (BUG-0487)
        if (!is_host())
            return;
        uint8_t n = r.u8();
        auto& known = g_known[from];
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            void* it = hn_obj(id);
            // only what this guest mirrors (the host sends it states): others come with the relevance rule
            if (!r.ok() || !it || !known.count(id) || !g_seen_now.count(it))
                continue;
            if (g_msg_live_fix && !host_npc(id)) { // CR-2 (BUG-1428): removed since the last host pass
                g_msg_gone_refused++;
                continue;
            }
            send_spawn(from, id, it);
            g_resync_sent++;
        }
        break;
    }
    case net::MSG_DAMAGE_REQ: { // host: a guest hit one of our NPCs
        net::DamageReq q;
        if (!is_host() || !q.read(r))
            return;
        void* it = hn_obj(q.net_id);
        if (!it || !g_seen_now.count(it))
            return;
        if (g_msg_live_fix && !host_npc(q.net_id)) { // CR-2 (BUG-1428): the game removed it since the last host pass
            g_msg_gone_refused++;
            return;
        }
        void* npc = it;
        if (creature_hp::drop_partner_hit(npc)) // BUG-0374: the host's pack / family (npc.party_peace)
            break;
        // BUG-0805: the guest's puppet as the attacker only while it is a live creature here (TakeDamage relays it)
        void* attacker = g_handler_live_fix && C.puppet_live ? C.puppet_live(from) : C.puppet_of(from);
        float dmg = teamplay::on_player_hit(npc, from, net::sane_damage(q.damage, kMaxHit));
        dmg = party::player_hits_npc(dmg); // WP-D1 (BUG-1473): all.party_difficulty, a guest's hit on our NPC
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
        {
            // WP-H1 (BUG-1471): the game's kill credit inside (cPlayer::AddTraitProgress, the DNA) is this guest's act
            actor::Scope act(from);
            game::apply_damage(npc, dmg, q.type, attacker);
        }
        g_hits_applied++;
        break;
    }
    case net::MSG_DAMAGE: { // guest: the host says we were hit
        net::Damage d;
        if (is_host() && from != 0 && friendly_fire() && d.read(r) && std::isfinite(d.damage)) {
            // F3 (BUG-0768), all.friendly_fire: a guest hit a partner's creature in its game: the host's own avatar
            // takes it here, another guest's hit goes on to that guest (as if the host's game had seen it)
            d.damage = std::clamp(d.damage, 0.f, kMaxHit);
            d.attacker = 0xFFFF;
            if (d.player == 0) {
                void* av = game::avatar();
                if (!av || game::mode_id() != modes::kCreature)
                    return;
                float dmg = teamplay::on_own_damage(av, d.damage);
                void* by = g_handler_live_fix && C.puppet_live ? C.puppet_live(from) : C.puppet_of ? C.puppet_of(from) : nullptr;
                log_hit(0, by, dmg, "friendly fire from a guest", true);
                game::apply_damage(av, dmg, d.type, by); // (BUG-0805: a live puppet or none)
                g_pvp_in++;
            } else if (d.player != from && net::is_player(*C.sess, d.player)) {
                net::Writer w;
                d.write(w);
                C.sess->send_reliable_to(d.player, net::MSG_DAMAGE, w);
                g_pvp_relayed++;
            }
            return;
        }
        if (!is_guest() || !d.read(r) || d.player != C.sess->my_id())
            return;
        void* av = game::avatar();
        if (!av)
            return;
        void* attacker = nullptr;
        auto it = px_find(d.attacker);
        if (it != nullptr)
            attacker = handler_c(*it); // BUG-0805
        if (player_rules::guest_drops_air_hit(av, attacker, d.ability)) { // BUG-0555: in the air in this game
            log_hit(C.sess->my_id(), attacker, 0, "from the host, dropped (creature.air_immune)", true);
            break;
        }
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
        uint16_t pid;
        if (px_id_of(c, pid)) { // hit on a host NPC: the host applies it
            if (attacker == av && social::drop_social_hit(pid))
                return false; // BUG-0021: the social stance does not attack
            if (attacker == av) {
                net::DamageReq q;
                q.net_id = pid;
                q.damage = damage;
                q.type = type;
                net::Writer w;
                q.write(w);
                C.sess->send_reliable_to(0, net::MSG_DAMAGE_REQ, w);
                g_hits_sent++;
            }
            return false;
        }
        if (game::is_puppet(c)) {
            if (attacker == av && av && friendly_fire()) // F3 (BUG-0768): the hit goes to that player (via the host)
                for (auto& p : C.sess->players())
                    if (p.id != C.sess->my_id() && C.puppet_of(p.id) == c) {
                        net::Damage d;
                        d.player = p.id;
                        d.damage = damage;
                        d.type = type;
                        d.attacker = 0xFFFF;
                        net::Writer w;
                        d.write(w);
                        C.sess->send_reliable_to(0, net::MSG_DAMAGE, w);
                        g_pvp_out++;
                        log_hit(p.id, attacker, damage, "friendly fire, sent");
                    }
            return false; // the puppet mirrors that player's own health
        }
        if (c == av) {
            damage = teamplay::on_own_damage(av, damage);
            log_hit(C.sess->my_id(), attacker, damage, "");
        }
        return true;
    }
    if (is_host()) {
        if (game::mode_id() == modes::kTribe)
            tribe::note_animal_hit(c, attacker);
        // WP-D1 (BUG-1473): all.party_difficulty: an NPC's hit on a player, the host's hit on an NPC (party_difficulty.h)
        bool npc_attacker = attacker && attacker != av && !game::is_puppet(attacker);
        if (c == av) {
            if (npc_attacker)
                damage = party::npc_hits_player(damage); // (before the pack rescue and the revive: they see the real hit)
            damage = teamplay::on_own_damage(av, damage);
            log_hit(0, attacker, damage, "");
            return true;
        }
        if (!game::is_puppet(c)) {
            if (attacker == av)
                damage = party::player_hits_npc(teamplay::on_player_hit(c, 0, damage));
            return true;
        }
        // a guest's puppet was hit: forward real NPC hits to that guest
        if (player_rules::air_hit_now()) { // BUG-0555: a melee hit the partner's creature in the air does not take
            for (auto& p : C.sess->players())
                if (p.id != 0 && C.puppet_of(p.id) == c)
                    log_hit(p.id, attacker, 0, "dropped (creature.air_immune)");
            return false;
        }
        bool pvp = attacker && attacker == av && friendly_fire(); // F3 (BUG-0768): the host hit a guest
        // BUG-0790: the attacker may be a vehicle, UFO, turret or building: the party check and the pack defence read
        // creature fields, they take a listed creature only
        void* att_creature = !attacker || !game::attacker_owner_fix() || game::creature_listed(attacker) ? attacker : nullptr;
        if (!pvp && creature_hp::drop_party_hit(att_creature)) { // BUG-0374: never from the host's pack / family
            for (auto& p : C.sess->players())
                if (p.id != 0 && C.puppet_of(p.id) == c)
                    log_hit(p.id, attacker, 0, "dropped (npc.party_peace)");
            return false;
        }
        for (auto& p : C.sess->players()) {
            if (p.id == 0 || C.puppet_of(p.id) != c)
                continue;
            if (pvp)
                g_pvp_out++;
            if (pvp || (attacker && attacker != av && !game::is_puppet(attacker))) {
                net::Damage d;
                d.player = p.id;
                d.damage = pvp ? damage : party::npc_hits_player(damage); // WP-D1 (BUG-1473): the guest takes it scaled
                d.type = type;
                HostNpc* it = hn_of(attacker);
                d.attacker = it ? it->id : 0xFFFF;
                d.ability = player_rules::hit_ability_type(); // BUG-0555: the guest answers the air rule too
                net::Writer w;
                d.write(w);
                C.sess->send_reliable_to(p.id, net::MSG_DAMAGE, w);
                log_hit(p.id, attacker, damage, "forwarded");
                if (!pvp && att_creature)
                    creature_hp::pack_defend(c, att_creature); // BUG-0735: the host's pack goes for the attacker
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
        for (auto [c, hn] : hn_walk())
            if (g_seen_now.count(c)) // alive this frame: its position is readable
                count(game::position(c), hn.dead);
    } else {
        for (auto [id, pe, px] : px_walk())
            if (!px.buf.empty())
                count(px.buf.latest().pos, px.killed);
    }
    char b[160];
    snprintf(b, sizeof b, "npcs %s: %u within %.0f m (%u dead), nearest %.0f m, %u in all", is_host() ? "simulated" : "mirrored",
             n_near, radius, dead, nearest, total);
    return b;
}

static std::string meat_status() // BUG-0557
{
    char b[160];
    snprintf(b, sizeof b, "npc body meat: fix=%d rx=%u set=%u bites_tx=%u bites_rx=%u\n", (int)g_meat_fix, g_meat_rx,
             g_meat_set, g_meat_bites_tx, g_meat_bites_rx);
    return b;
}

// C1 (BUG-1140): the two id tables (mirror::Registry counters)
static std::string registry_status()
{
    char b[96];
    snprintf(b, sizeof b, " rebound=%u editor_holds=%u held=%d host_gaps=%u\n", g_px_rebound, g_editor_holds, (int)g_editor_held,
             g_host_gaps);
    return "npc registry: " + g_hreg.status() + "\nnpc registry: " + g_px.status() + b;
}

std::string status()
{
    char b[1024];
    if (is_host()) {
        size_t known = 0;
        for (auto& [g, ks] : g_known)
            known += ks.size();
        size_t on_partner = 0;
        for (auto& [c, pup] : g_ai_target)
            on_partner += g_seen_now.count(c) != 0;
        snprintf(b, sizeof b, "npc(host): tracked=%u alive=%u mirrored=%u hits_applied=%u guest_kills=%u strays_removed=%u "
                 "hatch_helped=%u partner_targets=%d on_partner=%u partner_picks=%u ai_swaps=%u swaps_restored=%u ledger_restored=%u reused=%u hunt_targets=%u pooled=%u msg_gone_refused=%u\n",
                 (unsigned)hn_count(), (unsigned)g_seen_now.size(), (unsigned)known, g_hits_applied,
                 g_kills_credited, g_strays_removed, g_hatch_helped, (int)g_partner_targets, (unsigned)on_partner,
                 g_ai_partner_picks, game::ai_swaps(), game::swaps_restored(), game::swap_ledger_restored(), g_reused, (unsigned)teamplay::hit_targets(), g_pooled_retired, g_msg_gone_refused);
        char b2[320]; // BUG-0512
        snprintf(b2, sizeof b2, "npc(host) pack: partner_targets_cleared=%u partner_stimuli_cleared=%u frozen_s=%.1f frozen_spawns=%u frozen_despawns=%u frozen_retired=%u frozen_reused=%u frozen_moved=%u\n",
                 g_pack_targets_cleared, g_pack_stimuli_cleared, g_frozen_since > 0 ? H->time_seconds() - g_frozen_since : 0.0,
                 g_frozen_spawns, g_frozen_despawns, g_frozen_retired, g_frozen_reused, g_frozen_moved);
        char b3[128]; // F3 (BUG-0768)
        snprintf(b3, sizeof b3, "npc friendly fire: on=%d pvp_out=%u pvp_in=%u pvp_relayed=%u\n", (int)friendly_fire(), g_pvp_out,
                 g_pvp_in, g_pvp_relayed);
        char b4[128]; // BUG-0973
        snprintf(b4, sizeof b4, "npc civ: wild=%d quiet=%d quiet_skipped=%u\n", (int)g_civ_wild_fix, (int)g_civ_quiet_fix,
                 g_civ_quiet_skipped);
        return std::string(b) + b2 + b3 + b4 + guest_pack_status() + meat_status() + registry_status() + game::destroy_deferred_status() + "\n";
    } else {
        unsigned spawned = 0;
        for (auto [id, pe, px] : px_walk())
            spawned += pe.ref.p != nullptr;
        snprintf(b, sizeof b, "npc(guest): proxies=%u spawned=%u local_removed=%u hits_sent=%u damage_in=%u lost=%u host_away=%d "
                 "away_dropped=%u local_alive=%u in_nest=%u nests=%u stale_dropped=%u strays_removed=%u hatch_helped=%u "
                 "deaths_played=%u kill_held=%u away_locals=%u unlisted=%u nospecies=%u leave_locals=%u thaw_kept=%u rxkeep_cut=%u mound_lifts=%u mound_targets=%u max_hp_applied=%u proxy_holds=%u air_catches=%u civ_jumps=%u slope_slides=%u still_kept=%u\n",
                 (unsigned)px_count(), spawned, g_removed_local, g_hits_sent, g_damage_in, g_proxy_lost, (int)g_host_away,
                 g_away_proxies, g_local_alive, g_proxies_in_nest, (unsigned)g_local_herds.size(), g_stale_dropped,
                 g_strays_removed, g_hatch_helped, g_deaths_played, (unsigned)g_kill.size(), g_away_locals, g_proxy_unlisted,
                 g_proxy_nospecies, g_leave_locals, g_thaw_kept, g_rxkeep_cut, g_mound_lifts, g_mound_targets, g_max_hp_applied,
                 game::place_holds_kept(), g_air_catches, g_civ_jumps, g_slope_slides, game::still_holds_kept());
    }
    char b3[128]; // F3 (BUG-0768)
    snprintf(b3, sizeof b3, "npc friendly fire: on=%d pvp_out=%u pvp_in=%u pvp_relayed=%u\n", (int)friendly_fire(), g_pvp_out,
             g_pvp_in, g_pvp_relayed);
    char b5[96]; // BUG-1293
    snprintf(b5, sizeof b5, "npc species fallback: on=%d host_key_builds=%u\n", (int)g_species_fallback, g_species_fallbacks);
    return b + std::string(b3) + b5 + guest_pack_status() + meat_status() + registry_status() + game::destroy_deferred_status() + "\n";
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

// BUG-0917 (the class of `coop.abilities reach`): the live objects of the game's list right now. A console command
// runs between frames: the tables of the last tick (g_seen_now, the proxies, the herds) can name an object the
// game freed since, and `list` makes virtual calls on them (body_print, noun_id).
static std::unordered_set<void*> listed_now()
{
    std::unordered_set<void*> s;
    game::for_each_object(
        [](void* o, uint32_t, void* u) {
            if (game::object_alive(o))
                ((std::unordered_set<void*>*)u)->insert(o);
            return true;
        },
        &s);
    return s;
}

std::string list()
{
    std::string out;
    char b[640];
    void* av = game::avatar();
    net::Vec3 ap = av ? game::position(av) : net::Vec3{};
    const std::unordered_set<void*> live = listed_now();
    if (is_host()) {
        double now = H->time_seconds();
        for (auto [c, hn] : hn_walk()) {
            if (!g_seen_now.count(c) || !live.count(c)) { // maybe freed already: no field reads
                snprintf(b, sizeof b, "  #%u %p not updated for %d ms\n", hn.id, c, (int)((now - hn.last_seen) * 1000));
                out += b;
                continue;
            }
            net::Vec3 p = game::position(c);
            // " DEAD" right after dist: tests match "dist=N( DEAD)?"
            snprintf(b, sizeof b, "  #%u %p species=%08X hp=%.2f dist=%.1f%s pos=(%.1f %.1f %.1f) spd=%.1f vel=(%.1f %.1f %.1f) upd=%d/64 velfr=%d/64 moved64=%.1f%s%s%s body=%08X impressed=%d agl=%.2f alpha=%d\n", hn.id,
                     c, game::species_key(c).instance, game::health(c), net::dist(p, ap), game::is_dead(c) ? " DEAD" : "",
                     p.x, p.y, p.z, drive::speed(game::velocity(c)), game::velocity(c).x, game::velocity(c).y, game::velocity(c).z,
                     __builtin_popcountll(hn.seen_bits), __builtin_popcountll(hn.vel_bits),
                     net::dist(hn.pos_hist[hn.hist_at], hn.pos_hist[(hn.hist_at + 1) & 63]), game::pack_member(c) ? " pack" : "",
                     // BUG-0390: the avatar's herd (family): a partner's hit on it is dropped (npc.party_peace)
                     game::herd_of(c) && game::herd_of(c) == game::avatar_herd() ? " family" : "",
                     g_ai_target.count(c) ? " on_partner" : "", body_print(c), (int)game::impressed(c), drive::over_ground(p),
                     (int)game::alpha(c)); // BUG-1283 (tests): the game's own alpha bit
            out += b;
        }
    } else {
        double now = H->time_seconds();
        for (auto& [k, h] : g_local_herds) {
            bool hl = live.count(h) != 0; // (a herd gone from the list: its pointer only, no call on it)
            snprintf(b, sizeof b, "  nest %08X/%08X herd=%p alive=%d noun=%08X\n", (uint32_t)k, (uint32_t)(k >> 32), h,
                     (int)hl, hl ? game::noun_id(h) : 0u);
            out += b;
        }
        for (auto [id, pe, px] : px_walk()) {
            // a proxy whose creature left the list since the last tick is printed as pending (nothing read on it)
            void* pc = pe.ref.p && live.count(pe.ref.p) ? pe.ref.p : nullptr;
            net::PlayerState st;
            bool have = px.buf.sample(now, st);
            float err = pc && have ? net::dist(game::position(pc), st.pos) : -1.f;
            net::Vec3 at = pc ? game::position(pc) : net::Vec3{};
            snprintf(b, sizeof b, "  #%u species=%08X %s hp=%.2f dist=%.1f err=%.2f age=%.2f%s want=(%.1f %.1f %.1f) at=(%.1f %.1f %.1f) "
                     "agl=%.2f want_agl=%.2f made_agl=%.2f grounded=%u rx=%d/s spd=%.1f vel=(%.1f %.1f %.1f) own=%.1f tp=%u stall=%u trail=%u nudge=%u/%u puppet=%d ptr=%p herd=%p body=%08X"
                     " game_dead=%d impressed=%d herd_sp=%d fam=%d alpha=%d\n", // (the game's own flags of the proxy, tests: BUG-0383, w33)
                     id, px.key.instance, pc ? "spawned" : "pending", pc ? game::health(pc) : 0.f,
                     pc ? net::dist(at, ap) : -1.f, err, px.buf.age(now),
                     px.killed ? " DEAD" : "", st.pos.x, st.pos.y, st.pos.z, at.x, at.y, at.z, pc ? drive::over_ground(at) : 0.f,
                     have ? drive::over_ground(st.pos) : 0.f, px.made_agl, px.drv.grounded, px.buf.received_in(1.0), drive::speed(st.vel), st.vel.x, st.vel.y, st.vel.z,
                     pc ? drive::speed(game::velocity(pc)) : 0.f, px.drv.teleports, px.drv.stalls, px.drv.trails, px.drv.nudges_kept,
                     px.drv.nudges, pc ? (int)game::is_puppet(pc) : -1, pc, pc ? game::herd_of(pc) : nullptr, body_print(pc),
                     pc ? (int)game::is_dead(pc) : -1, pc ? (int)game::impressed(pc) : -1,
                     // BUG-0843: its herd (the game's +0x1674) is a listed herd of its own species (1), of another (0)
                     pc && game::herd_of(pc) && live.count(game::herd_of(pc))
                         ? (int)(game::herd_species(game::herd_of(pc)) == game::species_profile(pc))
                         : -1,
                     pc && game::herd_of(pc) && game::herd_of(pc) == game::avatar_herd() ? 1 : 0,
                     pc ? (int)game::alpha(pc) : -1); // BUG-1283 (tests)
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
            bool ours = game::is_puppet(o) || is_proxy(o);
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
    if (is_host()) { // tests (BUG-0926): the host's own creature hits its NPC #id (the game's TakeDamage, the filter)
        void* h = hn_obj(id);
        if (!h || !g_seen_now.count(h) || !game::avatar())
            return false;
        g_test_hit = true;
        game::hit(h, damage, 0, game::avatar());
        g_test_hit = false;
        return true;
    }
    auto it = px_find(id);
    if (it == nullptr || !it->ref.p || !game::avatar())
        return false;
    g_test_hit = true; // (BUG-0021: a test's hit passes in the social stance)
    game::hit(it->ref.p, damage, 0, game::avatar());
    g_test_hit = false;
    return true;
}

} // namespace npc

namespace npc {
void* proxy(uint16_t id)
{
    auto it = px_find(id);
    return it == nullptr ? nullptr : handler_c(*it); // BUG-0805 (social's prompt and result)
}

bool proxy_in_host_pack(void* c)
{
    uint16_t id;
    PxE* it = px_id_of(c, id) ? px_find(id) : nullptr;
    return it != nullptr && it->data.host_pack;
}

uint16_t id_of(void* c)
{
    if (!c)
        return 0xFFFF;
    if (const HostNpc* h = hn_of(c))
        return h->id;
    uint16_t id;
    return px_id_of(c, id) ? id : 0xFFFF;
}

void* host_npc(uint16_t id)
{
    void* c = hn_obj(id);
    if (!c || !g_seen_now.count(c))
        return nullptr;
    return ref_alive(g_hreg.find(id)->ref) ? c : nullptr; // C1: the object the id was given to, in the list now
}

int host_id(void* c)
{
    const HostNpc* h = hn_of(c);
    return h ? h->id : -1;
}

int proxy_id(void* c)
{
    uint16_t id;
    return px_id_of(c, id) ? id : -1;
}

// BUG-0994 (class BUG-0426, the live pointer package L2 of 0.7.14): social.cpp's h_Post (the avatar's social click,
// inside the game's simulation) asked which proxy owns an AI block by reading +0xB4C of every key of g_proxy_of, also
// of a creature the guest's game had removed since the last guest_tick (the settle second after an avatar change, a
// frame before check_identity): a freed block read, or another creature built there that matched and sent K_ENGAGE
// for the gone proxy (the host started a social game with its NPC). Now only proxies whose creature is the object
// they made (px_alive: the game's list and the serial; one walk, no game code runs in the loop) are asked.
// `coop.fix npc.ai_lookup_live off`: every key (0.7.13).
static bool g_ai_lookup_live_fix = true;
COOP_FIX("npc.ai_lookup_live", "BUG-0994", g_ai_lookup_live_fix,
         "creature: a guest's social click never names a copy of an animal its game has just removed");
static uint32_t g_ai_lookup_gone;
int proxy_id_of_ai(void* ai)
{
    if (g_ai_lookup_live_fix) {
        game::LiveScope live;
        for (auto [id, pe, px] : px_walk()) {
            if (!pe.ref.p)
                continue;
            if (!px_alive(pe)) {
                g_ai_lookup_gone++;
                continue;
            }
            if (*(void**)((uint8_t*)pe.ref.p + 0xB4C) == ai) // cCreatureAnimal +0xB4C: the AI block
                return id;
        }
        return -1;
    }
    for (auto [id, pe, px] : px_walk())
        if (pe.ref.p && *(void**)((uint8_t*)pe.ref.p + 0xB4C) == ai) // cCreatureAnimal +0xB4C: the AI block
            return id;
    return -1;
}
uint32_t ai_lookup_gone() { return g_ai_lookup_gone; }

// host: the pack recruits NPC #id, as the end of a social game with an ally does (tests)
bool debug_recruit(uint16_t id)
{
    void* it = hn_obj(id);
    if (!is_host() || !it || !g_seen_now.count(it))
        return false;
    static uintptr_t get = H->sym("SporeCoop::cPosseSimulator_Get"), add = H->sym("SporeCoop::cPosseSimulator_AddMember");
    void* posse = get && add ? skg::cdecl_call<void*>(get) : nullptr;
    return posse && skg::thiscall<bool>(add, posse, it);
}

// ---------------- BUG-1256: guest pack recruitment public helpers ----------------
bool recruit_for_player(uint16_t id, uint8_t player, int max_pack)
{
    if (!is_host() || !social::g_guest_recruit_fix || player == 0 || player >= 8)
        return false;
    void* c = host_npc(id);
    if (!c || game::is_dead(c) || game::pack_member(c))
        return false;
    if (g_pack_owners.owner_of(id) != social_logic::kNoOwner)
        return false;
    int cap = std::clamp(max_pack, 0, social_logic::kMaxPackSize);
    if (cap <= 0 || g_pack_owners.count_of(player) >= cap)
        return false;
    void* posse = a_PosseGet ? skg::cdecl_call<void*>(a_PosseGet) : nullptr;
    if (!posse || !a_PosseAdd)
        return false;
    // Mark mbTempPosseMember (+0xE68) so AddMember skips host's 3-member cap, increments mTempCount
    // (preserving host's own GetMemberCount), and skips adding a card to the host's CreatureUI.
    uint8_t prev_temp = *(uint8_t*)((uint8_t*)c + 0xE68);
    *(uint8_t*)((uint8_t*)c + 0xE68) = 1;
    bool ok = skg::thiscall<bool>(a_PosseAdd, posse, c);
    if (!ok) {
        *(uint8_t*)((uint8_t*)c + 0xE68) = prev_temp;
        return false;
    }
    if (!g_pack_owners.add(id, player, cap)) {
        host_pack_remove_creature(c);
        return false;
    }
    g_guest_pack_recruited++;
    g_pack_dirty = true;
    H->log(M, SK_INFO, "npc: player %u recruited NPC #%u into their pack (count=%d/%d, BUG-1256)",
           (unsigned)player, (unsigned)id, g_pack_owners.count_of(player), cap);
    return true;
}

bool dismiss_for_player(uint16_t id, uint8_t player)
{
    if (!is_host() || g_pack_owners.owner_of(id) != player)
        return false;
    g_pack_owners.remove(id);
    if (void* c = host_npc(id))
        host_pack_remove_creature(c);
    g_guest_pack_released++;
    g_pack_dirty = true;
    H->log(M, SK_INFO, "npc: player %u dismissed pack member #%u (BUG-1256)", (unsigned)player, (unsigned)id);
    return true;
}

void release_player_pack(uint8_t player)
{
    auto ids = g_pack_owners.release_player(player);
    if (ids.empty())
        return;
    for (uint16_t id : ids) {
        if (game::in_creature_stage()) {
            if (void* c = hn_obj(id))
                host_pack_remove_creature(c);
        }
        g_guest_pack_released++;
    }
    g_pack_dirty = true;
    H->log(M, SK_INFO, "npc: released %zu pack member(s) of player %u (BUG-1256)", ids.size(), (unsigned)player);
}

int pack_count_of(uint8_t player) { return g_pack_owners.count_of(player); }
uint8_t pack_owner_of(uint16_t id)
{
    if (is_host())
        return g_pack_owners.owner_of(id);
    auto it = g_guest_pack_owners.find(id);
    return it != g_guest_pack_owners.end() ? it->second : social_logic::kNoOwner;
}

std::vector<std::pair<uint16_t, uint8_t>> pack_owners_snapshot()
{
    const auto& src = is_host() ? g_pack_owners.owners : g_guest_pack_owners;
    std::vector<std::pair<uint16_t, uint8_t>> out;
    out.reserve(src.size());
    for (auto& [id, p] : src)
        out.push_back({id, p});
    return out;
}

bool take_pack_dirty()
{
    bool d = g_pack_dirty;
    g_pack_dirty = false;
    return d;
}

void set_guest_pack_owners(const std::vector<std::pair<uint16_t, uint8_t>>& entries)
{
    g_guest_pack_owners.clear();
    for (auto& [id, p] : entries)
        g_guest_pack_owners[id] = p;
    guest_pack_sync();
}

int local_posse_count()
{
    void* posse = a_PosseGet && game::in_creature_stage() ? skg::cdecl_call<void*>(a_PosseGet) : nullptr;
    return posse && a_PosseCount ? skg::thiscall<int>(a_PosseCount, posse) : 0;
}

int local_posse_max()
{
    void* posse = a_PosseGet && game::in_creature_stage() ? skg::cdecl_call<void*>(a_PosseGet) : nullptr;
    return posse && a_PosseMax ? std::clamp(skg::thiscall<int>(a_PosseMax, posse), 0, social_logic::kMaxPackSize) : 0;
}

bool debug_guest_dismiss(uint16_t id)
{
    if (!is_guest() || !o_PosseRemove || !a_PosseGet)
        return false;
    void* posse = skg::cdecl_call<void*>(a_PosseGet);
    void* px = proxy(id);
    if (!posse || !px)
        return false;
    h_PosseRemove(posse, px, 1, 0);
    return true;
}

std::string guest_pack_status()
{
    char b[220];
    snprintf(b, sizeof b,
             "guest_pack: fix=%d local_count=%d local_max=%d my_pack=%u total=%u recruited=%u released=%u dismissed=%u\n",
             (int)social::g_guest_recruit_fix, local_posse_count(), local_posse_max(), (unsigned)g_guest_my_pack.size(),
             (unsigned)(is_host() ? g_pack_owners.owners.size() : g_guest_pack_owners.size()), g_guest_pack_recruited,
             g_guest_pack_released, g_guest_pack_dismissed);
    return b;
}

// tests (BUG-0426 class, L1 of 0.7.13): an address the game freed is given to a new object. `reuse <ptr> [frames]
// [species]`: from now on, once per frame, a creature of `species` (default: the one the target had; then the avatar's)
// is made in a herd of its own next to the avatar; one at the target's address stays (the reuse), any other is
// destroyed again at once (so its block goes back too). Ends at the reuse or after `frames` frames.
struct ReuseHunt {
    uint8_t* target = nullptr;
    void* species = nullptr;
    int left = 0, tries = 0, unlisted_at = -1;
    void* hit = nullptr;
    uint32_t hit_serial = 0;
    uint64_t hit_frame = 0;
    int burst = 1;          // creatures made per tick
    bool herds = false;     // the tries are herds (CreateHerd), not creatures
    bool pool = false;      // Create with its reuse flag (a pooled creature of the species)
    void* herd = nullptr;   // the herd they are made in (null: a herd of their own each)
    bool keep = false;      // k: the misses stay alive until the hunt ends (their blocks do not go back while it runs)
    int every = 1;          // e<N>: a try every N frames
    std::vector<void*> kept;
    net::Vec3 at{};         // where (without an avatar: Tribe)
    std::vector<void*> made;
    // the heap chunk of the target (tests, BUG-0825: why a round got no reuse): the dlmalloc-style header before it
    // (size | 1 = the chunk before in use), the next chunk's header (bit 0: the target in use) and the size kept
    // at its end once free (the size of the free chunk, larger when merged with the one before)
    uint32_t hdr0 = 0, hdr_free = 0, next_free = 0, foot_free = 0;
    int freed_at = -1;     // the try count when its chunk was seen free
    uint64_t freed_frame = 0;
    int wait_free = 0;     // f<N>: once the chunk is free, at most N more tries (the game frees a destroyed creature when
                           // its last reference goes; the first creation after it takes the block, or none will)
    bool free_first = false; // w (BUG-1099): no creation until the chunk is seen free (the frames still count), then the
                             // tries at once (docs/re/creature.md#creature-reuse-merged): the creations before the
                             // free only fed other free blocks
    int free_wait = 60, waited = 0; // w<N>: at most N frames waiting for the free
    // BUG-1406 (defer_identity): the queued puppet's block is let go only while creatures are made and destroyed (0 of
    // 17 freed with no creation, 1 of 4 with the misses kept: docs/re/creature.md#creature-freed-in-cinematic), and once
    // free the next creations take it best 4 a frame with the misses kept (BUG-1099: 21 of 27). B<N>: N creatures per
    // tick once the chunk is free; K: the misses kept from the free on (destroyed at once before it)
    int burst_free = 0;
    bool keep_free = false;
};
static ReuseHunt g_reuse;
static void reuse_watch_chunk()
{
    uint8_t* t = g_reuse.target;
    if (!t || g_reuse.freed_at >= 0 || !g_reuse.hdr0)
        return;
    uint32_t sz = g_reuse.hdr0 & ~7u;
    if (sz < 16 || sz > 0x100000 || !skg::readable(t - 8, sz + 8))
        return;
    uint32_t next = *(uint32_t*)(t - 8 + sz + 4);
    // free: the next chunk's header says so, or (BUG-1099) the target's own header changed: the block was freed and
    // merged with the free chunk after it (the creature's own blocks behind it, freed with it), the old next header
    // is stale then (it read "in use" in 13 of 14 rounds that looked never freed, the header had grown: lab 2026-11-14)
    if ((next & 1) && *(uint32_t*)(t - 4) == g_reuse.hdr0)
        return;
    g_reuse.freed_at = g_reuse.tries;
    g_reuse.freed_frame = H->frame_index();
    g_reuse.hdr_free = *(uint32_t*)(t - 4);
    g_reuse.next_free = next;
    g_reuse.foot_free = *(uint32_t*)(t - 8 + sz);
}
static void reuse_release_kept()
{
    std::sort(g_reuse.kept.begin(), g_reuse.kept.end());
    g_reuse.kept.erase(std::unique(g_reuse.kept.begin(), g_reuse.kept.end()), g_reuse.kept.end());
    for (void* c : g_reuse.kept)
        if (game::listed(c))
            game::destroy_untracked(c);
    g_reuse.kept.clear();
}
static void reuse_tick()
{
    if (!g_reuse.kept.empty() && (g_reuse.left <= 0 || g_reuse.hit))
        reuse_release_kept();
    reuse_watch_chunk(); // (also after the hunt: when the block is freed at all)
    if (g_reuse.left <= 0 || !g_reuse.target)
        return;
    g_reuse.left--;
    if (g_reuse.free_first && g_reuse.freed_at < 0) {
        // (BUG-1099: the frames run, no creation yet; a block the game has not freed within free_wait frames: the
        // round ends)
        if (++g_reuse.waited >= g_reuse.free_wait)
            g_reuse.left = 0;
        return;
    }
    if (g_reuse.every > 1 && g_reuse.left % g_reuse.every)
        return;
    if (g_reuse.unlisted_at < 0 && !game::listed(g_reuse.target))
        g_reuse.unlisted_at = g_reuse.tries;
    if (g_reuse.wait_free > 0 && g_reuse.freed_at >= 0) // (freed: the next creations take it or the round is lost)
        g_reuse.left = std::min(g_reuse.left, g_reuse.wait_free - (g_reuse.tries - g_reuse.freed_at));
    void* av = game::avatar();
    void* sp = g_reuse.species ? g_reuse.species : av ? game::species_profile(av) : nullptr;
    if (!sp)
        return;
    net::Vec3 p = av ? game::position(av) : g_reuse.at;
    p.x += 6.f;
    std::vector<void*> miss;
    // burst: up to `burst` creatures in this one tick (the misses stay until the end of it, so each takes a new block)
    const bool freed = g_reuse.freed_at >= 0;
    const int burst = freed && g_reuse.burst_free > 0 ? g_reuse.burst_free : g_reuse.burst;
    const bool keep = g_reuse.keep || (g_reuse.keep_free && freed);
    for (int k = 0; k < std::max(1, burst) && !g_reuse.hit; k++) {
        void* herd = g_reuse.herd ? g_reuse.herd : game::create_herd(p, sp);
        if (keep && !g_reuse.herds && !g_reuse.herd)
            g_reuse.herd = herd; // (one herd for the kept misses)
        void* c = g_reuse.herds ? herd : herd ? game::spawn_creature(p, sp, herd, g_reuse.pool) : nullptr; // (herds: the herd is the try)
        g_reuse.tries++;
        if (!c)
            break;
        if ((uint8_t*)c == g_reuse.target) {
            g_reuse.hit = c;
            g_reuse.hit_serial = game::object_serial(c);
            g_reuse.hit_frame = H->frame_index();
            g_reuse.left = 0;
            if (g_console_npc_fix && !g_reuse.herds)
                g_console_npcs.insert(c);
            H->log(M, SK_INFO, "npc reuse: the freed address %p holds a new %s now (serial %u, try %d)", c,
                   g_reuse.herds ? "herd" : "creature",
                   g_reuse.hit_serial, g_reuse.tries);
            break;
        }
        miss.push_back(c);
    }
    for (void* c : miss) {
        if (g_reuse.made.size() < 8)
            g_reuse.made.push_back(c);
        if (keep && !g_reuse.herds)
            g_reuse.kept.push_back(c);
        else
            game::destroy_untracked(c);
    }
    if (g_reuse.left <= 0 || g_reuse.hit)
        reuse_release_kept();
}
std::string debug_reuse_hunt(void* target, int frames, void* species, int now_tries, int burst, void* herd,
                             const net::Vec3* at, bool herds, bool pool, bool keep, int every)
{
    reuse_release_kept();
    g_reuse = ReuseHunt{};
    g_reuse.keep = keep;
    g_reuse.every = std::max(1, every);
    g_reuse.target = (uint8_t*)target;
    g_reuse.hdr0 = target && skg::readable((uint8_t*)target - 4, 4) ? *(uint32_t*)((uint8_t*)target - 4) : 0;
    g_reuse.left = frames;
    g_reuse.species = species;
    g_reuse.burst = burst;
    g_reuse.herd = herd;
    g_reuse.herds = herds;
    g_reuse.pool = pool;
    if (at)
        g_reuse.at = *at;
    // now_tries > 0: the tries in this call (between two frames: no tick of ours sees the freed address before)
    for (int i = 0; i < now_tries && g_reuse.left > 0; i++) {
        g_reuse.left++; // (reuse_tick counts a frame)
        reuse_tick();
    }
    char b[160];
    snprintf(b, sizeof b, "reuse hunt for %p: %d frames, species %p\n", target, frames, species);
    return b;
}
std::string debug_chunk(void* obj)
{
    // tests (BUG-1099): the heap chunk of an object (docs/re/creature.md#creature-freed-in-cinematic): its size, whether
    // the chunk before it and the one after it are in use. A freed block merges with a free neighbour: then no new
    // object of the size starts at its address (the reuse hunts miss)
    uint8_t* t = (uint8_t*)obj;
    char b[160];
    if (!t || !skg::readable(t - 4, 4)) {
        snprintf(b, sizeof b, "chunk %p: not readable\n", obj);
        return b;
    }
    uint32_t hdr = *(uint32_t*)(t - 4), sz = hdr & ~7u;
    int self = -1, next = -1;
    if (sz >= 16 && sz <= 0x100000 && skg::readable(t - 8 + sz, 8)) {
        uint32_t nh = *(uint32_t*)(t - 8 + sz + 4), nsz = nh & ~7u; // the next chunk's header (bit 0: this one in use)
        self = (int)(nh & 1);
        if (nsz >= 16 && nsz <= 0x1000000 && skg::readable(t - 8 + sz + nsz + 4, 4))
            next = (int)(*(uint32_t*)(t - 8 + sz + nsz + 4) & 1);
    }
    int refs = skg::readable(t + 8, 4) ? *(int*)(t + 8) : -1; // the object's reference count (+8)
    snprintf(b, sizeof b, "chunk %p size=%X prev_used=%d used=%d next_used=%d refs=%d\n", obj, sz, (int)(hdr & 1), self,
             next, refs);
    return b;
}
void debug_reuse_free_first(int frames)
{
    g_reuse.free_wait = frames > 0 ? frames : 60;
    // (only with a heap chunk header that reads as one in use; otherwise the hunt tries at once, as before)
    uint32_t sz = g_reuse.hdr0 & ~7u;
    g_reuse.free_first = sz >= 16 && sz <= 0x100000;
    if (g_reuse.free_first && !(g_reuse.hdr0 & 1))
        g_reuse.left = 0; // the chunk before is free: the freed block merges into it, no creation starts at the target
}
void debug_reuse_wait_free(int tries)
{
    // (only with a heap chunk header that reads as one)
    uint32_t sz = g_reuse.hdr0 & ~7u;
    if (sz >= 16 && sz <= 0x100000 && (g_reuse.hdr0 & 1))
        g_reuse.wait_free = std::max(1, tries);
}
void debug_reuse_after_free(int burst, bool keep, bool end_merged)
{
    g_reuse.burst_free = std::max(0, burst);
    g_reuse.keep_free = keep;
    // M: a target whose chunk before is free (header bit 0 clear) merges backward when freed: no creation starts at its
    // address (docs/re/creature.md#creature-reuse-merged), the round ends at once
    uint32_t sz = g_reuse.hdr0 & ~7u;
    if (end_merged && sz >= 16 && sz <= 0x100000 && !(g_reuse.hdr0 & 1))
        g_reuse.left = 0;
}
uint32_t handler_dropped() { return g_handler_dropped; }
void* debug_proxy_creature(uint16_t id)
{
    auto it = px_find(id);
    return it == nullptr ? nullptr : it->ref.p;
}
std::string debug_reuse_status()
{
    char b[200];
    snprintf(b, sizeof b, "reuse target=%p left=%d tries=%d unlisted_at=%d hit=%p serial=%u hit_frame=%llu frame=%llu listed_now=%d kept=%u\n",
             (void*)g_reuse.target, g_reuse.left, g_reuse.tries, g_reuse.unlisted_at, g_reuse.hit, g_reuse.hit_serial,
             (unsigned long long)g_reuse.hit_frame, (unsigned long long)H->frame_index(), (int)game::listed(g_reuse.target), (unsigned)g_reuse.kept.size());
    std::string s = b;
    uint8_t* tt = g_reuse.target;
    uint32_t hsz = g_reuse.hdr0 & ~7u;
    uint32_t hdr_now = tt && skg::readable(tt - 4, 4) ? *(uint32_t*)(tt - 4) : 0;
    uint32_t next_now = tt && hsz >= 16 && hsz <= 0x100000 && skg::readable(tt - 8 + hsz + 4, 4) ? *(uint32_t*)(tt - 8 + hsz + 4) : 0;
    int refs_now = tt && skg::readable(tt + 8, 4) ? *(int*)(tt + 8) : -1;
    snprintf(b, sizeof b, "chunk hdr=%08X freed_at=%d freed_frame=%llu hdr_free=%08X next_free=%08X foot_free=%08X"
             " hdr_now=%08X next_now=%08X refs_now=%d\n",
             g_reuse.hdr0, g_reuse.freed_at, (unsigned long long)g_reuse.freed_frame, g_reuse.hdr_free, g_reuse.next_free,
             g_reuse.foot_free, hdr_now, next_now, refs_now);
    s += b;
    if (g_reuse.hit && !g_reuse.herds) { // the new object at the freed address: what the game does with it now
        bool l = game::listed(g_reuse.hit, 0x18EB45Eu) && game::object_serial(g_reuse.hit) == g_reuse.hit_serial;
        net::Vec3 q = l ? game::position(g_reuse.hit) : net::Vec3{};
        uint16_t pid = 0;
        const bool as_proxy = l && px_id_of(g_reuse.hit, pid);
        snprintf(b, sizeof b, "hit alive=%d pos=(%.2f %.2f %.2f) puppet=%d as_proxy=%d\n", (int)l, q.x, q.y, q.z,
                 (int)(l && game::is_puppet(g_reuse.hit)), as_proxy ? (int)pid : -1);
        s += b;
    }
    for (void* m : g_reuse.made) {
        snprintf(b, sizeof b, "  made %p\n", m);
        s += b;
    }
    return s;
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
    void* c = herd ? game::spawn_creature(p, sp, herd) : nullptr;
    if (c && g_console_npc_fix)
        g_console_npcs.insert(c); // BUG-0673: not a saved stray
    return c;
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

// host (tests, BUG-1100): NPC #id's object marked dead (the death flag set, health 0): the state debug_reuse starts from,
// where no hit reaches the NPC (Civ: the host's own blows are not a creature's)
std::string debug_kill(uint16_t id)
{
    void* c = host_npc(id);
    if (!is_host() || !c)
        return "not the host or no such npc";
    game::set_health(c, 0.f);
    *((uint8_t*)c + game::kDeadOff) = 1;
    char b[96];
    snprintf(b, sizeof b, "npc #%u %p species %08X: dead (flag set)", id, c, game::species_instance(c));
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
    for (auto [c, hn] : hn_walk()) {
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
    void* it = hn_obj(id);
    if (!it || !g_seen_now.count(it) || !game::avatar())
        return false;
    if (push > 0) { // BUG-0383 (tests): a hit that throws it, away from the avatar and up (as a charge does)
        net::Vec3 a = game::position(game::avatar()), b = game::position(it);
        float r = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
        net::Vec3 up = r > 1 ? net::Vec3{b.x / r, b.y / r, b.z / r} : net::Vec3{0, 0, 1};
        net::Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
        float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        d = dl > 0.1f ? net::Vec3{d.x / dl, d.y / dl, d.z / dl} : up;
        game::hit_push(it, damage, 0, game::avatar(),
                       {(d.x + up.x * 0.5f) * push, (d.y + up.y * 0.5f) * push, (d.z + up.z * 0.5f) * push});
        return true;
    }
    game::hit(it, damage, 0, game::avatar()); // through the damage filter
    return true;
}

// tests: the message an attack of the avatar on host NPC `id` posts (FUN_00C02B40, the creature's
// attack vt 0xB8: ExecuteAction(0xD335362C, {attacker, target, 0}) to every creature-stage
// listener, CreatureStageEventHandler among them; docs/re/creature.md 4)
bool debug_attack_msg(uint16_t id)
{
    void* it = hn_obj(id);
    void* av = game::avatar();
    static uintptr_t get = H->sym("App::cCreatureModeStrategy::Get"),
                     exec = H->sym("App::cCreatureModeStrategy::ExecuteAction");
    if (!it || !g_seen_now.count(it) || !av || !get || !exec)
        return false;
    void* strat = skg::cdecl_call<void*>(get);
    if (!strat)
        return false;
    void* msg[3] = {av, it, nullptr};
    skg::thiscall<void>(exec, strat, 0xD335362Cu, (void*)msg);
    return true;
}

bool debug_hurt(uint8_t player, uint16_t id, float damage)
{
    void* it = hn_obj(id);
    void* pup = C.puppet_of(player);
    if (!it || !g_seen_now.count(it) || !pup)
        return false;
    game::hit(pup, damage, 0, it);
    return true;
}
void for_each_diag(void (*fn)(const char* key, const net::Vec3& p, void* user), void* user)
{
    if (!is_guest())
        return;
    char k[8];
    const double now = H->time_seconds();
    for (auto [id, pe, px] : px_walk())
        if (px_live(pe) && px.diag_have && px_alive(pe) && !px.killed) {
            snprintf(k, sizeof k, "q%u", (unsigned)id);
            fn(k, px.diag_want, user);
            if (!px.buf.empty()) { // u<id>: M1's raw lead now (const: no late_time, no smoothing; changes no state)
                snprintf(k, sizeof k, "u%u", (unsigned)id);
                fn(k, px.buf.raw_lead(now), user);
            }
        }
}

// BUG-0919: the synced entries (`coop.targets all`, the sync-check) take a creature only when the game's object list has
// it now (game::listed), not on its own destroyed byte alone: a console command runs between frames with the tables of
// the last tick (they are not rebuilt while the simulation stands). Off: the destroyed byte (0.7.12).
static bool g_entries_listed_fix = true;
COOP_FIX("npc.entries_listed", "BUG-0919", g_entries_listed_fix,
         "tests: coop.targets all and the sync-check read only creatures the game's object list has now");
bool entries_listed() { return g_entries_listed_fix; }
std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    game::LiveScope live; // (one walk of the object list for all the questions below)
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto [c, h] : hn_walk()) // updated this frame: an unloaded one may be freed already
            // BUG-0919: and in the game's object list now (a command between frames, the table of the last tick)
            if (g_seen_now.count(c) && (g_entries_listed_fix ? game::listed(c) : game::object_alive(c)) && !game::is_dead(c))
                v.push_back({"n" + std::to_string(h.id), game::position(c), game::orientation(c)});
    } else {
        for (auto [id, pe, px] : px_walk())
            if (px_live(pe) && (g_entries_listed_fix ? game::listed(pe.ref.p) : game::object_alive(pe.ref.p)) && !px.killed)
                v.push_back({"n" + std::to_string(id), game::position(pe.ref.p), game::orientation(pe.ref.p)});
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
// host's own party, cCreatureAnimal +0xB58 & 0x100 / the avatar's herd), object (a UFO, vehicle, turret or
// building, BUG-1077), npc. posse=1: the attacker is
// of the hit game's player's party (BUG-0374 class); `?`: not known here (a guest knows only the host's
// family flag). posse=1 hits that land are written every time, the others rate limited (trace::ev).
// BUG-1077: since BUG-0790 the attacker of a hit may be a vehicle, UFO, turret or building (the combatant's owner
// object); the line still read the creature flags (+0xB58 & 0x100 pack, the herd) of every attacker, in a UFO the
// bytes of whatever lies there: a UFO's hit on the guest's puppet was written "hit by pack ... posse=1" in 3 of 6
// attacker_owner runs (the monitor's posse_damage gate failed; the hit itself was forwarded as it should). Now an
// attacker that is no creature or citizen (noun) is written as `object`, never of the party.
// `coop.fix npc.hit_log_object off`: read as a creature (0.7.13).
static bool g_hit_log_object = true;
COOP_FIX("npc.hit_log_object", "BUG-1077", g_hit_log_object,
         "monitor: a hit by a UFO, vehicle, turret or building is written as an object's hit, not read as a creature's (no false posse=1)");

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
    } else if (uint16_t pid; px_id_of(a, pid)) {
        kind = "proxy";
        const PxE* px = px_find(pid);
        posse = px != nullptr && (px->data.flags & net::NF_FAMILY);
        known = posse;
    } else if (game::is_puppet(a)) {
        kind = "player";
        owner = "-";
        for (auto& p : C.sess->players())
            if (C.puppet_of && C.puppet_of(p.id) == a)
                owner = p.id == 0 ? "host" : std::to_string(p.id);
    } else if (uint32_t noun = g_hit_log_object && game::attacker_owner_fix() ? game::noun_id(a) : 0u;
               noun && noun != 0x18EB45Eu && noun != 0x18EB4B7u) { // BUG-1077: not a creature (UFO, vehicle, ...)
        kind = "object";
        owner = "-";
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
    // BUG-0440 (diagnostics): the host's NPC between two audits (0.25 s): its largest step in the last 3 s and when,
    // and where the proxy and its error were when it went off (a host snap or a proxy that falls behind)
    net::Vec3 last_t{};
    bool have_t = false;
    float jump = 0;
    double jump_at = -1;
    net::Vec3 off_p0{};
    float off_e0 = 0;
};
static std::unordered_map<uint16_t, ProxyAudit> g_audit;
static std::set<uint16_t> g_audit_under, g_audit_stuck; // since the last trace line
static double g_audit_at = 0;

void monitor_tick(double now)
{
    if (!g_enabled || !is_guest() || !npc_stage() || g_host_away || now - g_audit_at < 0.25)
        return;
    g_audit_at = now;
    for (auto [id, pe, px] : px_walk()) {
        auto& a = g_audit[id];
        a.seen = now;
        net::PlayerState s;
        if (!px_live(pe) || px.killed || game::is_destroyed(pe.ref.p) || !px_alive(pe) ||
            game::destroy_pending(pe.ref.p) || game::is_dead(pe.ref.p) || !px.buf.sample(now, s) || (s.flags & net::NF_DEAD)) {
            a.under_since = a.off_since = -1;
            continue;
        }
        net::Vec3 p = game::position(pe.ref.p);
        float r = drive::speed(p), rt = drive::speed(s.pos), g = game::ground_radius(p);
        bool under = g > 0 && r - g < -0.5f;
        // off: from the host's NPC taken to this proxy's distance from the centre (each game its own
        // ground: Tribe drives at its own height, BUG-0329)
        net::Vec3 t = rt > 1 && r > 1 ? net::Vec3{s.pos.x * r / rt, s.pos.y * r / rt, s.pos.z * r / rt} : s.pos;
        bool off = net::dist(p, t) > 3.f;
        if (a.have_t) {
            float st = net::dist(a.last_t, t);
            if (st >= a.jump || now - a.jump_at > 3.0) {
                a.jump = st;
                a.jump_at = now;
            }
        }
        a.last_t = t;
        a.have_t = true;
        if (off && a.off_since < 0) {
            a.off_p0 = p;
            a.off_e0 = net::dist(p, t);
        }
        a.under_since = under ? (a.under_since < 0 ? now : a.under_since) : -1;
        a.off_since = off ? (a.off_since < 0 ? now : a.off_since) : -1;
        // which one and where (players' reports too): the first of each kind per line
        if (under && now - a.under_since >= 1.0 && g_audit_under.insert(id).second && g_audit_under.size() == 1)
            trace::line("npc", "proxy %u (species %08X) %.2f m under this game's ground at (%.1f %.1f %.1f) for %.1f s, the "
                        "host's NPC %.1f m from it (%s%s), made %.1f s ago at %.2f m over the ground", id, px.key.instance,
                        g - r, p.x, p.y, p.z, now - a.under_since, net::dist(p, t),
                        px.kinematic ? "placed on the host's path" : "driven", now < px.manual_until ? ", test walk" : "",
                        now - px.made_at, px.made_agl);
        if (off && now - a.off_since >= 2.0 && g_audit_stuck.count(id) == 0 && g_audit_stuck.empty()) {
            // BUG-0440 / BUG-0386 (diagnostics): the driving's own view (its error to the target it walks to, the place
            // lost / held by the game, teleports) and the nearest other proxy (a herd mate holding it?)
            float near_d = 1e9f;
            uint16_t near_id = 0;
            for (auto [oid, oe, ox] : px_walk())
                if (oid != id && px_live(oe) && !ox.killed && px_alive(oe)) {
                    float d = net::dist(game::position(oe.ref.p), p);
                    if (d < near_d) {
                        near_d = d;
                        near_id = oid;
                    }
                }
            uint32_t cur_anim = 0;
            game::drawn_state(pe.ref.p, nullptr, &cur_anim);
            // BUG-1106 (diagnostics): what stands where the host's NPC is (a building or rock of this game only?) and
            // whether a hold in the air puts the body back
            net::Vec3 hold_at{};
            const bool air_held = game::air_held(pe.ref.p, &hold_at);
            char hold_b[48];
            snprintf(hold_b, sizeof hold_b, air_held ? "%.1f m from it" : "none", net::dist(hold_at, p));
            trace::line("npc", "proxy %u: walk err %.2f m, held %d, place lost %u, teleports %u, walked %d, nearest proxy %u at "
                        "%.1f m; objects within 3 m: %s; pushed back %.1f m/s, off its placing after the call %.2f m, game "
                        "speed %.1f m/s; raised ground (surface over the terrain) under it %.2f m, at its target %.2f m; anim %08X, "
                        "loco mode %d, grounded %d, nest herd %d, host's speed %.1f, walk speed %.1f; objects within 3 m of its target: %s; "
                        "air hold %s (BUG-0440, BUG-1106)", id, px.drv.last_err, (int)px.drv.held, (unsigned)px.drv.place_lost,
                        (unsigned)px.drv.teleports, (int)px.drv.kinematic, near_id, near_d,
                        game::objects_near(p, 3.f, pe.ref.p).c_str(), px.drv.back_ema, px.drv.readback_ema,
                        drive::speed(game::velocity(pe.ref.p)), game::surface_radius(p) - game::ground_radius(p),
                        game::surface_radius(t) - game::ground_radius(t), cur_anim, *(int32_t*)((uint8_t*)pe.ref.p + game::kLocoModeOff),
                        (int)game::grounded(pe.ref.p), game::herd_of(pe.ref.p) && game::herd_nest(game::herd_of(pe.ref.p)) ? 1 : 0,
                        drive::speed(s.vel), px.walk_sp, game::objects_near(t, 3.f, pe.ref.p).c_str(), hold_b);
        }
        if (off && now - a.off_since >= 2.0 && g_audit_stuck.insert(id).second && g_audit_stuck.size() == 1)
            trace::line("npc", "proxy %u (species %08X) %.1f m off the host's NPC for %.1f s at (%.1f %.1f %.1f), host's speed "
                        "%.1f m/s (%s%s); went off at %.1f m, the proxy moved %.1f m since; the host's NPC's largest step "
                        "between two audits (0.25 s) %.1f m, %.1f s ago (BUG-0440)", id, px.key.instance, net::dist(p, t),
                        now - a.off_since, p.x, p.y, p.z, drive::speed(s.vel),
                        px.kinematic ? "placed on the host's path" : px.air ? "in the air" : "driven",
                        now < px.manual_until ? ", test walk" : "", a.off_e0, net::dist(p, a.off_p0), a.jump, now - a.jump_at);
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
