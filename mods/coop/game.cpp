#include "game.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "sporekit_game.hpp"
#include "lifecycle.h"
#include "call_guard.h"
#include "stage_epoch.h"
#include "swap_ledger.h"
#include "fixes.h"
#include "gamefixes.h"
#include <unordered_map>

using namespace skg;

namespace game {

static const SkHost* H;
static SkMod* M;
static bool g_ready;

static uintptr_t a_NounGet, a_DestroyInstance, a_CreatureCreate, a_NPCTickAI, a_ModeMgrGet;
static uintptr_t a_SpeciesMgrGet, a_GetSpecies, a_CreateHerd, a_CinematicGet;

typedef void(SK_CDECL* AddEvo_t)(float);
static AddEvo_t o_AddEvo;
static DnaListener g_dna_listener;

static void SK_CDECL h_AddEvo(float delta)
{ // (the listener may scale the gain: BUG-0510, dna_pace.h)
    if (g_dna_listener && !g_dna_listener(delta))
        return;
    o_AddEvo(delta);
}

// ---- herds with a null member (BUG-0256) ----
// SporeCoop::Herd_Update (0xC6D960, thiscall(herd, int dt) ret 4, every frame for an enabled herd)
// reads every member of its list (+0x40 .. +0x44) without a null check (Creature_IsAdult 0xC0B8D0
// reads +0xB34 first). A herd loaded with a null in that list (a member the save wrote as a null
// reference) faulted at 0x00C0B8D2 every frame on a guest that rejoined and loaded the host's world.
// The hook takes nulls out of the list before the update (the list holds counted references; a null
// has none, so moving the others down changes no count).
typedef void(SK_THISCALL* HerdUpdate_t)(void*, int);
static HerdUpdate_t o_HerdUpdate;
static bool g_herd_nulls_fix = true;
COOP_FIX("npc.herd_nulls", "BUG-0256", g_herd_nulls_fix, "creature: a herd loaded with a null member does not fault every frame");
static uint32_t g_herd_nulls_removed, g_herd_nulls_herds;
static void* g_herd_nulls_last;

static void SK_THISCALL h_HerdUpdate(void* herd, int dt)
{
    if (g_herd_nulls_fix && herd && !IsBadReadPtr((uint8_t*)herd + 0x40, 8)) {
        void**& b = *(void***)((uint8_t*)herd + 0x40);
        void**& e = *(void***)((uint8_t*)herd + 0x44);
        if (b && e > b && !IsBadReadPtr(b, (size_t)((uint8_t*)e - (uint8_t*)b))) {
            void** w = b;
            for (void** r = b; r != e; ++r)
                if (*r)
                    *w++ = *r;
            if (w != e) {
                uint32_t n = (uint32_t)(e - w);
                for (void** r = w; r != e; ++r)
                    *r = nullptr;
                e = w;
                g_herd_nulls_removed += n;
                if (herd != g_herd_nulls_last) {
                    g_herd_nulls_herds++;
                    g_herd_nulls_last = herd;
                    H->log(M, SK_INFO, "herd %p (archetype %08X, nest %p): %u null member(s) taken out of its list "
                           "(a loaded save held them)", herd, *(uint32_t*)((uint8_t*)herd + 0x88), herd_nest(herd), n);
                }
            }
        }
    }
    o_HerdUpdate(herd, dt);
}

std::string herd_nulls_status()
{
    char b[120];
    snprintf(b, sizeof b, "herd nulls: fix=%d hooked=%d removed=%u herds=%u", (int)g_herd_nulls_fix,
             (int)(o_HerdUpdate != nullptr), g_herd_nulls_removed, g_herd_nulls_herds);
    return b;
}

static std::vector<void*> g_puppets;

// BUG-0532: a partner's puppet in the air (creature.puppet_air) was drawn on its terrain for 1-3 frames when the
// game started another animation on it (fall -> idle, the walk start): something in that update grounds it. The
// puppets held in the air (air_hold, set by modes.cpp each frame) are put back where the partner is and marked not
// grounded right after the game's animation starts and its update when the game grounded them.
struct AirHold {
    void* c;
    void* ac; // its AnimatedCreature (+0xB54), the low animation start's this
    net::Vec3 pos, vel;
    net::Quat rot;
    uint32_t serial = 0; // BUG-0995: c's cGameData::mID when the hold was made
};
static std::vector<AirHold> g_air;
// BUG-0995 (the serial part, package L2 of 0.7.14; class BUG-0426): air_reassert matched the updated creature with the
// holds by address only: a held creature the game removed and freed while no forget path ran (the settle second of
// npc.cpp's guest_tick after an avatar change) gave its block to the next creature the game built, and that animal of
// the guest's own world was put in the air at the held spot on every update. Now a hold keeps the creature's serial
// and is dropped (nothing placed) when the creature at that address is another object; the serial of the creature the
// game updates is its own memory (no list walk on this hot path). `coop.fix game.air_live off`: by address (0.7.13).
static bool g_air_live_fix = true;
COOP_FIX("game.air_live", "BUG-0995", g_air_live_fix, "creature: a puppet's hold in the air never lifts another creature the game built where it was");
static uint32_t g_air_stale;
// BUG-0995 (the release part; the serial is package L2's): the holds are matched by address, so none may outlive its
// session or its world: a creature the game freed with the world and built again at that address (an animal of
// the next world) would be put in the air at the old spot. air_release_all had no caller. Off: kept (0.7.13).
static bool g_air_release_fix = true;
COOP_FIX("game.air_release", "BUG-0995", g_air_release_fix, "creature: no puppet held in the air outlives its session or its world");
LIFECYCLE_STATE("game.air", lifecycle::kSession | lifecycle::kWorld, &g_air_release_fix, [] { g_air.clear(); },
                []() -> size_t { return g_air.size(); });
static uint32_t g_air_regrounded[5]; // by the call after which it was grounded: 0 update, 1 anim, 2 idle, 3 BT anim, 4 low anim
static void air_reassert(void* c, int where);
typedef int(SK_THISCALL* AcPlay_t)(void*, uint32_t, int);
static AcPlay_t o_AcPlay;
static int SK_THISCALL h_AcPlay(void* ac, uint32_t id, int a);

static std::vector<void*> g_our_herds; // made by create_herd (puppets, proxies); diagnostics only
// BUG-1067: every herd of ours by its serial (cGameData::mID +0x24, new for every object the game creates): savestrip's
// empty-herd rule (BUG-0255) strips only these. Herds are never destroyed by the game; a world change frees them and a
// new object at the same address has another serial.
static std::unordered_map<void*, uint32_t> g_our_herd_serial;
static void note_our_herd(void* h)
{
    if (!h || !skg::readable(h, 0x28))
        return;
    if (g_our_herd_serial.size() > 1024) // (stale entries of older worlds)
        for (auto it = g_our_herd_serial.begin(); it != g_our_herd_serial.end();)
            it = !object_alive(it->first) || object_serial(it->first) != it->second ? g_our_herd_serial.erase(it) : std::next(it);
    g_our_herd_serial[h] = object_serial(h);
}
bool our_herd(void* h)
{
    auto it = h ? g_our_herd_serial.find(h) : g_our_herd_serial.end();
    return it != g_our_herd_serial.end() && skg::readable(h, 0x28) && object_alive(h) && object_serial(h) == it->second;
}

// ---- registry via cCreatureAnimal::Update (thiscall, int deltaTime) ----
typedef void(SK_THISCALL* Update_t)(void*, int);
static Update_t o_Update;
static std::vector<void*> g_seen, g_seen_out;
static UpdateFilter g_update_filter;

// BUG-0440: see place_hold (game.h)
struct PlaceHold {
    void* c;
    net::Vec3 pos, aim;
    net::Quat rot;
    double t;
    float back_m; // a move away from aim up to this (m) in one update is the game's own walk: kept
};
static std::vector<PlaceHold> g_place_hold;
static uint32_t g_place_holds_kept = 0;
struct TestPush {
    void* c;
    net::Vec3 dir;
    float mps;
    double t;
};
static std::vector<TestPush> g_test_push;
static float vdist(const net::Vec3& a, const net::Vec3& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
static void place_reassert(void* c)
{
    double now = H->time_seconds();
    for (size_t i = 0; i < g_place_hold.size(); i++)
        if (PlaceHold& h = g_place_hold[i]; h.c == c) {
            if (now - h.t > 0.1) { // (not placed held this frame)
                g_place_hold.erase(g_place_hold.begin() + i);
                return;
            }
            if (field<int32_t>(c, kLocoModeOff) != 0) // (thrown by a hit, abducted: the game's own move, BUG-0409)
                return;
            net::Vec3 p = position(c);
            if (vdist(p, h.aim) > vdist(h.pos, h.aim) + std::max(0.02f, h.back_m)) {
                teleport(c, h.pos, h.rot);
                g_place_holds_kept++;
            }
            return;
        }
}
void place_hold(void* c, const net::Vec3& pos, const net::Quat& rot, const net::Vec3& aim, float back_m)
{
    double now = H->time_seconds();
    for (auto& h : g_place_hold)
        if (h.c == c) {
            h = {c, pos, aim, rot, now, back_m};
            return;
        }
    for (size_t i = 0; i < g_place_hold.size();) // (stale ones)
        if (now - g_place_hold[i].t > 0.1)
            g_place_hold.erase(g_place_hold.begin() + i);
        else
            i++;
    if (g_place_hold.size() < 64)
        g_place_hold.push_back({c, pos, aim, rot, now, back_m});
}
uint32_t place_holds_kept() { return g_place_holds_kept; }
void test_push(void* c, const net::Vec3& dir, float mps)
{
    double now = H->time_seconds();
    for (auto& e : g_test_push)
        if (e.c == c) {
            e = {c, dir, mps, now};
            return;
        }
    if (g_test_push.size() < 8)
        g_test_push.push_back({c, dir, mps, now});
}
static void test_push_apply(void* c, int dt_ms)
{
    double now = H->time_seconds();
    for (size_t i = 0; i < g_test_push.size(); i++)
        if (TestPush& e = g_test_push[i]; e.c == c) {
            if (now - e.t > 0.1) {
                g_test_push.erase(g_test_push.begin() + i);
                return;
            }
            float s = e.mps * (float)std::min(std::max(dt_ms, 0), 100) / 1000.f;
            net::Vec3 p = position(c);
            teleport(c, {p.x + e.dir.x * s, p.y + e.dir.y * s, p.z + e.dir.z * s}, orientation(c));
            return;
        }
}

static void SK_THISCALL h_Update(void* self, int dt)
{
    g_seen.push_back(self);
    if (g_update_filter && !g_update_filter(self))
        return;
    if (!g_test_push.empty())
        test_push_apply(self, dt);
    o_Update(self, dt);
    if (!g_air.empty())
        air_reassert(self, 0);
    if (!g_place_hold.empty())
        place_reassert(self);
}

const std::vector<void*>& creatures_updated()
{
    g_seen_out.swap(g_seen);
    g_seen.clear();
    std::sort(g_seen_out.begin(), g_seen_out.end());
    g_seen_out.erase(std::unique(g_seen_out.begin(), g_seen_out.end()), g_seen_out.end());
    return g_seen_out;
}

void set_update_filter(UpdateFilter fn) { g_update_filter = fn; }
bool is_destroyed(void* c) { return field<uint8_t>(c, 0x21) != 0; } // cGameData::mbIsDestroyed

// ---- combat via cCreatureAnimal TakeDamage (cCombatant vtable[6], this = creature+0x5A8) ----
typedef int(SK_THISCALL* TakeDamage_t)(void*, float, uint32_t, int, const void*, void*);
static TakeDamage_t o_TakeDamage;
static DamageFilter g_damage_filter;

// tests / RE (P1): the last hits that went through the game's TakeDamage of a creature, with the caller
// (`coop.hp dmglog`): which code hits, with which damage type (3 = the charge's stun, docs/re/creature.md#creature-stun)
struct DamageRec {
    double t;
    void *victim, *attacker;
    float dmg;
    int type;
    uintptr_t ret;
    int how; // 0 filtered, 1 applied, 2 the mod's own apply_damage
    bool air; // the victim was not grounded (+0x137 == 0) at the hit
};
static DamageRec g_dmglog[48];
static unsigned g_dmglog_n = 0;

static void dmglog_add(void* victim, void* attacker, float dmg, int type, uintptr_t ret, int how)
{
    bool air = victim && readable(victim, 0x138) && field<uint8_t>(victim, 0x137) == 0;
    g_dmglog[g_dmglog_n++ % 48] = {H ? H->time_seconds() : 0, victim, attacker, dmg, type, ret, how, air};
}

std::string damage_log(bool clear)
{
    std::string s;
    char b[160];
    unsigned first = g_dmglog_n > 48 ? g_dmglog_n - 48 : 0;
    for (unsigned i = first; i < g_dmglog_n; i++) {
        const DamageRec& r = g_dmglog[i % 48];
        snprintf(b, sizeof b, "dmg t=%.2f victim=%p attacker=%p amount=%.2f type=%d ret=0x%08X %s air=%d\n", r.t, r.victim,
                 r.attacker, r.dmg, r.type, (unsigned)r.ret, r.how == 0 ? "filtered" : r.how == 1 ? "applied" : "direct",
                 (int)r.air);
        s += b;
    }
    if (clear)
        g_dmglog_n = 0;
    return s.empty() ? "no hits\n" : s;
}

// BUG-0790: the attacker of a hit is a cCombatant of any class: a creature's or citizen's at +0x5A8, a vehicle's and a
// UFO's at +0x508, a turret's at +0x588, a hut's at +0x120 (dc04: the vt 0x0C of each of these combatant vtables is
// `lea eax,[ecx - offset]; ret`, the owner object: 0xC0BFC0, 0xC3BDB0, 0xBCD080, 0xBCD230). `attacker - 0x5A8` was a
// wrong pointer for every non-creature attacker (into the object before it or its heap neighbour), and the damage
// filter read creature fields there (the party check, the pack defence's target). Now the owner comes from vt 0x0C;
// the creature-only readers check that it is a listed creature. `coop.fix creature.attacker_owner off`: the subtraction.
static bool g_attacker_owner_fix = true;
COOP_FIX("creature.attacker_owner", "BUG-0790", g_attacker_owner_fix, "creature: a hit by a vehicle, UFO, turret or building names that object as the attacker (not a wrong pointer)");
bool attacker_owner_fix() { return g_attacker_owner_fix; }
static void* attacker_object(void* attacker)
{
    if (!attacker)
        return nullptr;
    if (!g_attacker_owner_fix)
        return (uint8_t*)attacker - kCombatOff;
    if (!readable(attacker, 4) || !readable(*(void**)attacker, 0x10))
        return nullptr;
    return vcall<void*>(attacker, 0x0C / 4); // cCombatant -> its object (ToGameData)
}

static int SK_THISCALL h_TakeDamage(void* combatant, float dmg, uint32_t pol, int type, const void* pos, void* attacker)
{
    void* self = (uint8_t*)combatant - kCombatOff;
    void* att = attacker_object(attacker);
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (g_damage_filter && !g_damage_filter(self, dmg, type, att)) {
        dmglog_add(self, att, dmg, type, ret, 0);
        return 0;
    }
    dmglog_add(self, att, dmg, type, ret, 1);
    return o_TakeDamage(combatant, dmg, pol, type, pos, attacker);
}

void set_damage_filter(DamageFilter fn) { g_damage_filter = fn; }

void apply_damage(void* c, float dmg, int type, void* attacker)
{
    if (!o_TakeDamage)
        return;
    // the vector is the hit's push (knockback): the game passes (0,0,0) for bites and strikes.
    // It used to be the creature's position (|v| ~ 505): every forwarded hit threw the creature,
    // a kill ~80 m up (play_creature.py 2026-10-23, docs/re/creature.md 3)
    Vector3 push{};
    dmglog_add(c, attacker, dmg, type, (uintptr_t)__builtin_return_address(0), 2);
    o_TakeDamage((uint8_t*)c + kCombatOff, dmg, attacker ? political_id(attacker) : 0xFFFFFFFFu, type, &push,
                 attacker ? (uint8_t*)attacker + kCombatOff : nullptr);
}

void set_health(void* c, float hp) { field<float>(c, kHpOff) = hp; }

bool sneaking(void* c) { return c && skg::readable(c, 0xBB4) && field<uint8_t>(c, 0xBB1) != 0; }
float noise_radius(void* c) { return c && skg::readable(c, 0xB84) ? field<float>(c, 0xB80) : -1.f; }
float sneak_alpha(void* c) { return c && skg::readable(c, 0xB80) ? field<float>(c, 0xB7C) : -1.f; }

bool set_sneaking(void* c, bool on)
{
    static uintptr_t a = H ? H->sym("SporeCoop::CreatureSetSneaking") : 0;
    if (!a || !c)
        return false;
    thiscall<void>(a, c, (int)(on ? 1 : 0), 1);
    return true;
}

bool hatchling_waiting(void* c)
{
    if (!c || !skg::readable(c, 0xB50) || field<int32_t>(c, 0xB34) != 0)
        return false;
    uint8_t* bt = field<uint8_t*>(c, 0xB4C); // behaviour tree data
    return bt && skg::readable(bt + 0x1D8, 4) && field<uint32_t>(bt, 0x1D8) == kHatchlingTree;
}

bool grow_up(void* c)
{
    static uintptr_t grow = H->sym("SporeCoop::cCreatureAnimal_GrowTo");
    static uintptr_t posse_get = H->sym("SporeCoop::PosseSimulator_Get");
    static uintptr_t posse_grow = H->sym("SporeCoop::PosseSimulator_GrowMembers");
    if (!c || !grow)
        return false;
    thiscall<void>(grow, c, 1);
    if (void* posse = posse_get && posse_grow ? cdecl_call<void*>(posse_get) : nullptr)
        thiscall<void>(posse_grow, posse, 1);
    return true;
}

// ---------------- any game object ----------------
void* noun_manager() { return a_NounGet ? cdecl_call<void*>(a_NounGet) : nullptr; }

static bool in_exe(const void* p)
{
    uintptr_t v = (uintptr_t)p, b = H->image_base();
    return v > b && v < b + 0x2000000;
}

void for_each_object(bool (*fn)(void* obj, uint32_t noun, void* user), void* user)
{
    uint8_t* nm = (uint8_t*)noun_manager();
    if (!nm)
        return;
    uint8_t* anchor = nm + 0x78;
    uint8_t* n = *(uint8_t**)anchor;
    for (int i = 0; n && n != anchor && i < 200000; i++) {
        uint8_t* next = *(uint8_t**)n;
        void* obj = n - 0xC;
        if (!IsBadReadPtr(obj, 0x30) && in_exe(*(void**)obj) && !fn(obj, noun_id(obj), user))
            return;
        n = next;
    }
}

uint32_t noun_id(void* obj) { return obj ? vcall<uint32_t>(obj, 0x20 / 4) : 0; }
void* cast(void* obj, uint32_t type) { return obj ? vcall<void*>(obj, 0x0C / 4, type) : nullptr; }
bool object_alive(void* obj) { return obj && !field<bool>(obj, 0x21); }
uint32_t object_serial(void* obj) { return obj ? field<uint32_t>(obj, 0x24) : 0; }

uint32_t g_ufos_freed;

bool ufo_listed(void* u)
{
    // the game frees UFOs on its own (DestroyNPCUFOs 0x1009310 on every context switch, also our
    // Defender puppets and mirrors): only a pointer still in the UFO noun list may be touched
    // (docs/re/space.md 1). Reads only the list, never the object.
    static uintptr_t get = H ? H->sym("Simulator::GetAllUFOs") : 0;
    void* nm = noun_manager();
    void* v = u && get && nm ? thiscall<void*>(get, nm) : nullptr;
    if (!v)
        return false;
    void** b = field<void**>(v, 0);
    void** e = field<void**>(v, 4);
    for (void** it = b; it && it < e; ++it)
        if (*it == u)
            return true;
    return false;
}

bool object_position(void* obj, net::Vec3& out)
{
    void* sp = cast(obj, 0x1186577);
    if (!sp)
        return false;
    auto& v = field<Vector3>(sp, 4);
    out = {v.x, v.y, v.z};
    return true;
}

float object_scale(void* obj)
{
    void* sp = cast(obj, 0x1186577);
    return sp ? vcall<float>(sp, 0x34 / 4) : -1.f;
}

void set_object_scale(void* obj, float s)
{
    if (void* sp = cast(obj, 0x1186577))
        vcall<void>(sp, 0x40 / 4, s); // cSpatialObject::SetScale
}

bool set_object_position(void* obj, const net::Vec3& pos)
{
    void* sp = cast(obj, 0x1186577);
    if (!sp)
        return false;
    Vector3 p{pos.x, pos.y, pos.z};
    vcall<void>(sp, 0x38 / 4, &p); // cSpatialObject::SetPosition (as game::set_position of a creature)
    return true;
}

void* find_object(uint32_t noun, const net::Vec3& pos, float max_dist)
{
    struct Q {
        uint32_t noun;
        net::Vec3 pos;
        float best;
        void* obj;
    } q{noun, pos, max_dist * max_dist, nullptr};
    for_each_object(
        [](void* o, uint32_t n, void* u) {
            auto* q = (Q*)u;
            net::Vec3 p;
            if (n == q->noun && object_alive(o) && object_position(o, p)) {
                float dx = p.x - q->pos.x, dy = p.y - q->pos.y, dz = p.z - q->pos.z, d = dx * dx + dy * dy + dz * dz;
                if (d < q->best) {
                    q->best = d;
                    q->obj = o;
                }
            }
            return true;
        },
        &q);
    return q.obj;
}

void hit(void* c, float dmg, int type, void* attacker)
{
    Vector3 push{}; // no knockback, like a bite (see apply_damage)
    h_TakeDamage((uint8_t*)c + kCombatOff, dmg, attacker ? political_id(attacker) : 0xFFFFFFFFu, type, &push,
                 attacker ? (uint8_t*)attacker + kCombatOff : nullptr);
}

void hit_by_combatant(void* c, float dmg, int type, void* attacker_combatant, uint32_t pol)
{
    Vector3 push{};
    h_TakeDamage((uint8_t*)c + kCombatOff, dmg, pol, type, &push, attacker_combatant);
}

void hit_push(void* c, float dmg, int type, void* attacker, const net::Vec3& p)
{
    Vector3 push{p.x, p.y, p.z};
    h_TakeDamage((uint8_t*)c + kCombatOff, dmg, attacker ? political_id(attacker) : 0xFFFFFFFFu, type, &push,
                 attacker ? (uint8_t*)attacker + kCombatOff : nullptr);
}
static uintptr_t a_PlayAnim, a_PlayAnimTo;
static AnimListener g_anim_listener;
static bool g_in_puppet_anim;

typedef int(SK_THISCALL* PlayAnim_t)(void*, uint32_t, int, int);
typedef int(SK_THISCALL* PlayAnimTo_t)(void*, uint32_t, void*, int, int);
static PlayAnim_t o_PlayAnim;
static PlayAnimTo_t o_PlayAnimTo;

static void bhv_report(void* self, uint32_t id, int how, int a, int b);

static int SK_THISCALL h_PlayAnim(void* self, uint32_t id, int block, int disable)
{
    if (g_anim_listener && !g_in_puppet_anim)
        g_anim_listener(self, id, block, (disable & 0xFF) != 0, false);
    int r = o_PlayAnim(self, id, block, disable);
    if (!g_air.empty())
        air_reassert(self, 1);
    if (r)
        bhv_report(self, id, 1, disable & 0xFF, block);
    return r;
}

static int SK_THISCALL h_PlayAnimTo(void* self, uint32_t id, void* other, int a, int block)
{
    if (g_anim_listener && !g_in_puppet_anim)
        g_anim_listener(self, id, block, false, true);
    int r = o_PlayAnimTo(self, id, other, a, block);
    if (!g_air.empty())
        air_reassert(self, 1);
    if (r)
        bhv_report(self, id, 2, 0, block);
    return r;
}

void set_anim_listener(AnimListener fn) { g_anim_listener = fn; }

void play_animation(void* c, uint32_t anim_id, int block, bool disable_blend)
{
    g_in_puppet_anim = true;
    o_PlayAnim(c, anim_id, block, disable_blend ? 1 : 0);
    g_in_puppet_anim = false;
}

// ---- behaviour animations (BUG-0464) ----
// The behaviour tree's nodes (eat, look around, social answers, fights, the carnivores' idle node 0xD8DC30
// with B31EB92C ...) start their animations with SporeCoop::CreaturePlayAnim (0xC122F0), not with
// cCreatureBase::PlayAnimation. The creature's idle refresh 0xC148B0 (cCreatureAnimal::Update when no main
// animation is left, a jump landing, the BT nodes' ends) plays the idle / walk choice of 0xC0C790 through the
// same function: those plays are its own choice in every game and are not reported (depth counter).
typedef int(SK_THISCALL* CreaturePlayAnim_t)(void*, uint32_t, int, int);
typedef void(SK_THISCALL* IdleRefresh_t)(void*, int);
static CreaturePlayAnim_t o_CreaturePlayAnim;
static IdleRefresh_t o_IdleRefresh;
static BehaviourAnimListener g_bhv_listener;
static int g_idle_depth = 0;
CALL_GUARD_DEPTH("game.idle_depth", g_idle_depth); // BUG-1047
static uint32_t g_bhv_seen = 0, g_bhv_idle = 0;

static IdleAnimListener g_idle_listener;
static void bhv_report(void* self, uint32_t id, int how, int a, int b)
{
    if (g_idle_depth > 0) {
        g_bhv_idle++;
        if (g_idle_listener)
            g_idle_listener(self, id);
    }
    else if (g_bhv_listener && !g_in_puppet_anim) {
        g_bhv_seen++;
        g_bhv_listener(self, id, how, a, b);
    }
}

static int SK_THISCALL h_CreaturePlayAnim(void* self, uint32_t id, int a, int b)
{
    int r = o_CreaturePlayAnim(self, id, a, b);
    if (!g_air.empty())
        air_reassert(self, 3);
    if (r)
        bhv_report(self, id, 0, a, b);
    return r;
}

static void SK_THISCALL h_IdleRefresh(void* self, int force)
{
    {
        callguard::Depth idle(g_idle_depth);
        o_IdleRefresh(self, force);
    }
    if (!g_air.empty())
        air_reassert(self, 2);
}

void set_behaviour_anim_listener(BehaviourAnimListener fn) { g_bhv_listener = fn; }
void set_idle_anim_listener(IdleAnimListener fn) { g_idle_listener = fn; }
void idle_refresh(void* c, bool force)
{
    if (c && o_IdleRefresh)
        h_IdleRefresh(c, force ? 1 : 0);
}

bool play_behaviour_anim(void* c, uint32_t anim_id, int how, int a, int b)
{
    if (!c)
        return false;
    int r = 0;
    g_in_puppet_anim = true;
    if (how == 0 && o_CreaturePlayAnim)
        r = o_CreaturePlayAnim(c, anim_id, a, b);
    else if (how != 0 && o_PlayAnim) // (PlayAnimationTo's target is the host's object: played without one)
        r = o_PlayAnim(c, anim_id, b, a);
    g_in_puppet_anim = false;
    return r != 0;
}

void behaviour_anim_counts(uint32_t* seen, uint32_t* idle)
{
    *seen = g_bhv_seen;
    *idle = g_bhv_idle;
}

// ---- creature visual effects (BUG-0451) ----
// Every effect the game makes on or at a creature is created by one of the two effect pools of
// cCreatureBase (docs/re/creature.md#creature-effects-relay): pool 1 (map +0xCC8) by
// CreateEffectForPool1 / CreateEffectForPool1Renamed, pool 2 (map +0xD68) by
// CreateEffectForPool2Renamed, and stopped by StopEffectFromPools. They are hooked here; the
// listener decides whose effect it is and what to send. The relay receiver calls the game's own
// StartOrStopEffectFromPool1/2, so it gets the effect the same way the actor's game made it.
typedef void*(SK_THISCALL* CreateFx1_t)(void*, uint32_t);
typedef void*(SK_THISCALL* CreateFx1Keyed_t)(void*, uint32_t, uint32_t);
typedef void*(SK_THISCALL* CreateFx2Keyed_t)(void*, uint32_t, uint32_t);
typedef void*(SK_THISCALL* StartStopFx_t)(void*, int, uint32_t, uint32_t);
typedef int(SK_THISCALL* StopFx_t)(void*, uint32_t, uint32_t);
typedef void*(SK_THISCALL* GetFx_t)(void*, uint32_t);
static bool g_fx_log;
// support log only (coop.effects log on): the first return addresses into the game's code on the stack, the creator
// of an effect (no frame pointers in the game; BUG-0600 analysis)
struct FxFrom { uint32_t a[4] = {}; };
static FxFrom fx_from()
{
    FxFrom f;
    const uint32_t* sp = (const uint32_t*)__builtin_frame_address(0);
    for (int k = 0, n = 0; k < 128 && n < 4; k++) {
        uint32_t v = sp[k];
        if (v > 0x401000 && v < 0x1300000)
            f.a[n++] = v;
    }
    return f;
}
// The game calls effect creation from its UI skinner's render thread too (Tribe/Civ, measured in
// Skinner::cSkinPainterPipeline::ExecutePipeline). Everything of ours that touches game objects,
// the effect table or the network happens on the game's main thread only; on any other thread a
// hook body calls the original and returns. The id is set from the frame callback (main thread).
static std::atomic<unsigned long> g_main_tid{0};
static inline bool fx_main_thread() { return GetCurrentThreadId() == g_main_tid.load(std::memory_order_relaxed); }
static std::atomic<bool> g_fx_relay_on{false}; // effects.cpp's switch, read by the hooks
void set_effect_relay_enabled(bool on)
{
    g_main_tid.store(GetCurrentThreadId(), std::memory_order_relaxed); // effects::tick: main thread
    g_fx_relay_on.store(on, std::memory_order_relaxed);
}
// effect object -> id. A world effect created inside a creature effect creator must not be relayed
// twice, and effects.cpp reads the position the game gave it (effect +0x9C) in its tick. Open
// addressing keyed by the effect pointer: one probe for nearly every effect, fixed size, no
// allocation, no linear scan (the old scan over 4096 entries ran for every transform set of every
// effect in every stage). Cleared once per frame after effects::tick read what it needed.
struct FxCreated { void* fx; uint32_t id; bool relayed; bool foreign; };
static constexpr size_t kFxSlots = 1024; // power of two; far more than a frame creates
static FxCreated g_fx_ids[kFxSlots];
static uint32_t g_fx_creates, g_fx_probes, g_fx_evicted;
static uint32_t g_fx_mgr, g_fx_world, g_fx_anim; // creates seen per hook, animation effects skipped
static uint32_t g_fx_last_id; // the last effect id the game created (the perf workload uses it in every stage)
static uint32_t g_fx_last_main_id, g_fx_last_foreign_id; // ... in the game's own effect world / in another one
static uint32_t g_fx_foreign; // effects created in another effect world than the game's own (the skin painter's)
// the cost of our own work in the create hooks (the original call is not counted): the frame-time
// number in out/REPORT.md. Two QPC reads per created effect, on the main thread only.
static double g_fx_hook_sum, g_fx_hook_max;
static uint64_t g_fx_hook_calls;
static inline size_t fx_slot(void* fx)
{
    uint32_t h = (uint32_t)((uintptr_t)fx >> 4) * 2654435761u; // objects are at least 16-aligned
    return (h >> 20) & (kFxSlots - 1);
}
static FxCreated* fx_find(void* fx)
{
    size_t i = fx_slot(fx);
    for (unsigned n = 0; n < 6; n++, i = (i + 1) & (kFxSlots - 1)) {
        g_fx_probes++;
        if (g_fx_ids[i].fx == fx)
            return &g_fx_ids[i];
        if (!g_fx_ids[i].fx)
            return nullptr;
    }
    return nullptr;
}
static void fx_add(void* fx, uint32_t id, bool foreign = false)
{
    g_fx_creates++;
    size_t i = fx_slot(fx);
    for (unsigned n = 0; n < 6; n++, i = (i + 1) & (kFxSlots - 1)) {
        if (!g_fx_ids[i].fx || g_fx_ids[i].fx == fx) {
            // the manager's hook runs after the world's for the same object: keep a foreign mark
            bool was = g_fx_ids[i].fx == fx && g_fx_ids[i].foreign;
            g_fx_ids[i] = FxCreated{fx, id, false, foreign || was};
            return;
        }
    }
    g_fx_evicted++; // 6 probes over: take the home slot (does not happen with 1024 slots)
    g_fx_ids[fx_slot(fx)] = FxCreated{fx, id, false, foreign};
}
void clear_effect_table()
{
    for (auto& e : g_fx_ids)
        if (e.fx)
            e = FxCreated{};
}
void effect_table_stats(uint32_t& creates, uint32_t& probes, uint32_t& evicted, uint32_t& mgr, uint32_t& world,
                        uint32_t& anim)
{
    creates = g_fx_creates;
    probes = g_fx_probes;
    evicted = g_fx_evicted;
    mgr = g_fx_mgr;
    world = g_fx_world;
    anim = g_fx_anim;
}
void effect_hook_cost(double& sum, double& max, uint64_t& calls)
{
    sum = g_fx_hook_sum;
    max = g_fx_hook_max;
    calls = g_fx_hook_calls;
}
bool effect_position(void* fx, net::Vec3& out)
{
    // VisualEffect_SetTransform 0xA827B0 assigns Transform+4 to the math Vector3 object at effect
    // +0x9C: that object has a 4-byte header, its three floats are at +0xA0 (measured: reading
    // +0x9C gave (0, x, y)).
    if (!fx || !skg::readable((uint8_t*)fx + 0xA0, 12))
        return false;
    const float* p = (const float*)((uint8_t*)fx + 0xA0);
    out = net::Vec3{p[0], p[1], p[2]};
    return p[0] != 0 || p[1] != 0 || p[2] != 0;
}
// A bone index the game will accept for this creature: cCreatureBase::CreateEffect 0xC150D0
// indexes the model's bone array (AnimatedCreature +0xB54 -> +0x17C -> +0x2E4) with `bone * 700`
// without a check; the array's end (+0x2E8) gives the count (the fixture creature: 24 bones).
bool creature_bone_ok(void* c, int bone)
{
    if (!c || bone < 0 || !skg::readable((uint8_t*)c + 0xB54, 4))
        return false;
    uint8_t* ac = field<uint8_t*>(c, 0xB54);
    if (!ac || !skg::readable(ac + 0x17C, 4))
        return false;
    uint8_t* model = field<uint8_t*>(ac, 0x17C);
    if (!model || !skg::readable(model + 0x2E4, 8))
        return false;
    uint32_t begin = field<uint32_t>(model, 0x2E4), end = field<uint32_t>(model, 0x2E8);
    if (!begin || end <= begin || (end - begin) % 700)
        return false;
    uint32_t count = (end - begin) / 700;
    return count <= 1000 && (uint32_t)bone < count;
}

bool effect_relayed(void* fx)
{
    FxCreated* e = fx_find(fx);
    return e && e->relayed;
}
bool effect_foreign(void* fx)
{
    FxCreated* e = fx_find(fx);
    return e && e->foreign;
}
uint32_t last_effect_id(bool main_world_only) { return main_world_only ? g_fx_last_main_id : g_fx_last_id; }
uint32_t last_foreign_effect_id() { return g_fx_last_foreign_id; }
uint32_t foreign_effects() { return g_fx_foreign; }
static bool g_in_relay;        // a relayed effect played by us: never put it in the table again
static thread_local bool t_in_anim_fx; // inside the animation system's effect wrapper (main thread)
static WorldEffectListener g_world_listener;
static uintptr_t a_EffectsGet;
typedef int(SK_THISCALL* MgrCreate_t)(void*, uint32_t, uint32_t, void**);
static MgrCreate_t f_MgrCreate;
// the manager's create (cEffectsManager vt 0x2C): every IEffectsManager creation passes it
typedef int(SK_THISCALL* MgrCreateFx_t)(void*, uint32_t, uint32_t, void**);
static MgrCreateFx_t o_MgrCreateFx;
static int SK_THISCALL h_MgrCreateFx(void* mgr, uint32_t id, uint32_t a3, void** out)
{
    int r = o_MgrCreateFx(mgr, id, a3, out);
    if (!fx_main_thread() || !g_fx_relay_on.load(std::memory_order_relaxed))
        return r;
    if (t_in_anim_fx) {
        g_fx_anim++;
        return r;
    }
    if (g_in_relay)
        return r;
    if ((r & 0xFF) && out && *out) {
        double t0 = H->time_seconds();
        g_fx_mgr++;
        g_fx_last_id = id;
        g_fx_last_main_id = id; // the manager creates in the game's own effect world (manager +0x244)
        fx_add(*out, id);
        if (g_world_listener)
            g_world_listener(*out, id);
        double dt = H->time_seconds() - t0;
        g_fx_hook_sum += dt;
        g_fx_hook_max = dt > g_fx_hook_max ? dt : g_fx_hook_max;
        g_fx_hook_calls++;
    }
    return r;
}
typedef int(SK_THISCALL* WorldCreateFx_t)(void*, uint32_t, uint32_t, void**);
static WorldCreateFx_t o_WorldCreateFx;
static uintptr_t a_WorldCreateFx;
// the game's own effect world: the one cEffectsManager::CreateEffect forwards to (manager +0x244)
static void* main_effect_world()
{
    if (!a_EffectsGet)
        return nullptr;
    uint8_t* mgr = cdecl_call<uint8_t*>(a_EffectsGet);
    if (!mgr || !skg::readable(mgr + 0x244, 4))
        return nullptr;
    return *(void**)(mgr + 0x244);
}
static std::atomic<bool> g_world_on{false}; // the stage logic's gate (set_world_relay): no work outside a played stage
static int SK_THISCALL h_WorldCreateFx(void* world, uint32_t id, uint32_t a3, void** out)
{
    int r = o_WorldCreateFx(world, id, a3, out);
    if (!g_world_on.load(std::memory_order_relaxed) || !fx_main_thread()
        || !g_fx_relay_on.load(std::memory_order_relaxed))
        return r;
    if (g_fx_log) {
        FxFrom f = fx_from();
        H->log(M, SK_INFO, "fxw: world %p main %p id %08X ok %d anim %d relay %d from %X,%X,%X,%X", world, main_effect_world(), id,
               r & 0xFF, t_in_anim_fx ? 1 : 0, g_in_relay ? 1 : 0, f.a[0], f.a[1], f.a[2], f.a[3]);
    }
    if (t_in_anim_fx) {
        g_fx_anim++;
        return r;
    }
    if (g_in_relay)
        return r;
    if ((r & 0xFF) && out && *out) {
        double t0 = H->time_seconds();
        g_fx_world++;
        // BUG-0451 (Tribe / Civ): the skin painter (Skinner::cSkinPainterPipeline::ExecutePipeline) makes
        // its paint effects in an effect world of its own, on the main thread. Such an effect created in
        // the game's effect world (the relay's receiver, the perf load) has no painter and faults in
        // cSkinnerTexturePainter::StartRender every frame. Mark them: effects.cpp never sends them.
        static void* s_main_world; // the game's world changes only with a stage: ask the manager on a mismatch
        if (world != s_main_world)
            s_main_world = main_effect_world();
        bool foreign = world != s_main_world;
        if (foreign) {
            g_fx_foreign++;
            g_fx_last_foreign_id = id;
        } else {
            g_fx_last_main_id = id;
        }
        g_fx_last_id = id;
        fx_add(*out, id, foreign);
        if (g_world_listener)
            g_world_listener(*out, id);
        double dt = H->time_seconds() - t0;
        g_fx_hook_sum += dt;
        g_fx_hook_max = dt > g_fx_hook_max ? dt : g_fx_hook_max;
        g_fx_hook_calls++;
    }
    return r;
}
// The hooks are installed once, in init(); a hook removed while the UI skinner's render thread was
// inside it faulted (Skinner::cSkinnerTexturePainter::StartRender+0x28, thousands of caught faults
// in Tribe/Civ), so a stage change only flips this flag.
void set_world_relay(bool on) { g_world_on.store(on, std::memory_order_relaxed); }
// The animation system's effect wrapper 0x9FBF00: the animation relay (MSG_ANIM) already shows the
// effects of an animation on the partner's puppet, so the world create of such an effect must not
// be relayed again. The wrapper is called on the main thread; the flag is thread local so a worker
// thread can never read half of it (it replaces the caller-address check the old setter hook did).
static void anim_event_effect(void* fx, uint32_t id);
typedef int(SK_CDECL* AnimFxCreate_t)(void*, int, uint32_t, uint32_t, uint32_t, void**);
static AnimFxCreate_t o_AnimFxCreate;
static int SK_CDECL h_AnimFxCreate(void* world, int a, uint32_t id, uint32_t b, uint32_t c, void** out)
{
    bool was = t_in_anim_fx;
    t_in_anim_fx = true;
    int r = o_AnimFxCreate(world, a, id, b, c, out);
    t_in_anim_fx = was;
    if (out && *out) { // BUG-0603: an event's effect on another object than the animating one
        anim_event_effect(*out, id);
        if (g_fx_log && fx_main_thread()) {
            net::Vec3 p{};
            bool ok = effect_position(*out, p);
            FxFrom f = fx_from();
            H->log(M, SK_INFO, "fxanim: id %08X fx %p pos %d (%.2f %.2f %.2f) a %d b %08X c %08X from %X,%X,%X,%X", id, *out, ok ? 1 : 0,
                   p.x, p.y, p.z, a, b, c, f.a[0], f.a[1], f.a[2], f.a[3]);
        }
    }
    return r;
}

// BUG-0603 (docs/re/tribe.md#tribe-hit-impacts): an animation event (the handler 0x9A6D70, called by 0x9A7720 once per
// object the event's object spec resolves to) is made on an object of the event: param 2 is the animating instance,
// param 3 the resolved object's instance. An attack's hit impact is resolved to the attack's target (the victim the
// game gave the attacker's animation); a remote copy of the attacker (a guest's member mirror, a puppet, an NPC
// proxy) plays the relayed animation without a target, the spec resolves to nothing and its game never makes the
// impact. The hook notes the two instances for the effect wrapper above; an effect made for another object than
// the animating one goes to the listener with its id and position (effects.cpp relays it as a world effect).
typedef void(SK_CDECL* AnimEvent_t)(void*, void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t, uint32_t, uint32_t);
static AnimEvent_t o_AnimEvent;
static HitEffectListener g_hit_listener;
static uint32_t g_hit_seen, g_hit_calls, g_hit_last_other, g_hit_other_fx;
static thread_local void* t_evt_self;
static thread_local void* t_evt_obj;
static thread_local bool t_in_evt;
static void SK_CDECL h_AnimEvent(void* w, void* self, void* obj, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                                 uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13,
                                 uint32_t a14, uint32_t a15, uint32_t a16, uint32_t a17, uint32_t a18, uint32_t a19,
                                 uint32_t a20, uint32_t a21)
{
    bool was = t_in_evt;
    void *ws = t_evt_self, *wo = t_evt_obj;
    t_in_evt = true;
    t_evt_self = self;
    t_evt_obj = obj;
    o_AnimEvent(w, self, obj, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
    t_in_evt = was;
    t_evt_self = ws;
    t_evt_obj = wo;
}
// from h_AnimFxCreate: an animation event's effect was made
static void anim_event_effect(void* fx, uint32_t id)
{
    if (!t_in_evt || !fx_main_thread() || g_in_relay)
        return;
    g_hit_calls++;
    if (!t_evt_obj || t_evt_obj == t_evt_self) { // on the animating object itself: its remote copies make it too
        g_hit_other_fx++;
        return;
    }
    if (!g_world_on.load(std::memory_order_relaxed) || !g_fx_relay_on.load(std::memory_order_relaxed))
        return;
    net::Vec3 p{};
    if (!effect_position(fx, p))
        return;
    g_hit_seen++;
    g_hit_last_other = id;
    if (g_fx_log)
        H->log(M, SK_INFO, "fxhit: fx %p id %08X self %p obj %p at %.2f %.2f %.2f", fx, id, t_evt_self, t_evt_obj, p.x, p.y, p.z);
    if (g_hit_listener)
        g_hit_listener(id, p);
}
void set_hit_effect_listener(HitEffectListener fn) { g_hit_listener = fn; }
uint32_t hit_effects_seen() { return g_hit_seen; }
void hit_effect_calls(uint32_t& calls, uint32_t& last_other_ret, uint32_t& other_fx)
{
    calls = g_hit_calls;
    last_other_ret = g_hit_last_other;
    other_fx = g_hit_other_fx;
}

static void report_effect(void* c, const EffectEvent& ev);
typedef void*(SK_THISCALL* CreateCreatureFx_t)(void*, int, uint32_t, uint32_t);
static CreateCreatureFx_t o_CreateCreatureFx;
static void* SK_THISCALL h_CreateCreatureFx(void* self, int bone, uint32_t id, uint32_t extra)
{
    void* fx = o_CreateCreatureFx ? o_CreateCreatureFx(self, bone, id, extra) : nullptr;
    if (!fx_main_thread()) // the UI skinner's render thread: the original only
        return fx;
    if (g_fx_log)
        { FxFrom f = fx_from(); H->log(M, SK_INFO, "fxcr: self %p avatar %p id %08X bone %d fx %p from %X,%X,%X,%X", self, avatar(), id, bone, fx, f.a[0], f.a[1], f.a[2], f.a[3]); }
    if (fx && g_fx_relay_on.load(std::memory_order_relaxed)) {
        if (FxCreated* e = fx_find(fx))
            e->relayed = true;
        report_effect(self, {id, (uint32_t)bone, 3, false});
    }
    return fx;
}
static CreateFx1_t o_CreateFx1;
static CreateFx1Keyed_t o_CreateFx1Keyed;
static CreateFx2Keyed_t o_CreateFx2Keyed;
static StartStopFx_t o_StartStopFx1, o_StartStopFx2;
static StopFx_t o_StopFx;
static GetFx_t o_GetFx;
static EffectListener g_effect_listener;

static void report_effect(void* c, const EffectEvent& ev)
{
    if (g_effect_listener)
        g_effect_listener(c, ev);
}

static void* SK_THISCALL h_CreateFx1(void* self, uint32_t id)
{
    void* fx = o_CreateFx1(self, id);
    if (!fx_main_thread())
        return fx;
    if (g_fx_log) { FxFrom f = fx_from(); H->log(M, SK_INFO, "fx1: self %p avatar %p id %08X from %X,%X,%X,%X", self, avatar(), id, f.a[0], f.a[1], f.a[2], f.a[3]); }
    if (fx && g_fx_relay_on.load(std::memory_order_relaxed)) {
        if (FxCreated* e = fx_find(fx))
            e->relayed = true;
        report_effect(self, {id, id, 1, false});
    }
    return fx;
}

static void* SK_THISCALL h_CreateFx1Keyed(void* self, uint32_t id, uint32_t key)
{
    void* fx = o_CreateFx1Keyed(self, id, key);
    if (!fx_main_thread())
        return fx;
    if (g_fx_log) { FxFrom f = fx_from(); H->log(M, SK_INFO, "fx1k: self %p avatar %p id %08X key %08X from %X,%X,%X,%X", self, avatar(), id, key, f.a[0], f.a[1], f.a[2], f.a[3]); }
    if (fx && g_fx_relay_on.load(std::memory_order_relaxed)) {
        if (FxCreated* e = fx_find(fx))
            e->relayed = true;
        report_effect(self, {id, key, 1, false});
    }
    return fx;
}

static void* SK_THISCALL h_CreateFx2Keyed(void* self, uint32_t id, uint32_t key)
{
    void* fx = o_CreateFx2Keyed(self, id, key);
    if (!fx_main_thread())
        return fx;
    if (g_fx_log) { FxFrom f = fx_from(); H->log(M, SK_INFO, "fx2k: self %p avatar %p id %08X key %08X from %X,%X,%X,%X", self, avatar(), id, key, f.a[0], f.a[1], f.a[2], f.a[3]); }
    if (fx && g_fx_relay_on.load(std::memory_order_relaxed)) {
        if (FxCreated* e = fx_find(fx))
            e->relayed = true;
        report_effect(self, {id, key, 2, false});
    }
    return fx;
}

static int SK_THISCALL h_StopFx(void* self, uint32_t key, uint32_t arg)
{
    int r = o_StopFx(self, key, arg);
    if (r && fx_main_thread() && g_fx_relay_on.load(std::memory_order_relaxed))
        report_effect(self, {0, key, 0, true});
    return r;
}

void set_effect_listener(EffectListener fn) { g_effect_listener = fn; }
void set_world_effect_listener(WorldEffectListener fn) { g_world_listener = fn; }
void set_effect_log(bool on) { g_fx_log = on; }

static void* create_effect_at(uint32_t id, const void* xform, bool relayed)
{
    if (!a_EffectsGet || !f_MgrCreate || !xform)
        return nullptr;
    void* mgr = cdecl_call<void*>(a_EffectsGet);
    if (!mgr)
        return nullptr;
    void* fx = nullptr;
    bool was = g_in_relay;
    g_in_relay = relayed; // a relayed effect: the hooks must not send it back
    int ok = f_MgrCreate(mgr, id, 0, &fx);
    if ((ok & 0xFF) && fx) {
        vcall<void>(fx, 0x18 / 4, xform); // SetTransform
        vcall<void>(fx, 0x8 / 4, 0);      // Start
    } else {
        fx = nullptr;
    }
    g_in_relay = was;
    return fx;
}

// the receiver: create the partner's effect; the hooks skip it (no echo)
void* create_world_effect(uint32_t id, const void* xform) { return create_effect_at(id, xform, true); }

// diagnostics and the perf workload (coop.effects load): an effect the game would make at the local
// player, but one of ours; the relay hooks see it like a game effect and send it to the partners
void* create_local_effect(uint32_t id, const void* xform) { return create_effect_at(id, xform, false); }

void* create_creature_effect(void* c, int bone, uint32_t effect_id)
{
    if (!c || !o_CreateCreatureFx)
        return nullptr;
    bool was = g_in_relay;
    g_in_relay = true; // relayed: the hooks must not send it back
    void* fx = o_CreateCreatureFx(c, bone, effect_id, 0);
    g_in_relay = was;
    return fx;
}

void* start_effect(void* c, const EffectEvent& ev)
{
    if (!c)
        return nullptr;
    bool was = g_in_relay;
    g_in_relay = true; // relayed: the hooks must not send it back
    void* fx = ev.pool == 2 && o_StartStopFx2 ? o_StartStopFx2(c, 1, ev.id, ev.key)
                                                : o_StartStopFx1 ? o_StartStopFx1(c, 1, ev.id, ev.key) : nullptr;
    g_in_relay = was;
    return fx;
}

bool stop_effect(void* c, uint32_t key) { return c && o_StopFx && o_StopFx(c, key, 0) != 0; }

void* effect_from_pools(void* c, uint32_t key) { return c && o_GetFx ? o_GetFx(c, key) : nullptr; }

// an effect object we may call: it is alive and its vtable is readable (the slot calls below)
static bool effect_callable(void* effect)
{
    if (!effect || !skg::readable(effect, 4))
        return false;
    void** vt = *(void***)effect;
    return vt && skg::readable(vt, 0x20);
}

bool effect_active(void* effect)
{
    return effect_callable(effect) && vcall<bool>(effect, 0x10 / 4); // IVisualEffect::IsActive
}

// Stop an effect the relay created (vt 0xC, the slot StopEffectFromPools 0xC174B0 calls with its
// arg; 0 ends it). BUG-0451 review item 4: a looping relayed effect has no stop otherwise.
bool stop_world_effect(void* effect)
{
    if (!effect_callable(effect))
        return false;
    vcall<void>(effect, 0xC / 4, 0);
    return true;
}

// Drop the reference cEffectsManager::CreateEffect 0xA6CAD0 gave us (EffectWorld::CreateEffect
// 0xA71300 writes the effect into *out and calls its AddRef, vtable slot 0x0 at 0x923720; the
// game lets go of such a pointer with the Release slot 0x4 at 0xA81CE0, e.g. the pool map erase
// 0xD2C9F0). Without this the relay keeps every received world effect alive for the session.
// BUG-0585: Simulator::cTribe::UpdateFoodVisuals (0xC94CE0, thiscall(tribe, float change) ret 4) reads the tribe's
// food mat at +0x260 and calls [+0x260]+0x34 vt+0x2C (its position) without a null check. After the City Hall editor
// at the end of Tribe the player tribe has no food mat any more (+0x260 = 0) while the stage still runs for the
// vehicle editor's vote and captions; a guest's food change relayed then (shared.cpp) faulted the host's game at
// UpdateFoodVisuals+0x28 (data 0x34) inside the message pump and froze it. On: no visual without the food mat (the
// food itself is changed). `coop.fix tribe.food_fx_mat off`: called anyway (0.7.6).
static bool g_food_fx_fix = true;
COOP_FIX("tribe.food_fx_mat", "BUG-0585", g_food_fx_fix,
         "tribe: no +N/-N food visual on a tribe without its food mat (the end of Tribe: the City Hall stands)");
unsigned g_food_fx_skipped; // tests
bool tribe_food_visuals(void* tribe, float change)
{
    uintptr_t f = H->sym("Simulator::cTribe::UpdateFoodVisuals");
    if (!f || !tribe || !skg::readable((uint8_t*)tribe + 0x260, 4))
        return false;
    uint8_t* mat = *(uint8_t**)((uint8_t*)tribe + 0x260);
    if (g_food_fx_fix && (!mat || !skg::readable(mat + 0x34, 4) || !skg::readable(*(void**)(mat + 0x34), 0x30))) {
        if (g_food_fx_skipped++ < 3)
            H->log(M, SK_INFO, "tribe: no food visual: the tribe has no food mat (the City Hall stands, BUG-0585)");
        return false;
    }
    skg::thiscall<void>(f, tribe, change);
    return true;
}

bool release_world_effect(void* effect)
{
    if (!effect_callable(effect))
        return false;
    vcall<void>(effect, 0x4 / 4); // may destroy the object when this was the last reference
    return true;
}

typedef void(SK_THISCALL* NPCTickAI_t)(void*, float);
static NPCTickAI_t o_NPCTickAI;

static AiTargetPicker g_ai_picker;
static void* g_swap_real; // the real avatar while an NPC ticks with a puppet as mpAvatar
static void* g_swap_puppet; // ... the puppet written into mpAvatar, and the tuning lent to it
static void* g_swap_lent;
static uint32_t g_ai_swaps, g_swaps_restored;
static bool g_swap_guard = true;      // coop.npc swapguard off: no restore after an unwind (BUG-0110)
COOP_FIX("npc.swapguard", "BUG-0110", g_swap_guard, "creature: a fault in the NPC AI tick does not leave the puppet as the avatar");
static bool g_swap_fault_test = false; // tests: the next swapped tick leaves like a fault would
void set_ai_target_picker(AiTargetPicker fn) { g_ai_picker = fn; }
uint32_t ai_swaps() { return g_ai_swaps; }

static void SK_THISCALL h_NPCTickAI(void* self, float dt)
{
    if (is_puppet(self))
        return; // remote players drive their puppets, no local AI
    void* t = g_ai_picker && !g_swap_real ? g_ai_picker(self) : nullptr;
    void* nm = t ? cdecl_call<void*>(a_NounGet) : nullptr;
    void* real = nm ? field<void*>(nm, kNounAvatarOff) : nullptr;
    if (!real || t == real || t == self) {
        o_NPCTickAI(self, dt);
        return;
    }
    // the NPC's AI sees the partner as the player for this tick (its stimuli name the puppet as
    // the sender, so a chase or an attack started now goes on against it)
    g_swap_real = real;
    g_swap_puppet = t;
    field<void*>(nm, kNounAvatarOff) = t;
    // a puppet of the partner's own creature has no archetype tuning (+0xE84; its herd has no
    // archetype): code that reads the player's tuning would find none (the class of the 0xD48634
    // crash, docs/re/creature.md 4). It borrows the avatar's for the tick.
    void*& tun = field<void*>(t, 0xE84);
    void* lent = !tun ? field<void*>(real, 0xE84) : nullptr;
    if (lent)
        tun = lent;
    g_swap_lent = lent;
    g_ai_swaps++;
    o_NPCTickAI(self, dt);
    if (g_swap_fault_test) { // tests: as if the game faulted in the tick and its handler unwound past us
        g_swap_fault_test = false;
        return;
    }
    if (lent && tun == lent)
        tun = nullptr;
    field<void*>(nm, kNounAvatarOff) = real;
    g_swap_real = g_swap_puppet = g_swap_lent = nullptr;
}

// A fault inside o_NPCTickAI that the game's own handler catches unwinds past h_NPCTickAI: the
// restore above never runs, the puppet stays the game's avatar (mpAvatar), game::avatar() returns
// the stale real avatar for good and no NPC targets a partner again (BUG-0110). Called once per
// frame, outside every NPC tick: a swap still open here is such a leftover and is undone.
static void swap_ledger_tick();
// BUG-0882: a detour that lends game state around a call puts its undo into the swap ledger (views' relevance pass, the
// own creature's scale call); the mod's own call guards (inside a puppet's animation, an effect wrapper, an event,
// a relayed effect) are false outside every game call, so the frame's tick clears one a caught fault left set (a
// stuck guard switched its relay off for good). Off (`coop.fix game.lend_ledger off`): no entries, no reset.
static bool g_lend_ledger_fix = true;
COOP_FIX("game.lend_ledger", "BUG-0882", g_lend_ledger_fix, "creature: a fault inside the game's relevance pass or the own creature's scale call cannot leave the game's sphere list on the mod's buffer, the player species changed or a relay of the mod switched off");
bool lend_ledger_on() { return g_lend_ledger_fix; }
static uint32_t g_guards_reset;
// BUG-1047: the same rule for every module's call guard registered in call_guard.h (squadron, space_diplomacy,
// mirror_fx, spacecoop, civ, cellsync, tribe, ...): a guard still set here is a caught fault's leftover. Off
// (`coop.fix core.call_guard_reset off`): the modules' guards stay set (0.7.13).
static bool g_call_guard_fix = true;
COOP_FIX("core.call_guard_reset", "BUG-1047", g_call_guard_fix, "core: a module's call guard (g_applying and the like) left set by a fault the game caught is cleared on the next frame, the module's relay goes on");
static void reset_call_guards()
{
    callguard::reset(g_call_guard_fix);
    if (!g_lend_ledger_fix)
        return;
    if (g_in_puppet_anim || g_in_relay || t_in_anim_fx || t_in_evt)
        g_guards_reset++;
    g_in_puppet_anim = g_in_relay = t_in_anim_fx = t_in_evt = false;
}
uint32_t call_guards_reset() { return g_guards_reset; }
void swap_guard_tick()
{
    reset_call_guards();
    swap_ledger_tick();
    if (!g_swap_real || !g_swap_guard)
        return;
    void* nm = g_ready ? cdecl_call<void*>(a_NounGet) : nullptr;
    void* real = g_swap_real;
    if (nm && field<void*>(nm, kNounAvatarOff) == g_swap_puppet)
        field<void*>(nm, kNounAvatarOff) = real;
    if (g_swap_lent && g_swap_puppet && field<void*>(g_swap_puppet, 0xE84) == g_swap_lent)
        field<void*>(g_swap_puppet, 0xE84) = nullptr;
    g_swaps_restored++;
    H->log(M, SK_WARN, "creature AI: an NPC tick with a partner as the player did not return (a fault?): "
           "the avatar %p is the game's player again (puppet %p)", real, g_swap_puppet);
    g_swap_real = g_swap_puppet = g_swap_lent = nullptr;
}

static swapledger::Ledger<512> g_ledger;
size_t swap_open(void (*undo)(void*, uintptr_t), void* p, uintptr_t v, const char* what)
{
    return g_ledger.open(undo, p, v, what);
}
size_t swap_depth() { return g_ledger.n; }
void swap_close(size_t mark, size_t own) { g_ledger.close(mark, own); }
void swap_undo_u32(void* p, uintptr_t v)
{
    if (p && !IsBadWritePtr(p, 4))
        *(uint32_t*)p = (uint32_t)v;
}
void swap_undo_u8(void* p, uintptr_t v)
{
    if (p && !IsBadWritePtr(p, 1))
        *(uint8_t*)p = (uint8_t)v;
}
void swap_undo_or32(void* p, uintptr_t v)
{
    if (p && !IsBadWritePtr(p, 4))
        *(uint32_t*)p |= (uint32_t)v;
}
void swap_undo_ptr_if(void* p, uintptr_t v)
{
    if (p && !IsBadWritePtr(p, 4) && *(void**)p == (void*)v)
        *(void**)p = nullptr;
}
void debug_swap_ledger_fault() { g_ledger.fault = true; }
uint32_t swap_ledger_restored() { return g_ledger.restored; }

static void swap_ledger_tick()
{
    g_ledger.guard = g_swap_guard;
    if (unsigned k = g_ledger.tick())
        H->log(M, SK_WARN, "swap guard: %u change(s) around a game call were not put back (a fault?), undone now "
               "(last: %s)", k, g_ledger.last ? g_ledger.last : "?");
}

void set_swap_guard(bool on) { g_swap_guard = on; }
void debug_swap_fault() { g_swap_fault_test = true; }
uint32_t swaps_restored() { return g_swaps_restored; }

void play_death(void* c)
{
    if (!c || is_dead(c))
        return;
    vcall<void>(c, 0x8C / 4); // Die
    // the death node's default animation (by damage type it picks 0x54AC179 / 0x60DB981 /
    // 0x779E183, after a fall 0x92AA59CE), played like 0xC070D0 plays one: (anim, -1, 1)
    if (o_PlayAnim)
        play_animation(c, 0xF14FAA46u, -1, true);
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    static const char* req[] = {
        "Simulator::cGameNounManager::Get",
        "Simulator::cGameNounManager::DestroyInstance",
        "Simulator::cCreatureAnimal::Create",
        "Simulator::cCreatureAnimal::NPCTickAI",
        "App::IGameModeManager::Get",
    };
    if (H->sym_require(M, req, 5) != 0) {
        g_ready = false;
        return false;
    }
    a_NounGet = H->sym(req[0]);
    a_DestroyInstance = H->sym(req[1]);
    a_CreatureCreate = H->sym(req[2]);
    a_NPCTickAI = H->sym(req[3]);
    a_ModeMgrGet = H->sym(req[4]);
    if (H->hook_create(M, (void*)a_NPCTickAI, (void*)h_NPCTickAI, (void**)&o_NPCTickAI) != 0)
        return false;
    if (uintptr_t a = H->sym("Simulator::cCreatureAnimal::Update"))
        H->hook_create(M, (void*)a, (void*)h_Update, (void**)&o_Update);
    if (uintptr_t a = H->sym("Simulator::cCreatureAnimal::TakeDamage"))
        H->hook_create(M, (void*)a, (void*)h_TakeDamage, (void**)&o_TakeDamage);
    if (uintptr_t a = H->sym("Simulator::cCreatureGameData::AddEvolutionPoints"))
        H->hook_create(M, (void*)a, (void*)h_AddEvo, (void**)&o_AddEvo);
    if (uintptr_t a = H->sym("SporeCoop::Herd_Update"))
        H->hook_create(M, (void*)a, (void*)h_HerdUpdate, (void**)&o_HerdUpdate);
    a_CinematicGet = H->sym("Simulator::cCinematicManager::Get");
    a_SpeciesMgrGet = H->sym("Editors::cSpeciesManager::Get");
    a_GetSpecies = H->sym("Editors::cSpeciesManager::GetSpeciesProfile");
    a_CreateHerd = H->sym("Simulator::cGameNounManager::CreateHerd");
    a_PlayAnim = H->sym("Simulator::cCreatureBase::PlayAnimation");
    a_PlayAnimTo = H->sym("Simulator::cCreatureBase::PlayAnimationTo");
    if (a_PlayAnim)
        H->hook_create(M, (void*)a_PlayAnim, (void*)h_PlayAnim, (void**)&o_PlayAnim);
    if (uintptr_t a = H->sym("Anim::AnimatedCreature::PlayAnimation")) // BUG-0532
        H->hook_create(M, (void*)a, (void*)h_AcPlay, (void**)&o_AcPlay);
    if (a_PlayAnimTo)
        H->hook_create(M, (void*)a_PlayAnimTo, (void*)h_PlayAnimTo, (void**)&o_PlayAnimTo);
    // BUG-0464: the behaviour tree's animation starter and the idle refresh
    if (uintptr_t a = H->sym("SporeCoop::CreaturePlayAnim")) {
        uintptr_t idle = H->sym("SporeCoop::Creature_IdleRefresh");
        if (idle && H->hook_create(M, (void*)idle, (void*)h_IdleRefresh, (void**)&o_IdleRefresh) == 0)
            H->hook_create(M, (void*)a, (void*)h_CreaturePlayAnim, (void**)&o_CreaturePlayAnim);
        else
            H->log(M, SK_WARN, "behaviour anim hooks: no idle refresh hook (NPC animations not relayed)");
    }
    // BUG-0451: the two creature effect pools, their creators and the one stop
    struct {
        const char* name;
        void* hook;
        void** orig;
    } fx[] = {
        {"Simulator::cCreatureBase::CreateEffectForPool1", (void*)h_CreateFx1, (void**)&o_CreateFx1},
        {"Simulator::cCreatureBase::CreateEffectForPool1Renamed", (void*)h_CreateFx1Keyed, (void**)&o_CreateFx1Keyed},
        {"Simulator::cCreatureBase::CreateEffectForPool2Renamed", (void*)h_CreateFx2Keyed, (void**)&o_CreateFx2Keyed},
        {"Simulator::cCreatureBase::StopEffectFromPools", (void*)h_StopFx, (void**)&o_StopFx},
    };
    for (auto& f : fx)
        if (uintptr_t a = H->sym(f.name)) {
            int r = H->hook_create(M, (void*)a, f.hook, f.orig);
            H->log(M, r ? SK_WARN : SK_INFO, "effect hook %s at %p: %d", f.name, (void*)a, r);
        } else {
            H->log(M, SK_WARN, "effect hook %s: no symbol", f.name);
        }
    a_WorldCreateFx = H->sym("Swarm::EffectWorld::CreateEffect");
    if (uintptr_t a = H->sym("Swarm::cEffectsManager::CreateEffect")) {
        int r = H->hook_create(M, (void*)a, (void*)h_MgrCreateFx, (void**)&o_MgrCreateFx);
        H->log(M, r ? SK_WARN : SK_INFO, "fxmgr hook at %p: %d", (void*)a, r);
    }
    a_EffectsGet = H->sym("Swarm::IEffectsManager::Get");
    f_MgrCreate = (MgrCreate_t)H->sym("Swarm::cEffectsManager::CreateEffect");
    // Every hook is installed once here, for the life of the game (BUG-0451 review): the stage
    // logic only gates the work with set_world_relay/set_effect_relay_enabled. A hook removed while
    // the UI skinner's render thread was inside it faulted in Tribe/Civ.
    if (a_WorldCreateFx) {
        int r = H->hook_create(M, (void*)a_WorldCreateFx, (void*)h_WorldCreateFx, (void**)&o_WorldCreateFx);
        H->log(M, r ? SK_WARN : SK_INFO, "fxworld hook at %p: %d", (void*)a_WorldCreateFx, r);
    } else {
        H->log(M, SK_WARN, "fxworld hook: no symbol");
    }
    if (uintptr_t a = H->sym("SporeCoop::AnimEffect_Create")) {
        int r = H->hook_create(M, (void*)a, (void*)h_AnimFxCreate, (void**)&o_AnimFxCreate);
        H->log(M, r ? SK_WARN : SK_INFO, "fxanim hook at %p: %d", (void*)a, r);
    } else {
        H->log(M, SK_WARN, "fxanim hook: no symbol (animation effects may be relayed twice)");
    }
    if (uintptr_t a = H->sym("SporeCoop::AnimEvent_Handle")) {
        int r = H->hook_create(M, (void*)a, (void*)h_AnimEvent, (void**)&o_AnimEvent);
        H->log(M, r ? SK_WARN : SK_INFO, "fxevent hook at %p: %d", (void*)a, r);
    } else {
        H->log(M, SK_WARN, "fxevent hook: no symbol (the attacks' hit impacts are not relayed)");
    }
    if (uintptr_t a = H->sym("Simulator::cCreatureBase::CreateEffect")) {
        int r = H->hook_create(M, (void*)a, (void*)h_CreateCreatureFx, (void**)&o_CreateCreatureFx);
        H->log(M, r ? SK_WARN : SK_INFO, "fxcr hook at %p: %d", (void*)a, r);
    }
    o_StartStopFx1 = (StartStopFx_t)H->sym("Simulator::cCreatureBase::StartOrStopEffectFromPool1");
    o_StartStopFx2 = (StartStopFx_t)H->sym("Simulator::cCreatureBase::StartOrStopEffectFromPool2");
    o_GetFx = (GetFx_t)H->sym("Simulator::cCreatureBase::GetEffectFromPools");
    g_ready = true;
    return true;
}

bool ready() { return g_ready; }

uint32_t mode_id()
{
    if (!g_ready)
        return 0;
    void* mm = cdecl_call<void*>(a_ModeMgrGet);
    return mm ? vcall<uint32_t>(mm, 0x38 / 4) : 0;
}

bool in_creature_stage() { return mode_id() == 0x1654C01; }

// BUG-0264: the game leaves a stage for the galaxy menu (the quit message 0x4715068: a received
// world, coop.load, coop.menu) and tears the stage down while mode_id() still reads that stage: the
// noun manager's avatar field kept the freed avatar for about a second and every guest of a
// 4-player campaign faulted in max_health (its vtable read 0x01) from creature_fill. From the moment
// the quit is posted or handled until the mode changes (30 s at most) there is no avatar, and an
// avatar whose vtables are no longer the game's is never handed out. `coop.fix world.leave_avatar
// off`: the avatar field as it is (tests).
static bool g_leave_fix = true;
COOP_FIX("world.leave_avatar", "BUG-0264", g_leave_fix, "creature: no avatar while the game leaves the stage for the galaxy menu (it is freed before the mode changes)");
constexpr double kLeaveMax = 30.0;
static uint32_t g_leave_mode = 0; // the stage being left, 0 = none
static double g_leave_at = 0;
static LeaveStats g_leave;

void stage_leaving(const char* why)
{
    uint32_t m = g_ready ? mode_id() : 0;
    if (!m)
        return;
    if (g_leave_mode != m) {
        g_leave.windows++;
        H->log(M, SK_INFO, "game: leaving the stage 0x%08X (%s): no avatar until the mode changes", m, why);
    }
    g_leave_mode = m;
    g_leave_at = H->time_seconds();
}

void stage_leave_cancel()
{
    g_leave_mode = 0;
}

bool leaving_stage()
{
    if (!g_leave_mode)
        return false;
    if (mode_id() != g_leave_mode || H->time_seconds() - g_leave_at > kLeaveMax) {
        g_leave_mode = 0;
        return false;
    }
    return true;
}

const LeaveStats& leave_stats() { return g_leave; }

void* avatar_field()
{
    if (!g_ready)
        return nullptr;
    uint32_t m = mode_id();
    if (m != 0x1654C01 && m != 0x1654C10) // creature stage, adventures (the captain)
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    return nm ? field<void*>(nm, kNounAvatarOff) : nullptr;
}

// a creature whose object and combatant vtables are still the game's (a freed one read 0x01)
static bool creature_vtables_ok(void* c)
{
    return c && in_exe(*(void**)c) && in_exe(*(void**)((uint8_t*)c + kCombatOff));
}

void* avatar()
{
    if (!g_ready)
        return nullptr;
    uint32_t m = mode_id();
    if (m != 0x1654C01 && m != 0x1654C10) // creature stage, adventures (the captain)
        return nullptr;
    if (g_leave_fix && leaving_stage()) {
        g_leave.refused++;
        return nullptr;
    }
    if (g_swap_real) // inside an NPC's AI tick that sees a puppet as the player
        return g_swap_real;
    void* nm = cdecl_call<void*>(a_NounGet);
    if (!nm)
        return nullptr;
    void* av = field<void*>(nm, kNounAvatarOff);
    if (g_leave_fix && av && !creature_vtables_ok(av)) {
        g_leave.bad_vtable++;
        return nullptr;
    }
    return av;
}

// once per frame (coop.cpp on_frame, after g_local is set): while the stage is being left, is the
// avatar still the game's? `used` = what the mod took as the local entity this frame. A frame where
// the game's own object list no longer has the avatar the mod uses is the bug (tests).
void leave_probe_tick(void* used)
{
    bool leaving = leaving_stage();
    if (!leaving) {
        // outside the window, once a second: an avatar the game no longer lists while the stage
        // runs (not seen in the lab; evidence for the next campaign, logged once per avatar)
        static double next = 0;
        static void* reported = nullptr;
        double now = H->time_seconds();
        if (now < next)
            return;
        next = now + 1.0;
        void* raw = avatar_field();
        if (raw && raw != reported && !(creature_vtables_ok(raw) && creature_listed(raw))) {
            reported = raw;
            g_leave.stage_gone++;
            H->log(M, SK_WARN, "game: the avatar %p is no longer in the game's object list (mode 0x%08X, not leaving)",
                   raw, mode_id());
        }
        return;
    }
    g_leave.frames++;
    void* raw = avatar_field();
    bool raw_ok = raw && creature_vtables_ok(raw) && creature_listed(raw);
    if (raw && !raw_ok)
        g_leave.raw_gone++;
    bool used_bad = used && used == raw && !raw_ok;
    if (used_bad)
        g_leave.used_gone++;
    H->log(M, used_bad ? SK_WARN : SK_INFO, "game: leaving frame %.3f s after the quit: avatar %p %s, used %p",
           H->time_seconds() - g_leave_at, raw, !raw ? "none" : raw_ok ? "listed" : "GONE", used);
}

// BUG-0119: which creature world the game plays, as a counter. The avatar's address is no world
// identity: the game pools creature objects, the avatar of a reloaded world can have the old
// address. The counter goes up every time the creature stage is entered (a load or a received
// world goes through the galaxy menu and the load mode; the editors keep it) and when the avatar
// is replaced by another one inside the stage. `coop.npc worldfix off`: 0 always (the old checks
// by address only, tests).
static uint32_t g_world_epoch = 1;
static bool g_epoch_in_world = false;
static void* g_epoch_avatar = nullptr;
static bool g_epoch_fix = true;
COOP_FIX("npc.world", "BUG-0119", g_epoch_fix, "creature: worlds told apart by a counter, not by the avatar address (coop.npc worldfix)");
void set_world_epoch_fix(bool on) { g_epoch_fix = on; }

uint32_t world_epoch()
{
    uint32_t m = mode_id();
    bool editor = m == 0x1654C06 || m == 0x00DBDBA1; // kEdit, kEditor: the world stays
    if (m != 0x1654C01 && !(editor && g_epoch_in_world)) {
        g_epoch_in_world = false;
        g_epoch_avatar = nullptr;
    } else if (m == 0x1654C01) {
        void* av = avatar();
        if (!g_epoch_in_world || (av && g_epoch_avatar && av != g_epoch_avatar))
            g_world_epoch++;
        g_epoch_in_world = true;
        if (av)
            g_epoch_avatar = av;
    }
    return g_epoch_fix ? g_world_epoch : 0;
}

static stage_epoch::State g_stage_epochs;
void stage_epoch_tick() { stage_epoch::step(g_stage_epochs, mode_id()); }
uint32_t stage_epoch(uint32_t stage_mode) { return stage_epoch::get(g_stage_epochs, stage_mode); }

net::Vec3 position(void* c)
{
    auto& v = field<Vector3>(c, kPosOff);
    return {v.x, v.y, v.z};
}

net::Quat orientation(void* c)
{
    auto& q = field<Quaternion>(c, kRotOff);
    return {q.x, q.y, q.z, q.w};
}

net::Vec3 velocity(void* c)
{
    auto& v = field<Vector3>(c, kVelOff);
    return {v.x, v.y, v.z};
}

float health(void* c) { return field<float>(c, kHpOff); }

bool gait_state(void* c, int* gait, int* gait2, int* moving, int* speed_state)
{
    if (!c || !skg::readable(c, 0xF9C))
        return false;
    uint8_t* ac = field<uint8_t*>(c, 0xB54);
    if (!ac || !skg::readable(ac, 0x188))
        return false;
    uint8_t* cid = field<uint8_t*>(ac, 0x17C);
    if (!cid || !skg::readable(cid + 0x15B8, 8))
        return false;
    *gait = field<int>(cid, 0x15B8);
    *gait2 = field<int>(cid, 0x15BC);
    *moving = field<uint8_t>(c, 0x137);
    *speed_state = field<int>(c, 0xF98);
    return true;
}

bool grounded(void* c) { return c && skg::readable(c, 0x138) && field<uint8_t>(c, 0x137) != 0; }
void set_grounded(void* c, bool on)
{
    if (c && skg::readable(c, 0x138))
        field<uint8_t>(c, 0x137) = on ? 1 : 0;
}

bool drawn_state(void* c, net::Vec3* drawn, uint32_t* anim)
{
    if (!c || !skg::readable((uint8_t*)c + 0xB54, 4))
        return false;
    uint8_t* ac = field<uint8_t*>(c, 0xB54);
    if (!ac || !skg::readable(ac, 0x188) || !skg::readable(*(void**)ac, 0x5C))
        return false;
    if (drawn) {
        auto& p = field<Vector3>(ac, 0x04);
        *drawn = {p.x, p.y, p.z};
    }
    if (anim) {
        // GetCurrentAnimation(u32* id, float* time, int*, int* index), thiscall ret 0x10 (GOG 2016
        // 0xA05030, vtable 0x1449640): writes the id only when the creature has a main animation
        uint32_t id = 0;
        vcall<void>(ac, 0x58 / 4, &id, (float*)nullptr, (int*)nullptr, (int*)nullptr);
        *anim = id;
    }
    return true;
}
// cCombatant::GetMaxHitPoints (vtable 0x58); the +0x2C field holds 1.0 for avatars in this build
float max_health(void* c)
{
    float m = c ? skg::vcall<float>((uint8_t*)c + kCombatOff, 0x58 / 4) : 0.f;
    return m > 0 ? m : field<float>(c, kMaxHpOff);
}
bool is_dead(void* c) { return field<uint8_t>(c, kDeadOff) != 0; }
uint32_t species_instance(void* c) { return field<uint32_t>(c, kSpeciesKeyOff); }
void* species_profile(void* c) { return field<void*>(c, kSpeciesOff); }

void* avatar_herd()
{
    if (!g_ready || !avatar())
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    return nm ? field<void*>(nm, kNounAvatarHerdOff) : nullptr;
}

void* spawn_creature(const net::Vec3& pos, void* species, void* herd, bool reuse)
{
    if (!g_ready || !species || !herd)
        return nullptr;
    Vector3 p{pos.x, pos.y, pos.z};
    // static cCreatureAnimal* Create(const Vector3&, cSpeciesProfile*, int age,
    //                                cHerd*, bool isAvatar, bool reuse)   (cdecl)
    return cdecl_call<void*>(a_CreatureCreate, &p, species, 1, herd, 0, reuse ? 1 : 0);
}

// A cinematic of the creature stage (CRG_ClaimNest, CRG_SpeciesMigrate, ...) takes creatures as
// actors when it starts (the avatar herd's members: the family at the nest) and places them later,
// in callbacks the cinematic manager runs from a posted message (0x4470A41): an actor "relative to
// its herd's nest" reads creature +0x1674 (mHerd) -> +0x160. A creature destroyed in between has no
// herd any more (DestroyInstance -> 0xC083D0 clears +0x1674) and the game faulted at 0xC6AE60
// (read 0x160, a guest's crash right after the host "completed migration to a new nest": the
// guest's own game claimed the nest when it arrived there and cast the family the update filter
// was removing, docs/re/creature.md 16). Creatures we remove while a cinematic plays go when it is
// over.
// the creature world the pointers belong to: the noun manager's avatar (it stays through the
// editors, a load or a stage change replaces it) and the world counter (BUG-0119: a reused avatar
// address is not the same world)
struct WorldToken {
    void* av;
    uint32_t epoch;
    bool operator!=(const WorldToken& o) const { return av != o.av || epoch != o.epoch; }
};
static WorldToken world_now()
{
    void* nm = g_ready ? cdecl_call<void*>(a_NounGet) : nullptr;
    return {nm ? field<void*>(nm, kNounAvatarOff) : nullptr, world_epoch()};
}
struct LaterDestroy {
    void* first;           // the creature
    WorldToken second;     // the world when queued
    uint32_t serial = 0;   // BUG-0825: its cGameData::mID when queued
};
static std::vector<LaterDestroy> g_destroy_later;
static uint32_t g_destroys_deferred = 0, g_destroys_late = 0, g_destroys_not_same = 0;
// BUG-0825 (class BUG-0426): a creature of ours queued for destruction during a cinematic was destroyed when the
// cinematic ended if its address was still a listed creature; the game may have freed it meanwhile and built another
// creature there (an NPC of the world: the block comes back within frames), which then went too, and counted as a
// puppet (no AI, stripped from saves) while queued. Now the queue keeps the serial: an entry whose object is not the
// one queued is dropped (and is no puppet of ours) in every frame, also during the cinematic.
// Old (`coop.fix npc.defer_identity off`): the address.
static bool g_defer_identity_fix = true;
COOP_FIX("npc.defer_identity", "BUG-0825", g_defer_identity_fix, "creature: a creature that the game built where a copy waiting for the end of a cinematic was is never destroyed with it");
static bool g_destroy_defer = true; // tests: off = at once, as before (coop.npc cinedefer off)
COOP_FIX("npc.cinedefer", "BUG-0018", g_destroy_defer, "creature: creatures removed during a cinematic go when it is over");
void set_destroy_defer(bool on) { g_destroy_defer = on; }
static bool g_defer_keeps_puppet = true; // tests: off = unregistered at once (coop.npc deferpuppet off, BUG-0066)
COOP_FIX("npc.deferpuppet", "BUG-0066", g_defer_keeps_puppet, "creature: a deferred puppet or proxy keeps its puppet state");
void set_defer_keeps_puppet(bool on) { g_defer_keeps_puppet = on; }

bool destroy_pending(void* c)
{
    for (auto& d : g_destroy_later)
        if (d.first == c)
            return true;
    return false;
}

static bool cinematic_running()
{
    void* cm = a_CinematicGet ? cdecl_call<void*>(a_CinematicGet) : nullptr;
    return cm && field<uint32_t>(cm, 0x2C) != 0; // cCinematicManager state: 0 none, 2 playing
}

bool cinematic_playing() { return g_ready && cinematic_running(); }

void destroy_creature(void* c)
{
    if (!g_ready || !c)
        return;
    bool defer = g_destroy_defer && noun_id(c) == 0x18EB45Eu && cinematic_running();
    // a puppet or proxy that waits stays one (BUG-0066): no AI of its own (h_NPCTickAI), not an
    // NPC for host_tick to send to the guests. It leaves the puppet list when it is destroyed.
    if (!defer || !g_defer_keeps_puppet)
        puppet_unregister(c); // also used for UFOs and other cGameData puppets
    if (defer) {
        if (destroy_pending(c))
            return;
        g_destroy_later.push_back({c, world_now(), object_serial(c)});
        g_destroys_deferred++;
        return;
    }
    void* nm = cdecl_call<void*>(a_NounGet);
    if (nm)
        thiscall<void>(a_DestroyInstance, nm, c);
}

void destroy_instance(void* obj)
{
    void* nm = g_ready && obj ? cdecl_call<void*>(a_NounGet) : nullptr;
    if (nm)
        thiscall<void>(a_DestroyInstance, nm, obj);
}

// tests (BUG-0507): the game destroys c as it does on its own (cGameNounManager::DestroyInstance), without
// the mod's bookkeeping (no puppet_unregister, no deferring): a puppet freed behind the mod's back
void destroy_untracked(void* c)
{
    void* nm = g_ready && c ? cdecl_call<void*>(a_NounGet) : nullptr;
    if (nm)
        thiscall<void>(a_DestroyInstance, nm, c);
}

void destroy_deferred_tick()
{
    if (g_destroy_later.empty())
        return;
    WorldToken world = world_now();
    // another world since (a load, the stage left): the pointer may be another object now. It is
    // dropped at once, also while a cinematic of the new world plays, and is no puppet of ours
    for (auto it = g_destroy_later.begin(); it != g_destroy_later.end();)
        if (it->second != world) {
            H->log(M, SK_INFO, "deferred destroy of %p dropped: another world (avatar %p epoch %u -> %p %u)", it->first,
                   it->second.av, it->second.epoch, world.av, world.epoch);
            puppet_unregister(it->first);
            it = g_destroy_later.erase(it);
        } else
            ++it;
    if (g_defer_identity_fix) // BUG-0825: freed meanwhile, or another creature at its address: not ours any more
        for (auto it = g_destroy_later.begin(); it != g_destroy_later.end();)
            if (!alive(LiveRef{it->first, it->serial, 0x18EB45Eu})) {
                H->log(M, SK_INFO, "deferred destroy of %p dropped: not the creature queued any more (npc.defer_identity)",
                       it->first);
                puppet_unregister(it->first);
                g_destroys_not_same++;
                it = g_destroy_later.erase(it);
            } else
                ++it;
    if (g_destroy_later.empty() || cinematic_running())
        return;
    auto later = std::move(g_destroy_later);
    g_destroy_later.clear();
    void* nm = cdecl_call<void*>(a_NounGet);
    for (auto& d : later) {
        void* c = d.first;
        puppet_unregister(c);
        if (!creature_listed(c) || !nm)
            continue;
        thiscall<void>(a_DestroyInstance, nm, c);
        g_destroys_late++;
    }
}

std::string destroy_deferred_status()
{
    char b[120];
    snprintf(b, sizeof b, "destroys during cinematics: deferred=%u done_after=%u pending=%u not_same=%u", g_destroys_deferred,
             g_destroys_late, (unsigned)g_destroy_later.size(), g_destroys_not_same);
    return b;
}

static void* spatial(void* c) { return ptr_add<void>(c, kSpatialOff); }

// BUG-0614: a pose that came from another player (a state, a snapshot) reached the game unchecked outside Cell: a NaN
// or Inf position put the body nowhere (the game's physics and camera then work with NaN). Every write of a position
// or orientation into a body is checked here, once for all stages. Off: 0.7.8 (written as received).
static bool g_finite_fix = true;
COOP_FIX("game.finite_pose", "BUG-0614", g_finite_fix, "all: a position or rotation with NaN / Inf is never written into a body");
uint32_t g_nonfinite_drops = 0; // writes refused (coop.sync, tests)

static bool finite_v(const net::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
static bool finite_q(const net::Quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }
static bool pose_refused(const net::Vec3* p, const net::Quat* q)
{
    if (!g_finite_fix || ((!p || finite_v(*p)) && (!q || finite_q(*q))))
        return false;
    g_nonfinite_drops++;
    return true;
}

// BUG-0434: the game puts a creature that is off the observer's screen on the plain terrain (GetHeightAt) while it
// stands on raised ground (a nest mound): with the camera turned away the partner's puppet on a mound was drawn
// 0.8-1 m low in every frame off the screen and in the first frame back in view (the update before that frame's
// drawing still grounded it; writes at the mod's frame were undone by it). Not the physics' simple path
// (+0xF90 = 1, LOD +0xEAC = 2 in every such frame, out/lab/20261106-042950). After the physics manager's update
// (SporeCoop::PhysicsManager_Update, hooked by gamefixes.cpp for BUG-0449) a puppet whose target stands > 0.3 m over GetHeightAt and that was put within
// 0.1 m of it is lifted back to its target's height over the ground. Off: 0.7.8.
static bool g_culled_ground_fix = true;
COOP_FIX("creature.culled_ground", "BUG-0434", g_culled_ground_fix, "creature: the partner's creature on a nest mound stays on it while off the screen (no low frame when it comes into view)");
struct MoundHold {
    void* c;
    net::Vec3 tgt;
    double t;
};
static std::vector<MoundHold> g_mound;
uint32_t g_mound_lifts = 0;

void mound_hold(void* c, const net::Vec3& tgt)
{
    double now = H->time_seconds();
    for (auto& m : g_mound)
        if (m.c == c) {
            m.tgt = tgt;
            m.t = now;
            return;
        }
    if (g_mound.size() < 16)
        g_mound.push_back({c, tgt, now});
}

// tests (BUG-1082, coop.tribe hang): creatures held at a height over the terrain after every physics update
struct TestHang {
    void* c;
    float agl;
    double until;
};
static std::vector<TestHang> g_test_hang;
void test_hang(void* c, float agl, double secs)
{
    for (auto& h : g_test_hang)
        if (h.c == c) {
            h = {c, agl, H->time_seconds() + secs};
            return;
        }
    g_test_hang.push_back({c, agl, H->time_seconds() + secs});
}
static void test_hang_apply()
{
    double now = H->time_seconds();
    for (size_t i = 0; i < g_test_hang.size();) {
        auto& h = g_test_hang[i];
        if (now > h.until || !object_alive(h.c)) {
            if (object_alive(h.c))
                air_hold(h.c, false);
            g_test_hang.erase(g_test_hang.begin() + i);
            continue;
        }
        net::Vec3 p = position(h.c);
        float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), g = ground_radius(p);
        if (r > 1.f && g > 1.f) {
            float k = (g + h.agl) / r;
            net::Vec3 to{p.x * k, p.y * k, p.z * k};
            net::Quat q = orientation(h.c);
            place(h.c, to, q);
            set_grounded(h.c, false);
            field<net::Vec3>(h.c, kVelOff) = {0, 0, 0};
            air_hold(h.c, true, to, q, {0, 0, 0});
        }
        i++;
    }
}

// BUG-1082 (docs/re/tribe.md#tribe-member-hang): a guest's mirror placed in the air (air_place / air_hold: a member the
// host's game holds 12-110 m up) stood on the guest's terrain in most reads whenever the guest's camera was elsewhere:
// the guest's physics update puts a creature off its screen onto the plain terrain (the BUG-0434 path) and air_reassert
// only acts on the grounded byte, which that path does not set (lab 20261113-182546: the list received 5 times a
// second, the mirror placed every frame, read on its ground in 12 of 14 reads). After the game's physics update every
// held creature that is more than 0.5 m under its hold on its radial line is put back at the hold (not grounded, the
// hold's velocity). `coop.fix game.air_culled off`: as 0.7.13.
static bool g_air_culled_fix = true;
COOP_FIX("game.air_culled", "BUG-1082", g_air_culled_fix,
         "a creature the mod holds in the air (a partner's, a mirror, a proxy) stays there off the screen too");
static uint32_t g_air_culled_lifts = 0;
uint32_t air_culled_lifts() { return g_air_culled_lifts; }
static void air_culled_apply()
{
    for (auto& h : g_air) {
        if (!object_alive(h.c) || (h.serial && object_serial(h.c) != h.serial))
            continue;
        net::Vec3 p = position(h.c);
        float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), rh = std::sqrt(h.pos.x * h.pos.x + h.pos.y * h.pos.y + h.pos.z * h.pos.z);
        if (r < 1.f || rh - r < 0.5f)
            continue;
        float k = rh / r; // (the point on the hold's height over where the game put it: its walk across the frame stays)
        net::Vec3 up{p.x * k - h.pos.x, p.y * k - h.pos.y, p.z * k - h.pos.z};
        if (up.x * up.x + up.y * up.y + up.z * up.z > 4.f) // moved more than 2 m sideways: not a drop onto the terrain
            continue;
        place(h.c, h.pos, h.rot);
        set_grounded(h.c, false);
        field<net::Vec3>(h.c, kVelOff) = h.vel;
        g_air_culled_lifts++;
    }
}

void after_physics() // gamefixes.cpp's hook of the physics update (BUG-0449), after the game's update
{
    if (!g_test_hang.empty())
        test_hang_apply();
    if (g_air_culled_fix && !g_air.empty())
        air_culled_apply();
    if (!g_place_hold.empty()) { // BUG-0440 (a copy: reassert may drop entries)
        void* cs[64];
        size_t n = std::min<size_t>(g_place_hold.size(), 64);
        for (size_t i = 0; i < n; i++)
            cs[i] = g_place_hold[i].c;
        for (size_t i = 0; i < n; i++)
            if (creature_listed(cs[i]))
                place_reassert(cs[i]);
    }
    if (!g_culled_ground_fix || g_mound.empty())
        return;
    double now = H->time_seconds();
    for (size_t i = 0; i < g_mound.size();) {
        auto& m = g_mound[i];
        if (now - m.t > 0.5 || !creature_listed(m.c)) {
            g_mound.erase(g_mound.begin() + i);
            continue;
        }
        i++;
        net::Vec3 p = position(m.c);
        float gp = ground_radius(p), gt = ground_radius(m.tgt);
        float rp = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), rt = std::sqrt(m.tgt.x * m.tgt.x + m.tgt.y * m.tgt.y + m.tgt.z * m.tgt.z);
        if (gp <= 1.f || gt <= 1.f || rp <= 1.f || rt - gt < 0.3f || rp - gp > 0.1f || std::sqrt((p.x - m.tgt.x) * (p.x - m.tgt.x) + (p.y - m.tgt.y) * (p.y - m.tgt.y) + (p.z - m.tgt.z) * (p.z - m.tgt.z)) > 2.5f)
            continue;
        float k = (gp + (rt - gt)) / rp;
        set_position(m.c, {p.x * k, p.y * k, p.z * k});
        g_mound_lifts++;
    }
}

std::string objects_near(const net::Vec3& pos, float r, void* self, int max_n)
{
    struct U {
        net::Vec3 p;
        float r;
        void* self;
        std::vector<std::pair<float, uint32_t>> v;
    } u{pos, r, self, {}};
    for_each_object(
        [](void* o, uint32_t noun, void* up) {
            auto* u = (U*)up;
            if (o == u->self || u->v.size() > 256)
                return true;
            net::Vec3 q;
            if (!object_position(o, q))
                return true;
            float d = std::sqrt((q.x - u->p.x) * (q.x - u->p.x) + (q.y - u->p.y) * (q.y - u->p.y) + (q.z - u->p.z) * (q.z - u->p.z));
            if (d <= u->r)
                u->v.push_back({d, noun});
            return true;
        },
        &u);
    std::sort(u.v.begin(), u.v.end());
    std::string out;
    char b[40];
    for (int i = 0; i < (int)u.v.size() && i < max_n; i++) {
        snprintf(b, sizeof b, "%s%08X@%.1f", out.empty() ? "" : " ", u.v[i].second, u.v[i].first);
        out += b;
    }
    return out.empty() ? "none" : out;
}

// BUG-0894: the host's NPC and Tribe member snapshots carry a velocity that the drivers write raw into the body
// (kVelOff) or turn into SetDesiredSpeed: a NaN / Inf there (the BUG-0292 class) moved the proxy to a NaN position, and
// pose_refused guards only positions and rotations. Such a velocity is taken as 0 where the snapshot is received.
// `coop.fix game.finite_vel off`: as received (0.7.11).
static bool g_finite_vel_fix = true;
COOP_FIX("game.finite_vel", "BUG-0894", g_finite_vel_fix, "creature / tribe: a NaN or Inf velocity in the host's NPC or member snapshot is taken as 0, never written into a body");
static uint32_t g_vel_zeroed = 0;
uint32_t vel_zeroed() { return g_vel_zeroed; }
net::Vec3 finite_vel(const net::Vec3& v)
{
    if (!g_finite_vel_fix || (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
                              std::fabs(v.x) < 1e4f && std::fabs(v.y) < 1e4f && std::fabs(v.z) < 1e4f))
        return v;
    if (g_vel_zeroed++ < 5 && H)
        H->log(M, SK_WARN, "a snapshot velocity (%g %g %g) is not finite: taken as 0 (BUG-0894)", v.x, v.y, v.z);
    return {};
}

bool pose_bad(const net::Vec3& pos, const net::Quat& rot, const net::Vec3& vel)
{
    return pose_refused(&pos, &rot) || pose_refused(&vel, nullptr);
}

void teleport(void* c, const net::Vec3& pos, const net::Quat& rot)
{
    if (pose_refused(&pos, &rot))
        return;
    Vector3 p{pos.x, pos.y, pos.z};
    Quaternion q{rot.x, rot.y, rot.z, rot.w};
    vcall<void>(spatial(c), 0x44 / 4, &p, &q);
}

void place(void* c, const net::Vec3& pos, const net::Quat& rot)
{
    if (pose_refused(&pos, &rot))
        return;
    teleport(c, pos, rot);
    field<uint8_t>(c, 0xB5D) = 0;
}

void air_hold(void* c, bool on, const net::Vec3& pos, const net::Quat& rot, const net::Vec3& vel)
{
    for (size_t i = 0; i < g_air.size(); i++)
        if (g_air[i].c == c) {
            if (on) // (the serial stays the one of the creature the hold was made for: BUG-0995)
                g_air[i] = {c, field<void*>(c, 0xB54), pos, vel, rot, g_air[i].serial};
            else
                g_air.erase(g_air.begin() + i);
            return;
        }
    if (on && g_air.size() < 64) // (BUG-0550: NPC proxies in the air too)
        g_air.push_back({c, field<void*>(c, 0xB54), pos, vel, rot, object_serial(c)});
}

void air_release_all() { g_air.clear(); } // (also the lifecycle entry game.air above)

static void air_reassert(void* c, int where)
{
    for (size_t i = 0; i < g_air.size(); i++)
        if (AirHold& h = g_air[i]; h.c == c) {
            if (g_air_live_fix && h.serial && object_serial(c) != h.serial) { // BUG-0995: another creature there now
                g_air_stale++;
                g_air.erase(g_air.begin() + i);
                return;
            }
            if (!grounded(c))
                return;
            place(c, h.pos, h.rot);
            set_grounded(c, false);
            *(net::Vec3*)((uint8_t*)c + kVelOff) = h.vel;
            g_air_regrounded[where]++;
            return;
        }
}

// the animation system's own start (Anim::AnimatedCreature::PlayAnimation 0xA0C5D0, a vtable slot every start
// goes through, also the locomotion's: the walk start, fall -> idle)
static int SK_THISCALL h_AcPlay(void* ac, uint32_t id, int a)
{
    int r = o_AcPlay(ac, id, a);
    if (!g_air.empty())
        for (auto& h : g_air)
            if (h.ac == ac) {
                air_reassert(h.c, 4); // (may drop h: BUG-0995)
                break;
            }
    return r;
}

std::string air_status()
{
    char b[220];
    snprintf(b, sizeof b, "air hold: held=%u regrounded update=%u anim=%u idle=%u bt=%u low=%u nonfinite_refused=%u mound_lifts=%u stale=%u\n", (unsigned)g_air.size(),
             g_air_regrounded[0], g_air_regrounded[1], g_air_regrounded[2], g_air_regrounded[3], g_air_regrounded[4], g_nonfinite_drops, g_mound_lifts,
             g_air_stale);
    std::string s = b;
    for (auto& h : g_air) { // (BUG-0995 tests: each hold, by address)
        snprintf(b, sizeof b, "  hold %p serial=%u pos=(%.2f %.2f %.2f)\n", h.c, h.serial, h.pos.x, h.pos.y, h.pos.z);
        s += b;
    }
    return s;
}

void set_position(void* c, const net::Vec3& pos)
{
    if (pose_refused(&pos, nullptr))
        return;
    Vector3 p{pos.x, pos.y, pos.z};
    vcall<void>(spatial(c), 0x38 / 4, &p);
}

void set_orientation(void* c, const net::Quat& rot)
{
    if (pose_refused(nullptr, &rot))
        return;
    Quaternion q{rot.x, rot.y, rot.z, rot.w};
    vcall<void>(spatial(c), 0x3C / 4, &q);
}

void move_to(void* c, const net::Vec3& dst, float stop_dist)
{
    if (pose_refused(&dst, nullptr))
        return;
    Vector3 p{dst.x, dst.y, dst.z};
    // cLocomotiveObject::MoveTo(const Vector3&, float goalStop, float acceptableStop, bool)
    vcall<void>(spatial(c), 0xE0 / 4, &p, stop_dist, stop_dist * 2.f, 0);
}

void stop(void* c) { vcall<void>(spatial(c), 0xEC / 4); }

void set_speed(void* c, float speed) { vcall<void>(spatial(c), 0xC4 / 4, speed, 0); }

float scale(void* c) { return vcall<float>(spatial(c), 0x34 / 4); }       // GetScale
float bounding_radius(void* c) { return vcall<float>(spatial(c), 0x70 / 4); } // GetBoundingRadius
// GetBoundingBox (spatial vt 0x68, docs/re/tribe.md#tribe-item-size): min x y z, max x y z (BUG-0929)
// BUG-0929: the highest bone of the creature's current pose, model space (z up, before the creature's scale); -1 when
// the creature has no animated model. Anim::AnimatedCreature (creature +0xB54) +0x17C -> the model, its bone array
// +0x2E4..+0x2E8 (700 bytes a bone, docs/re/creature.md), each bone's posed position at +0x10 (runtime 2026-11-12: the
// fixture's red creature, 24 bones, the head bone at z 0.96-1.00 while it idles, the rest-pose box 1.50 high)
float posed_top(void* c)
{
    if (!c || !skg::readable(c, 0xB58))
        return -1.f;
    uint8_t* ac = field<uint8_t*>(c, 0xB54);
    if (!ac || !skg::readable(ac, 0x180))
        return -1.f;
    uint8_t* model = *(uint8_t**)(ac + 0x17C);
    if (!model || !skg::readable(model, 0x2EC))
        return -1.f;
    uint8_t *b0 = *(uint8_t**)(model + 0x2E4), *b1 = *(uint8_t**)(model + 0x2E8);
    if (!b0 || b1 <= b0 || (b1 - b0) % 700 || (b1 - b0) / 700 > 256 || !skg::readable(b0, (size_t)(b1 - b0)))
        return -1.f;
    float top = -1e9f;
    for (uint8_t* b = b0; b < b1; b += 700) {
        float z = *(float*)(b + 0x18);
        if (std::isfinite(z))
            top = std::max(top, z);
    }
    return top > -1e8f ? top : -1.f;
}

bool bounding_box(void* c, float out[6])
{
    const float* b = vcall<const float*>(spatial(c), 0x68 / 4);
    if (!b || !skg::readable(b, 24))
        return false;
    for (int i = 0; i < 6; i++)
        if (!std::isfinite(b[i]))
            return false;
    memcpy(out, b, 24);
    return true;
}
float ground_radius(const net::Vec3& pos)
{
    static uintptr_t get = H->sym("Simulator::cPlanetModel::Get"), height = H->sym("Simulator::cPlanetModel::GetHeightAt");
    void* pm = get ? cdecl_call<void*>(get) : nullptr;
    if (!pm || !height || !(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z > 1.f))
        return 0.f;
    float v[3] = {pos.x, pos.y, pos.z};
    return thiscall<float>(height, pm, v);
}

float surface_radius(const net::Vec3& pos)
{
    static uintptr_t get = H->sym("Simulator::cPlanetModel::Get"), surf = H->sym("SporeCoop::cPlanetModel_SurfacePoint");
    void* pm = get ? cdecl_call<void*>(get) : nullptr;
    if (!pm || !surf || !(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z > 1.f))
        return 0.f;
    float in[3] = {pos.x, pos.y, pos.z}, o[3] = {0, 0, 0};
    thiscall<float*>(surf, pm, o, (const float*)in, 0);
    float r2 = o[0] * o[0] + o[1] * o[1] + o[2] * o[2];
    return r2 > 1.f ? std::sqrt(r2) : 0.f;
}

// BUG-1088: the planet's water surface as a radius (0: no planet / no terrain). The terrain kind 0xB83110 (docs/re/tribe.md,
// a cPlanetModel method) takes the terrain object as GetHeightAt does (cPlanetModel +0x24, vt 0xC) and compares
// `terrain+0x34 + terrain+0x38 * terrain+0x3C` with GetHeightAt there: water deeper than 0 m is over water.
float water_radius()
{
    static uintptr_t get = H->sym("Simulator::cPlanetModel::Get");
    void* pm = get ? cdecl_call<void*>(get) : nullptr;
    if (!pm || !skg::readable(pm, 0x28))
        return 0.f;
    void* holder = field<void*>(pm, 0x24);
    if (!holder || !skg::readable(holder, 4))
        return 0.f;
    void* t = vcall<void*>(holder, 0xC / 4);
    if (!t || !skg::readable(t, 0x40))
        return 0.f;
    float w = field<float>(t, 0x34) + field<float>(t, 0x38) * field<float>(t, 0x3C);
    return std::isfinite(w) && w > 1.f ? w : 0.f;
}

void set_scale(void* c, float s) { vcall<void>(spatial(c), 0x40 / 4, s); } // SetScale

Key species_key(void* c)
{
    auto& k = field<ResourceKey>(c, kSpeciesKeyOff);
    return {k.instance, k.type, k.group};
}

void* species_by_key(const Key& k)
{
    if (!a_SpeciesMgrGet || !a_GetSpecies)
        return nullptr;
    void* mgr = cdecl_call<void*>(a_SpeciesMgrGet);
    ResourceKey rk{k.instance, k.type, k.group};
    return mgr ? thiscall<void*>(a_GetSpecies, mgr, &rk) : nullptr;
}

// BUG-0200: cSpeciesManager +4 is a hash map ResourceKey -> cSpeciesProfile* (Ghidra GOG 2024: GetSpeciesProfile
// 0x4DF5C0 -> find 0x4DF740 -> 0x4E0FF0): buckets at mgr +8, bucket count at mgr +0xC, bucket (instance ^ group) %
// count; a node is {instance, type, group, value, next}
void** species_slot(const Key& k)
{
    if (!a_SpeciesMgrGet)
        return nullptr;
    auto* mgr = cdecl_call<uint8_t*>(a_SpeciesMgrGet);
    if (!mgr || !readable(mgr + 8, 8))
        return nullptr;
    auto* buckets = field<uint32_t**>(mgr, 8);
    uint32_t n = field<uint32_t>(mgr, 0xC);
    if (!buckets || !n || n > (1u << 20) || !readable(buckets, n * 4))
        return nullptr;
    uint32_t* node = buckets[(k.instance ^ k.group) % n];
    for (int guard = 0; node && guard < 4096; guard++) {
        if (!readable(node, 0x14))
            return nullptr;
        if (node[0] == k.instance && node[1] == k.type && node[2] == k.group)
            return (void**)&node[3];
        node = (uint32_t*)node[4];
    }
    return nullptr;
}

void* create_herd(const net::Vec3& pos, void* species)
{
    if (!a_CreateHerd || !species)
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    Vector3 p{pos.x, pos.y, pos.z};
    // CreateHerd(const Vector3&, cSpeciesProfile*, herdSize, isOwnedByAvatar, personality, createNest)
    void* h = nm ? thiscall<void*>(a_CreateHerd, nm, &p, species, 0, 0, 0, 0) : nullptr;
    herd_quiet(h);
    if (h) {
        if (g_our_herds.size() > 256)
            g_our_herds.erase(g_our_herds.begin());
        g_our_herds.push_back(h);
        note_our_herd(h);
    }
    return h;
}

// A herd like the ones create_herd makes (no archetype, no nest, not the avatar's) of this species, or
// null. Each puppet used to get a new herd and the herd stayed after the puppet was gone: a session
// with rejoins and editors left one per spawn in the world, and every save kept them all.
void* own_herd_for(void* species)
{
    struct F {
        void* species;
        void* found;
    } f{species, nullptr};
    for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* f = (F*)u;
            if (noun != kHerdNoun || !object_alive(o) || IsBadReadPtr(o, 0x21C) || herd_species(o) != f->species ||
                field<uint32_t>(o, 0x88) || herd_nest(o) || field<uint8_t>(o, 0x84))
                return true;
            f->found = o;
            return false;
        },
        &f);
    note_our_herd(f.found); // (BUG-1067: one a save brought back is ours from now on)
    return f.found;
}

// A herd of our own (puppets, NPC proxies) must not grow: CreateHerd gives it a target size of 5
// and it was enabled, so the game filled it with 4 clones of the guest's creature on the host
// (real NPCs, mirrored to the guest too; play_creature.py 2026-10-23, docs/re/creature.md 6).
void herd_quiet(void* h)
{
    if (!h || !skg::readable(h, 0x21C))
        return;
    field<int32_t>(h, 0xF4) = 0; // mTargetHerdSize
    static uintptr_t enable = H->sym("Simulator::cHerd::SetEnabled");
    if (enable && field<uint8_t>(h, 0x218))
        thiscall<void>(enable, h, false); // mbEnabled
}

// The home of a herd without a nest (+0x34) is where its members' AI looks for land and water
// (0xC6B750 caches the result at +0x20C, flag +0x208): it must lie on the surface. A puppet's
// herd follows the puppet (docs/re/creature.md 10).
void herd_home(void* h, const net::Vec3& pos)
{
    if (!h || herd_nest(h) || !skg::readable(h, 0x21C))
        return;
    auto& home = field<Vector3>(h, 0x34);
    net::Vec3 cur{home.x, home.y, home.z};
    float dx = cur.x - pos.x, dy = cur.y - pos.y, dz = cur.z - pos.z;
    if (dx * dx + dy * dy + dz * dz < 20.f * 20.f)
        return;
    home = Vector3{pos.x, pos.y, pos.z};
    field<uint8_t>(h, 0x208) = 0; // the land point is searched again around the new home
}

void* herd_of(void* c) { return field<void*>(c, kHerdOff); }

bool nest_position(void* c, net::Vec3& out)
{
    static uintptr_t get = H->sym("SporeCoop::Herd_NestPosition");
    void* herd = c ? herd_of(c) : nullptr;
    if (!get || !herd || !herd_nest(herd)) // (no null check of the herd inside, BUG-0018)
        return false;
    const float* p = thiscall<const float*>(get, herd);
    if (!p)
        return false;
    out = {p[0], p[1], p[2]};
    return true;
}

uint32_t political_id(void* c) { return vcall<uint32_t>(c, 0x4C / 4); } // cGameData::GetPoliticalID

float dna()
{
    uintptr_t get = H->sym("Simulator::cCreatureGameData::GetEvolutionPoints");
    return get ? cdecl_call<float>(get) : 0.f;
}

// BUG-0630: AddEvolutionPoints (0xD2E8A0) adds its delta to the DNA AND, without a sign check, to cPlayer +0x10F0
// (the stage's evolution bar, the total earned: the brain-level and migration thresholds read it). The game spends
// DNA with SetEvolutionPoints (0xD2E480, the DNA only: the editor's result 0xD445AA), so in a game of one a spend
// never lowers the bar. The mod moved every shared change (the host's total on a guest, a guest's spend on the host,
// the own editor's price) through AddEvolutionPoints: each editor spend lowered the other player's bar by the amount
// (players: "the evolution bar reset to almost 0", "halved"). With the fix a negative change (a spend, anyone's)
// writes the DNA only, never below 0; a gain still goes through AddEvolutionPoints and raises the bar as the game's
// rewards do. Off (`coop.fix creature.bar_spend off`): 0.7.8, every change through AddEvolutionPoints.
static bool g_bar_spend_fix = true;
COOP_FIX("creature.bar_spend", "BUG-0630", g_bar_spend_fix,
         "creature: a DNA spend shared between players (an editor's price) moves the DNA only, not the evolution bar");

void dna_add_raw(float delta)
{
    if (g_bar_spend_fix && delta < 0) {
        if (uintptr_t set = H->sym("Simulator::cCreatureGameData::SetEvolutionPoints")) {
            cdecl_call<void>(set, std::max(0.f, dna() + delta));
            return;
        }
    }
    if (o_AddEvo)
        o_AddEvo(delta);
}

void dna_set_raw(float total)
{
    if (uintptr_t set = H->sym("Simulator::cCreatureGameData::SetEvolutionPoints"))
        cdecl_call<void>(set, std::max(0.f, total));
}

float evolution_bar()
{
    void* nm = noun_manager();
    void* p = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? *(void**)((uint8_t*)nm + 0x74) : nullptr; // cPlayer
    return p && skg::readable((uint8_t*)p + 0x10F0, 8) ? *(float*)((uint8_t*)p + 0x10F0) : NAN;
}

void set_dna_listener(DnaListener fn) { g_dna_listener = fn; }

// BUG-0433: TimeOfDay_Unavailable (= Simulator::GetCurrentContext, [[SpacePlayerData]+0x10]) reads the
// game's SpacePlayerData, which does not exist before the game reaches its main menu: a session hosted then
// (game mode 0) crashed in the host's once-a-second time-of-day send (on_frame -> time_of_day). Off: the
// read as before (the test coop_tod_early.py crashes the host).
static bool g_tod_guard_fix = true;
COOP_FIX("core.tod_guard", "BUG-0433", g_tod_guard_fix,
         "core: no time-of-day read or write while the game has no SpacePlayerData yet (before the main menu)");

static void* tod_object()
{
    if (g_tod_guard_fix) {
        uintptr_t spd = H->sym("Simulator::SpacePlayerData::ptr");
        if (!mode_id() || !spd || !*(void**)spd)
            return nullptr;
    }
    uintptr_t un = H->sym("SporeCoop::TimeOfDay_Unavailable"), get = H->sym("SporeCoop::TimeOfDay_GetObject");
    if (!un || !get || cdecl_call<int>(un) != 0)
        return nullptr;
    return cdecl_call<void*>(get);
}

bool time_of_day(net::Quat& q)
{
    void* o = tod_object();
    if (!o)
        return false;
    auto& r = field<Quaternion>(o, 0x10); // cSpatialObject::mOrientation
    q = {r.x, r.y, r.z, r.w};
    return true;
}

void set_time_of_day(const net::Quat& q)
{
    void* o = tod_object();
    uintptr_t apply = H->sym("SporeCoop::TimeOfDay_Apply");
    if (!o || !apply)
        return;
    Quaternion r{q.x, q.y, q.z, q.w};
    vcall<void>(o, 0x3C / 4, &r); // SetOrientation
    cdecl_call<void>(apply, o);
}

void puppet_register(void* c)
{
    if (!is_puppet(c))
        g_puppets.push_back(c);
    // a new puppet in an object that still waited for its deferred destroy: that one is gone
    // (freed and reused by the game), the new one must not be destroyed or unregistered with it
    for (auto it = g_destroy_later.begin(); it != g_destroy_later.end();)
        it = it->first == c ? g_destroy_later.erase(it) : std::next(it);
}

void puppet_unregister(void* c)
{
    air_hold(c, false); // BUG-0550: a proxy or puppet held in the air is released with it (no hold on a freed creature)
    g_puppets.erase(std::remove(g_puppets.begin(), g_puppets.end(), c), g_puppets.end());
}

bool is_puppet(void* c) { return std::find(g_puppets.begin(), g_puppets.end(), c) != g_puppets.end(); }
const std::vector<void*>& puppets() { return g_puppets; }

// ---- the object list of this frame (BUG-0426 class, L1 of 0.7.13; game.h listed / LiveRef) ----
// `coop.fix game.listed_frame off`: every listed() walks the whole list (the 0.7.12 creature_listed), nothing kept.
static bool g_listed_frame_fix = true;
COOP_FIX("game.listed_frame", "BUG-0426", g_listed_frame_fix, "all: one walk of the game's object list per frame answers whether a kept pointer is still a live object (kept exact by the game's destroys)");
static std::unordered_map<const void*, uint32_t> g_live; // object -> noun
static uint64_t g_live_frame = ~0ull;
static uint32_t g_live_counter = 0, g_live_epoch = ~0u;
static bool g_live_dirty = true;
static int g_live_scope; // LiveScope depth: inside the mod's frame tick
static uint32_t g_live_walks, g_live_rewalks, g_live_destroys, g_live_hits, g_live_misses;
static const uint32_t* serial_counter()
{
    // cGameNounManager::CreateInstance: `inc [0x167BE28]; mov [esi+0x24],eax` (docs/re/space.md#space-object-serial)
    static const uint32_t* p = H ? (const uint32_t*)H->sym("SporeCoop::NounSerialCounter") : nullptr;
    return p;
}
static void live_walk()
{
    g_live.clear();
    for_each_object(
        [](void* o, uint32_t noun, void*) {
            if (object_alive(o))
                g_live[o] = noun;
            return true;
        },
        nullptr);
    const uint32_t* sc = serial_counter();
    g_live_counter = sc ? *sc : 0;
    g_live_frame = H ? H->frame_index() : 0;
    g_live_dirty = false;
    g_live_walks++;
}
static bool scan_listed(const void* c, uint32_t noun)
{
    struct F {
        const void* c;
        uint32_t noun;
        bool found;
    } f{c, noun, false};
    for_each_object(
        [](void* o, uint32_t n, void* u) {
            auto* f = (F*)u;
            if (o != f->c)
                return true;
            f->found = (!f->noun || n == f->noun) && object_alive(o);
            return false;
        },
        &f);
    return f.found;
}
bool listed(const void* obj, uint32_t noun)
{
    if (!obj || !g_ready)
        return false;
    // (no destroy hook: a destroy inside the frame would not be seen, so every question walks the list)
    // only inside the mod's own frame tick (LiveScope): there no game update runs between the walk and the question.
    // In the game's update (our hooks) the game may tear objects down without DestroyInstance (a world's end) after
    // the walk: there every question walks the list as before.
    if (!g_listed_frame_fix || !gamefixes::destroy_hooked() || !H || !g_main_tid.load() || !fx_main_thread() ||
        !g_live_scope)
        return scan_listed(obj, noun);
    uint64_t f = H->frame_index();
    uint32_t ep = world_epoch();
    if (f != g_live_frame || ep != g_live_epoch || g_live_dirty) {
        live_walk();
        g_live_epoch = ep;
    }
    auto it = g_live.find(obj);
    if (it == g_live.end()) {
        const uint32_t* sc = serial_counter();
        if (sc && *sc != g_live_counter) { // made by the game in this frame: walk again
            live_walk();
            g_live_rewalks++;
            it = g_live.find(obj);
        }
    }
    if (it == g_live.end()) {
        g_live_misses++;
        return false;
    }
    // listed in this frame and not destroyed through the hook since: its memory is the object's
    if ((noun && it->second != noun) || field<uint8_t>((void*)obj, 0x21)) {
        g_live_misses++;
        return false;
    }
    g_live_hits++;
    return true;
}
LiveRef live_ref(void* obj, uint32_t noun)
{
    LiveRef r;
    if (!listed(obj, noun))
        return r;
    r.p = obj;
    r.serial = object_serial(obj);
    r.noun = noun;
    return r;
}
bool alive(const LiveRef& r) { return r.p && listed(r.p, r.noun) && object_serial(r.p) == r.serial; }
void note_destroyed(void* obj)
{
    if (g_live.erase(obj))
        g_live_destroys++;
}
void note_created() { g_live_dirty = true; }
LiveScope::LiveScope()
{
    if (!g_live_scope++)
        g_live_dirty = true; // (a game update ran since the last scope: walk again on the first question)
}
LiveScope::~LiveScope() { g_live_scope--; }
bool listed_exact() { return g_listed_frame_fix && gamefixes::destroy_hooked(); }
std::string live_status()
{
    char b[240];
    snprintf(b, sizeof b, "listed: fix=%d hooked=%d counter=%d objects=%u walks=%u rewalks=%u destroys=%u hits=%u misses=%u",
             (int)g_listed_frame_fix, (int)gamefixes::destroy_hooked(), (int)(serial_counter() != nullptr),
             (unsigned)g_live.size(), g_live_walks, g_live_rewalks, g_live_destroys, g_live_hits, g_live_misses);
    return b;
}

bool creature_listed(void* c)
{
    if (g_listed_frame_fix)
        return listed(c, 0x18EB45Eu);
    struct F {
        void* c;
        bool found;
    } f{c, false};
    if (!c)
        return false;
    for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* f = (F*)u;
            if (o != f->c)
                return true;
            f->found = noun == 0x18EB45Eu && object_alive(o);
            return false;
        },
        &f);
    return f.found;
}

std::string herds_dump()
{
    std::string out;
    char b[320];
    void* av = avatar();
    float ar = 0;
    if (av) {
        net::Vec3 p = position(av);
        ar = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    }
    snprintf(b, sizeof b, "avatar |pos|=%.1f\n", ar);
    out += b;
    struct U {
        std::string* out;
        float ar;
        int herds, low;
    } u{&out, ar, 0, 0};
    for_each_object(
        [](void* o, uint32_t noun, void* up) {
            auto* u = (U*)up;
            char b[320];
            if (noun == kHerdNoun && object_alive(o) && skg::readable(o, 0x220)) {
                auto& h = field<Vector3>(o, 0x34);
                float hr = std::sqrt(h.x * h.x + h.y * h.y + h.z * h.z);
                bool ours = std::find(g_our_herds.begin(), g_our_herds.end(), o) != g_our_herds.end();
                u->herds++;
                if (hr < u->ar * 0.8f)
                    u->low++;
                if (!ours && hr >= u->ar * 0.8f)
                    return true; // the game's herds on the surface: only counted
                auto** m0 = field<void**>(o, 0x40);
                auto** m1 = field<void**>(o, 0x44);
                snprintf(b, sizeof b,
                         "herd %p ours=%d nest=%p en=%d target=%d members=%d home=(%.1f %.1f %.1f) |home|=%.1f radius=%.0f "
                         "land_cached=%d\n",
                         o, (int)ours, herd_nest(o), (int)field<uint8_t>(o, 0x218), field<int32_t>(o, 0xF4), (int)(m1 - m0), h.x,
                         h.y, h.z, std::sqrt(h.x * h.x + h.y * h.y + h.z * h.z), field<float>(o, 0x108),
                         (int)field<uint8_t>(o, 0x208));
                *u->out += b;
            } else if (noun == 0x18EB45Eu && object_alive(o)) {
                void* h = herd_of(o);
                if (h && !herd_nest(h)) { // members of nestless herds: ours (puppets, proxies) or the game's
                    net::Vec3 p = position(o);
                    snprintf(b, sizeof b, "  creature %p herd=%p puppet=%d avatar=%d dead=%d species=%08X |pos|=%.1f\n", o,
                             h, (int)is_puppet(o), (int)(o == avatar()), (int)is_dead(o), species_instance(o),
                             std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
                    *u->out += b;
                }
            }
            return true;
        },
        &u);
    snprintf(b, sizeof b, "herds=%d below_surface=%d ours=%d\n", u.herds, u.low, (int)g_our_herds.size());
    return out + b;
}

} // namespace game
