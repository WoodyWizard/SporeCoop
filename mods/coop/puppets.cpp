// The partners' puppets of the coop core (B2 of 0.7.15: moved out of coop.cpp, docs/audit-0.7.15/core.md CORE-2): making,
// driving and despawning a puppet (drive_puppet, despawn, forget_puppets, strays), the local player's state that is
// sent, walking and clicking through the partners (BUG-0913), their animations (on_anim / on_remote_anim), and the
// recorders that look at them: coop.track, coop.bodyrec, the invariant monitor's fields and the support trace
// (trace_tick). The Remote table itself (g_remotes) and the session stay in coop.cpp.
#include "lifecycle.h"
#include "coop_state.h"
#include "cocoon.h"
#include "own_progress.h"

namespace coop_core {

// BUG-0091: a creature puppet can be freed by the game before the mod notices (a world teardown
// between two frames: the messages queued meanwhile are handled first). Animations and despawns
// only touch a creature puppet that is still in the game's object list. `coop.framefix 0` restores
// the old order and the unchecked calls (tests).
bool g_frame_fix = true;
COOP_FIX_UNSAFE("core.frame", "BUG-0091", g_frame_fix, "core: animations and despawns only touch a puppet the game still lists (coop.framefix)");
// calls that met a puppet the game no longer lists: dropped (fix on) / made anyway (framefix 0)
uint32_t g_anims_dropped = 0, g_anims_unlisted = 0, g_despawns_dropped = 0, g_despawns_unlisted = 0;
bool creature_puppet(const modes::Adapter* a)
{
    return a && (a == modes::for_mode(modes::kCreature) || a == modes::for_mode(modes::kScenario));
}

void despawn(Remote& r)
{
    cocoon::puppet_gone(r.id, r.puppet, r.relook); // WP-K: its cocoon ends with it (a new look: the new body goes in)
    if (r.puppet && r.puppet_adapter) {
        if (creature_puppet(r.puppet_adapter) && !game::creature_listed(r.puppet)) {
            (g_frame_fix ? g_despawns_dropped : g_despawns_unlisted)++;
            LOG("puppet of %s is gone already (%p): forgotten%s", r.name.c_str(), r.puppet,
                g_frame_fix ? "" : ", destroyed anyway (framefix 0)");
            if (g_frame_fix)
                game::puppet_unregister(r.puppet);
            else
                r.puppet_adapter->despawn(r.puppet);
        } else
            r.puppet_adapter->despawn(r.puppet);
    }
    r.puppet = nullptr;
    r.puppet_adapter = nullptr;
}

uint32_t g_mode = 0, g_world = 0;
bool g_save_on_share = true; // host saves before sending the world, so guests get the current state
const modes::Adapter* g_adapter = nullptr;
bool g_puppet_hold = false; // tests (coop.puppet hold): remote puppets are not driven (BUG-0251), never saved
double g_puppet_hide_until = 0; // tests (coop.puppet respawn <s>): partners' puppets stay away until then (BUG-0453)
void* g_local = nullptr; // entity the local player controls (adapter specific)

std::vector<Stray> g_strays;
uint32_t g_strays_removed = 0;

// BUG-1055: every module's session and world state registered in lifecycle.h is reset on every session end and on
// every new world (the stage counters moved), and `coop.lifecycle held` lists what a reset left. Off: each module's
// own on_session_end / on_world_change only (0.7.13).
bool g_lifecycle_fix = true;
COOP_FIX("core.lifecycle_reset", "BUG-1055", g_lifecycle_fix, "core: the session and world state every module registered (lifecycle.h) is reset on every session end and world change");
void forget_puppets() // world changed or the local player left it (editor, load)
{
    const modes::Adapter* creature = modes::for_mode(modes::kCreature);
    for (auto& [id, r] : g_remotes) {
        cocoon::puppet_gone(id, r.puppet, false); // WP-K
        if (r.puppet) {
            game::puppet_unregister(r.puppet);
            if (g_lifecycle_fix)
                creature_hp::forget_puppet(r.puppet); // BUG-1055: no reported health kept for this address
            if (r.puppet_adapter == creature && game::creature_listed(r.puppet))
                g_strays.push_back({r.puppet, game::herd_of(r.puppet), game::species_instance(r.puppet), r.puppet_world,
                                    H->time_seconds()});
        }
        r.puppet = nullptr;
        r.puppet_adapter = nullptr;
    }
}

// C2 (BUG-1141): the puppets belong to the world that was open (world_id.h); first of the world-change hooks
LIFECYCLE_HOOK("puppets.world", lifecycle::kWorldChange, -100, [](const lifecycle::Ctx& c) {
    if (c.from_stage >= 0)
        forget_puppets();
}, true);

void remove_strays(double now)
{
    if (g_strays.empty() || g_mode != modes::kCreature || !g_local)
        return;
    for (auto& s : g_strays) {
        // the same creature (pointer still listed, same herd), the same world, not ours again, not
        // destroyed already (npc.cpp remove_saved_strays meets it too: untuned, in a herd like ours)
        if (s.world == g_world && now - s.since < 3600 && game::creature_listed(s.c) && !game::is_destroyed(s.c) &&
            game::herd_of(s.c) == s.herd &&
            game::species_instance(s.c) == s.species && !game::is_puppet(s.c) && s.c != game::avatar()) {
            LOG("a puppet left over from before (%p, herd %p) removed", s.c, s.herd);
            game::destroy_creature(s.c);
            g_strays_removed++;
        }
    }
    g_strays.clear();
}


// tests (coop.hoststate mode <hex> <seconds>, BUG-0233): the mode our states report, for a while
uint32_t g_state_mode_override = 0;
double g_state_mode_until = 0;

// BUG-0271: what our last frame found may be gone by now. Console commands and network callbacks
// also run between frames, even from the game's message pump inside a stage teardown (the host's
// coop.status / coop.avatar read a freed cell while the game went Cell -> Creature, with the mode
// already Creature). The local entity counts only while the mode is still the frame's, the stage is
// not being left and the adapter still finds it; a puppet only while the mode is the frame's and its
// adapter still sees it alive. Inside on_frame these always agree.
static bool frame_world_now()
{
    return game::mode_id() == g_mode && !game::leaving_stage();
}

uint32_t g_local_refused, g_puppet_refused; // coop.cellgone (tests)

void* local_now()
{
    if (!g_adapter || !g_local)
        return g_local;
    if (frame_world_now() && g_adapter->local() == g_local)
        return g_local;
    g_local_refused++;
    return nullptr;
}

void* puppet_now(const Remote& r);
// BUG-0426 class (L1 of 0.7.13): player id's Creature puppet, only while it is the live object made for it
void* creature_puppet_live(uint8_t id)
{
    auto it = g_remotes.find(id);
    if (it == g_remotes.end() || !it->second.puppet || !creature_puppet(it->second.puppet_adapter))
        return nullptr;
    void* p = puppet_now(it->second);
    return p && game::creature_listed(p) ? p : nullptr;
}

// ---- BUG-0913: a partner's creature does not block the way nor take the attack click while friendly fire is off ----
// The second player to attack an animal the partner fights stopped 2 m short behind the partner's creature (its
// puppet here, held at the partner's place) and its attack key did nothing; a click on the animal picked the partner
// in front of it (the attack key then does nothing: status 12). Owner's decision 2026-11-11 (docs/plan-0.7.13.md):
// while friendly fire is off and outside Rivals, in Creature,
// (a) Havok's collision filter (hkGroupFilter::isCollisionEnabled 0x10C3F90, asked when two bodies' boxes start to
//     overlap) says no for the pair of our avatar and a partner's puppet: we walk through it
//     (docs/re/creature.md#creature-partner-pass);
// (b) the Creature mode's hover filter 0xD39070 (cdecl bool(HoverHit*), asked for every object under the cursor
//     ray) refuses a partner's puppet in the combat stance: the hover and the click take the object behind it.
// With friendly fire on or in Rivals nothing changes. Accepted: players' creatures can stand inside each other.
// Off: 0.7.12.
bool g_partner_pass_fix = true;
COOP_FIX("creature.partner_pass", "BUG-0913", g_partner_pass_fix, "creature: with friendly fire off a player walks through a partner's creature and a click goes to the animal behind it");
// what the hooks read (they may run inside Havok's step): written once a frame by partner_pass_tick
static void* volatile g_pass_avatar;
void* volatile g_pass_puppets[8];
volatile bool g_pass_on;
uint32_t g_pass_pairs, g_pass_hover_skipped, g_pass_seen_own; // tests (coop.status)
uintptr_t a_AbilityModeCore;
// BUG-1250 (players' video): in the social stance the partner's creature got the green ring and its name plate under
// the cursor, so the players thought it could be befriended or taken into the pack; a click on it starts nothing (no
// social game with a puppet, BUG-1241). The hover filter refused it only in the combat stance and only with friendly
// fire off (BUG-0913). Now it refuses a partner's puppet in the social stance always (in Creature, outside Rivals):
// the hover and the click take the creature behind it. `coop.fix creature.partner_social_hover off`: as 0.7.15.
static bool g_social_hover_fix = true;
COOP_FIX("creature.partner_social_hover", "BUG-1250", g_social_hover_fix, "creature: in the social stance a partner's creature gets no green ring or name plate under the cursor (it cannot be befriended)");
volatile bool g_social_hover_on;
uint32_t g_social_hover_skipped; // tests (coop.partnerpass)
void partner_pass_tick()
{
    bool creature = g_mode == modes::kCreature && g_sess.connected() && !(spaceext::rivals() || pfind::g_test_rivals);
    bool on = g_partner_pass_fix && creature && !settings::on(settings::kFriendlyFire);
    bool social = g_social_hover_fix && creature;
    size_t n = 0;
    if (on || social)
        for (auto& [id, r] : g_remotes)
            if (n < 8 && r.puppet && !r.echo)
                if (void* p = creature_puppet_live(id))
                    g_pass_puppets[n++] = p;
    for (size_t i = n; i < 8; i++)
        g_pass_puppets[i] = nullptr;
    g_pass_avatar = on ? game::avatar() : nullptr;
    g_pass_on = on && n > 0;
    g_social_hover_on = social && n > 0;
}
// o is the creature (its base pointer, as the casts give it) or its spatial part (+0xC0)
static bool pass_is(void* who, void* o) { return who && o && (o == who || o == (char*)who + 0xC0); }
static bool pass_partner(void* o)
{
    for (void* p : g_pass_puppets)
        if (pass_is(p, o))
            return true;
    return false;
}
// the game object of a Havok collidable: the root's owner (broadphase type 1 = an entity), its userData (+0xC)
static void* collidable_object(uint8_t* c)
{
    if (!c)
        return nullptr;
    while (*(uint8_t**)(c + 0xC))
        c = *(uint8_t**)(c + 0xC);
    if (c[0x18] != 1)
        return nullptr;
    uint8_t* entity = c + *(int32_t*)(c + 0x10);
    return *(void**)(entity + 0xC);
}
// tests (BUG-0913 on rel/0.7.13): the pairs of a non-entity collidable (broadphase type 2, a phantom: hkpWorldObject,
// its userData at +0xC as an entity's) with the avatar or a partner's puppet, which the rule above lets through
uint32_t g_pass_phantom_pairs, g_pass_phantom_own, g_pass_phantom_partner;
static void* collidable_phantom(uint8_t* c)
{
    if (!c)
        return nullptr;
    while (*(uint8_t**)(c + 0xC))
        c = *(uint8_t**)(c + 0xC);
    if (c[0x18] != 2)
        return nullptr;
    uint8_t* wo = c + *(int32_t*)(c + 0x10);
    return *(void**)(wo + 0xC);
}
CollEnabled_t o_CollEnabled;
uint8_t* SK_THISCALL h_CollEnabled(void* self, uint8_t* out, uint8_t* a, uint8_t* b)
{
    if (g_pass_on && out && a && b) {
        void* av = g_pass_avatar;
        void *oa = collidable_object(a), *ob = collidable_object(b);
        bool own_a = pass_is(av, oa), own_b = pass_is(av, ob);
        g_pass_seen_own += own_a || own_b;
        if (void* pa = !oa ? collidable_phantom(a) : nullptr; pa || (!ob && collidable_phantom(b))) {
            void* ph = pa ? pa : collidable_phantom(b);
            void* other = pa ? ob : oa;
            g_pass_phantom_pairs++;
            g_pass_phantom_own += (pass_is(av, ph) && pass_partner(other)) || (pass_partner(ph) && pass_is(av, other));
            g_pass_phantom_partner += pass_partner(ph) || pass_partner(other);
        }
        if ((own_a && pass_partner(ob)) || (own_b && pass_partner(oa))) {
            g_pass_pairs++;
            *out = 0;
            return out;
        }
    }
    return o_CollEnabled(self, out, a, b);
}
HoverFilter_t o_HoverFilter;
bool SK_CDECL h_HoverFilter(void** hit)
{
    if ((g_pass_on || g_social_hover_on) && hit && hit[0] && a_AbilityModeCore) {
        int mode = skg::cdecl_call<int>(a_AbilityModeCore); // 0 the social stance, 1 the combat stance
        if ((g_pass_on && mode != 0) || (g_social_hover_on && mode == 0)) {
            void* owner = *(void**)((uint8_t*)hit[0] + 0x64);
            void* obj = owner ? skg::vcall<void*>(owner, 0xC / 4, 0x17F243Bu) : nullptr; // the hover filter's own cast
            if (pass_partner(obj)) {
                (mode == 0 ? g_social_hover_skipped : g_pass_hover_skipped)++;
                return false; // the next object along the ray (the animal behind the partner)
            }
        }
    }
    return o_HoverFilter(hit);
}

void* puppet_now(const Remote& r)
{
    if (!r.puppet || !r.puppet_adapter)
        return r.puppet;
    if (!frame_world_now() || (r.puppet_adapter->alive && !r.puppet_adapter->alive(r.puppet))) {
        g_puppet_refused++;
        return nullptr;
    }
    return r.puppet;
}

// every synchronized object here (coop.sync, the support sync-check): the players (p<id>: our own
// creature, the partners' puppets) and the stage modules' objects
std::vector<syncdbg::Entry> sync_all()
{
    std::vector<syncdbg::Entry> v;
    auto player = [&](uint8_t id, const modes::Adapter* a, void* e) {
        if (!a || !e)
            return;
        net::PlayerState st;
        a->fill(e, st);
        v.push_back({"p" + std::to_string(id), st.pos, st.rot});
    };
    if (g_sess.connected()) {
        player(g_sess.my_id(), g_adapter, local_now()); // BUG-0271
        for (auto& [id, r] : g_remotes)
            if (!r.echo)
                player(id, r.puppet_adapter, puppet_now(r));
    }
    for (auto* f : {npc::sync_entries, tribe::sync_entries, civ::sync_entries, squadron::sync_entries,
                    cellsync::sync_entries})
        for (auto& e : f())
            v.push_back(e);
    return v;
}

net::PlayerState local_state()
{
    net::PlayerState st;
    st.mode = g_state_mode_override && H->time_seconds() < g_state_mode_until ? g_state_mode_override : g_mode;
    st.world = g_world;
    if (void* local = local_now())
        g_adapter->fill(local, st);
    if (g_sess.role() == net::Role::Host && world::host_reloading())
        st.flags |= net::PF_RESYNC; // BUG-0332
    return st;
}

// remote `s` is visible here only when both play the same mode in the same world
bool same_place(const net::PlayerState& s) { return s.mode == g_mode && s.world == g_world && g_world != 0; }

// ---- coop.track (BUG-0360): what this player sees of its partners, frame by frame. Tests sample the
// partner's puppet (its game position, where its model is drawn, its game speed, its animation, the
// interpolated target) and the own avatar every frame into a ring and read it back with `dump`.
struct TrackSample {
    float t;
    uint8_t who; // remote id, 255 = the own avatar
    uint8_t flags; // state flags of the target (PF_*), the own avatar: 0
    uint16_t tp;   // the puppet's teleports so far (drive::State)
    net::Vec3 p, d, tgt; // p: now (after the driving), d: drawn this frame (before it)
    float gv, rv; // the game's speed of the creature, the remote's reported speed
    uint32_t anim;
    int16_t gait = -1, gait2 = -1; // the animation's gait state (game::gait_state)
    int8_t moving = -1, speed_state = -1;
    int8_t vis = 0; // world_to_screen of its position answered (in front of the camera)
    float sx = 0, sy = 0; // its screen position then
    // BUG-0366: the height over this game's ground (cPlanetModel::GetHeightAt) of the drawn position
    // and of the target (the partner's own position, at its own game's height)
    float hd = 0, ht = 0;
    uint16_t id = 0;      // Tribe (BUG-0365): the member id (who = its owner), 0 in Creature
    // BUG-0414: the partner's animations played on the puppet so far (MSG_ANIM) and the last one's id
    uint32_t ranims = 0, ranim = 0;
    // BUG-0436: its size on the observer's screen, pixels per metre (a point 1 m over it projected), 0 when not known:
    // the game animates a creature far from its camera without its walk gait
    float ppm = 0;
    // BUG-0434: the physics' simple-path inputs (PhysicsManager_Update 0xB4C5D5): the drawn flag +0xF90, the LOD +0xEAC
    int8_t f90 = -1, lod = -1;
    // BUG-1093: the puppet: how far past its newest state the target ran (SnapshotBuffer::last_extrap, s) and that
    // state's age (s past its usual arrival); the own avatar: 0 and the time since its last state was sent (s)
    float ex = 0, age = 0;
    float knee = 0, hz = 0, ow = 0; // BUG-1093: the puppet's fall knee, its median horizon less g_one_way, g_one_way (s)
};
static std::deque<TrackSample> g_track;
// tests (BUG-0367): `coop.track lift <m>`: this game takes the partners' Creature states <m> further from
// the planet's centre (negative: under the ground), as a partner's position off this game's ground
// would be (an extrapolated fall, a ground that differs)
static float g_track_lift = 0;
// tests (BUG-0394): `coop.track novel 1`: this game takes the partners' Creature states without their
// velocity, as a partner carried by an animation reports none (B4212A07: 3-4.6 m/s at velocity 0)
static bool g_track_novel = false;
static void track_lift(net::PlayerState& s, bool novel = true)
{
    if (novel && g_track_novel && g_mode == modes::kCreature)
        s.vel = {0, 0, 0};
    float r = std::sqrt(s.pos.x * s.pos.x + s.pos.y * s.pos.y + s.pos.z * s.pos.z);
    if (g_track_lift == 0 || g_mode != modes::kCreature || r < 1.f)
        return;
    float k = (r + g_track_lift) / r;
    s.pos = {s.pos.x * k, s.pos.y * k, s.pos.z * k};
}
static bool g_track_on = false;
uint32_t g_own_anims = 0, g_own_anim_last = 0; // coop.track (BUG-0414): the own avatar's animations (on_anim)
static double g_track_t0 = 0;
constexpr size_t kTrackMax = 60000;

static TrackSample& track_one(uint8_t who, void* c, const net::PlayerState* s, uint16_t tp, double now,
                              const net::Vec3* drawn = nullptr)
{
    TrackSample x{};
    x.t = (float)(now - g_track_t0);
    x.who = who;
    x.tp = tp;
    x.p = game::position(c);
    game::drawn_state(c, nullptr, &x.anim);
    x.d = drawn ? *drawn : x.p; // the puppet: where the game drew it this frame (before our driving)
    int g = -1, g2 = -1, mv = -1, ss = -1;
    if (game::gait_state(c, &g, &g2, &mv, &ss)) {
        x.gait = (int16_t)g;
        x.gait2 = (int16_t)g2;
        x.moving = (int8_t)mv;
        x.speed_state = (int8_t)ss;
    }
    x.gv = drive::speed(game::velocity(c));
    float w[3] = {x.p.x, x.p.y, x.p.z}, sc[2] = {0, 0};
    x.vis = H->world_to_screen(w, sc) ? 1 : 0;
    x.sx = sc[0];
    x.sy = sc[1];
    {
        float r = std::sqrt(x.p.x * x.p.x + x.p.y * x.p.y + x.p.z * x.p.z), sc2[2] = {0, 0};
        float w2[3] = {x.p.x + x.p.x / r, x.p.y + x.p.y / r, x.p.z + x.p.z / r};
        if (x.vis && r > 1.f && H->world_to_screen(w2, sc2))
            x.ppm = std::sqrt((sc2[0] - sc[0]) * (sc2[0] - sc[0]) + (sc2[1] - sc[1]) * (sc2[1] - sc[1]));
    }
    if (s) {
        x.tgt = s->pos;
        x.rv = drive::speed(s->vel);
        x.flags = (uint8_t)s->flags;
    }
    auto over = [](const net::Vec3& v) {
        float g = game::ground_radius(v);
        return g > 0 ? std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) - g : 0.f;
    };
    x.hd = over(x.d);
    x.ht = s ? over(x.tgt) : x.hd;
    if (game::noun_id(c) == 0x18EB45Eu) { // a creature
        x.f90 = (int8_t)*((uint8_t*)c + 0xF90);
        x.lod = (int8_t)*(int32_t*)((uint8_t*)c + 0xEAC);
    }
    while (g_track.size() >= kTrackMax)
        g_track.pop_front();
    g_track.push_back(x);
    return g_track.back();
}

void track_tick(double now)
{
    if (g_track_on && g_mode == modes::kTribe) { // BUG-0365: the player tribe's members (mirrors on guests)
        tribe::for_each_tracked(
            [](const tribe::TrackMember& m, void* u) {
                net::PlayerState s{};
                if (m.tgt) {
                    s.pos = *m.tgt;
                    s.flags = net::PF_IN_WORLD | (m.rv > 0.3f ? net::PF_MOVING : 0);
                }
                TrackSample& x = track_one(m.owner, m.c, m.tgt ? &s : nullptr, m.teleports, *(double*)u, m.drawn);
                x.id = m.id;
                x.rv = m.rv;
                if (!m.tgt) { // the host's own member: its own state is the reference
                    x.tgt = x.p;
                    x.flags = net::PF_IN_WORLD | (m.rv > 0.3f ? net::PF_MOVING : 0);
                }
            },
            &now);
        return;
    }
    if (!g_track_on || g_mode != modes::kCreature || !g_local)
        return;
    {
        TrackSample& x = track_one(255, g_local, nullptr, 0, now);
        x.ranims = g_own_anims; // the own avatar: its animations sent (MSG_ANIM)
        x.ranim = g_own_anim_last;
        x.age = (float)(now - g_last_send); // (with net.send_phase: since the last state was due)
    }
    for (auto& [id, r] : g_remotes) {
        void* pp = puppet_now(r);
        if (!pp || !creature_puppet(r.puppet_adapter) || !game::creature_listed(pp))
            continue;
        net::PlayerState s;
        if (r.buf.sample(now, s) && (track_lift(s, false), true)) // (rv: the speed the partner reported)
        {
            TrackSample& x = track_one((uint8_t)id, pp, &s, (uint16_t)r.drv.teleports, now, g_puppet_hold ? nullptr : &r.drv.drawn);
            x.ranims = r.anims;
            x.ranim = r.anim_last;
            x.ex = (float)r.buf.last_extrap;
            x.age = (float)r.buf.age(now);
            x.knee = (float)r.buf.fall_knee();
            x.hz = (float)r.buf.horizon_med();
            x.ow = (float)net::g_one_way;
        }
    }
}

int c_track(void*, int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "count";
    if (!strcmp(a, "start")) {
        g_track.clear();
        g_track_on = true;
        g_track_t0 = H->time_seconds();
    } else if (!strcmp(a, "stop"))
        g_track_on = false;
    else if (!strcmp(a, "lift")) {
        if (argc > 2)
            g_track_lift = (float)atof(argv[2]);
        SAY("lift %.2f\n", g_track_lift);
        return 0;
    } else if (!strcmp(a, "air")) { // tests (BUG-0376): the puppet in the air 0 placed + not grounded, 1 placed, 2 teleported, 3 walked, 4 own fall speed
        if (argc > 2)
            modes::g_puppet_air_variant = atoi(argv[2]);
        SAY("air %d\n%s", modes::g_puppet_air_variant, game::air_status().c_str());
        return 0;
    } else if (!strcmp(a, "novel")) {
        if (argc > 2)
            g_track_novel = atoi(argv[2]) != 0;
        SAY("novel %d\n", (int)g_track_novel);
        return 0;
    }
    else if (!strcmp(a, "dump")) {
        // dump [from] [n]: t who flags tp px py pz dx dy dz tx ty tz gv rv anim gait gait2 moving speed_state vis sx sy
        // hd ht (BUG-0366: heights over the ground) id (BUG-0365: the Tribe member id, 0 in Creature) ranims ranim
        // (BUG-0414: the partner's animations played on the puppet so far, the last one) ppm (BUG-0436: pixels per metre on
        // the screen) f90 lod ex age knee hz ow (BUG-1093: the target's extrapolation, its newest state's age, the
        // extrapolation's fall knee, the median horizon less the one-way latency, the one-way latency); new columns go at the
        // end (parsers take >= the columns they know)
        size_t from = argc > 2 ? (size_t)atol(argv[2]) : 0, n = argc > 3 ? (size_t)atol(argv[3]) : 2000;
        for (size_t i = from; i < g_track.size() && i < from + n; i++) {
            const auto& x = g_track[i];
            SAY("%.4f %u %u %u %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.2f %.2f %08X %d %d %d %d %d %.0f %.0f %.3f %.3f %u %u %08X %.1f %d %d %.3f %.3f %.3f %.3f %.3f\n", x.t,
                x.who, x.flags, x.tp, x.p.x, x.p.y, x.p.z, x.d.x, x.d.y, x.d.z, x.tgt.x, x.tgt.y, x.tgt.z, x.gv, x.rv,
                x.anim, x.gait, x.gait2, x.moving, x.speed_state, x.vis, x.sx, x.sy, x.hd, x.ht, (unsigned)x.id, x.ranims, x.ranim, x.ppm, x.f90, x.lod, x.ex, x.age, x.knee, x.hz, x.ow);
        }
        return 0;
    }
    SAY("track %s samples=%u\n", g_track_on ? "on" : "off", (unsigned)g_track.size());
    return 0;
}

// ---- coop.bodyrec (BUG-0425, M1): every synchronized body (sync_all: players, NPCs, Tribe members, Civ vehicles,
// ships, cells) as this game drew it (read at the start of the mod's frame, before any message or driving), with the
// absolute time (H->time_seconds: QueryPerformanceCounter, one clock for every game on one PC). A test records on
// the host and on a guest and compares the guest's copy with the host's body at the same moment
// (tools/tests/body_trail.py), the game's own positions on both machines, not our bookkeeping.
struct BodyRow {
    double t;
    char key[8];
    net::Vec3 p;
};
static std::deque<BodyRow> g_bodyrec;
static bool g_bodyrec_on = false, g_bodyrec_diag = false;
static std::string g_bodyrec_kinds; // `coop.bodyrec start kinds <chars>`: only keys starting with one of them (Cell: p)
static constexpr size_t kBodyRecMax = 400000;

void bodyrec_tick(double now)
{
    if (!g_bodyrec_on || !g_sess.connected())
        return;
    for (auto& e : sync_all()) {
        if (!g_bodyrec_kinds.empty() && (e.key.empty() || g_bodyrec_kinds.find(e.key[0]) == std::string::npos))
            continue;
        BodyRow r{};
        r.t = now;
        snprintf(r.key, sizeof r.key, "%s", e.key.c_str());
        r.p = e.pos;
        g_bodyrec.push_back(r);
    }
    // BUG-1227: the Civ city citizens (z<host id>, y<id> a guest's walk goals, a<id> their animations) only when a test
    // names them (`coop.bodyrec start kinds z[y][a]`): the detector's `bodyrec on` rows stay as they were
    if (g_bodyrec_kinds.find('z') != std::string::npos)
        wobj::for_each_body(
            [](const char* key, const net::Vec3& p, void* u) {
                BodyRow r{};
                r.t = *(double*)u;
                snprintf(r.key, sizeof r.key, "%s", key);
                r.p = p;
                g_bodyrec.push_back(r);
            },
            &now, g_bodyrec_kinds.find('y') != std::string::npos, g_bodyrec_kinds.find('a') != std::string::npos);
    if (g_bodyrec_diag) { // (K3 diagnostics: the driving's own targets, `coop.bodyrec start diag`)
        auto row = [](const char* key, const net::Vec3& p, void* u) {
            BodyRow r{};
            r.t = *(double*)u;
            snprintf(r.key, sizeof r.key, "%s", key);
            r.p = p;
            g_bodyrec.push_back(r);
        };
        npc::for_each_diag(row, &now); // q<id>: the NPC proxies' targets (BUG-0363)
        tribe::for_each_diag(row, &now);
        civ::for_each_diag(row, &now);
    }
    while (g_bodyrec.size() > kBodyRecMax)
        g_bodyrec.pop_front();
}

int c_bodyrec(void*, int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "count";
    if (!strcmp(a, "start")) {
        g_bodyrec.clear();
        g_bodyrec_on = true;
        g_bodyrec_diag = argc > 2 && !strcmp(argv[2], "diag");
        g_bodyrec_kinds = argc > 3 && !strcmp(argv[2], "kinds") ? argv[3] : ""; // tests (BUG-0887): a Cell pool is hundreds of rows a frame
    } else if (!strcmp(a, "stop"))
        g_bodyrec_on = false;
    else if (!strcmp(a, "dump")) { // dump [from] [n]: t key x y z (t: absolute s)
        size_t from = argc > 2 ? (size_t)atol(argv[2]) : 0, n = argc > 3 ? (size_t)atol(argv[3]) : 2000;
        for (size_t i = from; i < g_bodyrec.size() && i < from + n; i++) {
            const auto& x = g_bodyrec[i];
            SAY("%.4f %s %.3f %.3f %.3f\n", x.t, x.key, x.p.x, x.p.y, x.p.z);
        }
        return 0;
    } else if (!strcmp(a, "since")) { // since <t> [n] [step]: rows after absolute time t, one frame per step s
        // (BUG-0609: the divergence detector reads the rows of its last period, the ring index moves as it drops rows)
        double t0 = argc > 2 ? atof(argv[2]) : 0.0, step = argc > 4 ? atof(argv[4]) : 0.0;
        size_t n = argc > 3 ? (size_t)atol(argv[3]) : 2000, said = 0;
        auto it = std::upper_bound(g_bodyrec.begin(), g_bodyrec.end(), t0,
                                   [](double t, const BodyRow& r) { return t < r.t; });
        double kept = -1e300, last = -1e300;
        for (; it != g_bodyrec.end() && said < n; ++it) {
            if (it->t != last) { // a new frame: keep it when step s passed since the last kept one
                if (it->t < kept + step) {
                    double ft = it->t;
                    while (it != g_bodyrec.end() && it->t == ft)
                        ++it;
                    if (it == g_bodyrec.end())
                        break;
                    --it;
                    continue;
                }
                kept = last = it->t;
            }
            SAY("%.4f %s %.3f %.3f %.3f\n", it->t, it->key, it->p.x, it->p.y, it->p.z);
            said++;
        }
        return 0;
    } else if (!strcmp(a, "on")) // on: record without clearing (the detector; a test's `start` clears)
        g_bodyrec_on = true;
    SAY("bodyrec %s samples=%u now=%.4f\n", g_bodyrec_on ? "on" : "off", (unsigned)g_bodyrec.size(), H->time_seconds());
    return 0;
}

// BUG-0513: a partner whose game stops sending (minimized, alt-tabbed full screen, a stall of 17-38 s) was hidden
// after 5 s without a word ("the guest is bugged into the ground"). Now the others are told
// once that the partner is not responding and when it plays again.
// BUG-0923: a partner's creation record arrives 0.3-0.4 s after its first puppet (at a join) or later (a new body):
// the puppet is rebuilt with that look, and each making told "<name> appeared in the world" (twice at every join on
// the host). Off: every puppet made is told (0.7.11)
static bool g_appeared_once_fix = true;
COOP_FIX("notify.appeared_once", "BUG-0923", g_appeared_once_fix, "core: a partner's puppet rebuilt with its new look is not told as 'appeared in the world' again");
uint32_t g_appeared_told = 0, g_appeared_quiet = 0; // coop.status

static void silent_tick(Remote& r, double now)
{
    if (r.echo || r.buf.empty())
        return;
    double age = r.buf.age(now);
    if (!r.silent_said && age > 5.0 && g_sess.connected()) {
        r.silent_said = true;
        feed(r.name + " is not responding (game minimized or frozen?)");
    } else if (r.silent_said && age < 1.0) {
        r.silent_said = false;
        feed(r.name + " is responding again");
    }
}

// BUG-1064: in Cell a partner's pose is in its own world scale (BUG-0519); a level change rescales each world over
// ~3.5 s at its own moment, so the host's view centre for a partner (the tiles kept round it, the cull's active area,
// h_TileActive) sat off the partner's place then (by the two scales' ratio): the host's game culled the cells round
// the partner that its own view keeps (a level down: 37 % of the partner's area against 20 % of the host's own,
// lab 20261113-033642-2). Now the centre is in the host's scale, where the partner's cell is drawn.
// Old (`coop.fix cell.view_centre_scale off`): the pose as it came.
static bool g_fix_view_centre_scale = true;
COOP_FIX("cell.view_centre_scale", "BUG-1064", g_fix_view_centre_scale, "cell: the host keeps the world round a partner at the partner's place in the host's world scale while a level change rescales either world");
static net::Vec3 view_pos(const net::PlayerState& s)
{
    if (!g_fix_view_centre_scale || g_mode != modes::kCell || !(s.scale > 0))
        return s.pos;
    float own = cellsync::scale_now();
    if (!(own > 0) || !std::isfinite(own))
        return s.pos;
    float k = s.scale / own;
    return {s.pos.x * k, s.pos.y * k, s.pos.z * k};
}

// WP-K (BUG-1303): partner r is in an editor of its own while its creature puppet stands in this Creature world: the
// puppet is held in the game's cocoon (cocoon.h), true then (not driven, not hidden). An editor state of the partner in
// this world, or, once in the cocoon, a state of this world without its avatar yet (its game coming back from the
// editor). Not for the echo test remote, a dead partner or a state older than 5 s.
static bool cocoon_held(Remote& r, const net::PlayerState& s, double now)
{
    const bool editor = s.mode == modes::kEdit || s.mode == modes::kEditor;
    const bool returning = cocoon::active(r.id) && s.mode == modes::kCreature && !(s.flags & net::PF_IN_WORLD);
    if (!(editor || returning) || r.echo || !r.puppet || g_mode != modes::kCreature || !g_adapter || !g_local ||
        r.puppet_adapter != g_adapter || !creature_puppet(r.puppet_adapter) || r.puppet_world != g_world || !g_world ||
        s.world != g_world || (s.flags & net::PF_DEAD) || r.buf.age(now) >= 5.0)
        return false;
    if (!game::creature_listed(r.puppet))
        return false; // (freed behind our back: the usual path below shows it again)
    if (!cocoon::editing(r.id, r.puppet, now))
        return false;
    if (g_sess.role() == net::Role::Host) // BUG-0280: the world round the cocoon stays simulated
        views::note(r.id, game::position(r.puppet), g_mode, now);
    return true;
}

void drive_puppet(Remote& r, double now)
{
    net::PlayerState s;
    r.buf.jump_speed = g_mode == modes::kCreature ? modes::puppet_jump_speed() : 0.f; // BUG-0499
    r.buf.interp_fall = g_mode == modes::kCreature; // BUG-0680 (drive.puppet_fall: the Creature puppets' stops)
    if (!r.buf.sample(now, s))
        return;
    silent_tick(r, now);
    // WP-K (BUG-1303): a partner in an editor of its own (its state: the editor, in this world) stays where it stood, in
    // the game's cocoon; once in it, also while its game is not back in the world yet (no avatar after the editor)
    if (cocoon_held(r, s, now))
        return;
    // dead players are hidden until they respawn
    bool visible = g_adapter && g_local && (s.flags & net::PF_IN_WORLD) && !(s.flags & net::PF_DEAD) &&
                   r.buf.age(now) < 5.0 && (r.echo || same_place(s)) && now >= g_puppet_hide_until;
    // BUG-0280: the host keeps its world simulated around every partner it shows in its own place
    if (visible && g_sess.role() == net::Role::Host)
        views::note(r.id, view_pos(s), g_mode, now);
    else
        views::forget(r.id);
    if (r.puppet && !visible) // why a partner disappears (docs/stability/cell.md)
        LOG("%s hidden: flags=0x%X age=%.1f same_place=%d", r.name.c_str(), (unsigned)s.flags, r.buf.age(now),
            (int)same_place(s));
    if (r.puppet && r.puppet_adapter && r.puppet_adapter->alive && !r.puppet_adapter->alive(r.puppet)) {
        LOG("puppet of %s removed by the game itself (mode %08X, place %08X): shown again", r.name.c_str(), g_mode,
            g_world);
        r.puppet = nullptr; // removed by the game itself: show it again
        r.puppet_adapter = nullptr;
    }
    if (!visible || (r.puppet && r.puppet_adapter != g_adapter)) {
        despawn(r);
        if (!visible) {
            r.relook = false; // gone from our place: its next puppet is a real appearance
            cocoon::puppet_gone(r.id, nullptr, false); // WP-K: no cocoon for a partner not shown
            return;
        }
    }
    if (!r.puppet) {
        r.puppet = g_adapter->spawn(s, g_local);
        if (!r.puppet) {
            feed("failed to show " + r.name);
            return;
        }
        r.puppet_adapter = g_adapter;
        r.puppet_world = g_world;
        r.drv = drive::State{};
        bool again = g_appeared_once_fix && r.relook; // BUG-0923: the same partner, rebuilt with its new look
        r.relook = false;
        if (again)
            g_appeared_quiet++;
        else {
            g_appeared_told++;
            feed(r.name + " appeared in the world");
        }
        if (g_mode == modes::kCreature || g_mode == modes::kScenario) // which body the partner got here and why (support trace)
            trace::line("body", "puppet of %u '%s': species %08X here; its creation here %s, its own game reports %08X%s",
                        r.id, r.name.c_str(), game::species_instance(r.puppet),
                        trace::hex(creations::player_key(r.id, creations::kCreature).instance).c_str(), s.species,
                        creations::player_key(r.id, creations::kCreature).instance ? "" : " (none received: built from our species)");
        else if (g_mode == modes::kCell)
            trace::line("body", "puppet of %u '%s': cell model %08X here; its creation here %s", r.id, r.name.c_str(),
                        *(uint32_t*)((uint8_t*)r.puppet + 0xFC),
                        trace::hex(creations::player_key(r.id, creations::kCell).instance).c_str());
    }
    track_lift(s);
    // WP-K: back from its editor: in the cocoon until the puppet wears its new design, then it comes out
    if (cocoon::active(r.id) && creature_puppet(r.puppet_adapter) && g_mode == modes::kCreature &&
        cocoon::back(r.id, r.puppet, s.species, now))
        return;
    modes::g_drive_extrap = r.buf.last_extrap; // BUG-0388 diagnostics (coop.fleet track)
    if (!g_puppet_hold)
        g_adapter->drive(r.puppet, s, r.drv, now);
}

// ---- support trace (trace.h, docs/REPORTING.md#reporting-reading-logs) ----
double g_trace_at = -100, g_trace_body_at = -100, g_trace_stage_at = -100, g_trace_ver_at = -100;
uint32_t g_trace_mode = 0, g_trace_world = 0, g_trace_own_body = 0;
double g_trace_since = 0; // when the mode / world of the last "stage:" line began
bool g_trace_dead = false;
std::string g_trace_body_line;
std::map<uint8_t, uint8_t> g_trace_rdead; // partner id -> PF_DEAD of its last state seen here
std::map<uint8_t, uint32_t> g_trace_rspecies; // partner id -> the species its Creature state reported

const char* role_str()
{
    return g_sess.role() == net::Role::Host ? "host" : g_sess.role() == net::Role::Client ? (g_sess.connected() ? "guest" : "joining")
                                                                                          : "offline";
}

// why a partner's puppet is shown or not (the same tests as drive_puppet)
const char* puppet_why(const Remote& r, double now)
{
    if (r.buf.empty())
        return "hidden: no state received";
    const auto& s = r.buf.latest();
    if (!g_adapter || !g_local)
        return "hidden: no own avatar here";
    if (!(s.flags & net::PF_IN_WORLD))
        return "hidden: partner not in a world";
    if (s.flags & net::PF_DEAD)
        return "hidden: partner dead";
    if (r.buf.age(now) >= 5.0)
        return "hidden: state older than 5 s";
    if (s.mode != g_mode)
        return "hidden: partner in another mode";
    if (s.world != g_world || !g_world)
        return "hidden: partner in another world";
    return r.puppet ? "shown" : "hidden: not spawned (yet)";
}

// ---- the invariant monitor's fields (docs/06-testing.md#testing-monitor) ----
// The trace lines are written at the session clock's 10 s marks (every machine of a session in the same
// frame, within the clock's error of a few ms), so the lab compares a partner's puppet with that partner's
// own trace line of the same moment. Without a session clock: every 10 s of the local clock as before.
double g_trace_slot = -1e9, g_trace_slot_now = -1e9;
bool g_trace_due = false;

// the start of the mod's frame (after the stale-world check, before any message, module or puppet
// driving): is a trace due this frame, and if so each partner's puppet as the game drew it
static void tribe_air_watch(double now);
void monitor_frame_start(double now)
{
    bool ok = false;
    double st = support::session_time(&ok);
    g_trace_slot_now = ok ? std::floor(st / 10.0) : -1e9;
    g_trace_due = g_trace_at < -50 || (ok ? g_trace_slot_now != g_trace_slot : now - g_trace_at >= 10.0);
    tribe_air_watch(now);
    if (!g_trace_due || !trace::on() || !g_sess.connected())
        return;
    for (auto& [id, r] : g_remotes) {
        r.drawn_at = -1;
        void* p = r.echo ? nullptr : puppet_now(r);
        if (!p || (creature_puppet(r.puppet_adapter) && !game::creature_listed(p)))
            continue;
        r.drawn_st = net::PlayerState{};
        r.puppet_adapter->fill(p, r.drawn_st);
        r.drawn_at = now;
    }
}

// height over this game's terrain (cPlanetModel::GetHeightAt); false without a planet
static bool agl_of(const net::Vec3& p, float& h)
{
    float g = game::ground_radius(p);
    if (g <= 0)
        return false;
    h = drive::speed(p) - g;
    return std::isfinite(h);
}

// BUG-1088 (docs/re/tribe.md#tribe-member-swim): a member swimming is at the water's surface (its centre 0.6-1.9 m under
// it), over a sea bed that GetHeightAt gives as the ground: 3-28 m "over the ground" for as long as it swims, on every
// game (the guests' mirrors float on their own water) and also with no session (lab 20261113-175131, the host alone).
// The full campaign's ground gate failed on such a member (20261113-142203: #16 at radius 499.77, Cadian's water
// 499.7). Over water (the planet's water radius above the terrain there) a member's height counts over the water's
// surface, 0 at most for one in it: a member high over the water still counts. `coop.fix monitor.tribe_water off`:
// over the terrain (0.7.13).
static bool g_tribe_water_fix = true;
COOP_FIX("monitor.tribe_water", "BUG-1088", g_tribe_water_fix,
         "tests: a tribe member swimming counts as on the ground (its height over the water's surface, not the sea bed)");
static bool member_agl_of(const net::Vec3& p, float& h)
{
    if (!agl_of(p, h))
        return false;
    float w = g_tribe_water_fix ? game::water_radius() : 0.f, r = drive::speed(p);
    if (w > 1.f && r - h < w) // (the ground r - h is under the water's surface: over water)
        h = std::min(h, std::max(0.f, r - w));
    return true;
}

// Tribe: the lowest and highest of the player tribe's members of `owner` as this game draws them
// (host: its own members; guest: the mirrors, drawn before this frame's driving)
static bool tribe_agl(uint8_t owner, float& lo, float& hi)
{
    struct Acc {
        uint8_t owner;
        float lo = 1e9f, hi = -1e9f;
        int n = 0;
    } a{owner};
    tribe::for_each_tracked(
        [](const tribe::TrackMember& m, void* u) {
            auto* a = (Acc*)u;
            float h;
            if (m.owner != a->owner || !member_agl_of(m.drawn ? *m.drawn : game::position(m.c), h))
                return;
            a->lo = std::min(a->lo, h);
            a->hi = std::max(a->hi, h);
            a->n++;
        },
        &a);
    lo = a.lo;
    hi = a.hi;
    return a.n > 0;
}

// BUG-0945: which member is over the ground, for how long, and what it does there. The trace's agl_hi named no
// member and no duration: the monitor's "tribe members 5.62 m over the ground on 2 lines" of a campaign could not be
// told from its logs (a member hanging in the air, or members tossed by blows at two samples in a row: a hard blow
// throws a member 3.4-3.7 m up for about 2 s on the host, docs/re/tribe.md#tribe-member-toss). 4 times a second:
// every tracked member more than 3 m over the terrain has an air time (since when without a break, the highest
// point); a line when it starts (the node, the speed, the height over the game's own surface: a rock or a mound over
// the terrain is "on something") and one when it is down again. The trace gives the longest running air time of
// the owner's members as agl_held=<s> next to agl_hi: the monitor's rule is about a member that stays up.
struct AirTime {
    double since, last;
    float top;
    uint8_t owner;
};
static std::unordered_map<uint16_t, AirTime> g_air;
static constexpr float kAirOver = 3.f;
// BUG-1104: the monitor's ground rule found partner 1's members 1.26 m under the host's ground for 20 s in the 0.7.14
// final check's campaign and nothing named the member (lab 20261114-005006). The same watch: a member more than 0.5 m
// under (the rule's limit) is logged once with its node, speed, and its height over the game's surface and the water.
static std::unordered_map<uint16_t, AirTime> g_under;
static constexpr float kUnderAt = -0.5f;
static void tribe_air_watch(double now)
{
    static double next = 0;
    if (g_mode != modes::kTribe || !g_sess.connected()) {
        g_air.clear();
        g_under.clear();
        return;
    }
    if (now < next)
        return;
    next = now + 0.25;
    struct Acc {
        double now;
    } a{now};
    tribe::for_each_tracked(
        [](const tribe::TrackMember& m, void* u) {
            auto* a = (Acc*)u;
            net::Vec3 p = m.drawn ? *m.drawn : game::position(m.c);
            float h;
            if (!member_agl_of(p, h))
                return;
            if (h < kUnderAt) { // BUG-1104: name a member under the ground too (once per stay)
                auto u = g_under.find(m.id);
                if (u != g_under.end()) {
                    u->second.last = a->now;
                    u->second.top = std::min(u->second.top, h);
                    return;
                }
                g_under[m.id] = {a->now, a->now, h, m.owner};
                float sr = game::surface_radius(p), r = drive::speed(p), w = game::water_radius();
                net::Vec3 v = game::velocity(m.c);
                H->log(M, SK_INFO, "tribe: member #%u of player %u is %.2f m under the ground at (%.1f %.1f %.1f): %s speed=%.1f "
                       "(up %.1f) over the game's surface %.2f, over the water %.2f",
                       (unsigned)m.id, (unsigned)m.owner, -h, p.x, p.y, p.z, tribe::member_brief(m.c).c_str(), drive::speed(v),
                       r > 1 ? (v.x * p.x + v.y * p.y + v.z * p.z) / r : 0.f, sr > 1.f ? r - sr : -999.f, w > 1.f ? r - w : -999.f);
                return;
            }
            if (h <= kAirOver)
                return;
            auto it = g_air.find(m.id);
            if (it != g_air.end()) {
                it->second.last = a->now;
                it->second.top = std::max(it->second.top, h);
                return;
            }
            g_air[m.id] = {a->now, a->now, h, m.owner};
            float sr = game::surface_radius(p), r = drive::speed(p);
            net::Vec3 v = game::velocity(m.c);
            H->log(M, SK_INFO, "tribe: member #%u of player %u is %.2f m over the ground at (%.1f %.1f %.1f): %s speed=%.1f "
                   "(up %.1f) over the game's surface %.2f",
                   (unsigned)m.id, (unsigned)m.owner, h, p.x, p.y, p.z, tribe::member_brief(m.c).c_str(), drive::speed(v),
                   r > 1 ? (v.x * p.x + v.y * p.y + v.z * p.z) / r : 0.f, sr > 1.f ? r - sr : -999.f);
        },
        &a);
    for (auto it = g_under.begin(); it != g_under.end();) {
        if (it->second.last >= now) {
            ++it;
            continue;
        }
        H->log(M, SK_INFO, "tribe: member #%u of player %u is on the ground again (or gone) after %.1f s under it, lowest %.2f m",
               (unsigned)it->first, (unsigned)it->second.owner, it->second.last - it->second.since + 0.25, -it->second.top);
        it = g_under.erase(it);
    }
    for (auto it = g_air.begin(); it != g_air.end();) {
        if (it->second.last >= now) {
            ++it;
            continue;
        }
        H->log(M, SK_INFO, "tribe: member #%u of player %u is down again (or gone) after %.1f s over %.0f m, highest %.2f m",
               (unsigned)it->first, (unsigned)it->second.owner, it->second.last - it->second.since + 0.25, kAirOver,
               it->second.top);
        it = g_air.erase(it);
    }
}
// the longest running air time of the owner's members, 0: none is up
static double tribe_air_held(uint8_t owner, double now)
{
    double held = 0;
    for (auto& [id, at] : g_air)
        if (at.owner == owner)
            held = std::max(held, now - at.since + 0.25);
    return held;
}

// " agl=<m>" (Creature: the creature; Tribe: " agl=<lowest> agl_hi=<highest>" of the player's members)
static std::string agl_field(uint8_t owner, void* creature, const net::Vec3* at)
{
    char b[64];
    float lo, hi;
    if (g_mode == modes::kCreature && creature && at && agl_of(*at, lo)) {
        snprintf(b, sizeof b, " agl=%.2f", lo);
        return b;
    }
    if (g_mode == modes::kTribe && tribe_agl(owner, lo, hi)) {
        int n = snprintf(b, sizeof b, " agl=%.2f agl_hi=%.2f", lo, hi);
        if (hi > kAirOver) // BUG-0945: for how long a member has been up without a break (0.0: it came up just now)
            snprintf(b + n, sizeof b - n, " agl_held=%.1f", tribe_air_held(owner, H->time_seconds()));
        return b;
    }
    return "";
}

// BUG-0368 class: the unlocked creature parts of the cPlayer's collected parts (cCollectableItems,
// docs/re/creations.md#creations-own-editor-items): over the unlockable items (the map at +0x1490),
// the number unlocked and an order-free hash of their keys. `locked` gets the keys still locked.
bool parts_census(int& unlocked, uint32_t& hash, int& unlockable, std::vector<std::pair<uint32_t, uint32_t>>* locked)
{
    return parts::census(unlocked, hash, unlockable, locked); // BUG-0401: parts.cpp holds it
}

// the partner's speed as this game sees it: its reported speed, or (the RTS pointer reports none) the
// way its interpolated position went in the last 0.5 s
static float seen_speed(const Remote& r, double now)
{
    net::PlayerState a, b;
    if (!r.buf.sample(now, a) || !r.buf.sample(now - 0.5, b))
        return 0;
    float v = a.mode == b.mode && a.world == b.world ? net::dist(a.pos, b.pos) / 0.5f : 0.f;
    return std::max(v, drive::speed(a.vel));
}

// the monitor's fields at the end of the own trace line
static std::string monitor_own_fields(bool have)
{
    std::string out;
    if (!have)
        return out;
    net::Vec3 p = g_local && g_mode == modes::kCreature ? game::position(g_local) : net::Vec3{};
    out += agl_field(g_sess.my_id(), g_mode == modes::kCreature ? g_local : nullptr, &p);
    if (g_mode == modes::kCreature) {
        int n, all;
        uint32_t h;
        if (parts_census(n, h, all)) {
            char b[48];
            snprintf(b, sizeof b, " parts=%d:%08X", n, h);
            out += b;
        }
        // BUG-1310 (own DNA, WP-D): the evolution bar (the party's on every game) and, on the host, the record of each
        // guest's own wallet (what that guest's dna= must be): the monitor's totals compare with them
        float bar = game::evolution_bar();
        if (std::isfinite(bar)) {
            char b[32];
            snprintf(b, sizeof b, " bar=%.0f", bar);
            out += b;
        }
        if (own_progress::active())
            out += " own_progress=1";
        else if (g_sess.role() == net::Role::Host)
            out += dna_own_field();
    }
    if (g_mode == modes::kTribe || g_mode == modes::kCiv) {
        // BUG-0416: how long the own pointer has stood still (its partner's copy is ~0.1 s + half a round
        // trip behind a pointer that just jumped: the monitor's drawn rule judges only a resting one)
        double still = modes::rts_marker_still(g_adapter, local_now());
        if (still >= 0) {
            char b[32];
            snprintf(b, sizeof b, " ptr_still=%.1f", std::min(still, 999.0));
            out += b;
        }
    }
    if (g_mode == modes::kCell) {
        // BUG-0681: the game's world scale its Cell positions (own pos=, the partners' drawn=) are in: during a
        // level change each game rescales at its own moment, the monitor's drawn rule compares in one scale
        float k = cellsync::scale_now();
        if (k > 0) {
            char b[32];
            snprintf(b, sizeof b, " wscale=%.6g", k);
            out += b;
        }
    }
    out += npc::monitor_fields();
    return out;
}

// the monitor's fields at the end of a partner's trace line
static std::string monitor_partner_fields(uint8_t id, const Remote& r, double now)
{
    std::string out;
    char b[160];
    bool drawn = r.drawn_at == now && strcmp(puppet_why(r, now), "shown") == 0;
    if (drawn) {
        const net::Vec3& d = r.drawn_st.pos;
        snprintf(b, sizeof b, " drawn=(%.1f %.1f %.1f) drawn_v=%.1f", d.x, d.y, d.z, seen_speed(r, now));
        out += b;
    }
    bool creature = drawn && creature_puppet(r.puppet_adapter) && g_mode == modes::kCreature;
    out += agl_field(id, creature ? r.puppet : nullptr, creature ? &r.drawn_st.pos : nullptr);
    if (creature && r.drawn_st.max_health > 0) { // read from the puppet (BUG-0373: the partner's health)
        snprintf(b, sizeof b, " puppet_hp=%.0f/%.0f", r.drawn_st.health, r.drawn_st.max_health);
        out += b;
    }
    return out;
}

// the first line of a module's status()
std::string first_line(const std::string& s) { return s.substr(0, s.find('\n')); }

void trace_tick(double now)
{
    if (!trace::on())
        return;
    trace::tick(now);
    net::PlayerState st = local_state();
    bool have = g_local && local_now();
    // own avatar: death and its body (Creature: species; Cell: the cell model)
    bool dead = have && (st.flags & net::PF_DEAD);
    if (have && dead != g_trace_dead)
        trace::line("ko", "own avatar %s (hp %.0f/%.0f, %s)", dead ? "dead / knocked out" : "alive again", st.health,
                    st.max_health, modes::mode_name(g_mode));
    g_trace_dead = dead;
    uint32_t body = !have ? 0 : g_mode == modes::kCreature ? st.species : g_mode == modes::kCell ? cellsync::own_model() : 0;
    if (body && body != g_trace_own_body && g_sess.connected()) {
        trace::line("body", "own avatar %s %s -> %08X: %s", g_mode == modes::kCreature ? "species" : "cell model",
                    trace::hex(g_trace_own_body).c_str(), body,
                    creations::trace_line(g_mode == modes::kCreature ? creations::kCreature : creations::kCell).c_str());
        g_trace_body_at = now;
    }
    if (body)
        g_trace_own_body = body;
    for (auto& [id, r] : g_remotes) {
        if (r.echo || r.buf.empty())
            continue;
        uint8_t d = r.buf.latest().flags & net::PF_DEAD;
        auto it = g_trace_rdead.find(id);
        if (it != g_trace_rdead.end() && it->second != d)
            trace::line("ko", "player %u '%s' %s (state of its own game)", id, r.name.c_str(), d ? "dead / knocked out" : "alive again");
        g_trace_rdead[id] = d;
        // the body a partner's own game reports (Creature): a change after its editor or its own creation
        const auto& ls = r.buf.latest();
        if (ls.mode == modes::kCreature && ls.species && g_trace_rspecies[id] != ls.species) {
            void* pp = puppet_now(r);
            trace::line("body", "player %u '%s' reports species %s -> %08X; its creation here %s, its puppet here %s", id,
                        r.name.c_str(), trace::hex(g_trace_rspecies[id]).c_str(), ls.species,
                        trace::hex(creations::player_key(id, creations::kCreature).instance).c_str(),
                        pp && g_mode == modes::kCreature ? trace::hex(game::species_instance(pp)).c_str() : "-");
            g_trace_rspecies[id] = ls.species;
        }
    }
    bool changed = g_mode != g_trace_mode || g_world != g_trace_world;
    if (changed) {
        trace::line("stage", "%s %s -> %s %s '%s' (%.1f s in the last)", modes::mode_name(g_trace_mode),
                    trace::hex(g_trace_world).c_str(), modes::mode_name(g_mode), trace::hex(g_world).c_str(),
                    modes::current_world_name().c_str(), g_trace_since > 0 ? now - g_trace_since : 0.0);
        g_trace_mode = g_mode;
        g_trace_world = g_world;
        g_trace_since = now;
        g_trace_body_at = g_trace_stage_at = -100; // the stage lines with the next trace
    }
    if (!g_sess.connected() || (!changed && !g_trace_due))
        return;
    g_trace_at = now;
    if (g_trace_due)
        g_trace_slot = g_trace_slot_now;
    char b[700];
    std::string res = shared::brief();
    snprintf(b, sizeof b,
             "%s id=%u players=%d %s world=%s '%s' avatar=%s pos=(%.1f %.1f %.1f) hp=%.0f/%.0f species=%s flags=0x%X "
             "dna=%.0f%s%s host_away=%d vote=%s input_lock=%d editor_pending=%s",
             role_str(), g_sess.my_id(), (int)g_sess.players().size(), modes::mode_name(g_mode), trace::hex(g_world).c_str(),
             modes::current_world_name().c_str(), have ? "yes" : "no", st.pos.x, st.pos.y, st.pos.z, st.health,
             st.max_health, g_mode == modes::kCell ? "-" : trace::hex(st.species).c_str(), (unsigned)st.flags,
             game::avatar() ? game::dna() : -1.f, res.empty() ? "" : " ", res.c_str(), (int)g_host_away,
             g_vote.active ? "on" : "off", coop_ui::input_lock_now(), trace::hex(g_own_editor_due).c_str());
    if (const char* why = share_wait_reason()) // BUG-0382 (new fields go at the end: monitor.py)
        snprintf(b + strlen(b), sizeof b - strlen(b), " share_wait=%s", why);
    if (unsigned gb = game_busy()) // BUG-0404: the busy gate (census / sync checks skip a game that stands)
        snprintf(b + strlen(b), sizeof b - strlen(b), " busy=%s", busy::names(gb).c_str());
    snprintf(b + strlen(b), sizeof b - strlen(b), "%s", monitor_own_fields(have).c_str()); // (before version=)
    trace::line("trace", "%s%s%s", b, now - g_trace_ver_at > 60.0 ? " version=" : "",
                now - g_trace_ver_at > 60.0 ? g_sess.version.c_str() : "");
    float host_d = -1;
    for (auto& [id, r] : g_remotes) {
        if (r.echo)
            continue;
        net::Session::Link lk;
        bool link = g_sess.link_of(id, lk);
        int rtt = link ? lk.rtt_ms : g_lobby_rtt.count(id) ? g_lobby_rtt[id] : -1;
        if (r.buf.empty()) {
            trace::line("trace", "  partner %u '%s': no state yet, rtt=%d", id, r.name.c_str(), rtt);
            continue;
        }
        const auto& s = r.buf.latest();
        float d = net::dist(s.pos, st.pos);
        bool here = same_place(s);
        if (id == 0 && here)
            host_d = d;
        snprintf(b, sizeof b,
                 "  partner %u '%s' %s world=%s d=%s same_place=%d puppet=%s hp=%.0f/%.0f species=%s flags=0x%X "
                 "state_age=%.1f s rtt=%d ms",
                 id, r.name.c_str(), modes::mode_name(s.mode), trace::hex(s.world).c_str(),
                 here ? std::to_string((int)d).c_str() : "-", (int)here, puppet_why(r, now), s.health, s.max_health,
                 s.mode == modes::kCell ? "-" : trace::hex(s.species).c_str(), (unsigned)s.flags, r.buf.age(now), rtt);
        std::string l = b;
        if (now - g_trace_ver_at > 60.0) // the versions once a minute (they do not change in a session)
            for (auto& pl : g_sess.players())
                if (pl.id == id)
                    l += " version=" + (pl.version.empty() ? std::string("unknown (pre-0.7.4)") : pl.version);
        if (link) {
            snprintf(b, sizeof b, " resends=%u/%u silent=%u ms pending=%u", lk.resends, lk.sent, lk.silent_ms, lk.pending);
            l += b;
        }
        l += monitor_partner_fields(id, r, now);
        trace::line("trace", "%s", l.c_str());
    }
    if (now - g_trace_ver_at > 60.0)
        g_trace_ver_at = now;
    if (!have)
        return;
    if (g_mode == modes::kCell) {
        std::string c = cellsync::trace_line();
        if (g_sess.role() == net::Role::Client) {
            // the host's game populates only about one screen around its own cell (BUG-0174)
            snprintf(b, sizeof b, " | host d=%s%s", host_d < 0 ? "-" : std::to_string((int)host_d).c_str(),
                     host_d < 0 ? " (not here)" : host_d > 40 ? " (outside the host's populated area: little or no food)" : "");
            c += b;
        }
        trace::line("cell", "%s", c.c_str());
    } else if (g_mode == modes::kCreature) {
        std::string c = npc::trace_line(st.pos, 60.f);
        net::Vec3 nest;
        if (game::nest_position(g_local, nest)) {
            snprintf(b, sizeof b, ", nest d=%.0f m", net::dist(nest, st.pos));
            c += b;
        } else
            c += ", no nest";
        trace::line("creature", "%s", c.c_str());
    }
    bool body_stage = g_mode == modes::kCreature || g_mode == modes::kCell;
    if (body_stage) {
        std::string bl = creations::trace_line(g_mode == modes::kCreature ? creations::kCreature : creations::kCell);
        if (bl != g_trace_body_line || now - g_trace_body_at > 60.0) {
            trace::line("body", "%s", bl.c_str());
            g_trace_body_line = bl;
            g_trace_body_at = now;
        }
    }
    if (now - g_trace_stage_at > 30.0) {
        g_trace_stage_at = now;
        if (g_mode == modes::kTribe)
            trace::line("tribe", "%s", first_line(tribe::status()).c_str());
        else if (g_mode == modes::kCiv)
            trace::line("civ", "%s", first_line(civ::status()).c_str());
        else if (g_mode == modes::kSpace) {
            trace::line("space", "%s", first_line(squadron::status()).c_str());
            trace::line("space", "%s", first_line(spacecoop::status()).c_str());
        }
    }
}

void on_anim(void* creature, uint32_t anim, int block, bool disable_blend, bool to_target)
{
    void* av = game::avatar();
    if (!av || creature != av)
        return;
    if (g_anim_log)
        LOG("avatar anim 0x%08X block=%d%s%s", anim, block, disable_blend ? " noblend" : "", to_target ? " (to)" : "");
    net::AnimEvent ev;
    ev.id = g_sess.my_id();
    ev.anim = anim;
    ev.block = block;
    ev.flags = (disable_blend ? 1 : 0) | (to_target ? 2 : 0);
    g_own_anims++;
    g_own_anim_last = anim;
    if (g_sess.connected()) {
        net::Writer w;
        ev.write(w);
        g_sess.send_event(net::MSG_ANIM, w);
    }
    if (g_echo) { // loopback test: replay on the echo puppet
        auto it = g_remotes.find(200);
        if (it != g_remotes.end() && it->second.puppet)
            game::play_animation(it->second.puppet, anim, block, disable_blend);
    }
}

// BUG-1253 (players' video): the guest ate a body and its creature stood idle next to it in the host's game. The
// avatar's behaviour tree (the eat node after the player's click, ...) starts its animations with
// SporeCoop::CreaturePlayAnim, not with PlayAnimation, so on_anim never saw them (only the NPCs' were relayed, BUG-0464).
// Now the avatar's own CreaturePlayAnim plays outside its idle refresh go out as MSG_ANIM with flag 4 (its `a` in flag
// 8, its `b` in block) and the partner plays the same call on the puppet. `coop.fix creature.avatar_bhv_anims off`:
// not sent (0.7.15).
static bool g_avatar_bhv_fix = true;
COOP_FIX("creature.avatar_bhv_anims", "BUG-1253", g_avatar_bhv_fix, "creature: what a player's creature does by itself after a click (eating a body, ...) is shown on the partner's screen");
uint32_t g_own_bhv_anims = 0, g_bhv_anims_played = 0; // tests (coop.status / coop.track)
void on_own_behaviour_anim(void* creature, uint32_t anim, int a, int b)
{
    if (!g_avatar_bhv_fix || !creature || creature != game::avatar() || !game::in_creature_stage())
        return;
    if (g_anim_log)
        LOG("avatar behaviour anim 0x%08X a=%d b=%d", anim, a, b);
    net::AnimEvent ev;
    ev.id = g_sess.my_id();
    ev.anim = anim;
    ev.block = b;
    ev.flags = 4 | ((a & 0xFF) ? 8 : 0);
    g_own_bhv_anims++;
    g_own_anims++;
    g_own_anim_last = anim;
    if (g_sess.connected()) {
        net::Writer w;
        ev.write(w);
        g_sess.send_event(net::MSG_ANIM, w);
    }
}

uint32_t g_anims_rx = 0, g_anims_played = 0;
uint32_t g_anims_cocooned = 0; // WP-K: a partner's animations not played while its puppet is in the cocoon

static bool g_anim_creature_only_fix = true;
COOP_FIX("core.anim_creature_only", "BUG-0796", g_anim_creature_only_fix, "creature: a partner's animation is never played on its cell or its Tribe marker around a stage switch (the game read a creature's animation data there)");
uint32_t g_anims_not_creature = 0;
void on_remote_anim(const net::AnimEvent& ev)
{
    g_anims_rx++;
    if (g_anim_log)
        LOG("remote %u anim 0x%08X", ev.id, ev.anim);
    auto it = g_remotes.find(ev.id);
    if (it == g_remotes.end() || !it->second.puppet)
        return;
    // BUG-0796: a partner's animation is a creature's (cCreatureBase::PlayAnimation reads the animated creature at
    // +0xB54): around a stage switch the puppet can be a Cell cell (a pool slot) or a Tribe marker (a heap object of
    // 40 bytes), and the reliable MSG_ANIM still comes. Dropped unless the puppet is a live Creature one.
    // Old (`coop.fix core.anim_creature_only off`): only a creature puppet was checked, any other one was played on.
    if (g_anim_creature_only_fix && !creature_puppet(it->second.puppet_adapter)) {
        g_anims_not_creature++;
        return;
    }
    if (creature_puppet(it->second.puppet_adapter) && !game::creature_listed(it->second.puppet)) {
        if (g_frame_fix) { // freed by a teardown the mod has not seen yet (BUG-0091)
            g_anims_dropped++;
            return;
        }
        g_anims_unlisted++;
    }
    if (cocoon::active(ev.id)) { // WP-K (BUG-1303): in the cocoon its pose is the cocoon's until it comes out
        g_anims_cocooned++;
        return;
    }
    if (ev.flags & 4) { // BUG-1253: the partner's behaviour play, the same call (CreaturePlayAnim(anim, a, b))
        game::play_behaviour_anim(it->second.puppet, ev.anim, 0, (ev.flags & 8) ? 1 : 0, ev.block);
        g_bhv_anims_played++;
    } else
        game::play_animation(it->second.puppet, ev.anim, ev.block, ev.flags & 1);
    g_anims_played++;
    it->second.anims++;
    it->second.anim_last = ev.anim;
}

} // namespace coop_core
