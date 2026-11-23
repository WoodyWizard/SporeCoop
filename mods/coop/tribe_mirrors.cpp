#include "tribe.h"
#include "tribe_internal.h"
#include "tribe_logic.h"
#include "call_guard.h"
#include "lifecycle.h"
#include "mirror_registry.h"
#include "player_life.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "civ.h"
#include "coop_ui.h"
#include "creations.h"
#include "driver.h"
#include "effects.h"
#include "game.h"
#include "modes.h"
#include "npc.h"
#include "savestrip.h"
#include "space_ext.h"
#include "stage_epoch.h"
#include "world.h"
#include "owner_claims.h"
#include "autosave_glue.h"
#include "store.h"
#include "wsync.h"
#include "wire/tribe_anim.h"
#include "net/held_cmds.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/snapshot_rx.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "settings.h"
#include "fields.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {

// ---------------- guest: mirrors ----------------
// The game-over caption ("Alas! Your population has fallen below the minimum...") comes when no
// member of the player tribe has health left. A member added while it is up has no species fields
// (+0xB20, +0xB54) and the OK (bounce back) faulted at 0xC0DAE3 on it, then the game froze
// (BUG-0172). No member is added to the player tribe then. `coop.tribe fix gameover 0`: the old way.
bool g_fix_gameover = true;
COOP_FIX("tribe.gameover", "BUG-0172", g_fix_gameover, "tribe: no member is added during the game-over caption");
uint32_t g_spawns_blocked;
// BUG-0288: a guest's player tribe holds mirrors only. When every mirror had played its death (the
// host's tribe knocked out) the guard above refused every new mirror for good, also after the host
// bounced back and the dead mirrors were removed (an empty tribe has no member with health): the
// guests missed the host's whole new tribe (4-player campaign, "t: 16 missing on the guest"). The
// game-over caption comes on the host only (the guest's game shows none, its input lock stays 0):
// on a guest the guard holds only while its own game has a caption or cinematic up (the input lock).
uint32_t g_mirror_out_passed;
bool player_tribe_out(void* t)
{
    if (!g_fix_gameover || !t || t != player_tribe())
        return false;
    if (is_guest() && coop_ui::input_lock_now() <= 0) {
        bool none = true;
        for (void* c : members(t))
            if (game::health(c) > 0)
                none = false;
        if (none)
            g_mirror_out_passed++;
        return false;
    }
    for (void* c : members(t))
        if (game::health(c) > 0)
            return false;
    return true;
}

// BUG-0685: why the last spawn_member returned null (the mirror log names it)
static const char* g_spawn_fail = "";
// BUG-0857: "a baby stays a baby on the guest". A guest's mirror of a baby member grows up by the game's own timer of
// that mirror, from its spawn: a mirror spawned (or bound) after the host's baby was born (a guest joining or rejoining
// mid-growth) grew up that much later, and the host's age in the snapshot was used only at a spawn. Now a bound mirror
// still a baby (+0xB34 0) whose host member is an adult grows up at once through the citizen's own GrowUp
// (vt 0x78 0xC248C0: age 1, the adult's scale, model and AI flags, the HUD's selectable member).
// `coop.fix tribe.mirror_grow off`: the mirror grows by its own timer only.
static bool g_mirror_grow_fix = true;
COOP_FIX("tribe.mirror_grow", "BUG-0857", g_mirror_grow_fix, "tribe: a guest's mirror of a baby member grows up when the host's member does, also when it was made later than the host's baby");
uint32_t g_mirrors_grown;

void* spawn_member(void* t, uint8_t age, uint8_t owner)
{
    if (player_tribe_out(t)) {
        g_spawns_blocked++;
        g_spawn_fail = "the player tribe is out (tribe.gameover guard: no member with health, input lock up)";
        return nullptr;
    }
    birth_begin(t, owner); // BUG-1299: born in its owner's line species
    // SpawnMember (vt 0x98) returns null at the population cap: then create + add directly
    void* c = vcall<void*>(t, 0x98 / 4, (int)age);
    if (!c) {
        c = thiscall<void*>(a_CreateCitizen, t, (int)age);
        if (c)
            thiscall<void>(a_AddMember, t, c, 1);
        else
            g_spawn_fail = "the game made no citizen (SpawnMember and CreateCitizen null)";
    }
    birth_end({c});
    return c;
}

// BUG-0667: a detached body's tribe back into its +0x1014 for its teardown, when that tribe is still one of the world's
void restore_body_tribe(void* c, void* t)
{
    if (!t || F<void*>(c, kOwnerTribeOff))
        return;
    auto tribes = all_tribes();
    if (std::find(tribes.begin(), tribes.end(), t) == tribes.end())
        return;
    F<void*>(c, kOwnerTribeOff) = t;
    g_teardown_restored++;
}

// BUG-0844: c is a live citizen and a live tribe of the world has it in its member vector: that tribe (else null)
void* tribe_holding(void* c)
{
    if (!game::listed(c, 0x18EB4B7u)) // (kCitizenNoun)
        return nullptr;
    for (void* t : all_tribes()) {
        void** b = F<void**>(t, kMembersOff);
        void** e = F<void**>(t, kMembersOff + 4);
        for (void** it = b; it && it < e && it - b < 1024; ++it)
            if (*it == c)
                return t;
    }
    return nullptr;
}
void remove_member(void* c)
{
    game::air_hold(c, false); // BUG-0946: no hold on a citizen the game frees later
    if (g_member_lifetime_fix && !g_detached.count(c)) {
        void* lt = tribe_holding(c);
        if (!lt) {
            g_guest.remove_refused++;
            MIRROR_LOG("tribe: %p is no member of a live tribe (freed, or another object there): forgotten, not removed (BUG-0844)", c);
            return;
        }
        vcall<void>(lt, 0x9C / 4, c, 1); // RemoveMember(c, destroy) on the tribe that holds it
        return;
    }
    void* t = F<void*>(c, kOwnerTribeOff);
    if (t)
        vcall<void>(t, 0x9C / 4, c, 1); // RemoveMember(c, destroy)
    else if (auto it = g_detached.find(c); it != g_detached.end()) { // BUG-0620: a body out of its tribe: the object
        void* bt = it->second;
        g_detached.erase(it);
        restore_body_tribe(c, bt); // BUG-0667
        game::destroy_instance(c);
    }
}
void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), nt = r.u8();
    if (!r.ok())
        return;
    g_guest.snap.policy.keep_order = g_cycle_order_fix; // C4 (BUG-1143; BUG-0612's switch, read with every chunk)
    if (!g_guest.snap.begin(cycle, chunk, chunks))
        return; // a bad header or a late chunk of an older cycle
    auto cut = [] { g_guest.snap.end(false); }; // a chunk read only in part counts for nothing
    std::vector<std::pair<net::Vec3, bool>> htribes;
    for (int i = 0; i < nt && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        htribes.push_back({p, r.u8() != 0});
    }
    if (!r.ok())
        return cut();
    // host tribe index -> local tribe (the player tribe directly, others by position)
    auto locals = all_tribes();
    void* pt = player_tribe();
    g_guest.local_tribe_of.assign(htribes.size(), nullptr);
    for (size_t i = 0; i < htribes.size(); i++) {
        if (htribes[i].second) {
            g_guest.local_tribe_of[i] = pt;
            continue;
        }
        float best = 40.f;
        for (void* t : locals) {
            float d = net::dist(tribe_pos(t), htribes[i].first);
            if (t != pt && d < best) {
                best = d;
                g_guest.local_tribe_of[i] = t;
            }
        }
    }
    double now = H->time_seconds();
    uint8_t n = r.u8();
    // BUG-0365: the host's clock after the entries (none from a host before 0.7.3.4)
    bool timed = false;
    uint32_t host_ms = 0;
    // BUG-0381: the tribe table's species / archetype after the trailer
    size_t extra = r.ok() && r.left() >= n * kEntry + kTrailerSize ? r.left() - n * kEntry - kTrailerSize : 0;
    bool info = extra >= 5 && extra == 5 + (size_t)r.cur()[n * kEntry + kTrailerSize + 4] * kTribeInfo;
    if (r.ok() && (r.left() == n * kEntry + kTrailerSize || info)) {
        const uint8_t* t = r.cur() + n * kEntry;
        uint32_t magic;
        memcpy(&magic, t, 4);
        memcpy(&host_ms, t + 4, 4);
        timed = magic == kStateTrailer;
    }
    if (info && timed)
        make_npc_tribes(r.cur() + n * kEntry + kTrailerSize, extra, htribes, locals);
    g_mirror_timed += timed;
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        GMember m;
        m.tribe = r.u8(); m.owner = r.u8(); m.age = r.u8();
        m.body = (m.age & kBodyBit) != 0; // BUG-0620
        m.age &= (uint8_t)~kBodyBit;
        m.pos.x = r.f32(); m.pos.y = r.f32(); m.pos.z = r.f32();
        m.vel.x = r.f32(); m.vel.y = r.f32(); m.vel.z = r.f32();
        m.rot.x = r.f32(); m.rot.y = r.f32(); m.rot.z = r.f32(); m.rot.w = r.f32();
        m.hp = r.f32();
        if (!r.ok())
            return cut();
        g_guest.snap.saw(id);
        GEntry& e = g_guest.mirrors.at(id);
        GMember& g = e.data;
        void* c = e.ref.p;
        g.tribe = m.tribe; g.owner = m.owner; g.age = m.age; g.body = m.body;
        g.pos = m.pos; g.vel = game::finite_vel(m.vel); g.rot = m.rot; g.hp = m.hp; g.seen = now; // BUG-0894
        if (g_test_lift != 0.f) { // tests (BUG-0329): the host's member as if its ground were higher
            float r = std::sqrt(g.pos.x * g.pos.x + g.pos.y * g.pos.y + g.pos.z * g.pos.z);
            if (r > 1.f) {
                float k = (r + g_test_lift) / r;
                g.pos = {g.pos.x * k, g.pos.y * k, g.pos.z * k};
            }
        }
        if (timed) {
            net::PlayerState ps{};
            ps.time_ms = host_ms;
            ps.flags = net::PF_IN_WORLD;
            ps.pos = g.pos;
            ps.rot = g.rot;
            ps.vel = g.vel;
            g.buf.delay = g_test_delay;
            // BUG-0974: the lead does not read `delay`: a planted delay holds the lead target back by itself
            g.buf.test_lag = g_test_delay != kMirrorDelay && g_test_lag_fix ? g_test_delay : 0;
            g.buf.lead = true; // BUG-0425 (drive.lead)
            g.buf.smooth_max = kMirrorSmoothMax; // BUG-0619 (drive.lead_smooth)
            g.buf.push(ps, now);
        }
        if (c && !mirror_ok(e)) { // (BUG-0844: also another object at its address)
            MIRROR_LOG("tribe: mirror #%u: its object is gone (not removed by the mod, BUG-0685)", id);
            if (g_member_lifetime_fix && game::object_alive(c))
                g_guest.stale++;
            g_guest.mirrors.detach(id);
            game::air_hold(c, false); // (BUG-0946)
            g.air = false;
            c = nullptr;
        }
        if (c && tribe_logic::grow_up(g_mirror_grow_fix, m.age, m.body, F<uint8_t>(c, kAgeOff), game::is_dead(c))) {
            vcall<void>(c, 0x78 / 4); // BUG-0857: the citizen's own GrowUp (vt 0x78, plain ret)
            g_mirrors_grown++;
            MIRROR_LOG("tribe: mirror #%u grew up with the host's member (BUG-0857)", id);
        }
        if (!tribe_logic::make_mirror(c != nullptr, m.body, g_join_body_fix)) // (no new mirror for a body before BUG-0665)
            continue;
        auto not_made = [&g, id](const char* why) {
            if (g.not_made != why)
                MIRROR_LOG("tribe: mirror #%u is not made: %s (BUG-0685)", id, why);
            g.not_made = why;
        };
        void* t = m.tribe < g_guest.local_tribe_of.size() ? g_guest.local_tribe_of[m.tribe] : nullptr;
        if (!t) {
            not_made("its tribe has no tribe of this game");
            continue;
        }
        // bind to the nearest unbound local member of that tribe, else spawn one
        {
            auto ms = members(t);
            std::vector<tribe_logic::Candidate> cand;
            for (void* lc : ms) {
                net::Vec3 q = game::position(lc);
                cand.push_back({q.x, q.y, q.z, g_guest.mirrors.bound(lc)});
            }
            int k = tribe_logic::pick_bind(cand, m.pos.x, m.pos.y, m.pos.z); // (D3)
            if (k >= 0)
                c = ms[k];
        }
        bool made = false;
        if (!c) {
            g_spawn_fail = "";
            c = spawn_member(t, m.age, m.owner);
            if (!c) {
                not_made(g_spawn_fail);
                continue;
            }
            note_ours(g_ours_c, c); // BUG-0495
            // BUG-0444: the host's position (g.pos: with the tests' lift), onto this game's ground where that lies
            // higher (BUG-0329)
            game::teleport(c, drive::out_of_ground(g.pos, true), m.rot);
            g_guest.spawned++;
            made = true;
        }
        if (m.body) { // BUG-0665: guest_tick lays it down as a body
            g_join_bodies++;
            MIRROR_LOG("tribe: mirror #%u is made for a body of the host's (BUG-0665)", id);
        }
        if (g.not_made)
            MIRROR_LOG("tribe: mirror #%u is made again (BUG-0685)", id);
        g.not_made = nullptr;
        g_guest.mirrors.bind(id, mirror::Ref{c, game::object_serial(c), kMemberNoun}, made); // (BUG-0844: its serial)
    }
    if (!g_guest.snap.end(r.ok())) // C4: true once, on the chunk that completes the cycle (a cut one counts for nothing)
        return;
    // end of a full snapshot: members the host no longer has go away, and so do local members
    // the host does not know (the guest's tribes are only mirrors)
    auto remove = [](GEntry& e, bool alive) {
        if (!alive) // (BUG-0844: only forgotten)
            return;
        MIRROR_LOG("tribe: mirror #%u goes: the host's snapshot has it no more%s (BUG-0685)", e.id,
                   g_detached.count(e.ref.p) ? " (a body)" : "");
        remove_member(e.ref.p);
        g_guest.removed++;
    };
    // C1 + C4: every mirror the cycle did not name goes (the registry's sweep; a dead one is only forgotten)
    g_guest.snap.finish(g_guest.mirrors, [](GEntry&, bool) { return mirror::Unseen::Remove; }, remove);
    for (void* t : g_guest.local_tribe_of) // the orphans: members of this game the host has not
        if (t)
            for (void* lc : members(t))
                if (!g_guest.mirrors.bound(lc)) {
                    MIRROR_LOG("tribe: a member of this game the host has not goes (leader %d, BUG-0685)", (int)is_leader(lc));
                    remove_member(lc);
                    g_guest.removed++;
                }
    unmapped_tick(locals, pt, now); // BUG-0483
}

// BUG-0173: a member knocked out on the host (health 0, the game's dead flag +0xB5E, behaviour
// node 0x05CA7FA7, it stays in the tribe until it respawns: the chieftain, NPC tribes) stood on the
// guest with its last health. The mirror's behaviour tree does not run on a guest, so the mod plays
// the death itself (Die + the death animation, game::play_death). When the host's member has health
// again, the dead mirror is replaced by a fresh one (the next snapshot binds or spawns it).
// `coop.tribe fix ko 0`: the code before (tests).
bool g_fix_ko = true;
COOP_FIX("tribe.ko", "BUG-0173", g_fix_ko, "tribe: a guest's mirror of a member without health plays the death");
uint32_t g_ko_shown, g_ko_replaced;

// BUG-0365: a mirror of a walking member was driven by drive::step towards the latest snapshot moved on
// by its arrival age: a MoveTo 0.25 s ahead every 0.1 s and SetPosition pulls. It walked at 9 m/s (~ the
// MoveTo given after SetDesiredSpeed resets the speed): while the host's member speeds up or slows down
// (1-5 m/s, every start and end of a walk) it reached the point ahead within a frame or two, stopped and
// walked again (the drawn step off the target's in 14-29 % of those frames), set off 0.5-1.6 m late and
// caught up at 15-24 m/s. The snapshots carried no sender time, so network jitter went straight into the
// target (the BUG-0122 class; over a mobile network 0.1 s windows matched the path in 71-80 %).
// * the host sends its clock with every chunk (an 8-byte trailer older guests ignore); each mirror keeps a
//   net::SnapshotBuffer (as the Creature puppets) and is driven towards the state kMirrorDelay s back,
//   walking at the interpolated path's velocity. From an older host: the latest state + vel * age.
// * while the member moves and the mirror is within kMirrorWalkMax of its path, the shared
//   drive::walk_place (driver.h, BUG-0360) walks it and places it on the path every frame (at its own
//   ground height, BUG-0329; the speed set after the MoveTo).
static void mirror_target(GEntry& e, double now, net::Vec3& pos, net::Quat& rot, net::Vec3& vel, double& age)
{
    GMember& g = e.data;
    net::PlayerState s, back;
    if (!g.buf.empty() && g.buf.sample(now, s)) {
        pos = s.pos;
        rot = s.rot;
        // the velocity of the interpolated path over the last kPathDt, not the newer state's: from a
        // standing state to a walking one the path moves slower than the walking state says, and the
        // game walks the mirror at this speed between two placings (it showed as stop-and-go)
        vel = s.vel;
        // BUG-0619 (drive.lead_smooth): the smoothed target's own velocity (its offset closing a late state's step
        // included): walked at the raw path's speed the mirror fell 0.5-1.4 m behind a closing target and surged
        // after it at 7-8 m/s when the closing ended (tribe_walk_mobile 20261106-140306 #3: uneven 16 %)
        g.lead_sp = -1;
        if (g.buf.lead_off() != g.jittery) { // (diagnostics: the lead switched off / on by the link's jitter)
            g.jittery = g.buf.lead_off();
            static unsigned said = 0; // (BUG-0650: 400 lines; 12 did not cover one test run)
            if (said++ < 400)
                H->log(M, SK_INFO, "tribe: mirror #%u lead %s (one-way latency %.0f ms, BUG-0619)", e.id,
                       g.jittery ? "off" : "on", g.buf.jitter() * 1000.0);
        }
        if (g.buf.smooth_velocity(vel)) {
            // the walk's speed rises at most kMirrorWalkAcc to the target's (it never goes under the lead's own):
            // set at once, the game's walk of the next frame grew by up to 0.4 m on top of the placing (1-1.3 m frames)
            float want = drive::speed(vel), raw = drive::speed(s.vel);
            double dt = now - g.walk_t;
            float w = (g.walk_t <= 0 || dt > 0.5) ? raw : g.walk_sp + kMirrorWalkAcc * (float)std::min(std::max(dt, 0.0), 0.1);
            w = want < raw ? want : std::min({std::max(w, raw), want, raw + kMirrorWalkOver});
            g.walk_sp = w;
            g.lead_sp = raw;
            g.walk_t = now;
            if (want > 0.01f) {
                float k = w / want;
                vel = {vel.x * k, vel.y * k, vel.z * k};
            }
            age = 0;
            return;
        }
        if (g.buf.sample(now - kPathDt, back)) {
            float k = (float)(1.0 / kPathDt);
            vel = {(s.pos.x - back.pos.x) * k, (s.pos.y - back.pos.y) * k, (s.pos.z - back.pos.z) * k};
            // never faster than the member walks: a late state over a bad network corrects the path by
            // 0.3-0.9 m at once, and walked at that path speed (13 m/s seen) the mirror surged ~1 m in a
            // frame on top of the placing's catch-up
            float ps = drive::speed(vel), ss = drive::speed(s.vel);
            if (ss > 0.3f && ps > ss) {
                float c = ss / ps;
                vel = {vel.x * c, vel.y * c, vel.z * c};
            }
        }
        age = 0;
        return;
    }
    pos = g.pos;
    rot = g.rot;
    vel = g.vel;
    age = now - g.seen;
    g.lead_sp = -1;
}

// BUG-0444: this game's time is paused (cGameTimeManager +0x48 bit 0: after a cold join for 10-40 s, the pause
// menu): no citizen update runs, so nothing but the mod moves or grounds the mirrors
static bool game_time_paused()
{
    static uintptr_t get = H->sym("Simulator::cGameTimeManager::Get");
    uint8_t* tm = get ? skg::cdecl_call<uint8_t*>(get) : nullptr;
    return tm && !IsBadReadPtr(tm + 0x48, 1) && (tm[0x48] & 1);
}

// drive::step at the mirror's own height (BUG-0329) and out of this game's ground (BUG-0444; onto it from above too
// while the game is paused: nothing else brings it down)
static void mirror_step(GEntry& e, const net::Vec3& pos, const net::Quat& rot, const net::Vec3& vel, double now, double age)
{
    GMember& g = e.data;
    void* const c = e.ref.p;
    const uint32_t gr = g.ds.grounded;
    if (game_time_paused()) {
        float a = (float)std::min(std::max(age, 0.0), 0.5);
        net::Vec3 t = drive::onto_ground(
            drive::at_own_height(net::Vec3{pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a}, game::position(c)));
        drive::step(c, t, rot, vel, g.ds, now, 0, true, 8.f, false, false, false, true);
    } else
        drive::step(c, pos, rot, vel, g.ds, now, age, true, 8.f, false, false, true, true);
    if (g.ds.grounded == gr)
        return;
    g_mirror_grounded++;
    if (g.ds.grounded <= 3) // (diagnostics: the fix at work, the first times per mirror)
        H->log(M, SK_INFO, "tribe: mirror %u (owner %u) was %.2f m under this game's ground: put onto it (tribe.mirror_ground)",
               (unsigned)e.id, (unsigned)g.owner, -g.ds.grounded_from);
}

// BUG-1235: a member mirror driven by the smoothed target (a walk start) is held to 2 x the body's own speed + catch_up a
// frame as the raw lead is (tribe_logic::mirror_lead_speed); off: no bound there (0.95 m frames at 30 fps)
static bool g_smooth_bound_fix = true;
COOP_FIX("tribe.smooth_bound", "BUG-1235", g_smooth_bound_fix,
         "tribe: a guest's copy of a member that starts to walk over a laggy network does not jump about 1 m in one frame");
// BUG-1451: with smooth_bound, a copy that started its walk late and stands far behind its aim catches up at a speed that
// grows with the lag (tribe_logic::mirror_lead_speed), not only at the host body's current speed
static bool g_smooth_catch_fix = true;
COOP_FIX("tribe.smooth_catch", "BUG-1451", g_smooth_catch_fix,
         "tribe: a guest's copy of a member that starts to walk late over a laggy network catches up with it within a second");

// BUG-1460: a dead member's mirror taken out of its tribe (detach_body) stands still: its walk ended, no MoveTo; off:
// mirror_step drives it on (0.7.16 and older)
bool g_body_still_fix = true;
COOP_FIX("tribe.body_still", "BUG-1460", g_body_still_fix,
         "tribe: a guest's copy of a member that died lies still (it does not walk on out of its tribe, which made the "
         "guest's game fault)");
uint32_t g_bodies_stilled;

static void mirror_drive(GEntry& e, double now)
{
    GMember& g = e.data;
    void* const c = e.ref.p;
    net::Vec3 pos, vel;
    net::Quat rot;
    double age;
    mirror_target(e, now, pos, rot, vel, age);
    float a = (float)std::min(std::max(age, 0.0), 0.5);
    g.aim = drive::at_own_height(net::Vec3{pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a}, game::position(c));
    float sp = drive::speed(vel);
    g.aim_speed = sp;
    // BUG-0444: never aimed under this game's ground (walk_place's ground takes 3 m only), nor over it while the
    // game is paused (walked down a slope it stayed at its height, 3 m over the ground)
    g.aim = game_time_paused() ? drive::onto_ground(g.aim) : drive::out_of_ground(g.aim, true);
    if (sp > 0.3f && net::dist(game::position(c), g.aim) < kMirrorWalkMax) {
        drive::WalkPlace o;
        o.own_height = false; // (the aim is at its own height already)
        o.speed_after_goal = true;
        o.max_dt = 0.4; // a slow guest keeps up with its members (driver.h)
        o.ground = true; // BUG-0444
        // BUG-0425; BUG-0619: not while the smoothed target closes the error itself (catch_prop on top measured the
        // catch-up from the game's walk and drew walk + catch-up, 0.9-1.25 m frames), nor without the lead (0.7.8)
        o.catch_prop = g.lead_sp < 0 && !(net::g_lead_smooth_fix && g.buf.lead_off());
        // BUG-0651 (drive.lead_keep): while the late lead catches up (the target and the walk at up to 2x the body's speed)
        // a frame draws at most 2 x the body's own speed + catch_up (WalkPlace::lead_speed, BUG-0619's bound): without it
        // the game's walk at 13-17 m/s plus catch_prop's closing drew 1.0-1.4 m frames (tribe_walk_player 20261107-102038
        // #3); with catch_prop off instead a 6-9 m error (BUG-0674) took 0.6 s to close
        // (and always: a mirror held by its game and moved by keep_lost's marked teleports, BUG-0386, caught up in 0.91 m
        // frames at 12.8 m/s, tribe_walk_player 20261107-111106 #2)
        // BUG-1235 (tribe.smooth_bound): the smoothed target's walk start too (tribe_logic::mirror_lead_speed)
        // BUG-1451 (tribe.smooth_catch): and at least a catch-up speed that grows with the lag behind the aim
        o.lead_speed = tribe_logic::mirror_lead_speed(net::g_lead_keep_fix, g_smooth_bound_fix, g.lead_sp, sp,
                                                      g.buf.late_catching(), g.buf.late_rate(), g_smooth_catch_fix,
                                                      net::dist(game::position(c), g.aim));
        // BUG-1451 (tribe.smooth_catch): keep walk_speed within the body/catch-up cap so placing gets the rest of
        // the 2 x v + 3 m/s frame budget (<= 21 m/s total) even when the game walks ~0 m/s or sp decayed
        {
            const float lag = net::dist(game::position(c), g.aim);
            const tribe_logic::CatchBound cb = tribe_logic::mirror_catch_bound(
                o.lead_speed, g_smooth_catch_fix, g.lead_sp, sp, g.buf.late_rate(), o.catch_up, o.catch_prop,
                net::g_lead_fix, lag);
            o.lead_speed = cb.lead_speed;
            o.walk_speed = cb.walk_speed;
            o.catch_up = cb.catch_up;
            g.diag_walk_speed = cb.walk_speed;
            g.diag_game_walk = drive::speed(game::velocity(c));
            g.diag_walked = g.ds.kinematic && now > g.ds.last_step
                                ? net::dist(game::position(c), g.ds.placed_to) / (float)(now - g.ds.last_step) : 0.f;
        }
        o.keep_lost = g_mirror_keep_fix; // BUG-0386
        g.diag_lead_speed = o.lead_speed;
        drive::walk_place(c, g.aim, rot, vel, g.ds, now, o);
        // tests (BUG-0386): the game's walk and a placing without the teleport mark are undone, the mirror crawls 0.04 m
        // a frame towards its aim (what the guest's game did to the held mirror); a marked teleport (keep_lost's held) is
        // kept, the hold goes on from there
        if (now < g.test_undo_until) {
            if (g.ds.held)
                g.test_undo_at = g.ds.placed_to;
            else {
                net::Vec3 d = g.test_undo_at, to = g.aim;
                float e = net::dist(d, to), k = e > 0.04f ? 0.04f / e : 1.f;
                g.test_undo_at = {d.x + (to.x - d.x) * k, d.y + (to.y - d.y) * k, d.z + (to.z - d.z) * k};
                game::place(c, g.test_undo_at, rot);
            }
        }
        g_mirror_walks++;
        // BUG-0386 (diagnostics): a walked mirror held over 2 m off its aim for 1 s: what is around it, once per 10 s
        if (g.ds.last_err > 2.f) {
            if (g.held_since <= 0)
                g.held_since = now;
            if (now - g.held_since > 1.0 && now - g.held_said > 10.0) {
                g.held_said = now;
                H->log(M, SK_INFO, "tribe: mirror #%u held %.1f m off its aim for %.1f s (place lost %u, held %d, %.1f m/s); "
                       "objects within 3 m: %s (BUG-0386)", e.id, g.ds.last_err, now - g.held_since, (unsigned)g.ds.place_lost,
                       (int)g.ds.held, sp, game::objects_near(game::position(c), 3.f, c).c_str());
            }
        } else
            g.held_since = 0;
        return;
    }
    drive::walk_end(c, g.aim, g.ds, now);
    mirror_step(e, pos, rot, vel, now, age);
}

// BUG-1084 (docs/re/tribe.md#tribe-away-tree-end): while the host is away the guest's game runs its mirrors' trees
// (tribe.away); when the host is back they are skipped again (h_BTTick), frozen in whatever node they were, and the
// world-object sync then changes what the mirror holds (a carried bundle the host's game has not is removed,
// BUG-0617). At the host's next away the frozen node went on with its bundle gone: the food delivery node
// 0x04623E0C (state 3, 0xC25BB0) found the member's container empty and called Citizen_HoldBundle 0xC23460 with null
// (caught faults at 0xC23469, campaign_tribe_end). tribe.away_tree_end: in the frame the host is back every mirror's
// running tree is ended (Citizen_BTTerminate: the nodes' exits run while what they hold is still there, its tribe
// set), so the next away starts the trees afresh. Off: the trees stay frozen mid-node (0.7.13).
static bool g_away_tree_end_fix = true;
COOP_FIX("tribe.away_tree_end", "BUG-1084", g_away_tree_end_fix,
         "tribe: a guest's member busy with food when the host comes back does not fault at the host's next away");
uint32_t g_away_trees_ended;
void away_tree_end_tick()
{
    static bool was_local;
    bool local = local_play();
    bool back = was_local && !local;
    was_local = local;
    if (back) { // (tests: the held trees go with the host's return)
        g_bt_frozen.clear();
        g_bt_freeze_at.clear();
    }
    if (!back || !g_away_tree_end_fix || !a_BTTerminate || game::mode_id() != modes::kTribe)
        return;
    uint32_t n = 0;
    g_guest.mirrors.for_each([&](GEntry& e_) {
        if (!mirror_ok(e_) || g_detached.count(e_.ref.p) || g_bt_local.count(e_.ref.p) || !F<void*>(e_.ref.p, 0x1014))
            return;
        uint8_t* ai = F<uint8_t*>(e_.ref.p, 0xB4C);
        if (!ai || !skg::readable(ai, 0x620) || *(int*)(ai + 0x1D0) == -1) // (no tree running)
            return;
        skg::cdecl_call<void>(a_BTTerminate, e_.ref.p);
        n++;
    });
    g_away_trees_ended += n;
    H->log(M, SK_INFO, "tribe: the host is back: %u mirror trees left running by the away play ended (BUG-1084)", n);
}

void guest_tick(double now)
{
    if (local_play())
        return; // the last states would hold the members for 3 s
    g_guest.mirrors.for_each([&](GEntry& e_) {
        const uint16_t id = e_.id;
        GMember& g = e_.data;
        if (!mirror_ok(e_) || now - g.seen > 3.0) // (BUG-0844)
            return;
        void* const c = e_.ref.p;
        g.ds.drawn = game::position(c); // coop.track (BUG-0365): where the game drew it this frame
        tribe_logic::Ko ko = tribe_logic::ko(g_fix_ko, g.hp, game::is_dead(c)); // (D3)
        if (ko == tribe_logic::Ko::Death) {
            if (g.air) { // (BUG-0946)
                g.air = false;
                game::air_hold(c, false);
            }
            if (!game::is_dead(c)) {
                game::set_health(c, 0);
                game::play_death(c);
                g_ko_shown++;
            }
            if (g.body)
                detach_body(c);
            // BUG-1460 (tribe.body_still): a body out of its tribe is not walked: mirror_step's MoveTo left it steering
            // with +0x1014 null, and a blocked one made the game's make-way step (Creature_AskGroupToMakeWay 0xC28530)
            // read its null tribe (0x00C8F6D3, 38 caught faults, campaign_tribe_noclick 20261123-023643). Its walk is
            // ended once; it is put where the host's body lies when it is over 1 m off (StopMovement can put a
            // creature back up to 3 m, driver.h)
            if (tribe_logic::body_still(g_body_still_fix, g.body, g_detached.count(c) > 0)) {
                if (!g.body_stopped) {
                    game::stop(c);
                    g.body_stopped = true;
                    g_bodies_stilled++;
                }
                if (net::dist(game::position(c), g.pos) > 1.f)
                    game::teleport(c, drive::out_of_ground(g.pos, true), g.rot);
                return;
            }
            mirror_step(e_, g.pos, g.rot, g.vel, now, now - g.seen);
            return;
        }
        if (ko == tribe_logic::Ko::Replace) { // back on its feet on the host
            MIRROR_LOG("tribe: mirror #%u goes: dead here, back on its feet on the host (made again, BUG-0685)", id);
            g_guest.mirrors.detach(id);
            remove_member(c);
            g_ko_replaced++;
            return;
        }
        if (g_mirror_air_fix && now - g.air_at < (g_mirror_hang_fix ? kMirrorHangHold : kMirrorAirFresh) &&
            g.air_agl > drive::kAirOver && g.air_agl < (g_mirror_hang_fix ? kMirrorHangMax : drive::g_agl_max)) {
            // BUG-0946: at the host's height over this game's ground (BUG-1082: any height, held 1 s, a landing ends it)
            net::Vec3 pos, vel;
            net::Quat rot;
            double age;
            mirror_target(e_, now, pos, rot, vel, age);
            float r = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z), gr = game::ground_radius(pos);
            if (r > 1.f && gr > 0) {
                float k = (gr + g.air_agl) / r;
                g.aim = {pos.x * k, pos.y * k, pos.z * k};
                drive::air_place(c, g.aim, rot, vel, g.ds, now);
                g.air = true;
                g_anim.air_placed++;
            }
        } else {
            if (g.air) { // (its game lands it)
                g.air = false;
                game::air_hold(c, false);
            }
            mirror_drive(e_, now);
        }
        float hp = game::health(c);
        if (std::fabs(hp - g.hp) > 0.01f && g.hp > 0)
            game::set_health(c, g.hp);
    });
}

} // namespace tribe
