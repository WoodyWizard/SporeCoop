#include "modes.h"
#include "mouse.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "cell_logic.h"
#include "creations.h"
#include "creature_hp.h"
#include "game.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "world_name.h"
#include "net/interp.h"

extern const SkHost* g_host_for_modes;
extern SkMod* g_mod_for_modes;

namespace modes {

void* (*g_species_for_player)(uint8_t player);
bool (*g_model_for_player)(uint8_t player, int kind, uint32_t key[3]);

// ---------------------------------------------------------------- creature / captain
static void* creature_local() { return game::avatar(); }

// BUG-0376: the partner's jumps (its creature off the ground) were mostly not shown: its game sent no airborne
// state (PF_JUMPING was never set), creature.puppet_ground put a target up to kOverGround over this game's ground
// into it and the game put a grounded puppet (+0x137 = 1) placed higher back onto its terrain in the same frame
// (21-22 of 24 hops stayed on the ground). With creature.puppet_air the partner's game sends PF_JUMPING while its
// creature is off the terrain (+0x137 = 0, more than kAirOver over GetHeightAt; on a rock too, where nothing
// changes: a creature on a rock is not grounded either) and the puppet of such a state is placed at the target
// (not onto the ground) and marked not grounded, so the game lets it fall as the partner's own creature does.
// Off: 0.7.4 (no flag sent, the flag ignored).
static bool g_puppet_air_fix = true;
COOP_FIX("creature.puppet_air", "BUG-0376", g_puppet_air_fix, "creature: the partner's jumps are shown (its creature leaves the ground on this screen too)");
constexpr float kAirOver = drive::kAirOver; // m over GetHeightAt: a grounded creature walks 0-3 cm over it
constexpr float kAirGap = 0.3f;   // m: variant 3, farther from the target in the air it is placed there
// BUG-0532: in the middle of a fall the game put the puppet onto its terrain for 1-3 frames whenever it started
// another animation on it (fall -> idle 02481DE5, the walk start 04866D8E; lab 20261104-042712): the puppet is held
// where the partner is through the game's animation starts and updates (game::air_hold). Off: 0.7.5.
static bool g_puppet_air_anim_fix = true;
COOP_FIX("creature.puppet_air_anim", "BUG-0532", g_puppet_air_anim_fix, "creature: the partner's creature in the air does not drop to the ground for a frame when it starts another animation");
int g_puppet_air_variant = 0;     // tests (coop.track air <v>): 1 placed without the not-grounded mark, 2 teleported,
                                  // 3 walked on in the air, 4 its own fall speed kept

static bool own_airborne(void* av)
{
    return g_puppet_air_fix && game::in_creature_stage() && !game::grounded(av) && !game::is_dead(av) &&
           drive::over_ground(game::position(av)) > kAirOver;
}

static void creature_fill(void* av, net::PlayerState& st)
{
    st.flags |= net::PF_IN_WORLD;
    st.pos = game::position(av);
    st.rot = game::orientation(av);
    st.vel = game::velocity(av);
    float sp = drive::speed(st.vel);
    if (sp > 0.3f)
        st.flags |= net::PF_MOVING;
    if (sp > 6.f)
        st.flags |= net::PF_RUNNING;
    if (game::is_dead(av))
        st.flags |= net::PF_DEAD;
    if (game::sneaking(av))
        st.flags |= net::PF_SNEAKING;
    if (own_airborne(av)) {
        st.flags |= net::PF_JUMPING; // BUG-0376
        st.agl = drive::over_ground(st.pos); // BUG-0425: its height over this game's terrain
    }
    st.health = game::health(av);
    st.max_health = game::max_health(av);
    st.species = game::species_instance(av);
}

// BUG-0507: a guest crashed in drive_puppet -> creature_drive -> walk_place -> set_speed on a puppet whose
// memory was another object's (a cModelObject vtable at +0xC0): the game had freed the partner's puppet and
// the Creature adapter had no liveness check (Cell and Space have one), so coop.cpp's puppet_now() and
// drive_puppet() kept driving the raw pointer. With creature.puppet_alive a puppet is alive only while the
// game's object list still holds it as a live creature (noun 0x18EB45E, +0x21 not set) with the serial
// (cGameData::mID +0x24) it had when we made it: a freed puppet, or its memory reused by a new creature,
// is dropped and the partner is shown again with a new puppet. The list walk runs once per frame and
// puppet (a cache by frame index). `coop.fix creature.puppet_alive off`: no check (0.7.3.6).
static bool g_puppet_alive_fix = true;
COOP_FIX("creature.puppet_alive", "BUG-0507", g_puppet_alive_fix, "creature: a partner's creature the game removed is shown again instead of being used after it was freed (crash)");
struct AliveNote {
    uint32_t serial = 0;
    uint64_t frame = ~0ull; // the frame of the last check that found it alive
};
static std::unordered_map<void*, AliveNote> g_puppet_serials;
uint32_t g_puppets_gone_alive = 0; // tests: creature puppets found freed by the game (coop.status)

static void puppet_alive_note(void* p)
{
    if (g_puppet_serials.size() > 64) // (forgotten puppets of old worlds)
        g_puppet_serials.clear();
    g_puppet_serials[p] = AliveNote{game::object_serial(p), ~0ull};
}

static bool creature_alive(void* p)
{
    if (!g_puppet_alive_fix || !p)
        return true;
    uint64_t f = g_host_for_modes ? g_host_for_modes->frame_index() : 0;
    auto it = g_puppet_serials.find(p);
    if (it != g_puppet_serials.end() && it->second.frame == f && f)
        return true; // checked this frame
    bool ok = game::creature_listed(p) && (it == g_puppet_serials.end() || !it->second.serial ||
                                           game::object_serial(p) == it->second.serial);
    if (!ok) {
        if (it != g_puppet_serials.end())
            g_puppet_serials.erase(it);
        g_puppets_gone_alive++;
        game::puppet_unregister(p); // (a new creature at this address is no puppet of ours)
        creature_hp::forget_puppet(p);
        return false;
    }
    if (it != g_puppet_serials.end())
        it->second.frame = f;
    return true;
}

static void* creature_spawn(const net::PlayerState& s, void* av)
{
    void* own = g_species_for_player ? g_species_for_player(s.id) : nullptr;
    // one herd per species, reused (also one a save brought back): a new herd per spawn stayed in
    // the world after the puppet was gone
    void* herd = own ? game::own_herd_for(own) : nullptr;
    if (herd) {
        game::herd_home(herd, s.pos); // (a reused herd's home may be anywhere)
        game::herd_quiet(herd);
    } else if (own)
        herd = game::create_herd(s.pos, own);
    void* p = own && herd ? game::spawn_creature(s.pos, own, herd)
                          : game::spawn_creature(s.pos, game::species_profile(av), game::avatar_herd());
    if (!p)
        return nullptr;
    game::puppet_register(p);
    game::teleport(p, s.pos, s.rot);
    game::set_scale(p, game::scale(p) * 1.15f);
    puppet_alive_note(p);
    return p;
}

// BUG-0360: while the partner walked, its puppet was given a MoveTo again every 0.1 s (which restarts the
// game's walk: the puppet stood or crept) and pulled towards the path with SetPosition (which the
// locomotion ignores), so it moved in steps, trailed 1-4 m and the 4 m snap teleported it ("the
// partner teleports, sometimes it walks"). Now, while the partner moves:
// * the game walks the puppet: the partner's speed (SetDesiredSpeed) and a MoveTo 12 m ahead on its
//   path, given again only when that direction turns (or every 1.5 s): its walk gait and walk
//   animation run as on the partner's screen;
// * game::place puts it on the interpolated path every frame with the partner's facing: a teleport
//   (the locomotion keeps its own position, SetPosition alone is undone) without the teleport mark
//   +0xB5D, which restarts the animation's gait (teleported every frame with it, as npc.kinematic
//   does, the creature slides without a walk gait). An error left from standing is closed at
//   kCatchUp faster than the partner, not in one frame.
// Standing, the driving (drive::step) works as before.
// `coop.fix creature.puppet_walk off`: the driving only (0.7.3.2).
static bool g_puppet_walk_fix = true;
COOP_FIX("creature.puppet_walk", "BUG-0360", g_puppet_walk_fix, "creature: the walking partner's creature walks on its path instead of moving in steps and teleporting");
// BUG-0366 / BUG-0367: the puppet was placed at the partner's own position, taken from the partner's
// game, every frame (0.7.3.3, creature.puppet_walk). Walking downhill a creature placed exactly on the
// ground is a few cm over it after the game's next step (its walk goes along, not down): the game
// takes it as falling (+0x137 = 0) and plays the fall animation 0x0692E6A9 for 1-5 frames, again and
// again ("constantly jumping"; 4-7 times in 40 s on a slope, the partner's own creature never). A
// target under this game's ground (an extrapolated state, a ground that differs) was put there.
// creature.puppet_ground: a target from kUnderGround under to kOverGround over this game's ground
// (cPlanetModel::GetHeightAt) is put kSink into it (drive::on_own_ground, driver.h: shared with the
// other drivings as WalkPlace::ground); the game sets a grounded creature on its ground (0-3 cm up) and
// it stays grounded. A partner higher up (a jump, a rock) is placed where it is.
static bool g_puppet_ground_fix = true;
COOP_FIX("creature.puppet_ground", "BUG-0366", g_puppet_ground_fix, "creature: the partner's creature is put on this game's ground (no fall animation walking downhill, never under the ground)");
// BUG-0513: a guest whose game stalled (minimized, alt-tabbed, paused while it received a world) reported its own
// creature 3-7 m under its ground (its own trace: agl=-6.53, busy=paused), and the host drew the puppet there:
// on_own_ground leaves a target more than kUnderGround under the ground where it is ("the guest is bugged into the
// ground"). With creature.puppet_deep a target 3-15 m under this game's ground (the terrain only: a creature is never
// there legitimately) is put onto it as the proxies are (drive::out_of_ground). Off: 0.7.3.6.
static bool g_puppet_deep_fix = true;
COOP_FIX("creature.puppet_deep", "BUG-0513", g_puppet_deep_fix, "creature: a partner reported deep under the ground (a stalled or minimized game) is drawn on this game's ground");

// BUG-0394: the partner's creature can move without a speed of its own: an animation that carries it
// (B4212A07 after a click, 3-4.6 m/s for a second with the game's velocity 0 and speed state 0 on the
// partner's screen). Its states then say "standing", the puppet stood (drive::step) while its path went
// on, fell 2.9 m behind and was put in place (puppet_walk at 30 fps: 18 % standing windows). With
// creature.puppet_path the puppet walks with the speed of its path (drive::path_velocity) when the state
// reports none.
static bool g_puppet_path_fix = true;
COOP_FIX("creature.puppet_path", "BUG-0394", g_puppet_path_fix, "creature: the partner's creature also walks where the partner moves without a speed (moves carried by an animation)");

// BUG-0463: a player's sneak (the sneak ability: the creature +0xBB1, the noise radius +0xB80 = 0, the model
// eased to alpha 0.2 by the creature's update) ran only on the sneaking player's game. The partner's puppet
// walked upright and opaque, and on the host the NPCs whose AI looks at the puppet (npc.cpp ai_target: the
// puppet as the avatar of NPCTickAI) heard it at the full noise radius. With creature.sneak the puppet is
// put into the same state with the game's own setter (CreatureSetSneaking 0xC1B030, force: no ability
// check) whenever the partner's PF_SNEAKING differs from it; the puppet's energy is kept so the game's
// update does not end the sneak (it does at 0 energy). `coop.fix creature.sneak off`: the old behaviour.
static bool g_sneak_fix = true;
COOP_FIX("creature.sneak", "BUG-0463", g_sneak_fix, "creature: a sneaking partner is shown sneaking (see-through) and the host's NPCs do not hear it");

static void puppet_sneak(void* p, const net::PlayerState& s)
{
    if (!g_sneak_fix || !game::in_creature_stage())
        return;
    bool want = (s.flags & net::PF_SNEAKING) && !(s.flags & net::PF_DEAD);
    if (want && skg::readable(p, 0xE5C) && skg::field<float>(p, 0xE58) < 1.f)
        skg::field<float>(p, 0xE58) = 1.f; // (the update ends a sneak at 0 energy, +0xE58)
    if (game::sneaking(p) != want)
        game::set_sneaking(p, want);
}

// BUG-0499: the partner's own game puts its creature somewhere else at once: the hatchling steps out of
// its egg (5-11 m in one frame, ~10 s after the respawn), the respawn at the nest (100+ m). The states
// were blended across that step (the target slid the whole way in 0.1 s), the puppet walked or was
// nudged after it and stood 6-23 m off for up to a second (play_creature `never more than 6 m apart`,
// one sample, puppet speed 0). With creature.puppet_jump the buffer does not blend across such a step
// (net::SnapshotBuffer::jump_speed, the death flag) and a target that jumps more than kJumpM in one
// frame puts the puppet there (a teleport), as the partner's own game did. Off: blended and walked.
static bool g_puppet_jump_fix = true;
COOP_FIX("creature.puppet_jump", "BUG-0499", g_puppet_jump_fix, "creature: the partner's creature jumps where its own game put it (out of the egg, the respawn) instead of walking there");
constexpr float kJumpM = 3.f;          // m in one frame: no creature walks that fast (180 m/s at 60 fps)
constexpr float kJumpSpeed = 40.f;     // m/s between two states: a jump (sprints and charges stay under ~15)
float puppet_jump_speed() { return g_puppet_jump_fix ? kJumpSpeed : 0.f; }

// BUG-0435: creature.puppet_ground put a target 0-kOverGround over GetHeightAt kSink into it, but GetHeightAt is the plain
// terrain: on the lower part of a nest mound (a grounded partner 0.4 m over it) the puppet was put under the mound's
// surface and stayed there (drawn 0.44 m in the ground; the game does not lift a creature put that far under), and a
// partner crossing 0.5 m made it step 0.4-0.5 m. With creature.puppet_mound a grounded partner (no PF_JUMPING) more
// than kMoundMin over GetHeightAt is put kSink under its own height instead (on the mound, as on the terrain: the game
// sets a creature 5 cm in its ground onto it). Lower down (the plain terrain, 0-3 cm) as before. Off: 0.7.4.
static bool g_puppet_mound_fix = true;
COOP_FIX("creature.puppet_mound", "BUG-0435", g_puppet_mound_fix, "creature: the partner's creature on a nest mound stands on the mound, not in it");
constexpr float kMoundMin = 0.1f; // m over GetHeightAt

// BUG-0408: when the partner stopped, walk_end gave the puppet a MoveTo to where the partner stands and drive::step
// took over without placing it: the game's walk slowed down over ~0.4 s and slid 0.4-0.9 m past the stop (both games'
// own positions, coop.bodyrec: 2 of 14 stops of puppet_stops, lab 20261106-053105; on a slope it rose 0.1 m over the
// ground and played the fall animation). With creature.puppet_stop the puppet is placed where the partner stands for
// kStopHold s after a walk (while it is within kStopNear of it and the partner stands), as it was placed on the path
// while it walked. Off: 0.7.8. (Tried on 2026-11-04 and dropped then: no test failed without it.)
static bool g_puppet_stop_fix = true;
COOP_FIX("creature.puppet_stop", "BUG-0408", g_puppet_stop_fix, "creature: the partner's creature stops where the partner stopped instead of sliding on");
constexpr double kStopHold = 0.5; // s
constexpr float kStopNear = 1.5f; // m
constexpr float kStopSpeed = 4.f;  // m/s: the hold's own approach

// BUG-0683 / BUG-0504 for the partners' creatures: after the evolution editor all players come back on one point of the
// nest; while the brain upgrade caption plays (busy=cinematic,input_lock) the game separates the creatures stacked there
// sideways and pressed a standing puppet 0.55-0.74 m under its terrain (the monitor's ground rule in campaign_tribe,
// rel/0.7.5 and rel/0.7.10). The partner stands, so the drive placed nothing and the puppet stayed in the ground until
// the caption ended. creature.own_ground covers the own creature only. With creature.puppet_unground a puppet whose
// partner stands (under 0.3 m/s, on its ground), that has been more than kPupUnder under this game's terrain
// (GetHeightAt) for kPupUnderFor s, is put onto it where it stands (or at its target's height on a mound next to it).
// `coop.fix creature.puppet_unground off`: 0.7.10.
static bool g_puppet_unground_fix = true;
COOP_FIX("creature.puppet_unground", "BUG-0683", g_puppet_unground_fix,
         "creature: a partner's standing creature pushed into the ground (the nest after the evolution) is put back onto it");
constexpr float kPupUnder = 0.25f;     // m under GetHeightAt
constexpr double kPupUnderFor = 0.5;   // s
uint32_t g_puppets_ungrounded = 0;     // tests (coop.puppet agl)

static void puppet_unground(void* p, const net::PlayerState& s_in, const net::Vec3& target, bool air, drive::State& ds,
                            double now)
{
    if (!game::in_creature_stage() || air || (s_in.flags & net::PF_DEAD) || drive::speed(s_in.vel) >= 0.3f) {
        ds.under_since = -1;
        return;
    }
    const net::Vec3 q = game::position(p);
    const float r = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), g = r > 1.f ? game::ground_radius(q) : 0.f;
    if (!(g > 0 && r - g < -kPupUnder && r - g > -drive::kOutMax)) {
        ds.under_since = -1;
        return;
    }
    if (ds.under_since < 0)
        ds.under_since = now;
    if (now - ds.under_since < kPupUnderFor)
        return;
    static uint32_t said = 0;
    if (said++ < 5 && g_host_for_modes)
        g_host_for_modes->log(g_mod_for_modes, SK_INFO, "creature: a partner's standing puppet stood %.2f m under this game's ground "
                              "for %.1f s (%.1f m from its target)%s", g - r, now - ds.under_since, net::dist(q, target),
                              g_puppet_unground_fix ? ": put onto it (creature.puppet_unground)" : "");
    ds.under_since = -1;
    if (!g_puppet_unground_fix)
        return;
    float want = g;
    const float tr = std::sqrt(target.x * target.x + target.y * target.y + target.z * target.z);
    if (net::dist(q, target) < 2.f && tr > want && tr - g < drive::kOverGround) // (a mound: as high as its partner stands)
        want = tr;
    const float k = want / r;
    game::place(p, {q.x * k, q.y * k, q.z * k}, game::orientation(p));
    ds.ungrounded++;
    g_puppets_ungrounded++;
}

static void creature_drive(void* p, const net::PlayerState& s_in, drive::State& ds, double now)
{
    puppet_sneak(p, s_in);
    ds.drawn = game::position(p);
    net::PlayerState s = s_in;
    // BUG-0376: the partner is off the ground: never onto this game's ground
    const bool air = g_puppet_air_fix && game::in_creature_stage() && (s.flags & net::PF_JUMPING) &&
                     !(s.flags & net::PF_DEAD);
    if (g_puppet_ground_fix && game::in_creature_stage() && !air) {
        const float over = drive::over_ground(s.pos);
        if (g_puppet_mound_fix && over > kMoundMin && over <= drive::kOverGround) {
            // BUG-0435: a grounded partner this high stands on raised ground (a nest mound): kSink under its own height
            float r = std::sqrt(s.pos.x * s.pos.x + s.pos.y * s.pos.y + s.pos.z * s.pos.z), k = (r - drive::kSink) / r;
            s.pos = {s.pos.x * k, s.pos.y * k, s.pos.z * k};
        } else
            s.pos = drive::on_own_ground(s.pos);
        game::mound_hold(p, s.pos); // BUG-0434
    }
    if (g_puppet_deep_fix && game::in_creature_stage())
        s.pos = drive::out_of_ground(s.pos, true); // BUG-0513: 3-15 m under this game's ground
    puppet_unground(p, s_in, s.pos, air, ds, now); // BUG-0683
    const bool jumped = g_puppet_jump_fix && ds.jump_have && net::dist(s.pos, ds.jump_from) > kJumpM;
    ds.jump_from = s.pos;
    ds.jump_have = true;
    if (jumped && net::dist(game::position(p), s.pos) > kJumpM) {
        game::air_hold(p, false);
        drive::walk_end(p, s.pos, ds, now);
        game::teleport(p, s.pos, s.rot);
        ds.teleports++;
        ds.jumps++;
        ds.path = drive::PathTrack{}; // (no path velocity across the jump)
        ds.track = drive::Track{};
        ds.last_step = now;
        ds.last_cmd = now;
        return;
    }
    drive::path_update(ds, s.pos, now);
    // BUG-0408: the partner stopped (its own speed fell to 0.3 m/s or less after a walk): held where it stops for
    // kStopHold s, not walked on with the path's velocity (that walk's goal ahead slid it 0.3-0.9 m past the stop)
    const float own_sp = drive::speed(s.vel);
    if (g_puppet_stop_fix && ds.kinematic && own_sp <= 0.3f && ds.tgt_sp > 0.3f)
        ds.stop_until = now + kStopHold;
    ds.tgt_sp = own_sp;
    const bool holding = g_puppet_stop_fix && now < ds.stop_until && own_sp <= 0.3f;
    if (g_puppet_path_fix && drive::speed(s.vel) <= 0.3f && !holding)
        s.vel = drive::path_velocity(ds);
    float sp = drive::speed(s.vel);
    bool walk = g_puppet_walk_fix && game::in_creature_stage() && sp > 0.3f && net::dist(game::position(p), s.pos) < 8.f;
    if (air) {
        // BUG-0376: in the air the puppet is where the partner is (walked on: its walk goes on after the landing),
        // not grounded, so the game's update lets it fall instead of putting it onto its terrain
        // Not walked in the air: walked (walk_place) it was put onto its terrain in 15-20 frames of a 0.4 s fall
        // (lab, coop.track air 3 = this, 0 the walk)
        // BUG-0425 (drive.sender_agl): at the partner's height over its terrain, over this game's terrain
        if (drive::g_sender_agl_fix)
            s.pos = drive::at_sender_agl(s.pos, s.agl);
        if (walk && g_puppet_air_variant == 3) {
            drive::walk_place(p, s.pos, s.rot, s.vel, ds, now);
            if (net::dist(game::position(p), s.pos) > kAirGap) // (a jump's start: the walk's catch-up is too slow)
                game::place(p, s.pos, s.rot);
        } else {
            drive::walk_end(p, s.pos, ds, now); // (it walks again after the landing)
            if (g_puppet_air_variant == 2)
                game::teleport(p, s.pos, s.rot);
            else
                game::place(p, s.pos, s.rot);
            ds.last_step = now;
            ds.track = drive::Track{};
        }
        if (g_puppet_air_variant != 1)
            game::set_grounded(p, false);
        // its fall speed is the partner's: left to the game it grew frame by frame while the placing held it up, and
        // after 0.3-0.5 s one update took it 1-1.6 m down onto its terrain (landed) while the partner was still up
        if (g_puppet_air_variant != 4)
            *(net::Vec3*)((uint8_t*)p + game::kVelOff) = s.vel;
        game::air_hold(p, g_puppet_air_anim_fix && g_puppet_air_variant != 1, s.pos, s.rot, s.vel);
        ds.airs++;
    } else if (walk) {
        game::air_hold(p, false);
        drive::walk_place(p, s.pos, s.rot, s.vel, ds, now); // (the shared driver, driver.h)
    } else {
        // it stands again: no walk to the goal ahead (a MoveTo to where it stands, not StopMovement: with
        // the placing by SetPosition, tried first, the stop put it back up to 3 m), the driving takes over
        game::air_hold(p, false);
        if (ds.kinematic && g_puppet_stop_fix && ds.stop_until < now)
            ds.stop_until = now + kStopHold; // BUG-0408: the walk ends here
        drive::walk_end(p, s.pos, ds, now);
        if (now < ds.stop_until && net::dist(game::position(p), s.pos) < kStopNear) {
            // BUG-0408: onto where the partner stopped, not where the game's walk slides; at most kStopSpeed m/s (put
            // there at once, the puppet 0.5-1 m behind at the stop made a step)
            net::Vec3 cur = game::position(p), to = s.pos;
            float e = net::dist(cur, to), mx = kStopSpeed * (float)std::min(0.1, std::max(1.0 / 120, now - ds.last_step));
            if (e > mx)
                to = {cur.x + (to.x - cur.x) * mx / e, cur.y + (to.y - cur.y) * mx / e, cur.z + (to.z - cur.z) * mx / e};
            game::place(p, to, s.rot);
            *(net::Vec3*)((uint8_t*)p + game::kVelOff) = net::Vec3{};
            ds.stop_holds++;
            ds.last_step = now;
            ds.track = drive::Track{};
        } else {
            // a partner's creature: lunges and charges move it faster than a puppet can walk, and attack
            // animations undo the nudges (~10-20 % of them in fights): it trailed by 5-8 m after a fight
            // move (play_creature.py 2026-10-23); snap at 4 m instead of 8
            drive::step(p, s.pos, s.rot, s.vel, ds, now, 0, true, 4.f);
        }
    }
    if ((walk && own_sp > 0.3f) || air)
        ds.stop_until = 0;
    if (game::herd_of(p) != game::avatar_herd()) {
        game::herd_quiet(game::herd_of(p)); // (the game may enable a herd again)
        game::herd_home(game::herd_of(p), game::position(p));
    }
    // BUG-0373: the partner's health and max health (creature.partner_hp); off: pinned to full
    if (!creature_hp::drive_puppet(p, s.health, s.max_health)) {
        float mh = game::max_health(p); // puppets mirror the remote health, never die locally
        if (mh > 0)
            game::set_health(p, mh);
    }
}

static void creature_despawn(void* p)
{
    game::air_hold(p, false);
    g_puppet_serials.erase(p);
    creature_hp::forget_puppet(p);
    game::destroy_creature(p);
}

// BUG-0592 / BUG-0504: right after a guest's world load (the share after its "menu" or editor return) both players'
// own standing creatures stood 0.5-0.7 m under the ground for ~20 s (the monitor's ground rule). Observed in the
// host's trace (lab 20261105-015350 long_creature): the guest's avatar comes back where the host's save had the
// host's (the partner's puppet appears 0.2 m from the host's creature, d=0), and the host's standing creature is
// then 0.7 m to the side at the same distance from the planet's centre (agl -0.69 on a slope) until it walks.
// The game separates two overlapping creatures sideways and does not put a standing one back onto its terrain
// (it does once the creature walks). With creature.own_ground the own creature that stands (under 0.3 m/s), is
// alive and has been more than kOwnUnder under this game's terrain (GetHeightAt, the terrain only) for
// kOwnUnderFor s is placed onto it (game::place: no teleport mark). `coop.fix creature.own_ground off`: 0.7.7.
static bool g_own_ground_fix = true;
COOP_FIX("creature.own_ground", "BUG-0592", g_own_ground_fix, "creature: your own creature is never left standing in the ground (after a partner joins or comes back at your spot)");
constexpr float kOwnUnder = 0.25f;     // m under GetHeightAt
constexpr double kOwnUnderFor = 0.5;   // s
static double g_own_under_since = -1;
uint32_t g_own_grounded = 0;           // tests (coop.status): times the own creature was put onto its ground

void own_ground_tick(double now)
{
    void* av = game::in_creature_stage() ? game::avatar() : nullptr;
    if (!av || game::is_dead(av)) {
        g_own_under_since = -1;
        return;
    }
    const net::Vec3 p = game::position(av);
    const float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), g = r > 1.f ? game::ground_radius(p) : 0.f;
    const bool under = g > 0 && r - g < -kOwnUnder && r - g > -drive::kOutMax && drive::speed(game::velocity(av)) < 0.3f;
    if (!under) {
        g_own_under_since = -1;
        return;
    }
    if (g_own_under_since < 0)
        g_own_under_since = now;
    if (now - g_own_under_since < kOwnUnderFor)
        return;
    float nearest = 1e9f;
    for (void* q : game::puppets())
        if (q != av && game::creature_listed(q))
            nearest = std::min(nearest, net::dist(game::position(q), p));
    static uint32_t said = 0;
    if (said++ < 5 && g_host_for_modes)
        g_host_for_modes->log(g_mod_for_modes, SK_INFO,
                              "creature: the own creature stood %.2f m under this game's ground for %.1f s (grounded byte %d, "
                              "nearest puppet %.1f m)%s", g - r, now - g_own_under_since, (int)game::grounded(av), nearest,
                              g_own_ground_fix ? ": put onto it (creature.own_ground)" : "");
    g_own_under_since = -1;
    if (!g_own_ground_fix)
        return;
    const float k = g / r;
    game::place(av, {p.x * k, p.y * k, p.z * k}, game::orientation(av));
    g_own_grounded++;
}

static float creature_tag(void* p) { return game::bounding_radius(p) * 1.3f + 0.3f; }

// adventure identity: cScenarioMode (*ptr) -> mpData (+0x74) -> mpResource (+0x10) -> resource key (+8)
static uint32_t S2(const char* n);
static uint32_t adventure_place()
{
    uintptr_t p = S2("App::cScenarioMode::ptr");
    uint8_t* sm = p ? *(uint8_t**)p : nullptr;
    if (!sm || !skg::readable(sm, 0xD0))
        return 0;
    uint8_t* data = *(uint8_t**)(sm + 0x74);
    uint8_t* res = data && skg::readable(data, 0x14) ? *(uint8_t**)(data + 0x10) : nullptr;
    if (!res || !skg::readable(res, 0x14))
        return 0;
    uint32_t inst = *(uint32_t*)(res + 8), group = *(uint32_t*)(res + 16);
    return (inst ^ (group * 0x9E3779B1u)) | 1u;
}

// ---------------------------------------------------------------- space: UFOs
// cGameDataUFO: cGameData 0x00, cLocomotiveObject 0x34 (pos +0x38, rot +0x44, vel +0x1FC),
// cCombatant 0x508 (hp +0x540, max +0x534, invincible +0x588)
constexpr uintptr_t kUfoLoco = 0x34, kUfoCombat = 0x508;

static uintptr_t S(const char* n) { return g_host_for_modes ? g_host_for_modes->sym(n) : 0; }
static uint32_t S2(const char* n) { return (uint32_t)S(n); }

static uint8_t* space_player_data()
{
    uintptr_t p = S("Simulator::SpacePlayerData::ptr");
    return p ? *(uint8_t**)p : nullptr;
}

static void* space_local()
{
    uintptr_t get = S("Simulator::cSimulatorPlayerUFO::Get");
    void* mgr = get ? skg::cdecl_call<void*>(get) : nullptr;
    return mgr ? skg::field<void*>(mgr, 0x40) : nullptr;
}

static uint32_t space_place()
{
    uint8_t* spd = space_player_data();
    if (!spd)
        return 0;
    int context = *(int*)(spd + 0x10); // 0 planet, 1 solar system, 2 galaxy
    void* planet = *(void**)(spd + 0x04);
    void* star = *(void**)(spd + 0x08);
    if (context == 0 && planet) {
        void* rec = skg::field<void*>(planet, 0x13C);                    // cPlanet::mpPlanetRecord
        return rec ? (skg::field<uint32_t>(rec, 0x184) | 0x40000000u) : 0; // PlanetID
    }
    if (context == 1 && star) {
        void* rec = skg::field<void*>(star, 0x48);                // cStar::mpStarRecord
        return rec ? (skg::field<uint32_t>(rec, 0x70) | 0x80000000u) : 0; // StarID
    }
    return 0; // galaxy view: not shown
}

static void space_fill(void* u, net::PlayerState& st)
{
    st.flags |= net::PF_IN_WORLD;
    auto& p = skg::field<skg::Vector3>(u, kUfoLoco + 0x04);
    auto& q = skg::field<skg::Quaternion>(u, kUfoLoco + 0x10);
    auto& v = skg::field<skg::Vector3>(u, kUfoLoco + 0x1C8);
    st.pos = {p.x, p.y, p.z};
    st.rot = {q.x, q.y, q.z, q.w};
    st.vel = {v.x, v.y, v.z};
    if (drive::speed(st.vel) > 0.3f)
        st.flags |= net::PF_MOVING;
    st.health = skg::field<float>(u, kUfoCombat + 0x38);
    st.max_health = skg::field<float>(u, kUfoCombat + 0x2C);
}

bool ufo_ident_fix = true;
COOP_FIX("drive.nudge_dt", "BUG-0175", drive::g_nudge_dt_fix, "creature, tribe: a mirror's pull to its place is per second, not per frame (low frame rates trailed more)");
COOP_FIX("drive.lead", "BUG-0425", net::g_lead_fix, "creature, tribe, civ: the host's animals, tribe members and vehicles are shown where the host has them now, not 0.2 s behind");
COOP_FIX("drive.lead_smooth", "BUG-0619", net::g_lead_smooth_fix, "tribe, creature: a remote body's lead target goes on smoothly after a late state (Tribe members jumped ~1 m per frame over a jittery network)");
COOP_FIX("drive.lead_keep", "BUG-0651", net::g_lead_keep_fix, "tribe: the members are shown where the host has them now over any link (the lead no longer goes off over 100 ms, a late state is caught up at up to twice the walk)");
COOP_FIX("drive.lead_fall", "BUG-0674", net::g_lead_fall_fix, "every stage: a remote body is moved on past its last state at a falling speed through a latency spike or a loss burst (members 8-9 m off, NPCs teleported, ships past a stop)");
COOP_FIX("drive.puppet_fall", "BUG-0680", net::g_puppet_fall_fix, "creature: the partner's creature stops where the partner stopped in a latency spike instead of walking on past it");
COOP_FIX("drive.lead_throw", "BUG-0675", net::g_lead_throw_fix, "creature, tribe: the copy of a thrown NPC flies with it instead of waiting for each of its states");
COOP_FIX("drive.lead_low", "BUG-0651", net::g_lead_low_fix, "tribe: the member lead stays on over a player network and on a loaded PC (the rule reads the link's own latency, not the mean round trip)");
COOP_FIX("drive.lead_late", "BUG-0650", net::g_lead_late_fix, "tribe: a member's state that arrives late (a latency spike) is brought in over time, not at once (members jumped ~0.9 m per frame at a walk start in a spike)");
COOP_FIX("drive.sender_agl", "BUG-0425", drive::g_sender_agl_fix, "creature, tribe: a partner or animal off the ground is drawn at its height over the sender's ground, over this game's ground");
COOP_FIX("drive.own_height", "BUG-0329", drive::g_own_height_fix, "tribe: a member's or animal's mirror is driven at its own ground height, not the host's absolute one");
COOP_FIX("fleet.type", "BUG-0111", ufo_ident_fix, "space: only our own puppets count as puppets, not any UfoType 6 ship (coop.fleet typefix)");
// our puppets: the model key they got and (BUG-0218) their object serial (cGameData mID)
struct UfoIdent {
    skg::ResourceKey key;
    uint32_t serial;
};
static std::unordered_map<void*, UfoIdent> g_ufo_ident;
// BUG-0218: the identity is the object serial, not the flags. Our puppet whose invincible byte the
// game cleared gets it back; our puppet that a game mission took (owner mission +0x800) is retired
// (held still, never sent or fired, destroyed once the mission lets go). Off: the 0.7.3.0 check
// (flags in the identity, such a ship forgotten and left in the game as an armed NPC).
bool puppet_id_fix = true;
COOP_FIX("fleet.puppet_id", "BUG-0218", puppet_id_fix,
         "space: a puppet is ours by its object serial; a cleared flag is restored, a mission's ship retired");
struct Retired {
    void* u;
    uint32_t serial;
};
static std::vector<Retired> g_retired;
static std::unordered_set<void*> g_fake_mission; // tests (coop.fleet impostor_mission): as if +0x800 were set
uint32_t g_puppet_flag_restored, g_puppet_retired, g_retired_destroyed, g_retired_gone, g_retired_left;
static void* owner_mission(void* u) { return g_fake_mission.count(u) ? u : skg::field<void*>(u, 0x800); }

static void* space_spawn(const net::PlayerState& s, void* local)
{
    uintptr_t spawn = S("Simulator::SpawnUFO"), empid = S("Simulator::GetPlayerEmpireID"),
              getemp = S("Simulator::GetPlayerEmpire");
    if (!spawn || !empid || !getemp)
        return nullptr;
    uint32_t pol = skg::cdecl_call<uint32_t>(empid);
    void* emp = skg::cdecl_call<void*>(getemp);
    if (!emp)
        return nullptr;
    skg::ResourceKey model = skg::field<skg::ResourceKey>(emp, 0xB8); // cEmpire::mUFOKey
    uint32_t own[3];
    if (g_model_for_player && g_model_for_player(s.id, 2, own)) // the player's own ship
        model = {own[0], own[1], own[2]};
    // UfoType 6 = Defender: friendly to its political id (ours)
    void* u = skg::cdecl_call<void*>(spawn, 6, &pol, &model);
    if (!u)
        return nullptr;
    skg::field<uint8_t>(u, kUfoCombat + 0x80) = 1; // mbInvincible
    g_ufo_ident[u] = {skg::field<skg::ResourceKey>(u, 0x780), game::object_serial(u)}; // mDesiredModelKey (BUG-0111), mID (BUG-0218)
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    skg::Quaternion q{s.rot.x, s.rot.y, s.rot.z, s.rot.w};
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x44 / 4, &p, &q); // Teleport
    (void)local;
    return u;
}

// BUG-0378: the pin that makes the game's own update draw a ship at `p`. The UFO controller update
// (0x1062CF0, every frame after our write) moves the ship to mNextPosition + mOffsetFromPosition
// (+0x740); in a star system at its destination it snaps there (SnapToDestination 0xC37FA0:
// position = mDestination + offset, mZoomAltitude = |mDestination|); in the galactic core (0x105E270)
// it keeps the offset at (0, 0, 30 * min(1, r / 400)) m and pulls the next position to its own orbit
// radius and turns it (up to the whole difference in one frame); on a planet (0x1060E00) a ship at its
// destination whose mZoomAltitude (+0x768) differs from |mDestination| leaves it and flies. So: the
// ship is marked pinned for this frame (squadron's hook of the controller update leaves a pinned ship
// where we put it, no velocity), and mNextPosition = mDestination = p minus the ship's own offset with
// the zoom altitude their length, as SnapToDestination leaves it (what the controller would draw if
// it ran). Off: the remote position itself, the controller runs (the partner drawn 20.7-30 m above or
// off its orbit point in the core, BUG-0378).
bool puppet_pin_fix = true;
COOP_FIX("space.puppet_pin", "BUG-0378", puppet_pin_fix,
         "space: a partner's ship stays where the mod puts it (its controller update skipped, pinned minus its offset): drawn where it is (core: 20-30 m off)");
// the ships pinned since the last Present (squadron's controller hook leaves them where we put them)
static std::unordered_set<void*> g_pinned;
void space_pin_frame() { g_pinned.clear(); }
bool space_pinned(void* u) { return !g_pinned.empty() && g_pinned.count(u); }

void space_pin(void* u, const float pp[3], bool fix)
{
    skg::Vector3 p{pp[0], pp[1], pp[2]}, n = p;
    if (fix) {
        g_pinned.insert(u);
        const auto& o = skg::field<skg::Vector3>(u, 0x740); // mOffsetFromPosition
        if (std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z))
            n = {p.x - o.x, p.y - o.y, p.z - o.z};
        skg::field<float>(u, 0x768) = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z); // mZoomAltitude
    }
    skg::field<skg::Vector3>(u, 0x718) = n; // mNextPosition
    skg::field<skg::Vector3>(u, 0x750) = n; // mDestination
}

double g_drive_extrap;
struct DriveNote {
    float p[3];
    float extrap;
};
static std::unordered_map<void*, DriveNote> g_drive_notes;
bool space_drive_note(void* u, float p[3], float& extrap)
{
    auto it = g_drive_notes.find(u);
    if (it == g_drive_notes.end())
        return false;
    memcpy(p, it->second.p, sizeof it->second.p);
    extrap = it->second.extrap;
    return true;
}

// BUG-0697: the partner's ship was drawn at full health on the other game: space_drive wrote the puppet's own max into
// its health every frame, the sent health (space_fill) was never read. On: the partner's share of its max, at least 2 %
// (the puppet never dies by this game's hand). Off: full health (0.7.10).
static bool g_space_hp_fix = true;
COOP_FIX("drive.space_hp", "BUG-0697", g_space_hp_fix, "space: the partner's ship shows its real health on the other game");
static void space_drive(void* u, const net::PlayerState& s, drive::State&, double)
{
    if (g_drive_notes.size() > 64)
        g_drive_notes.clear();
    g_drive_notes[u] = {{s.pos.x, s.pos.y, s.pos.z}, (float)g_drive_extrap};
    // UFOs fly freely: place the puppet exactly where the remote ship is, every frame
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    skg::Quaternion q{s.rot.x, s.rot.y, s.rot.z, s.rot.w};
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x38 / 4, &p); // SetPosition
    skg::vcall<void>((uint8_t*)u + kUfoLoco, 0x3C / 4, &q); // SetOrientation
    skg::field<skg::Vector3>(u, kUfoLoco + 0x1C8) = {s.vel.x, s.vel.y, s.vel.z};
    // the UFO update steers towards mNextPosition/mDestination: pin them to the remote state
    const float pa[3] = {p.x, p.y, p.z};
    space_pin(u, pa, puppet_pin_fix);          // mNextPosition, mDestination (BUG-0378)
    skg::field<skg::Vector3>(u, 0x724) = {s.vel.x, s.vel.y, s.vel.z}; // mNextVelocity
    skg::field<skg::Quaternion>(u, 0x730) = q; // mNextOrientation
    skg::field<uint8_t>(u, 0x74C) = 1;         // mbAtDestination
    float mh = skg::field<float>(u, kUfoCombat + 0x2C);
    if (mh > 0)
        skg::field<float>(u, kUfoCombat + 0x38) = space_puppet_health(mh, s.health, s.max_health, g_space_hp_fix);
}

float space_puppet_health(float puppet_max, float health, float max_health, bool fix)
{
    if (!fix || !(max_health > 0) || !std::isfinite(health))
        return puppet_max;
    // the partner's share of its own max, on this game's puppet's max (another design: another max); never 0 here:
    // the partner's game decides its own ship's fate (disabled, rescued, destroyed), this game must not
    float f = health / max_health;
    f = f < 0.02f ? 0.02f : f > 1.f ? 1.f : f;
    return puppet_max * f;
}

std::string space_puppet_hp(void* u)
{
    char b[96];
    if (!u || !space_puppet_ok(u))
        return "no puppet";
    snprintf(b, sizeof b, "hp %.1f max %.1f", skg::field<float>(u, kUfoCombat + 0x38), skg::field<float>(u, kUfoCombat + 0x2C));
    return b;
}

// DestroyInstance works for any cGameData; a puppet the game freed already (leaving a planet or a
// system destroys every NPC UFO, the puppets too) is only forgotten
static void space_despawn(void* u)
{
    g_ufo_ident.erase(u);
    if (game::ufo_listed(u)) {
        game::destroy_creature(u);
    } else {
        game::g_ufos_freed++;
        game::puppet_unregister(u);
    }
}

// the game removes NPC UFOs on its own: driving a freed puppet crashed the host (play_space.py,
// 2026-10-23: AV in space_drive, vtable overwritten)
// (a listed pointer may be a new UFO at a reused address: it must still be a Defender, type +0x714)
// BUG-0111: and it must be the UFO space_spawn made: a game Defender (an empire's or a colony's)
// allocated at the freed puppet's address is not ours (model key, invincible, no owner mission)
bool space_puppet_ok(void* u)
{
    if (!u || !game::ufo_listed(u) || !game::object_alive(u) || skg::field<int>(u, 0x714) != 6)
        return false;
    if (!ufo_ident_fix)
        return true;
    auto it = g_ufo_ident.find(u);
    if (it == g_ufo_ident.end())
        return false;
    const skg::ResourceKey& k = skg::field<skg::ResourceKey>(u, 0x780);
    if (puppet_id_fix) // BUG-0218: the same object (serial), not taken by a game mission
        return game::object_serial(u) == it->second.serial && !memcmp(&k, &it->second.key, sizeof k) &&
               !owner_mission(u);
    return !memcmp(&k, &it->second.key, sizeof k) && skg::field<uint8_t>(u, kUfoCombat + 0x80) == 1 &&
           !skg::field<void*>(u, 0x800);
}

static bool space_alive(void* u)
{
    if (space_puppet_ok(u)) {
        if (puppet_id_fix && ufo_ident_fix && skg::field<uint8_t>(u, kUfoCombat + 0x80) != 1) {
            // our own puppet lost its invincibility (the game? only the test command is known to do
            // it): give it back, count it, log the first one
            skg::field<uint8_t>(u, kUfoCombat + 0x80) = 1;
            if (!g_puppet_flag_restored++)
                g_host_for_modes->log(g_mod_for_modes, SK_WARN, "space: a puppet's invincible byte was cleared: restored (mode %08X)",
                       game::mode_id());
        }
        return true;
    }
    auto it = ufo_ident_fix ? g_ufo_ident.find(u) : g_ufo_ident.end();
    if (it != g_ufo_ident.end()) {
        uint32_t serial = it->second.serial;
        g_ufo_ident.erase(it);
        if (puppet_id_fix && game::ufo_listed(u) && game::object_alive(u) && game::object_serial(u) == serial &&
            owner_mission(u)) {
            // our ship, held by a game mission: never freed while the mission points to it; retired
            g_retired.push_back({u, serial});
            g_puppet_retired++;
            g_host_for_modes->log(g_mod_for_modes, SK_INFO, "space: a game mission took a puppet: retired until the mission lets go");
        }
        game::puppet_unregister(u); // (a different serial: another object at the address, forgotten, never destroyed)
    }
    game::g_ufos_freed++;
    return false;
}

bool space_retired(void* u)
{
    for (auto& r : g_retired)
        if (r.u == u)
            return true;
    return false;
}

void space_retired_tick()
{
    for (size_t i = 0; i < g_retired.size();) {
        void* u = g_retired[i].u;
        if (!game::ufo_listed(u) || !game::object_alive(u) || game::object_serial(u) != g_retired[i].serial) {
            g_retired_gone++; // the game freed it (another object may be at the address now)
            g_fake_mission.erase(u);
            g_retired.erase(g_retired.begin() + i);
            continue;
        }
        if (!owner_mission(u)) { // the mission let go: ours to destroy (the deferred path)
            g_retired_destroyed++;
            g_retired.erase(g_retired.begin() + i);
            game::destroy_creature(u);
            continue;
        }
        // held still where it is (its own controller would fly it)
        skg::Vector3 p = skg::field<skg::Vector3>(u, kUfoLoco + 0x04);
        skg::field<skg::Vector3>(u, 0x718) = p;
        skg::field<skg::Vector3>(u, 0x724) = {0, 0, 0};
        skg::field<skg::Vector3>(u, kUfoLoco + 0x1C8) = {0, 0, 0};
        skg::field<skg::Vector3>(u, 0x750) = p;
        i++;
    }
}

void space_retired_clear(const char* why)
{
    if (!g_retired.empty()) {
        g_retired_left += (uint32_t)g_retired.size();
        if (g_host_for_modes)
            g_host_for_modes->log(g_mod_for_modes, SK_WARN, "space: %u retired puppet(s) still held by a mission at %s: left to the game",
                   (unsigned)g_retired.size(), why);
    }
    g_retired.clear();
    g_fake_mission.clear();
}

std::string space_ident_test(void* u, const std::string& what, bool on)
{
    auto it = g_ufo_ident.find(u);
    if (what == "impostor_id") { // another object at the address: the recorded serial no longer matches
        if (it == g_ufo_ident.end())
            return "not a puppet";
        it->second.serial ^= 0x80000000u;
        return "puppet serial changed";
    }
    if (what == "impostor_mission") { // a game mission holds the ship (+0x800 seen as set), or lets go
        if (on)
            g_fake_mission.insert(u);
        else
            g_fake_mission.erase(u);
        return on ? "mission set" : "mission cleared";
    }
    char b[160];
    snprintf(b, sizeof b, "flag_restored=%u retired=%u (now %u) retired_destroyed=%u retired_gone=%u retired_left=%u",
             g_puppet_flag_restored, g_puppet_retired, (unsigned)g_retired.size(), g_retired_destroyed, g_retired_gone,
             g_retired_left);
    return b;
}

static float space_tag(void*) { return 4.f; }

// ---------------------------------------------------------------- cell stage
// cCellGame (*0x16B4BE4): mCells pool at +0x1C (mpData +0, object size +0x14), mpCellQuery +0x40FC,
// mAvatarCellIndex +0x411C. cCellObjectData (0x398 bytes): index +0, mIsIdle +4, mTargetPosition +8,
// mTransform +0x48 (offset +0x4C, scale +0x58, rotation 3x3 +0x5C), mCellResource +0x108,
// mIsInvulnerable +0x111, mChaseCellIndex +0x22C, mHealthPoints +0x244, mScale +0x358.
uint8_t* cell_game()
{
    uintptr_t p = S("Simulator::Cell::cCellGame::_ptr");
    return p ? *(uint8_t**)p : nullptr;
}

// cObjectPoolIndex = slot (low 16 bits) | generation (high 16); the object's first field holds
// its own full index while it lives (cObjectPool::GetIfNotDeleted 0xB72290)
// particle arrays of the cell soft bodies: positions at +4, velocities at +0x4654 = 4 + 1500*12
constexpr uint32_t kCellParticles = 1500;

bool cell_at_fix = true;
COOP_FIX_UNSAFE("pool.cellat", "BUG-0083", cell_at_fix, "cell: modes::cell_at refuses the slot past the pool's used range");

// BUG-0271: on the way Cell -> Creature the game frees the cell pool's memory (3.7 MB, its own
// mapping) while mode_id() still reads Cell, and later points +0x1C at junk (docs/re/cell.md 13).
// Our console commands and the keepalive's network update also run between frames, from the game's
// message pump inside that teardown (the host's coop.status read a freed puppet there). So a cell
// is read only from pool memory that is still mapped: VirtualQuery over the whole block, checked
// once per frame scope of ours (no game code runs inside it) and on every call outside one. The
// local cell and the puppets go through the same pool check (cell_usable) before cell_fill reads
// them; coop.cpp's local_now() / puppet_now() do the same for every mode before a command or a
// network callback reads the entities our last frame found. `coop.fix core.stale_local off`: the old
// reads (tests).
bool stale_fix = true;
COOP_FIX("core.stale_local", "BUG-0271", stale_fix, "core: no cell, local entity or puppet read between frames once the game freed or changed the stage (Cell -> Creature)");
static int g_scope_depth;
static uint8_t* g_scope_data;   // the block found mapped in this scope
static uint32_t g_scope_bytes;
static CellGoneStats g_gone;
static uint8_t* g_gone_logged; // the freed block already logged

void cell_scope(bool enter)
{
    if (enter) {
        if (g_scope_depth++ == 0)
            g_scope_data = nullptr; // the game ran since the last scope: check again
    } else if (g_scope_depth > 0) {
        g_scope_depth--;
    }
}

// [p, p + n) is committed, readable memory (one or more regions)
static bool mapped(const uint8_t* p, size_t n)
{
    const uint8_t* end = p + n;
    while (p < end) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        p = (const uint8_t*)mi.BaseAddress + mi.RegionSize;
    }
    return true;
}

// the cell game's pool block is the game's live memory (with the fix off: always yes)
static bool pool_mapped(uint8_t* data, uint32_t bytes)
{
    if (!stale_fix)
        return true;
    if (g_scope_depth > 0 && data == g_scope_data && bytes == g_scope_bytes)
        return true;
    g_gone.checks++;
    if (g_scope_depth == 0)
        g_gone.outside++;
    bool ok = mapped(data, bytes);
    if (ok && g_scope_depth > 0) {
        g_scope_data = data;
        g_scope_bytes = bytes;
    }
    if (!ok) {
        g_gone.pool_gone++;
        if (data != g_gone_logged) {
            g_gone_logged = data;
            g_host_for_modes->log(g_mod_for_modes, SK_WARN,
                                  "cell: the pool block %p (%u bytes) is no longer mapped (mode 0x%08X, %s a frame): no cell read from it",
                                  data, bytes, game::mode_id(), g_scope_depth ? "inside" : "outside");
        }
    }
    return ok;
}

// once per frame (diagnostic, whatever the switch): in the Cell stage, is the pool block still
// mapped? Logs when it goes away while the mode still reads Cell and how long that lasts.
void cell_probe_tick()
{
    static bool gone = false;
    static double gone_at = 0;
    uint8_t* g = game::mode_id() == kCell ? cell_game() : nullptr;
    if (!g) {
        if (gone)
            g_host_for_modes->log(g_mod_for_modes, SK_INFO, "cell: the mode left Cell %.3f s after the pool block was freed (%u frames)",
                                  g_host_for_modes->time_seconds() - gone_at, g_gone.frames_gone);
        gone = false;
        return;
    }
    uint8_t* data = *(uint8_t**)(g + 0x1C);
    uint32_t cap = *(uint32_t*)(g + 0x1C + 0x18);
    bool ok = data && mapped(data, (cap && cap <= 4096 ? cap : 4096) * 0x398);
    if (!ok) {
        g_gone.frames_gone++;
        if (!gone) {
            gone = true;
            gone_at = g_host_for_modes->time_seconds();
            g_gone.windows++;
            g_host_for_modes->log(g_mod_for_modes, SK_WARN, "cell: frame with the pool block %p unmapped while the mode reads Cell",
                                  data);
        }
    } else if (gone) {
        gone = false; // a new block (the pool was rebuilt)
    }
}

uint8_t* cell_at(uint32_t idx)
{
    uint8_t* g = cell_game();
    uint32_t slot = idx & 0xFFFF;
    if (!g || !idx || slot >= 4096)
        return nullptr;
    uint8_t* data = *(uint8_t**)(g + 0x1C);
    uint32_t size = *(uint32_t*)(g + 0x1C + 0x14);
    uint32_t cap = *(uint32_t*)(g + 0x1C + 0x18);
    // live slots are [0, highWater) inside [0, capacity) (docs/re/cell.md 0, BUG-0083)
    if (!data || size != 0x398 ||
        !cell_logic::slot_in_pool(slot, *(uint32_t*)(g + 0x1C + 0x10), cap, cell_at_fix))
        return nullptr;
    if (!pool_mapped(data, (cap && cap <= 4096 ? cap : 4096) * size))
        return nullptr;
    uint8_t* c = data + (size_t)slot * size;
    return *(uint32_t*)c == idx ? c : nullptr;
}

// A cell with a soft body (GFX +0x248 set, particle count +0x250 > 1) gets +0x4C and +0x90 back
// from its particles every cell step (0xE7C080 -> 0xE553B0): particle i = *(u32*)(c+0x254+4i),
// position at parts+4+12i, velocity at parts+0x4654+12i (parts = *(G+0x4108)). Moving the cell
// moves the particle positions; `still` also stops them (a teleport). Writing the velocities
// instead (as this did until 2026-10-22) made the cell glide there and overshoot by metres.
void cell_move(uint8_t* c, const net::Vec3& p, bool still)
{
    auto* pos = (float*)(c + 0x4C);
    float d[3] = {p.x - pos[0], p.y - pos[1], p.z - pos[2]};
    int n = *(int*)(c + 0x250);
    uint8_t* g = cell_game();
    uint8_t* parts = g ? *(uint8_t**)(g + 0x4108) : nullptr;
    if (*(uint32_t*)(c + 0x248) && parts && n > 1 && n <= 64) // +0x254..+0x353: at most 64 indices
        for (int i = 0; i < n; i++) {
            uint32_t k = *(uint32_t*)(c + 0x254 + 4 * i);
            if (k >= kCellParticles)
                continue;
            auto* q = (float*)(parts + 4 + 12 * (size_t)k);
            q[0] += d[0]; q[1] += d[1]; q[2] += d[2];
            if (still) {
                auto* v = (float*)(parts + 0x4654 + 12 * (size_t)k);
                v[0] = v[1] = v[2] = 0;
            }
        }
    pos[0] = p.x; pos[1] = p.y; pos[2] = p.z;
    auto* prev = (float*)(c + 0x84);
    prev[0] = p.x; prev[1] = p.y; prev[2] = p.z;
    *(uint16_t*)(c + 0x48) |= 4; // offset dirty
    (*(uint16_t*)(c + 0x4A))++;
}

static std::unordered_map<void*, uint32_t> g_cell_ids; // puppet -> its pool index

// The cell step moves and turns every cell (a puppet turns towards where it swims); poses written
// at the end of the frame were overridden before the next drawing. The last pose of each puppet is
// applied again right after the step (SimUpdate), so what is drawn is the remote pose.
struct CellPose {
    net::Vec3 pos;
    float rot[9];
};
static std::unordered_map<void*, CellPose> g_cell_pose;
typedef void(__cdecl* SimUpdate_t)(float);
static SimUpdate_t o_SimUpdate;
static uint8_t* cell_at_checked(void* cp);

// BUG-0308: a rejoined guest found a corrupt heap when the Cell pool was freed for the world reload
// (lab 20261030-104149 campaign_short#3). Between the posted quit and the mode change (~0.8 s) the
// game tears the stage down while mode_id() still reads Cell, and our code went on writing the pool:
// the guest's state handler created and moved mirrors, the guest / host ticks and the puppet poses
// wrote cells. With cell.leave_guard none of that runs while game::leaving_stage() (not shown to be
// the corruption: a guard, like BUG-0292's).
static bool g_fix_leave_guard = true;
COOP_FIX("cell.leave_guard", "BUG-0308", g_fix_leave_guard, "cell: no mirror creation and no pool write of ours while the stage is being left (quit posted for a world load or the menu)");
bool cell_leaving()
{
    return g_fix_leave_guard && game::leaving_stage();
}

static void __cdecl h_SimUpdate(float dt)
{
    o_SimUpdate(dt);
    if (cell_leaving())
        return;
    for (auto it = g_cell_pose.begin(); it != g_cell_pose.end();) {
        uint8_t* c = cell_at_checked(it->first);
        if (!c) {
            it = g_cell_pose.erase(it);
            continue;
        }
        cell_move(c, it->second.pos);
        memcpy(c + 0x5C, it->second.rot, sizeof it->second.rot);
        *(uint16_t*)(c + 0x48) |= 2; // rotation dirty
        ++it;
    }
}

static void* cell_local()
{
    uint8_t* g = cell_game();
    return g ? cell_at(*(uint32_t*)(g + 0x411C)) : nullptr;
}

void mat_to_quat(const float* m, net::Quat& q) // row-major 3x3; unit length, identity for a zero matrix (BUG-0160)
{
    cell_logic::Quat r = cell_logic::mat_to_quat(m);
    q.x = r.x, q.y = r.y, q.z = r.z, q.w = r.w;
}

void quat_to_mat(const net::Quat& q, float* m)
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z,
          wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    m[0] = 1 - 2 * (yy + zz); m[1] = 2 * (xy - wz);     m[2] = 2 * (xz + wy);
    m[3] = 2 * (xy + wz);     m[4] = 1 - 2 * (xx + zz); m[5] = 2 * (yz - wx);
    m[6] = 2 * (xz - wy);     m[7] = 2 * (yz + wx);     m[8] = 1 - 2 * (xx + yy);
}

// BUG-0271: the local cell or a puppet is still a live cell of a mapped pool (the caller may hold it
// from an earlier frame: g_local, a puppet, read by a command between frames)
bool cell_usable(void* cp)
{
    if (!cp)
        return false;
    if (!stale_fix)
        return true;
    return cp == cell_local() || cell_at_checked(cp);
}

const CellGoneStats& cell_gone_stats() { return g_gone; }

static void cell_fill(void* cp, net::PlayerState& st)
{
    uint8_t* c = (uint8_t*)cp;
    if (!cell_usable(cp)) { // BUG-0271: nothing is read, the state says "not in the world"
        g_gone.refused++;
        return;
    }
    auto* pos = (float*)(c + 0x4C);
    st.flags |= net::PF_IN_WORLD;
    if (uint8_t* g0 = cell_game()) // BUG-0519: the world scale this pose is in
        st.scale = *(float*)(g0 + 0x514C);
    st.pos = {pos[0], pos[1], pos[2]};
    mat_to_quat((float*)(c + 0x5C), st.rot);
    auto* tgt = (float*)(c + 0x08);
    net::Vec3 d{tgt[0] - pos[0], tgt[1] - pos[1], tgt[2] - pos[2]};
    if (!*(bool*)(c + 4) && drive::speed(d) > 0.05f)
        st.flags |= net::PF_MOVING;
    auto* vel = (float*)(c + 0x90);
    st.vel = {vel[0], vel[1], vel[2]};
    st.health = (float)*(int*)(c + 0x244);
    st.max_health = 6;
    if (st.health <= 0)
        st.flags |= net::PF_DEAD;
    st.species = *(uint32_t*)(c + 0x58); // transform scale (cell size) travels in the species slot
}

// CreateCellObject asks GetModelKeyForCellResource for the model; the avatar's resource
// ("PlayerCreature") maps to the species picked in the creator, not to the evolved cell the
// player has now. While spawning a puppet the hook answers with the avatar's current model.
typedef skg::ResourceKey*(__cdecl* GetModelKey_t)(skg::ResourceKey*, void*);
static GetModelKey_t o_GetModelKey;
static bool g_force_key;
static skg::ResourceKey g_key;

static skg::ResourceKey* __cdecl h_GetModelKey(skg::ResourceKey* ret, void* res)
{
    if (g_force_key) {
        *ret = g_key;
        return ret;
    }
    return o_GetModelKey(ret, res);
}

// BUG-0342: a puppet is the cell at its address AND its pool index. Our own cell's level rebuild
// (RebuildLevel 0xE7FD00, the own cell applied at a load) frees every cell and makes the avatar
// first, often at the address the partner's puppet had: by the address alone the new avatar was
// "a puppet", so h_Pair dropped every collision pair of it (no eating, biting or being hit for
// minutes: the players' "ghost"). `coop.fix cell.puppet_index off`: the address only (0.7.3.1).
static bool g_fix_puppet_index = true;
COOP_FIX("cell.puppet_index", "BUG-0342", g_fix_puppet_index, "cell: a puppet is recognised by its address and its pool index (a new avatar at an old puppet's address was taken for the puppet: the ghost)");
bool cell_is_puppet(void* c)
{
    auto it = g_cell_ids.find(c);
    if (it == g_cell_ids.end())
        return false;
    return !g_fix_puppet_index || cell_at(it->second) == (uint8_t*)c;
}

// The Cell stage was left (an editor, the Creature stage, a load): the pool the puppets lived in is
// rebuilt or gone. Stale entries could match a new cell at the same address and index in the next
// pool (h_SimUpdate would then pose an NPC as the partner); coop.cpp forgets the puppets themselves.
void cell_forget()
{
    g_cell_ids.clear();
    g_cell_pose.clear();
}

void cell_destroy(uint32_t idx)
{
    // DestroyCell(idx, spawnLoot, float, int): the game's own removal (setting hp 0 does not remove)
    if (uintptr_t a = S("Simulator::Cell::DestroyCell"))
        if (cell_at(idx))
            skg::cdecl_call<void>(a, idx, 0, 0.f, 0);
}

static uint8_t* cell_at_checked(void* cp)
{
    auto it = g_cell_ids.find(cp);
    return it != g_cell_ids.end() && cell_at(it->second) == cp ? (uint8_t*)cp : nullptr;
}

static void ensure_model_hook()
{
    if (!o_SimUpdate) {
        uintptr_t a = S("Simulator::Cell::SimUpdate");
        if (a)
            g_host_for_modes->hook_create(g_mod_for_modes, (void*)a, (void*)h_SimUpdate, (void**)&o_SimUpdate);
    }
    if (!o_GetModelKey) {
        uintptr_t a = S("Simulator::Cell::GetModelKeyForCellResource");
        if (a)
            g_host_for_modes->hook_create(g_mod_for_modes, (void*)a, (void*)h_GetModelKey, (void**)&o_GetModelKey);
    }
}

// BUG-0348: the cell broadphase (cCellGame+0x4104, docs/re/cell.md#cell-sap-pending) queues every
// new cell's endpoints in its pending arrays (AddObject 0xBBB290, no bound check; 2 x capacity
// entries each) and empties them only in its sweep, inside SimUpdate. A game that does not run
// SimUpdate (the pause menu) while we create cells (a guest's mirrors churning) wrote past the
// arrays: heap corruption (found when the Cell pools are freed) and a broken endpoint order (the
// ~1024 objects left open of BUG-0337). We create no cell while the pending entries are near full.
// Old (`coop.fix cell.sap_pending off`): no bound.
static bool g_fix_sap_pending = true;
COOP_FIX_UNSAFE("cell.sap_pending", "BUG-0348", g_fix_sap_pending, "cell: no cell of ours is created while the broadphase's pending list (emptied only by the running simulation) is near full (it overflowed into the heap while a guest sat in the pause menu)");
static uint32_t g_sap_held;

uint32_t cell_sap_held() { return g_sap_held; }

bool cell_sap_room()
{
    uint8_t* g = cell_game();
    uint8_t* sap = g ? *(uint8_t**)(g + 0x4104) : nullptr;
    if (!g_fix_sap_pending || !sap)
        return true;
    int cap = *(int*)(sap + 4), pend = *(int*)(sap + 0x2C);
    // 2 x cap entries per axis; each new cell takes 2; keep room for the game's own (a level change
    // or loot made by one frame of the game before its sweep)
    if (cap > 0 && pend + 2 + 512 <= 2 * cap)
        return true;
    g_sap_held++;
    return false;
}

// BUG-0455 (the class of BUG-0004): a cell made with a forced model key builds that key's cell model data
// on first use (GetCellModelData 0xE679E0 -> 0xE83E30 -> Editors::cEditor::LoadCreatureData); a key whose
// creation this game does not have yet loads nothing and the game reads the missing data at 0xE83EC8 (read
// 0x9C). A guest mirrored the host's cells of the host's new creation right after the host left its Cell
// editor, before the creation's records arrived. Every forced key is checked first: a cell of an unknown
// creation is not made (cellsync tries again with the next state round, the creation's bundle follows),
// a puppet falls back to the own avatar's model. Off (`coop.fix cell.unknown_model off`): 0.7.3.4.
static bool g_fix_unknown_model = true;
COOP_FIX("cell.unknown_model", "BUG-0455", g_fix_unknown_model,
         "cell: no cell is made with the model of a creation this game does not have (yet): the game read its missing data at 0xE83EC8");
static uint32_t g_unknown_models, g_unknown_last;

static bool model_known(const uint32_t key[3])
{
    if (!g_fix_unknown_model || !key[0] || creations::key_known(creations::Key{key[0], key[1], key[2]}))
        return true;
    if (key[0] != g_unknown_last || g_unknown_models % 200 == 0) // (once per key, then rarely)
        g_host_for_modes->log(g_mod_for_modes, SK_INFO,
                              "cell: model %08X/%08X/%08X is not in this game (a creation not received yet): no cell with it "
                              "now (BUG-0455, %u)", key[0], key[1], key[2], g_unknown_models + 1);
    g_unknown_last = key[0];
    g_unknown_models++;
    return false;
}

uint32_t cell_unknown_models() { return g_unknown_models; }

uint32_t cell_create(const net::Vec3& pos, void* res_ref, int level, const uint32_t key[3], const net::Quat* rot)
{
    if (!model_known(key))
        return 0;
    ensure_model_hook();
    uint8_t* g = cell_game();
    uintptr_t create = S("Simulator::Cell::CreateCellObject");
    if (!g || !res_ref || !create || !cell_sap_room())
        return 0;
    skg::Vector3 p{pos.x, pos.y, pos.z};
    skg::Quaternion q{0, 0, 0, 1};
    if (rot)
        q = {rot->x, rot->y, rot->z, rot->w};
    g_key = {key[0], key[1], key[2]};
    g_force_key = o_GetModelKey && key[0] != 0;
    // (query, pos, elevation, ref, level, sizeFactor, cellSize 0 = auto, applySize, orientation, collision group -1)
    uint32_t idx = skg::cdecl_call<uint32_t>(create, *(void**)(g + 0x40FC), &p, 0.f, res_ref, level, 1.f, 0.f, 1,
                                             (void*)&q, -1);
    g_force_key = false;
    return cell_at(idx) ? idx : 0;
}

static void* cell_spawn(const net::PlayerState& s, void* local)
{
    ensure_model_hook();
    uint8_t* g = cell_game();
    uint8_t* me = (uint8_t*)local;
    uintptr_t create = S("Simulator::Cell::CreateCellObject");
    if (!g || !me || !create || !cell_sap_room())
        return nullptr;
    skg::Vector3 p{s.pos.x, s.pos.y, s.pos.z};
    void* query = *(void**)(g + 0x40FC);
    void* res = *(void**)(me + 0x108);
    // the avatar's own mScale is -1 ("player"); other cells with -1 are not drawn: use the level the
    // player is at (GetCurrentAdvectInfo reports the current CellStageScale; cell_drive corrects it)
    int scale = 1;
    if (uintptr_t adv = S("Simulator::Cell::GetCurrentAdvectInfo")) {
        int lvl = -1;
        float a, b, c;
        skg::cdecl_call<uint32_t>(adv, &lvl, &a, &b, &c);
        if (lvl >= 0 && lvl < 8)
            scale = lvl;
    }
    g_key = skg::field<skg::ResourceKey>(me, 0xFC);
    uint32_t own[3];
    if (g_model_for_player && g_model_for_player(s.id, 1, own) && model_known(own)) // the player's own cell
        g_key = {own[0], own[1], own[2]};
    uint32_t forced[3] = {g_key.instance, g_key.type, g_key.group};
    if (!model_known(forced)) // (the own avatar's model: known; else the game's own choice for the resource)
        g_key.instance = 0;
    g_force_key = o_GetModelKey && g_key.instance != 0;
    uint32_t idx = skg::cdecl_call<uint32_t>(create, query, &p, 0.f, res, scale, 1.f, 1.f, 1, (void*)nullptr, 0);
    g_force_key = false;
    uint8_t* c = cell_at(idx);
    if (!c)
        return nullptr;
    if (g_fix_puppet_index) // BUG-0342: entries of puppets that are gone (a level rebuild freed them)
        for (auto it = g_cell_ids.begin(); it != g_cell_ids.end();)
            it = cell_at(it->second) == (uint8_t*)it->first ? std::next(it) : g_cell_ids.erase(it);
    g_cell_ids[c] = idx;
    *(bool*)(c + 0x111) = true; // invulnerable: nobody eats a friend
    *(uint8_t*)(c + 0x354) = 1; // we own the lifetime: not removed outside the local active area
    return c;
}

COOP_FIX("cell.puppet_scale", "BUG-0519", net::SnapshotBuffer::scale_lerp, "cell: the partner's cell is drawn in this game's world scale while a level change rescales either world");
static void cell_drive(void* cp, const net::PlayerState& s_in, drive::State&, double)
{
    uint8_t* c = (uint8_t*)cp;
    // BUG-0519: the partner's pose is in its own world scale; a level change rescales each world over
    // ~3.5 s at its own moment (docs/re/cell.md#cell-world-scale): draw it in ours (as the mirrors,
    // cell.world_scale)
    float k = 1.f;
    if (net::SnapshotBuffer::scale_lerp && s_in.scale > 0)
        if (uint8_t* g0 = cell_game()) {
            float own = *(float*)(g0 + 0x514C);
            if (own > 0 && std::isfinite(own))
                k = s_in.scale / own;
        }
    net::PlayerState sc;
    if (k != 1.f) {
        sc = s_in;
        sc.pos = {s_in.pos.x * k, s_in.pos.y * k, s_in.pos.z * k};
        sc.vel = {s_in.vel.x * k, s_in.vel.y * k, s_in.vel.z * k};
        float sz;
        memcpy(&sz, &s_in.species, 4);
        if (sz > 0.01f && sz < 1000.f) {
            sz *= k;
            memcpy(&sc.species, &sz, 4);
        }
    }
    const net::PlayerState& s = k != 1.f ? sc : s_in;
    // exactly where the remote cell is (interpolated), every frame; the target ahead along its
    // velocity keeps the swimming animation going between our writes
    cell_move(c, s.pos);
    bool moving = (s.flags & net::PF_MOVING) != 0;
    auto* tgt = (float*)(c + 0x08);
    tgt[0] = s.pos.x + (moving ? s.vel.x * 0.3f : 0.f);
    tgt[1] = s.pos.y + (moving ? s.vel.y * 0.3f : 0.f);
    tgt[2] = s.pos.z;
    *(bool*)(c + 4) = !moving;
    // facing: the rotation itself, not only the target the game turns towards; applied again
    // after the next cell step (h_SimUpdate), before the frame is drawn
    quat_to_mat(s.rot, (float*)(c + 0x5C));
    *(uint16_t*)(c + 0x48) |= 2; // rotation dirty
    CellPose& pose = g_cell_pose[cp];
    pose.pos = s.pos;
    memcpy(pose.rot, c + 0x5C, sizeof pose.rot);
    skg::field<skg::Quaternion>(c, 0x14) = {s.rot.x, s.rot.y, s.rot.z, s.rot.w}; // mTargetOrientation
    *(uint8_t*)(c + 0x354) = 1;
    float size;
    memcpy(&size, &s.species, 4);
    if (size > 0.01f && size < 1000.f) {
        *(float*)(c + 0x58) = size;
        *(float*)(c + 0xB4) = size; // mTargetSize
    }
    // Keep the puppet at the players' level (+0x358). It used to keep the level of the moment it
    // was created: after a few level-ups the host's NPCs judged the partner several levels smaller
    // than them (scale difference 0, IsPrey 0xE68910 says "too small to hunt") and hunted only the
    // host's own cell (docs/re/cell.md 9, docs/known-issues.md history).
    // The game reads the avatar's -1 as the level of the food progress (GetScaleDifference
    // 0xE57400 -> 0xE4EE50); GetCurrentAdvectInfo, which the spawn uses, does not always agree (8
    // for 7 at food 600). Ask the game: the scale difference avatar -> puppet is 2 at the same level.
    uint8_t* g = cell_game();
    uint8_t* av = g ? cell_at(*(uint32_t*)(g + 0x411C)) : nullptr;
    if (uintptr_t diff = S("Simulator::Cell::GetScaleDifference"); av && av != c && diff) {
        int d = skg::cdecl_call<int>(diff, av, c); // clamped to 0..4: converges within a few frames
        int lvl = *(int*)(c + 0x358) + 2 - d;
        if (d != 2 && lvl >= 0 && lvl < 32)
            *(int*)(c + 0x358) = lvl;
    }
    *(int*)(c + 0x22C) = -1; // never chase anything
    *(int*)(c + 0x220) = -1; // nor flee
    *(int*)(c + 0x244) = 6;
    *(bool*)(c + 0x111) = true;
}

static bool cell_alive(void* cp)
{
    auto it = g_cell_ids.find(cp);
    bool ok = it != g_cell_ids.end() && cell_at(it->second) == cp;
    if (!ok && it != g_cell_ids.end())
        g_cell_ids.erase(it);
    return ok;
}

static void cell_despawn(void* cp)
{
    if (!cell_alive(cp))
        return;
    uint32_t idx = g_cell_ids[cp];
    g_cell_ids.erase(cp);
    g_cell_pose.erase(cp);
    *(bool*)((uint8_t*)cp + 0x111) = false;
    cell_destroy(idx); // hp 0 or opacity 0 would only hide it (docs/re/cell.md §5)
}

static float cell_tag(void* cp) { return *(float*)((uint8_t*)cp + 0x58) * 1.2f + 0.5f; }

// ---------------------------------------------------------------- tribe / civilization
// RTS stages have no single avatar: a player is shown as a pointer - the point of the planet
// under their mouse - with the name tag. Both play the same (host) world, so the tribe or the
// cities themselves already exist on every machine.
struct Marker {
    net::Vec3 pos;
    net::Vec3 still{};     // BUG-0380: where it last stood still (a move of 0.5 units starts a new one)
    double moved_at = -1e9; // ... and when it left that point (H->time_seconds)
};
static void marker_moved(Marker* m)
{
    if (net::dist(m->pos, m->still) > 0.5f) {
        m->still = m->pos;
        m->moved_at = g_host_for_modes ? g_host_for_modes->time_seconds() : 0;
    }
}
static Marker g_local_marker;
static bool g_marker_pinned; // tests (coop.marker): the pointer stays where the test put it
void test_pin_marker(bool pin, const net::Vec3& p)
{
    g_marker_pinned = pin;
    if (pin)
        g_local_marker.pos = p;
}

static void* noun_manager()
{
    uintptr_t get = S("Simulator::cGameNounManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

// a point on the planet surface of the player's community (sets the ray-cast sphere radius)
static bool rts_anchor(net::Vec3& p)
{
    void* nm = noun_manager();
    if (!nm)
        return false;
    void* tribe = skg::field<void*>(nm, 0x70);
    if (tribe && skg::readable(tribe, 0x130)) {
        auto v = skg::field<skg::Vector3>(tribe, 0x124);
        p = {v.x, v.y, v.z};
        return drive::speed(p) > 1;
    }
    uintptr_t f = S("Simulator::cGameNounManager::GetPlayerCivilization");
    void* civ = f ? skg::thiscall<void*>(f, nm) : nullptr;
    if (civ && skg::readable(civ, 0xA8)) {
        void** b = skg::field<void**>(civ, 0x9C); // mCities (vector begin)
        void** e = skg::field<void**>(civ, 0xA0);
        if (b && e > b && skg::readable(b, 4) && *b && skg::readable(*b, 0x130)) {
            auto v = skg::field<skg::Vector3>(*b, 0x124);
            p = {v.x, v.y, v.z};
            return drive::speed(p) > 1;
        }
    }
    return false;
}

// tests (BUG-0223): frames in which Tribe's / Civ's local entity is null, as when the anchor city
// cannot be read for a moment
static int g_test_null_frames;
static bool g_test_null_now;
void test_null_local(int frames) { g_test_null_frames = frames; }
void test_tick()
{
    g_test_null_now = g_test_null_frames > 0;
    if (g_test_null_now)
        g_test_null_frames--;
}

static void* rts_local()
{
    if (g_test_null_now)
        return nullptr;
    net::Vec3 a;
    return rts_anchor(a) ? &g_local_marker : nullptr;
}

// planet point under the mouse (ray from the game camera against the planet sphere)
static bool mouse_on_planet(net::Vec3& out)
{
    net::Vec3 anchor;
    if (!rts_anchor(anchor))
        return false;
    HWND w = (HWND)g_host_for_modes->game_window();
    POINT pt;
    // BUG-0713 (class BUG-0429): where the pointer last moved over the game window (mouse.h), not the screen cursor
    // in another program (input.window off: GetCursorPos)
    int mx = 0, my = 0;
    if (mouse::g_window_input) {
        mouse::install(w);
        if (!w || !mouse::last_move(mx, my))
            return false;
        pt = {mx, my};
    } else if (!w || !GetCursorPos(&pt) || !ScreenToClient(w, &pt))
        return false;
    uintptr_t mmget = S("App::IGameModeManager::Get"), ray = S("App::cViewer::GetCameraToPoint");
    void* mm = mmget ? skg::cdecl_call<void*>(mmget) : nullptr;
    void* viewer = mm ? skg::vcall<void*>(mm, 0x58 / 4) : nullptr;
    if (!viewer || !ray)
        return false;
    skg::Vector3 o{}, d{};
    if (!skg::thiscall<bool>(ray, viewer, (float)pt.x, (float)pt.y, &o, &d))
        return false;
    float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dl < 1e-6f)
        return false;
    d.x /= dl; d.y /= dl; d.z /= dl;
    float R = drive::speed(anchor);
    float b = o.x * d.x + o.y * d.y + o.z * d.z;
    float c = o.x * o.x + o.y * o.y + o.z * o.z - R * R;
    float disc = b * b - c;
    if (disc < 0)
        return false;
    float t = -b - std::sqrt(disc);
    if (t < 0)
        t = -b + std::sqrt(disc);
    if (t < 0)
        return false;
    out = {o.x + d.x * t, o.y + d.y * t, o.z + d.z * t};
    return true;
}

static void rts_fill(void* m, net::PlayerState& st)
{
    Marker* mk = (Marker*)m;
    net::Vec3 p;
    if (mk == &g_local_marker && !g_marker_pinned && mouse_on_planet(p))
        mk->pos = p;
    marker_moved(mk);
    st.flags |= net::PF_IN_WORLD;
    st.pos = mk->pos;
}

static void* rts_spawn(const net::PlayerState& s, void*)
{
    auto* m = new Marker;
    m->pos = s.pos;
    return m;
}

static void rts_drive(void* m, const net::PlayerState& s, drive::State&, double)
{
    ((Marker*)m)->pos = s.pos;
    marker_moved((Marker*)m);
}
static void rts_despawn(void* m) { delete (Marker*)m; }
static float rts_tag(void*) { return 0.5f; }

bool rts_marker_moving(const Adapter* a, void* e, double within)
{
    if (!a || !e || a->fill != rts_fill || !g_host_for_modes)
        return false;
    return g_host_for_modes->time_seconds() - ((Marker*)e)->moved_at < within;
}

double rts_marker_still(const Adapter* a, void* e)
{
    if (!a || !e || a->fill != rts_fill || !g_host_for_modes)
        return -1;
    return g_host_for_modes->time_seconds() - ((Marker*)e)->moved_at;
}

static const Adapter kAdapters[] = {
    {"Tribe", kTribe, nullptr, false, rts_local, rts_fill, rts_spawn, rts_drive, rts_despawn, rts_tag},
    {"Civilization", kCiv, nullptr, false, rts_local, rts_fill, rts_spawn, rts_drive, rts_despawn, rts_tag},
    {"Cell", kCell, nullptr, false, cell_local, cell_fill, cell_spawn, cell_drive, cell_despawn, cell_tag, cell_alive, true},
    {"Creature", kCreature, nullptr, false, creature_local, creature_fill, creature_spawn, creature_drive, creature_despawn, creature_tag, creature_alive},
    // adventures are not saves: every player starts the same adventure, puppets meet inside it
    {"Adventure", kScenario, adventure_place, true, creature_local, creature_fill, creature_spawn, creature_drive, creature_despawn, creature_tag, creature_alive},
    {"Space", kSpace, space_place, false, space_local, space_fill, space_spawn, space_drive, space_despawn, space_tag, space_alive},
};

net::Vec3 entity_pos(const Adapter* a, void* e)
{
    net::PlayerState st;
    if (a && e)
        a->fill(e, st);
    return st.pos;
}

const Adapter* for_mode(uint32_t mode)
{
    for (auto& a : kAdapters)
        if (a.mode == mode)
            return &a;
    return nullptr;
}

const char* mode_name(uint32_t m)
{
    switch (m) {
    case kCell: return "Cell";
    case kCreature: return "Creature";
    case kTribe: return "Tribe";
    case kCiv: return "Civilization";
    case kSpace: return "Space";
    case kEdit: case kEditor: return "Editor";
    case kLoad: return "Loading";
    case kScenario: return "Adventure";
    case kMenu: return "Galaxy menu";
    case 0: return "-";
    default: return "other";
    }
}

// BUG-0258: after the live Creature -> Tribe change the game's name reads "snapshot.spo" until the
// next save; the planet before it is still the one played (world_name.h)
static bool g_snapshot_name_fix = true;
COOP_FIX("world.snapshot_name", "BUG-0258", g_snapshot_name_fix, "world: a host whose save name reads snapshot.spo after the Tribe intro keeps its planet's place");
static std::mutex g_world_name_mx;
static world_name::State g_world_name;

std::string raw_world_name()
{
    uintptr_t a = g_host_for_modes ? g_host_for_modes->sym("SporeCoop::CurrentGameName") : 0;
    if (!a)
        return {};
    const char16_t* b = skg::field<const char16_t*>((void*)a, 0);
    const char16_t* e = skg::field<const char16_t*>((void*)a, 4);
    if (!b || e < b || e - b > 260 || !skg::readable(b, (e - b) * 2))
        return {};
    // UTF-8, the way world.cpp names the files it sends: a player may type any name for a new planet
    // ("Земля"), and the save file has that name. Non-ASCII letters used to become '_', so the host did
    // not find the file of the planet it played and sent another one (docs/07-modes.md 7.2).
    int n = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)b, (int)(e - b), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n : 0, 0);
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)b, (int)(e - b), out.data(), n, nullptr, nullptr);
    return out;
}

std::string current_world_name()
{
    std::string raw = raw_world_name();
    std::lock_guard<std::mutex> lk(g_world_name_mx);
    return world_name::effective(g_world_name, raw, g_snapshot_name_fix);
}

// once per frame: the memory follows the game; every change of the game's own name is logged
// (BUG-0258: when "snapshot.spo" comes and goes)
void world_name_tick()
{
    static std::string seen;
    std::string raw = raw_world_name();
    std::string eff;
    {
        std::lock_guard<std::mutex> lk(g_world_name_mx);
        eff = world_name::effective(g_world_name, raw, g_snapshot_name_fix);
    }
    if (raw != seen) {
        if (g_host_for_modes && g_mod_for_modes)
            g_host_for_modes->log(g_mod_for_modes, SK_INFO, "world: the game's save name is now '%s' (mode %08X; the place uses '%s')",
                                  raw.c_str(), game::mode_id(), eff.c_str());
        seen = raw;
    }
}

uint32_t current_place(const Adapter* a)
{
    if (a && a->place)
        return a->place();
    return current_world();
}

uint32_t current_world()
{
    std::string n = current_world_name();
    return n.empty() ? 0 : net::crc32_update(0, (const uint8_t*)n.data(), n.size());
}

} // namespace modes

const SkHost* g_host_for_modes;
SkMod* g_mod_for_modes;
