#include "nest.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/interp.h"
#include "net/protocol.h"
#include "savestrip.h"
#include "sporekit_game.hpp"

namespace nest {

using namespace skg;

static const SkHost* H;
static SkMod* M;
static Ctx C;

// nouns (RegisterNounType in 0xB24720, GOG 2024; docs/re/creature.md#creature-nest-eggs)
constexpr uint32_t kEggNoun = 0x02A034CD;   // cEgg (size 0x228, ctor 0xC6A410)
constexpr uint32_t kFruitNoun = 0x02C9CC91; // cFruit
constexpr uint32_t kNestNoun = 0x52AA6122;  // cNest
// cEgg fields (attribute table 0x134CF40)
constexpr uintptr_t kEggState = 0x1E8, kEggHatch = 0x1F0, kEggHerd = 0x1F4, kEggToHatch = 0x218;

static bool over_ground(const net::Vec3& q, float& og)
{
    float rad = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), gr = game::ground_radius(q);
    og = gr > 1 ? rad - gr : -999.f;
    return gr > 1;
}


// ---------------------------------------------------------------- BUG-0384
// The eggs at another species' nest are cEgg objects (noun 0x2A034CD) of the nest's herd (cHerd mEggs
// +0x54): a herd that misses members picks one of them as the egg layer (cHerd::Update 0xC6D960,
// +0x194, its eggs to lay +0x1678), the layer lays them (behaviour node 0xD6CDB0 -> cHerd::LayEgg
// 0xC6D0B0, state 6), they hatch into new members after 2.5-3.5 min (0xC6B1E0, state 3) or are eaten
// (the eat node 0xDBD460 -> cEgg::SetState(4)). Every game runs this for its own herds; on a guest the
// mod removes its own NPCs, so the guest's nest herds have no members, no layer and no eggs: the
// host's eggs were missing on the guest (the guest's own eggs, while it simulated its NPCs itself, were
// never on the host). Now the host sends the eggs of the NPC nests near each guest (1 Hz, positions,
// states) and the guest shows them as eggs of its own (mirrors: no herd, never hatch, never saved),
// on the guest's own ground; a mirror the guest eats is eaten on the host.
// `coop.fix nest.eggs off`: each game keeps its own eggs (0.7.3.3).
static bool g_eggs_fix = true;
COOP_FIX("nest.eggs", "BUG-0384", g_eggs_fix, "creature: the host's eggs at the NPC nests are shown and eaten on the guests");

// BUG-0896: the host sent its eggs once a second (unreliable). An egg that hatched on the host stood whole on a guest for
// up to a second while its hatchling (a small creature, half health) already stood there as an NPC: a guest's click
// or bite at the egg took the hatchling, killed it at once, and the host credited the guest's hunt (+4 DNA, a body at
// the egg's place). With nest.hatch_now the host looks at the eggs of its last snapshot every frame and sends a
// snapshot at once when one of them hatched or was eaten; a guest's eat of an egg that is no longer whole is answered
// with a snapshot at once too. Off: once a second only (0.7.12).
static bool g_hatch_now_fix = true;
COOP_FIX("nest.hatch_now", "BUG-0896", g_hatch_now_fix, "creature: an NPC egg that hatches or is eaten on the host is shown so on the guests at once, not up to a second later");

// BUG-0474: the animal nests of the Tribe stage are the same cHerd / cEgg objects, and the guest's game removes its
// own animals there too (tribe proxies): the host's eggs were missing on every guest. The mirrors run in Tribe too.
// `coop.fix nest.eggs_tribe off`: Creature only (0.7.3.6).
static bool g_tribe_fix = true;
COOP_FIX("nest.eggs_tribe", "BUG-0474", g_tribe_fix, "tribe: the host's eggs at the animal nests are shown and eaten on the guests");

// BUG-0660 (docs/re/creature.md#creature-family-eggs): the players' own nest is the avatar family's herd (+0x84 set),
// left out of nest.eggs: its members are NPCs too (the guest's are the host's, mirrored, with no AI), so only the host's
// family lays eggs (a family member missing: cHerd::Update's layer) and they were the host's alone (the detector's
// `Creature:noun_only_host:egg` / `host_only:egg` at the players' nest for minutes). With nest.eggs_family the host sends
// the family's eggs too (the state byte | kFamilyBit: older guests skip them); a guest binds its own family egg within
// kOwnBind of a host egg (both games load the save's) and follows its hatch / eat, and shows the others as mirrors of
// its own family herd. Off: the family's eggs are each game's own (0.7.10).
static bool g_family_fix = true;
COOP_FIX("nest.eggs_family", "BUG-0660", g_family_fix, "creature: the eggs in the players' own nest are the host's in every game");
constexpr uint8_t kFamilyBit = 0x80;
constexpr float kOwnBind = 4.f, kOwnDrift = 0.5f;
static uint32_t g_family_tx, g_own_bound, g_family_made, g_own_placed;

static bool egg_stage() { return game::in_creature_stage() || (g_tribe_fix && game::mode_id() == modes::kTribe); }

constexpr uint8_t kKindSnap = 0, kKindEat = 1;
constexpr int kMaxSnap = 20;           // eggs per snapshot (49 bytes each, one packet)
constexpr float kEggRelevance = 110.f; // m around a guest (the NPCs' relevance)
// Tribe (BUG-0474): a player is a pointer that roams the small map; the camera sees the nests far from it (lane 9:
// 3 of 4 eggs farther than 110 m from the guest's pointer were missing on it)
constexpr float kEggRelevanceTribe = 400.f;
constexpr double kSnapInterval = 1.0, kStale = 3.5;

static uintptr_t a_NounGet, a_CreateInstance, a_DestroyInstance, a_SetState, a_Snap, a_Surface, a_PlanetGet;

// the surface under / over q on its radial line, as cEgg_SnapToGround finds it (the terrain or a collision
// object over it: the nest mound, a rock; only where the collision is loaded, near the camera)
static bool surface_at(const net::Vec3& q, net::Vec3& s)
{
    void* pm = a_PlanetGet ? cdecl_call<void*>(a_PlanetGet) : nullptr;
    if (!pm || !a_Surface || !(q.x * q.x + q.y * q.y + q.z * q.z > 1.f))
        return false;
    float in[3] = {q.x, q.y, q.z}, o[3] = {0, 0, 0};
    thiscall<float*>(a_Surface, pm, o, (const float*)in, 0);
    s = {o[0], o[1], o[2]};
    return s.x * s.x + s.y * s.y + s.z * s.z > 1.f;
}

// how far q is over the surface (negative: under it, e.g. inside the nest mound)
static bool over_surface(const net::Vec3& q, float& os)
{
    net::Vec3 s;
    if (!surface_at(q, s))
        return false;
    os = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z) - std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    return true;
}

struct HostEgg {
    uint32_t id;
    net::Vec3 pos;
    uint8_t state;
    float scale;
    net::Vec3 home; // its herd's home (+0x34 mInitialPosition, the same on every game: the save's)
    bool family = false; // BUG-0660: an egg of the avatar family's herd
};

// host: the eggs of the NPC herds (not the avatar's family eggs, not ours)
static std::vector<std::pair<void*, HostEgg>> host_eggs()
{
    std::vector<std::pair<void*, HostEgg>> v;
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* v = (std::vector<std::pair<void*, HostEgg>>*)p;
            if (noun != kEggNoun || !game::object_alive(o) || IsBadReadPtr(o, 0x228))
                return true;
            void* herd = field<void*>(o, kEggHerd);
            if (!herd || IsBadReadPtr(herd, 0x220))
                return true;
            bool family = field<uint8_t>(herd, 0x84) != 0; // avatar-owned: the family's eggs (BUG-0660)
            if (family && !g_family_fix)
                return true;
            HostEgg e{};
            if (!game::object_position(o, e.pos))
                return true;
            e.id = game::object_serial(o);
            e.state = (uint8_t)field<int>(o, kEggState);
            e.scale = field<float>(o, 0x220);
            const float* hp = &field<float>(herd, 0x34);
            e.home = {hp[0], hp[1], hp[2]};
            e.family = family;
            v->push_back({o, e});
            return true;
        },
        &v);
    return v;
}

struct Mirror {
    void* egg = nullptr;
    void* herd = nullptr; // our reference in the egg's mpHerd +0x1F4 (the egg view draws an egg of an enabled herd only)
    uint32_t serial = 0;
    uint8_t state = 0; // the state we set (a change of the game's = eaten here)
    net::Vec3 pos{};
    double seen = 0;
};
static std::map<uint32_t, Mirror> g_mirrors; // host egg id -> our egg
// BUG-0660: guest, host family egg id -> this game's own family egg bound to it
struct OwnEgg {
    void* egg;
    uint32_t serial;
    bool placed = false;
    net::Vec3 host{}, after{}; // the host's place it was put at, where it lay after the snap
};
static std::map<uint32_t, OwnEgg> g_own; // host egg id -> this game's own family egg
static uint32_t g_epoch = 0;
static double g_last_snap_tx = 0, g_last_snap_rx = -100;
static uint32_t g_made = 0, g_removed = 0, g_eats_sent = 0, g_eats_applied = 0, g_snaps_rx = 0, g_snaps_tx = 0;

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

static void* noun_mgr() { return a_NounGet ? cdecl_call<void*>(a_NounGet) : nullptr; }

// our egg is still the object we made (same world, not destroyed by the game, same serial)
static bool mirror_ok(const Mirror& m)
{
    return m.egg && g_epoch == game::world_epoch() && game::object_alive(m.egg) && game::object_serial(m.egg) == m.serial;
}

static void place_mirror(Mirror& m, const net::Vec3& p)
{
    float v[3] = {p.x, p.y, p.z};
    vcall<void>((uint8_t*)m.egg + 0x34, 0x38 / 4, (const float*)v); // cSpatialObject::SetPosition
    cdecl_call<void>(a_Snap, m.egg);                                // onto this game's ground, upright
    m.pos = p;
}

// guest: its own herd of the host's nest (the same home: both games have the save's herds)
static void* local_herd(const net::Vec3& home, bool family = false)
{
    struct F {
        net::Vec3 home;
        void* found;
        bool family;
    } f{home, nullptr, family};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* f = (F*)p;
            if (noun != game::kHerdNoun || !game::object_alive(o) || IsBadReadPtr(o, 0x220) || (field<uint8_t>(o, 0x84) != 0) != f->family)
                return true;
            const float* hp = &field<float>(o, 0x34);
            if (net::dist(net::Vec3{hp[0], hp[1], hp[2]}, f->home) > 1.f)
                return true;
            f->found = o;
            return false;
        },
        &f);
    return f.found;
}

static void* make_mirror(const HostEgg& e, Mirror& m)
{
    void* herd = local_herd(e.home, e.family);
    if (!herd)
        return nullptr; // no such nest here: an egg would not be drawn
    void* nm = noun_mgr();
    void* o = nm && a_CreateInstance ? thiscall<void*>(a_CreateInstance, nm, kEggNoun) : nullptr;
    if (!o || game::noun_id(o) != kEggNoun)
        return nullptr;
    vcall<void>(o, 0); // AddRef: ours until remove_mirror
    m.egg = o;
    m.serial = game::object_serial(o);
    vcall<void>(herd, 0); // AddRef, as cHerd::LayEgg does; not in its mEggs: its update never hatches or clears ours
    field<void*>(o, kEggHerd) = herd;
    m.herd = herd;
    field<int>(o, kEggHatch) = 0; // no hatch timer (only a herd's update hatches, and it is in no herd)
    field<int>(o, kEggState) = e.state == 3 || e.state == 4 ? 6 : e.state;
    field<uint8_t>(o, 0x1EC) = 1;
    if (e.scale > 0.01f && e.scale < 100.f) {
        field<float>(o, 0x220) = e.scale;
        vcall<void>((uint8_t*)o + 0x34, 0x40 / 4, e.scale); // SetScale
    }
    place_mirror(m, e.pos);
    if (e.state == 3 || e.state == 4)
        thiscall<void>(a_SetState, o, (int)e.state);
    m.state = e.state;
    g_made++;
    g_family_made += e.family;
    return o;
}

static void remove_mirror(Mirror& m)
{
    if (mirror_ok(m)) {
        if (m.herd && field<void*>(m.egg, kEggHerd) == m.herd) { // as cHerd::ResizeEggs 0xC6E8B0 before DestroyInstance
            field<void*>(m.egg, kEggHerd) = nullptr;
            vcall<void>(m.herd, 1);
        }
        if (void* nm = noun_mgr())
            thiscall<void>(a_DestroyInstance, nm, m.egg);
        vcall<void>(m.egg, 1); // Release our reference
    }
    // another world: the game freed it with the world, the pointer is not ours to touch
    m.egg = nullptr;
    g_removed++;
}

// BUG-0833: the world counter (game::world_epoch) also goes up when the avatar is replaced inside the stage (the own
// creature chosen, an editor's new body): the egg mirrors were only forgotten then, while they were still eggs of the
// guest's world, and the next snapshot made them again (two eggs per host egg, more after every change; the old ones
// never hatch, are never removed, hold our references and their herds'). A world load frees the world's objects but
// not the blocks we hold a reference to. Now a mirror of an older epoch is removed the normal way while the game's list
// still has it (same serial), and only released (our two references) when the game destroyed it with its world.
// `coop.fix nest.mirror_epoch off`: forgotten (0.7.11).
static bool g_mirror_epoch_fix = true;
COOP_FIX("nest.mirror_epoch", "BUG-0833", g_mirror_epoch_fix, "creature: a guest's egg mirrors are removed (not left behind) when its avatar or world changes");
static uint32_t g_epoch_removed = 0, g_epoch_released = 0;
static void drop_old_mirror(Mirror& m)
{
    if (!g_mirror_epoch_fix || !m.egg) {
        m.egg = nullptr;
        return;
    }
    bool listed = game::listed(m.egg, kEggNoun) && game::object_serial(m.egg) == m.serial;
    if (listed) { // still an egg of this world: as remove_mirror
        if (m.herd && field<void*>(m.egg, kEggHerd) == m.herd) {
            field<void*>(m.egg, kEggHerd) = nullptr;
            vcall<void>(m.herd, 1);
        }
        if (void* nm = noun_mgr())
            thiscall<void>(a_DestroyInstance, nm, m.egg);
        vcall<void>(m.egg, 1);
        g_epoch_removed++;
    } else if (!IsBadReadPtr(m.egg, 0x228) && field<uint32_t>(m.egg, 0x24) == m.serial &&
               field<uint8_t>(m.egg, 0x21)) {
        // destroyed with its world; the block is still ours (our reference): give back the herd's and ours
        if (m.herd && field<void*>(m.egg, kEggHerd) == m.herd) {
            field<void*>(m.egg, kEggHerd) = nullptr;
            vcall<void>(m.herd, 1);
        }
        vcall<void>(m.egg, 1);
        g_epoch_released++;
    }
    m.egg = nullptr;
}

static void clear_mirrors()
{
    for (auto& [id, m] : g_mirrors)
        remove_mirror(m);
    g_mirrors.clear();
}

static bool savestrip_filter(void* obj, uint32_t noun)
{
    if (noun != kEggNoun)
        return false;
    for (auto& [id, m] : g_mirrors)
        if (m.egg == obj)
            return true;
    return false;
}

// BUG-0896: the eggs of the last snapshot (object, serial, state) and the snapshots sent early for them
struct SentEgg {
    game::LiveRef egg; // (the game frees an eaten egg: never read through a raw pointer, BUG-0426 class)
    int state;
};
static std::vector<SentEgg> g_sent_eggs;
static uint32_t g_early_snaps = 0;

static bool sent_egg_changed()
{
    for (auto& e : g_sent_eggs) {
        void* o = game::live(e.egg);
        if (!o)
            return true; // gone (eaten up, the herd removed it)
        int st = field<int>(o, kEggState);
        if (st != e.state && (st == 3 || st == 4))
            return true; // hatched / eaten here
    }
    return false;
}

static void host_tick(double now)
{
    bool early = false;
    if (now - g_last_snap_tx < kSnapInterval) {
        if (!g_hatch_now_fix || g_sent_eggs.empty() || now - g_last_snap_tx < 0.05 || !sent_egg_changed())
            return;
        early = true;
        g_early_snaps++;
    }
    g_last_snap_tx = now;
    auto eggs = host_eggs();
    g_sent_eggs.clear();
    for (auto& [o, e] : eggs)
        if (g_sent_eggs.size() < 64)
            g_sent_eggs.push_back({game::live_ref(o, kEggNoun), (int)e.state});
    if (early)
        H->log(M, SK_INFO, "nest: an egg hatched or was eaten: snapshot now (BUG-0896, %u so far)", g_early_snaps);
    for (auto& p : C.sess->players()) {
        if (p.id == 0)
            continue;
        net::Vec3 gp;
        if (C.in_place && !C.in_place(p.id))
            continue;
        if (!C.player_pos || !C.player_pos(p.id, gp) || (gp.x == 0 && gp.y == 0 && gp.z == 0)) {
            // Tribe (BUG-0474): a player is a pointer, often (0 0 0) in a background game: the tribe's place
            if (!(g_tribe_fix && game::mode_id() == modes::kTribe && C.stage_center && C.stage_center(gp)))
                continue;
        }
        std::vector<const HostEgg*> close;
        for (auto& [o, e] : eggs)
            if (net::dist(e.pos, gp) < (game::mode_id() == modes::kTribe ? kEggRelevanceTribe : kEggRelevance))
                close.push_back(&e);
        std::sort(close.begin(), close.end(), [&](const HostEgg* a, const HostEgg* b) { return net::dist(a->pos, gp) < net::dist(b->pos, gp); });
        if (close.size() > (size_t)kMaxSnap)
            close.resize(kMaxSnap);
        net::Writer w;
        w.u8(kKindSnap);
        w.u8((uint8_t)close.size());
        for (const HostEgg* e : close) {
            w.u32(e->id);
            w.f32(e->pos.x);
            w.f32(e->pos.y);
            w.f32(e->pos.z);
            w.u8(e->state | (e->family ? kFamilyBit : 0)); // BUG-0660 (older guests skip a state over 16)
            g_family_tx += e->family;
            w.f32(e->scale);
            w.f32(e->home.x);
            w.f32(e->home.y);
            w.f32(e->home.z);
        }
        C.sess->send_unreliable_to(p.id, net::MSG_NEST, w);
        g_snaps_tx++;
    }
}

static void guest_tick(double now)
{
    if (g_epoch != game::world_epoch()) { // another world (or another avatar in this one, BUG-0833)
        for (auto& [id, m] : g_mirrors)
            drop_old_mirror(m);
        g_mirrors.clear();
        g_own.clear();
        g_epoch = game::world_epoch();
    }
    if (!g_mirrors.empty() && (now - g_last_snap_rx > kStale || !egg_stage())) {
        clear_mirrors(); // the host stopped sending (left the stage, its own world, gone)
        return;
    }
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        Mirror& m = it->second;
        if (!mirror_ok(m)) { // the game took it (a cleanup of its own): forget it (BUG-0833: and give back our references)
            drop_old_mirror(m);
            it = g_mirrors.erase(it);
            continue;
        }
        int st = field<int>(m.egg, kEggState);
        if (st == 4 && m.state != 4 && m.state != 3) { // eaten (or broken) here: the host's egg too
            net::Writer w;
            w.u8(kKindEat);
            w.u32(it->first);
            C.sess->send_reliable_to(0, net::MSG_NEST, w);
            m.state = 4;
            g_eats_sent++;
        }
        ++it;
    }
}

// BUG-0660: guest, a host family egg: this game's own egg of its family herd bound to it (the save's egg both games
// have), its hatch / eat followed. true: handled (bound now or before), false: none here (a mirror is made)
static bool own_family_egg(const HostEgg& e)
{
    auto it = g_own.find(e.id);
    if (it != g_own.end()) {
        void* o = it->second.egg;
        if (game::object_alive(o) && game::object_serial(o) == it->second.serial && !IsBadReadPtr(o, 0x228)) {
            int st = field<int>(o, kEggState);
            if ((e.state == 3 || e.state == 4) && st != 3 && st != 4)
                thiscall<void>(a_SetState, o, (int)e.state); // hatched or eaten on the host
            net::Vec3 q;
            OwnEgg& b = it->second;
            if (st != 3 && st != 4 && game::object_position(o, q) &&
                (!b.placed || net::dist(e.pos, b.host) > 0.3f || net::dist(q, b.after) > kOwnDrift)) {
                // where the host's lies (its game's own place differed: lab 6.4 m off, 6.5 m over the ground), and again
                // when the host's moves or this game pushes it away
                float v[3] = {e.pos.x, e.pos.y, e.pos.z};
                vcall<void>((uint8_t*)o + 0x34, 0x38 / 4, (const float*)v); // cSpatialObject::SetPosition
                cdecl_call<void>(a_Snap, o);                                // onto this game's ground, upright
                b.placed = true;
                b.host = e.pos;
                game::object_position(o, b.after);
                g_own_placed++;
            }
            return true;
        }
        g_own.erase(it); // the game cleared it
        return false;
    }
    struct F {
        net::Vec3 at;
        float best;
        void* egg;
    } f{e.pos, kOwnBind, nullptr};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* f = (F*)p;
            net::Vec3 q;
            if (noun != kEggNoun || !game::object_alive(o) || IsBadReadPtr(o, 0x228) || !game::object_position(o, q))
                return true;
            void* herd = field<void*>(o, kEggHerd);
            if (!herd || IsBadReadPtr(herd, 0x220) || !field<uint8_t>(herd, 0x84) || net::dist(q, f->at) >= f->best)
                return true;
            for (auto& [id, m] : g_mirrors)
                if (m.egg == o)
                    return true;
            for (auto& [id, b] : g_own)
                if (b.egg == o)
                    return true;
            f->best = net::dist(q, f->at);
            f->egg = o;
            return true;
        },
        &f);
    if (!f.egg)
        return false;
    g_own[e.id] = {f.egg, game::object_serial(f.egg)};
    g_own_bound++;
    return own_family_egg(e); // placed at once
}

static void on_snapshot(net::Reader& r, double now)
{
    uint8_t n = r.u8();
    if (!r.ok() || n > kMaxSnap)
        return;
    std::vector<HostEgg> v;
    for (int i = 0; i < n; i++) {
        HostEgg e{};
        e.id = r.u32();
        e.pos.x = r.f32();
        e.pos.y = r.f32();
        e.pos.z = r.f32();
        e.state = r.u8();
        e.family = (e.state & kFamilyBit) != 0; // BUG-0660
        e.state &= (uint8_t)~kFamilyBit;
        e.scale = r.f32();
        e.home.x = r.f32();
        e.home.y = r.f32();
        e.home.z = r.f32();
        if (!r.ok())
            return;
        if (!std::isfinite(e.pos.x) || !std::isfinite(e.pos.y) || !std::isfinite(e.pos.z) || !std::isfinite(e.scale) ||
            !std::isfinite(e.home.x) || !std::isfinite(e.home.y) || !std::isfinite(e.home.z) ||
            e.pos.x * e.pos.x + e.pos.y * e.pos.y + e.pos.z * e.pos.z > 1e8f || e.state > 16)
            continue;
        v.push_back(e);
    }
    if (!g_eggs_fix || !egg_stage() || (C.host_away && C.host_away()))
        return;
    g_snaps_rx++;
    g_last_snap_rx = now;
    if (g_epoch != game::world_epoch())
        return; // guest_tick takes the new world first
    std::set<uint32_t> ids;
    for (const HostEgg& e : v) {
        ids.insert(e.id);
        if (e.family && !g_family_fix)
            continue;
        if (e.family && g_mirrors.find(e.id) == g_mirrors.end() && own_family_egg(e)) // BUG-0660: this game's own copy
            continue;
        auto it = g_mirrors.find(e.id);
        if (it == g_mirrors.end()) {
            Mirror m;
            if (make_mirror(e, m)) {
                m.seen = now;
                g_mirrors[e.id] = m;
            }
            continue;
        }
        Mirror& m = it->second;
        if (!mirror_ok(m))
            continue;
        m.seen = now;
        if (net::dist(m.pos, e.pos) > 0.3f)
            place_mirror(m, e.pos);
        if (e.state != m.state && (e.state == 3 || e.state == 4)) { // hatched or eaten on the host
            if (field<int>(m.egg, kEggState) != e.state)
                thiscall<void>(a_SetState, m.egg, (int)e.state);
            m.state = e.state;
        }
    }
    for (auto it = g_own.begin(); it != g_own.end();)
        it = ids.count(it->first) ? std::next(it) : g_own.erase(it);
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();)
        if (!ids.count(it->first)) { // gone on the host (its herd cleared it) or out of the guest's range
            remove_mirror(it->second);
            it = g_mirrors.erase(it);
        } else
            ++it;
}

static void on_eat(uint8_t from, net::Reader& r)
{
    uint32_t id = r.u32();
    if (!r.ok() || !g_eggs_fix || !egg_stage())
        return;
    for (auto& [o, e] : host_eggs())
        if (e.id == id && (e.state == 5 || e.state == 6)) {
            thiscall<void>(a_SetState, o, 4); // what the game's eat node does to the egg
            g_eats_applied++;
            H->log(M, SK_INFO, "nest: player %u ate egg %u", from, id);
            return;
        }
    // not whole here (hatched, eaten, gone): the guest gets the egg's real state now, not in up to a second (BUG-0896)
    H->log(M, SK_INFO, "nest: player %u ate egg %u, which is not whole here: refused%s", from, id,
           g_hatch_now_fix ? ", a snapshot now" : "");
    if (g_hatch_now_fix)
        g_last_snap_tx = 0;
}


// ---------------------------------------------------------------- BUG-0443
// An NPC egg has no gravity while it is asleep: the lay node puts it at the layer's position (cHerd_LayEgg
// 0xC6D0B0, snapped onto the surface only for the avatar's eggs), it settles with the physics for a moment
// and then stays where it is; an egg moved 2 m up stayed there until a creature came close and woke it
// (docs/re/creature.md#creature-egg-gravity). An egg that ends up over the surface (laid by a layer in the
// air, pushed up by a creature) hangs there on the host for minutes, and the guests' copies (nest.eggs,
// snapped onto their own surface) lie on the ground: the player report "eggs flying mid air".
// The host puts an NPC egg that hangs more than kHang over the surface (the terrain or the nest mound, as
// cEgg_SnapToGround finds it) and has not moved for a second onto that surface with cEgg_SnapToGround, what
// the game does to the avatar's eggs when they are laid and to every egg when it hatches. Only near the
// host's avatar (kGroundNear): farther away the nest's collision is not loaded and the surface would be the
// bare terrain under the mound. `coop.fix nest.egg_ground off`: they stay in the air (0.7.3.3).
static bool g_ground_fix = true;
COOP_FIX("nest.egg_ground", "BUG-0443", g_ground_fix, "creature: an NPC egg left hanging in the air on the host is put onto the ground");

constexpr float kHang = 0.3f;        // m over the surface (settled eggs: 0.00-0.1, measured)
constexpr float kGroundNear = 60.f;  // m from the host's avatar
constexpr float kStill = 0.02f;      // m between two checks: asleep
constexpr double kGroundInterval = 0.5;
struct Still {
    net::Vec3 pos{};
    int n = 0;
    double seen = 0;
};
static std::map<uint32_t, Still> g_still; // egg serial -> its last check
static double g_last_ground = 0;
static uint32_t g_grounded = 0;
static float g_grounded_max = 0;

static void ground_tick(double now)
{
    if (now - g_last_ground < kGroundInterval)
        return;
    g_last_ground = now;
    void* av = game::avatar();
    if (!av)
        return;
    net::Vec3 me = game::position(av);
    for (auto& [o, e] : host_eggs()) {
        if ((e.state != 5 && e.state != 6) || net::dist(e.pos, me) > kGroundNear)
            continue;
        Still& st = g_still[e.id];
        st.n = st.seen > 0 && net::dist(st.pos, e.pos) < kStill ? st.n + 1 : 0;
        st.pos = e.pos;
        st.seen = now;
        float os = 0;
        if (st.n < 2 || !over_surface(e.pos, os) || os <= kHang)
            continue;
        cdecl_call<void>(a_Snap, o);
        net::Vec3 p{};
        game::object_position(o, p);
        st.pos = p;
        st.n = 0;
        g_grounded++;
        g_grounded_max = std::max(g_grounded_max, os);
        H->log(M, SK_INFO, "nest: egg %u hung %.2f m over the surface: put onto it (BUG-0443)", e.id, os);
    }
    for (auto it = g_still.begin(); it != g_still.end();) // eggs gone (hatched, eaten, out of range)
        it = now - it->second.seen > 10.0 ? g_still.erase(it) : std::next(it);
}

void tick(double now)
{
    if (!a_SetState || !a_Snap || !a_CreateInstance || !a_DestroyInstance || !a_NounGet)
        return;
    if (is_host()) {
        if (g_eggs_fix && egg_stage())
            host_tick(now);
        if (g_ground_fix && a_Surface && a_PlanetGet && game::in_creature_stage())
            ground_tick(now);
    } else if (is_guest())
        guest_tick(now);
    else if (!g_mirrors.empty())
        clear_mirrors();
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_NEST)
        return;
    uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (kind == kKindSnap && !is_host() && from == 0)
        on_snapshot(r, H->time_seconds());
    else if (kind == kKindEat && is_host())
        on_eat(from, r);
}

void on_session_end() { clear_mirrors(); }

void on_world_change()
{
    g_still.clear();
    for (auto& [id, m] : g_mirrors)
        drop_old_mirror(m); // BUG-0833 (off: forgotten, "freed with the world")
    g_mirrors.clear();
}

// coop.nest objs [r]: eggs, fruit and nests within r m of the avatar (tests, BUG-0384)
static void list_objs(SkOut* out, float r)
{
    void* av = game::avatar();
    if (!av) {
        H->out_printf(out, "no avatar\n");
        return;
    }
    struct U {
        net::Vec3 c;
        float r;
        SkOut* out;
        int n;
    } u{game::position(av), r, out, 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            if (noun != kEggNoun && noun != kFruitNoun && noun != kNestNoun)
                return true;
            net::Vec3 q;
            if (!game::object_position(o, q) || net::dist(q, u->c) > u->r || u->n >= 400)
                return true;
            float og = 0, os = -999.f;
            over_ground(q, og);
            if (noun == kEggNoun)
                over_surface(q, os);
            const char* kind = noun == kEggNoun ? "egg" : noun == kFruitNoun ? "fruit" : "nest";
            H->out_printf(u->out, "%s %p pos=(%.2f %.2f %.2f) over_ground=%.2f alive=%d dist=%.1f", kind, o, q.x, q.y, q.z, og,
                          (int)game::object_alive(o), net::dist(q, u->c));
            if (noun == kEggNoun && !IsBadReadPtr(o, 0x228)) {
                uint32_t host_id = 0;
                for (auto& [id, m] : g_mirrors)
                    if (m.egg == o)
                        host_id = id;
                void* herd = field<void*>(o, kEggHerd);
                H->out_printf(u->out, " state=%d hatch=%d herd=%p family=%d serial=%u mirror_of=%u tohatch=%p over_surface=%.2f", field<int>(o, kEggState),
                              field<int>(o, kEggHatch), herd, herd && !IsBadReadPtr(herd, 0x88) ? (int)field<uint8_t>(herd, 0x84) : 0,
                              game::object_serial(o), host_id, field<void*>(o, kEggToHatch), os);
            }
            H->out_printf(u->out, "\n");
            u->n++;
            return true;
        },
        &u);
}

// coop.nest herds: herds with eggs (mEggs +0x54), their nest, mbEggsInNest +0x198, mpEggLayer +0x194
static void list_herds(SkOut* out)
{
    void* av = game::avatar();
    net::Vec3 c = av ? game::position(av) : net::Vec3{};
    struct U {
        net::Vec3 c;
        SkOut* out;
    } u{c, out};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            if (noun != game::kHerdNoun || IsBadReadPtr(o, 0x220))
                return true;
            void** b = field<void**>(o, 0x54);
            void** e = field<void**>(o, 0x58);
            int n = b && e >= b ? (int)(e - b) : -1;
            void* ns = game::herd_nest(o);
            net::Vec3 q{};
            bool have = ns && game::object_position(ns, q);
            H->out_printf(u->out, "herd %p nest=%p eggs=%d in_nest=%d layer=%p avatar=%d changed=%d index=%d enabled=%d dist=%.0f\n", o, ns, n,
                          (int)field<uint8_t>(o, 0x198), field<void*>(o, 0x194), (int)field<uint8_t>(o, 0x84),
                          (int)field<uint8_t>(o, 0xFC), field<int>(o, 0x12C), (int)field<uint8_t>(o, 0x218),
                          have ? net::dist(q, u->c) : -1.f);
            return true;
        },
        &u);
}

// coop.nest herd <hex>: members (+0x40), the egg layer +0x194 and its eggs to lay (+0x1678), the eggs
static void show_herd(SkOut* out, void* h)
{
    void** b = field<void**>(h, 0x40);
    void** e = field<void**>(h, 0x44);
    H->out_printf(out, "herd %p target=%d members=%d layer=%p eggs_to_lay=%d respawn=%u shortfall=%d\n", h, field<int>(h, 0xF4),
                  b && e >= b ? (int)(e - b) : -1, field<void*>(h, 0x194),
                  field<void*>(h, 0x194) ? field<int>(field<void*>(h, 0x194), 0x1678) : -1, field<uint32_t>(h, 0x11C), field<int>(h, 0xF8));
    for (void** it = b; it && it < e && it - b < 32; ++it)
        if (*it) {
            net::Vec3 p = game::position(*it);
            float og = 0;
            over_ground(p, og);
            H->out_printf(out, "  member %p hp=%.1f dead=%d to_lay=%d pos=(%.1f %.1f %.1f) og=%.2f\n", *it, game::health(*it),
                          (int)game::is_dead(*it), field<int>(*it, 0x1678), p.x, p.y, p.z, og);
        }
    void** eb = field<void**>(h, 0x54);
    void** ee = field<void**>(h, 0x58);
    for (void** it = eb; it && it < ee && it - eb < 32; ++it)
        if (*it) {
            net::Vec3 p{};
            game::object_position(*it, p);
            float og = 0;
            over_ground(p, og);
            H->out_printf(out, "  egg %p state=%d hatch=%d tohatch=%p pos=(%.2f %.2f %.2f) og=%.2f\n", *it, field<int>(*it, kEggState),
                          field<int>(*it, kEggHatch), field<void*>(*it, kEggToHatch), p.x, p.y, p.z, og);
        }
}

static int c_nest(void*, int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "";
    if (!strcmp(a, "eat") && argc > 2) { // tests: egg <hex> is eaten here (what the game's eat node 0xDBD460 does to it)
        void* o = (void*)strtoul(argv[2], nullptr, 16);
        bool ok = false;
        game::for_each_object(
            [](void* x, uint32_t noun, void* p) {
                if (x == *(void**)p && noun == kEggNoun && game::object_alive(x)) {
                    *(void**)p = nullptr;
                    return false;
                }
                return true;
            },
            &o);
        ok = o == nullptr;
        if (ok)
            thiscall<void>(a_SetState, (void*)strtoul(argv[2], nullptr, 16), 4);
        H->out_printf(out, ok ? "eaten\n" : "no such egg\n");
        return ok ? 0 : 1;
    }
    if (!strcmp(a, "evolve")) { // tests (BUG-0639): every herd's evolution fields: +0x85 mbShouldEvolve, +0xF0 generation,
        // +0xA8 evolved species keys (count), +0x94 DNA threshold, +0x190 extinct, members, home, distance
        void* av = game::avatar();
        struct U {
            net::Vec3 c;
            SkOut* out;
        } u{av ? game::position(av) : net::Vec3{}, out};
        game::for_each_object(
            [](void* o, uint32_t noun, void* p) {
                auto* u = (U*)p;
                if (noun != game::kHerdNoun || IsBadReadPtr(o, 0x220))
                    return true;
                void** b = field<void**>(o, 0x40);
                void** e = field<void**>(o, 0x44);
                void** kb = field<void**>(o, 0xA8);
                void** ke = field<void**>(o, 0xAC);
                auto& h = field<net::Vec3>(o, 0x34);
                void* sp = game::herd_species(o);
                // BUG-0668: the species' key instance (profile +0x504) and the key of the next generation (+0xA8[gen + 1])
                int gen = field<int>(o, 0xF0), nk = kb && ke >= kb ? (int)((uint8_t*)ke - (uint8_t*)kb) / 12 : -1;
                uint32_t skey = sp && !IsBadReadPtr(sp, 0x510) ? field<uint32_t>(sp, 0x504) : 0;
                uint32_t next = gen >= 0 && gen + 1 < nk && !IsBadReadPtr(kb, (gen + 2) * 12) ? *(uint32_t*)((uint8_t*)kb + (gen + 1) * 12) : 0;
                H->out_printf(u->out,
                              "herd %p should_evolve=%d gen=%d evolved_keys=%d threshold=%.0f extinct=%d members=%d species=%p "
                              "skey=%08X next=%08X home=(%.0f %.0f %.0f) dist=%.0f\n",
                              o, (int)field<uint8_t>(o, 0x85), gen, nk, field<float>(o, 0x94),
                              (int)field<uint8_t>(o, 0x190), b && e >= b ? (int)(e - b) : -1, sp, skey, next, h.x, h.y, h.z,
                              net::dist(h, u->c));
                return true;
            },
            &u);
        return 0;
    }
    if (!strcmp(a, "respawn") && argc > 3) { // tests: herd <hex>'s respawn time (+0x11C mRespawnRate, ms)
        void* h = (void*)strtoul(argv[2], nullptr, 16);
        if (IsBadReadPtr(h, 0x220) || game::noun_id(h) != game::kHerdNoun) {
            H->out_printf(out, "no such herd\n");
            return 1;
        }
        field<uint32_t>(h, 0x11C) = (uint32_t)strtoul(argv[3], nullptr, 10);
        H->out_printf(out, "respawn %u ms\n", field<uint32_t>(h, 0x11C));
        return 0;
    }
    if (!strcmp(a, "look") && argc > 4) { // tests (screenshots): the avatar d m from x y z, facing it
        static uintptr_t pm_get = H->sym("Simulator::cPlanetModel::Get"), orient = H->sym("Simulator::cPlanetModel::GetOrientation");
        void* av = game::avatar();
        void* pm = pm_get ? cdecl_call<void*>(pm_get) : nullptr;
        if (!av || !pm || !orient) {
            H->out_printf(out, "no avatar or planet\n");
            return 1;
        }
        net::Vec3 t{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        float d = argc > 5 ? (float)atof(argv[5]) : 6.f;
        float r = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        net::Vec3 up{t.x / r, t.y / r, t.z / r};
        net::Vec3 side = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
        float sl = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
        net::Vec3 p{t.x + side.x / sl * d + up.x * 1.5f, t.y + side.y / sl * d + up.y * 1.5f, t.z + side.z / sl * d + up.z * 1.5f};
        if (argc > 8) // look <target x y z> <d> <from x y z>: from that point instead (tests, BUG-0465)
            p = {(float)atof(argv[6]), (float)atof(argv[7]), (float)atof(argv[8])};
        float pv[3] = {p.x, p.y, p.z}, dv[3] = {t.x - p.x, t.y - p.y, t.z - p.z}, q[4] = {0, 0, 0, 1};
        thiscall<void*>(orient, pm, q, pv, dv);
        game::teleport(av, p, net::Quat{q[0], q[1], q[2], q[3]});
        H->out_printf(out, "avatar at (%.1f %.1f %.1f) facing (%.1f %.1f %.1f)\n", p.x, p.y, p.z, t.x, t.y, t.z);
        return 0;
    }
    if (!strcmp(a, "target") && argc > 3) { // tests: herd <hex> wants <n> members (+0xF4 mTargetHerdSize): it misses some
        void* h = (void*)strtoul(argv[2], nullptr, 16);
        if (IsBadReadPtr(h, 0x220) || game::noun_id(h) != game::kHerdNoun) {
            H->out_printf(out, "no such herd\n");
            return 1;
        }
        field<int>(h, 0xF4) = atoi(argv[3]);
        H->out_printf(out, "target %d\n", field<int>(h, 0xF4));
        return 0;
    }
    if (!strcmp(a, "hatch") && argc > 3) { // tests: egg <hex> hatches in <ms> (+0x1F0 mTimeToHatch)
        void* o = (void*)strtoul(argv[2], nullptr, 16);
        if (IsBadReadPtr(o, 0x228) || game::noun_id(o) != kEggNoun) {
            H->out_printf(out, "no such egg\n");
            return 1;
        }
        field<int>(o, kEggHatch) = atoi(argv[3]);
        H->out_printf(out, "hatch in %d ms\n", field<int>(o, kEggHatch));
        return 0;
    }
    if (!strcmp(a, "lift") && argc > 3) { // tests: egg <hex> <m> higher (cSpatialObject::SetPosition, as a layer in the air puts it)
        void* o = (void*)strtoul(argv[2], nullptr, 16);
        if (IsBadReadPtr(o, 0x228) || game::noun_id(o) != kEggNoun) {
            H->out_printf(out, "no such egg\n");
            return 1;
        }
        net::Vec3 p{};
        game::object_position(o, p);
        float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), k = (r + (float)atof(argv[3])) / r;
        float v[3] = {p.x * k, p.y * k, p.z * k};
        vcall<void>((uint8_t*)o + 0x34, 0x38 / 4, (const float*)v);
        float og = 0;
        over_ground({v[0], v[1], v[2]}, og);
        H->out_printf(out, "egg at %.2f m over the terrain\n", og);
        return 0;
    }
    if (!strcmp(a, "herd") && argc > 2) {
        show_herd(out, (void*)strtoul(argv[2], nullptr, 16));
        return 0;
    }
    if (!strcmp(a, "kill") && argc > 3) { // tests: kill n living members of herd <hex> (the avatar as the attacker)
        void* h = (void*)strtoul(argv[2], nullptr, 16);
        int n = atoi(argv[3]), k = 0;
        void** b = field<void**>(h, 0x40);
        void** e = field<void**>(h, 0x44);
        std::vector<void*> v(b, e);
        for (void* c : v)
            if (c && !game::is_dead(c) && k < n) {
                game::hit(c, 10000.f, 0, game::avatar());
                k++;
            }
        H->out_printf(out, "killed %d\n", k);
        return 0;
    }
    if (!strcmp(a, "objs")) {
        list_objs(out, argc > 2 ? (float)atof(argv[2]) : 30.f);
        return 0;
    }
    if (!strcmp(a, "herds")) {
        list_herds(out);
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

std::string status()
{
    char b[600];
    int live = 0;
    for (auto& [id, m] : g_mirrors)
        live += mirror_ok(m);
    snprintf(b, sizeof b, "nest eggs fix=%d ground_fix=%d grounded=%u grounded_max=%.2f role=%s mirrors=%u live=%d made=%u removed=%u snaps_tx=%u early=%u snaps_rx=%u eats_sent=%u eats_applied=%u family_fix=%d family_tx=%u family_made=%u own_bound=%u/%u own_placed=%u epoch_removed=%u epoch_released=%u\n",
             (int)g_eggs_fix, (int)g_ground_fix, g_grounded, g_grounded_max, is_host() ? "host" : is_guest() ? "guest" : "none", (unsigned)g_mirrors.size(), live, g_made, g_removed,
             g_snaps_tx, g_early_snaps, g_snaps_rx, g_eats_sent, g_eats_applied, (int)g_family_fix, g_family_tx, g_family_made, (unsigned)g_own.size(), g_own_bound, g_own_placed, g_epoch_removed, g_epoch_released);
    return b;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    a_NounGet = H->sym("Simulator::cGameNounManager::Get");
    a_CreateInstance = H->sym("Simulator::cGameNounManager::CreateInstance");
    a_DestroyInstance = H->sym("Simulator::cGameNounManager::DestroyInstance");
    a_SetState = H->sym("SporeCoop::cEgg_SetState");
    a_Snap = H->sym("SporeCoop::cEgg_SnapToGround");
    a_Surface = H->sym("SporeCoop::cPlanetModel_SurfacePoint");
    a_PlanetGet = H->sym("Simulator::cPlanetModel::Get");
    savestrip::add_filter(savestrip_filter); // BUG-0384: the guest's egg mirrors are not its world
    H->command_add(M, "coop.nest", "coop.nest [objs [r]|herds|herd <h>|kill <h> <n>|respawn <h> <ms>|target <h> <n>|hatch <egg> <ms>|eat <egg>|look x y z [d]] - eggs at the nests (BUG-0384)", c_nest,
                   nullptr, SK_CMD_MAIN_THREAD);
}


} // namespace nest
