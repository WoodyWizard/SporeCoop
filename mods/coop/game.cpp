#include "game.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <cmath>
#include <string>
#include <vector>

#include "sporekit_game.hpp"
#include "stage_epoch.h"
#include "swap_ledger.h"
#include "fixes.h"

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
static std::vector<void*> g_our_herds; // made by create_herd (puppets, proxies); diagnostics only

// ---- registry via cCreatureAnimal::Update (thiscall, int deltaTime) ----
typedef void(SK_THISCALL* Update_t)(void*, int);
static Update_t o_Update;
static std::vector<void*> g_seen, g_seen_out;
static UpdateFilter g_update_filter;

static void SK_THISCALL h_Update(void* self, int dt)
{
    g_seen.push_back(self);
    if (g_update_filter && !g_update_filter(self))
        return;
    o_Update(self, dt);
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

static int SK_THISCALL h_TakeDamage(void* combatant, float dmg, uint32_t pol, int type, const void* pos, void* attacker)
{
    void* self = (uint8_t*)combatant - kCombatOff;
    void* att = attacker ? (uint8_t*)attacker - kCombatOff : nullptr;
    if (g_damage_filter && !g_damage_filter(self, dmg, type, att))
        return 0;
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
    if (r)
        bhv_report(self, id, 1, disable & 0xFF, block);
    return r;
}

static int SK_THISCALL h_PlayAnimTo(void* self, uint32_t id, void* other, int a, int block)
{
    if (g_anim_listener && !g_in_puppet_anim)
        g_anim_listener(self, id, block, false, true);
    int r = o_PlayAnimTo(self, id, other, a, block);
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
static uint32_t g_bhv_seen = 0, g_bhv_idle = 0;

static void bhv_report(void* self, uint32_t id, int how, int a, int b)
{
    if (g_idle_depth > 0)
        g_bhv_idle++;
    else if (g_bhv_listener && !g_in_puppet_anim) {
        g_bhv_seen++;
        g_bhv_listener(self, id, how, a, b);
    }
}

static int SK_THISCALL h_CreaturePlayAnim(void* self, uint32_t id, int a, int b)
{
    int r = o_CreaturePlayAnim(self, id, a, b);
    if (r)
        bhv_report(self, id, 0, a, b);
    return r;
}

static void SK_THISCALL h_IdleRefresh(void* self, int force)
{
    g_idle_depth++;
    o_IdleRefresh(self, force);
    g_idle_depth--;
}

void set_behaviour_anim_listener(BehaviourAnimListener fn) { g_bhv_listener = fn; }

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
struct FxCreated { void* fx; uint32_t id; bool relayed; };
static constexpr size_t kFxSlots = 1024; // power of two; far more than a frame creates
static FxCreated g_fx_ids[kFxSlots];
static uint32_t g_fx_creates, g_fx_probes, g_fx_evicted;
static uint32_t g_fx_mgr, g_fx_world, g_fx_anim; // creates seen per hook, animation effects skipped
static uint32_t g_fx_last_id; // the last effect id the game created (the perf workload uses it in every stage)
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
static void fx_add(void* fx, uint32_t id)
{
    g_fx_creates++;
    size_t i = fx_slot(fx);
    for (unsigned n = 0; n < 6; n++, i = (i + 1) & (kFxSlots - 1)) {
        if (!g_fx_ids[i].fx || g_fx_ids[i].fx == fx) {
            g_fx_ids[i] = FxCreated{fx, id, false};
            return;
        }
    }
    g_fx_evicted++; // 6 probes over: take the home slot (does not happen with 1024 slots)
    g_fx_ids[fx_slot(fx)] = FxCreated{fx, id, false};
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
uint32_t last_effect_id() { return g_fx_last_id; }
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
static std::atomic<bool> g_world_on{false}; // the stage logic's gate (set_world_relay): no work outside a played stage
static int SK_THISCALL h_WorldCreateFx(void* world, uint32_t id, uint32_t a3, void** out)
{
    int r = o_WorldCreateFx(world, id, a3, out);
    if (!g_world_on.load(std::memory_order_relaxed) || !fx_main_thread()
        || !g_fx_relay_on.load(std::memory_order_relaxed))
        return r;
    if (t_in_anim_fx) {
        g_fx_anim++;
        return r;
    }
    if (g_in_relay)
        return r;
    if ((r & 0xFF) && out && *out) {
        double t0 = H->time_seconds();
        g_fx_world++;
        g_fx_last_id = id;
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
// The hooks are installed once, in init(); a hook removed while the UI skinner's render thread was
// inside it faulted (Skinner::cSkinnerTexturePainter::StartRender+0x28, thousands of caught faults
// in Tribe/Civ), so a stage change only flips this flag.
void set_world_relay(bool on) { g_world_on.store(on, std::memory_order_relaxed); }
// The animation system's effect wrapper 0x9FBF00: the animation relay (MSG_ANIM) already shows the
// effects of an animation on the partner's puppet, so the world create of such an effect must not
// be relayed again. The wrapper is called on the main thread; the flag is thread local so a worker
// thread can never read half of it (it replaces the caller-address check the old setter hook did).
typedef int(SK_CDECL* AnimFxCreate_t)(void*, int, uint32_t, uint32_t, uint32_t, void**);
static AnimFxCreate_t o_AnimFxCreate;
static int SK_CDECL h_AnimFxCreate(void* world, int a, uint32_t id, uint32_t b, uint32_t c, void** out)
{
    bool was = t_in_anim_fx;
    t_in_anim_fx = true;
    int r = o_AnimFxCreate(world, a, id, b, c, out);
    t_in_anim_fx = was;
    return r;
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
        H->log(M, SK_INFO, "fxcr: self %p avatar %p id %08X bone %d fx %p", self, avatar(), id, bone, fx);
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
    if (g_fx_log) H->log(M, SK_INFO, "fx1: self %p avatar %p id %08X", self, avatar(), id);
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
    if (g_fx_log) H->log(M, SK_INFO, "fx1k: self %p avatar %p id %08X key %08X", self, avatar(), id, key);
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
    if (g_fx_log) H->log(M, SK_INFO, "fx2k: self %p avatar %p id %08X key %08X", self, avatar(), id, key);
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
void swap_guard_tick()
{
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

void* spawn_creature(const net::Vec3& pos, void* species, void* herd)
{
    if (!g_ready || !species || !herd)
        return nullptr;
    Vector3 p{pos.x, pos.y, pos.z};
    // static cCreatureAnimal* Create(const Vector3&, cSpeciesProfile*, int age,
    //                                cHerd*, bool isAvatar, bool reuse)   (cdecl)
    return cdecl_call<void*>(a_CreatureCreate, &p, species, 1, herd, 0, 0);
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
static std::vector<std::pair<void*, WorldToken>> g_destroy_later; // creature, world when queued
static uint32_t g_destroys_deferred = 0, g_destroys_late = 0;
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
        g_destroy_later.push_back({c, world_now()});
        g_destroys_deferred++;
        return;
    }
    void* nm = cdecl_call<void*>(a_NounGet);
    if (nm)
        thiscall<void>(a_DestroyInstance, nm, c);
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
    if (g_destroy_later.empty() || cinematic_running())
        return;
    auto later = std::move(g_destroy_later);
    g_destroy_later.clear();
    void* nm = cdecl_call<void*>(a_NounGet);
    for (auto& [c, queued_in] : later) {
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
    snprintf(b, sizeof b, "destroys during cinematics: deferred=%u done_after=%u pending=%u", g_destroys_deferred,
             g_destroys_late, (unsigned)g_destroy_later.size());
    return b;
}

static void* spatial(void* c) { return ptr_add<void>(c, kSpatialOff); }

void teleport(void* c, const net::Vec3& pos, const net::Quat& rot)
{
    Vector3 p{pos.x, pos.y, pos.z};
    Quaternion q{rot.x, rot.y, rot.z, rot.w};
    vcall<void>(spatial(c), 0x44 / 4, &p, &q);
}

void place(void* c, const net::Vec3& pos, const net::Quat& rot)
{
    teleport(c, pos, rot);
    field<uint8_t>(c, 0xB5D) = 0;
}

void set_position(void* c, const net::Vec3& pos)
{
    Vector3 p{pos.x, pos.y, pos.z};
    vcall<void>(spatial(c), 0x38 / 4, &p);
}

void set_orientation(void* c, const net::Quat& rot)
{
    Quaternion q{rot.x, rot.y, rot.z, rot.w};
    vcall<void>(spatial(c), 0x3C / 4, &q);
}

void move_to(void* c, const net::Vec3& dst, float stop_dist)
{
    Vector3 p{dst.x, dst.y, dst.z};
    // cLocomotiveObject::MoveTo(const Vector3&, float goalStop, float acceptableStop, bool)
    vcall<void>(spatial(c), 0xE0 / 4, &p, stop_dist, stop_dist * 2.f, 0);
}

void stop(void* c) { vcall<void>(spatial(c), 0xEC / 4); }

void set_speed(void* c, float speed) { vcall<void>(spatial(c), 0xC4 / 4, speed, 0); }

float scale(void* c) { return vcall<float>(spatial(c), 0x34 / 4); }       // GetScale
float bounding_radius(void* c) { return vcall<float>(spatial(c), 0x70 / 4); } // GetBoundingRadius
float ground_radius(const net::Vec3& pos)
{
    static uintptr_t get = H->sym("Simulator::cPlanetModel::Get"), height = H->sym("Simulator::cPlanetModel::GetHeightAt");
    void* pm = get ? cdecl_call<void*>(get) : nullptr;
    if (!pm || !height || !(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z > 1.f))
        return 0.f;
    float v[3] = {pos.x, pos.y, pos.z};
    return thiscall<float>(height, pm, v);
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

void dna_add_raw(float delta)
{
    if (o_AddEvo)
        o_AddEvo(delta);
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
    g_puppets.erase(std::remove(g_puppets.begin(), g_puppets.end(), c), g_puppets.end());
}

bool is_puppet(void* c) { return std::find(g_puppets.begin(), g_puppets.end(), c) != g_puppets.end(); }
const std::vector<void*>& puppets() { return g_puppets; }

bool creature_listed(void* c)
{
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
