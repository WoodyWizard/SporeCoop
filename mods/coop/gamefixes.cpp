#include "gamefixes.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "creations.h"
#include "fixes.h"
#include "game.h"
#include "sporekit_game.hpp"

namespace gamefixes {

static const SkHost* H;
static SkMod* M;
using skg::readable;

// The texture job of a terrain sphere (docs/re/space.md 8.3): a one-shot render layer that the
// sphere queues in the render manager to draw its planet textures face by face (pass +0x10 0..3,
// cube face +0x14 0..5). Its DrawLayer (vtable slot 3, thiscall, 4 arguments, ret 0x10) reads the
// sphere (+0xC) without checks: the terrain state (sphere+0x20C, the sphere's vt 0x10) and the face
// render targets (sphere+0x148/+0x160 [face]). While a save loaded from the galaxy menu the game drew
// such a job for a sphere whose state and targets were already gone: first-chance AVs at 0xFB89D0
// (cTerrainStateMgr_GetTemperatureScore, ecx 0) and 0xF9E660 (the face target). The job's vtable
// slot goes to this guard, which skips the draw (as the game's own early return when +0x20 is
// null) when anything it reads is missing.
using DrawLayerFn = void(__thiscall*)(void*, int, int, void*, void*);
static DrawLayerFn g_draw_orig;
static uintptr_t* g_draw_slot;
static std::atomic<unsigned> g_skipped{0};
static std::string g_last; // the last skipped job (main thread only)

template <class T> static T at(const void* p, size_t off) { return *(const T*)((const uint8_t*)p + off); }

static bool sphere_ready(const void* job, const uint8_t* sphere)
{
    if (!sphere || !at<void*>(sphere, 0x20C))
        return false;
    int pass = at<int>(job, 0x10), face = at<int>(job, 0x14);
    if (pass >= 0 && pass <= 2 && face >= 0 && face < 6) {
        if (!at<void*>(sphere, 0x160 + 4 * face))
            return false;
        if (pass == 2 && !at<void*>(sphere, 0x148 + 4 * face))
            return false;
    }
    return true;
}

static void __fastcall draw_layer_guard(void* job, void*, int flags, int index, void* viewers, void* engine)
{
    const uint8_t* sphere = at<const uint8_t*>(job, 0xC);
    if (!at<void*>(job, 0x20) || sphere_ready(job, sphere)) {
        g_draw_orig(job, flags, index, viewers, engine);
        return;
    }
    if (sphere)
        *(int*)(sphere + 0x8A4) = -1; // what DrawLayer does first: this job is no longer queued
    unsigned n = ++g_skipped;
    if (n > 8)
        return;
    char b[320];
    snprintf(b, sizeof b,
             "job %p sphere %p pass %d face %d state %p targets %p/%p sphere job %p id %d tid %lu",
             job, sphere, at<int>(job, 0x10), at<int>(job, 0x14), sphere ? at<void*>(sphere, 0x20C) : nullptr,
             sphere ? at<void*>(sphere, 0x148 + 4 * (at<int>(job, 0x14) & 7)) : nullptr,
             sphere ? at<void*>(sphere, 0x160 + 4 * (at<int>(job, 0x14) & 7)) : nullptr,
             sphere ? at<void*>(sphere, 0x8A0) : nullptr, sphere ? at<int>(sphere, 0x8A4) : 0, GetCurrentThreadId());
    g_last = b;
    H->log(M, SK_WARN, "game fixes: skipped a planet texture job of a terrain sphere without terrain (%s)", b);
}

// The tribe stage's setup (TribeGame_Setup 0xCD78E0, one step after a load) points a simulator
// camera object ([0x167FAAC], null in the galaxy menu) at the tribe: LookAt (0xB12CE0, thiscall,
// 3 arguments) and then 0xB13A60 (thiscall, none) on it without a check. A guest of a 4-player tribe
// session got an AV there twice (0xB12D32, data 0x2B4: the object was null, a quit or load had come
// while the stage was still setting up). Both are skipped while the object is null.
using LookAtFn = void(__thiscall*)(void*, void*, int, int);
using CamUpdateFn = void(__thiscall*)(void*);
static LookAtFn g_lookat_orig;
static CamUpdateFn g_camupd_orig;
static std::atomic<unsigned> g_cam_skipped{0};

static void __fastcall lookat_guard(void* obj, void*, void* target, int a, int instant)
{
    if (!obj) {
        if (++g_cam_skipped <= 4)
            H->log(M, SK_WARN, "game fixes: skipped the tribe camera set-up without a simulator camera");
        return;
    }
    g_lookat_orig(obj, target, a, instant);
}

static void __fastcall camupd_guard(void* obj, void*)
{
    if (obj)
        g_camupd_orig(obj);
}

// The end of the respawn caption CVG2SPG(_short) (message 0x44F1189 in the cSimulatorSpaceGame
// handler 0x100B730, at 0x100C513) calls 0xFF59D0 (thiscall, no arguments: clears the set at +0x1E4
// that 0xFF5D90, which starts that caption, filled) on [cSimulatorSpaceGame+0x54], the high-LOD planet
// simulation, without a check. The pointer is null in a star system: a ship that left the planet while
// the caption played (or died there, or a jump of the partner took it away) faulted at 0x7CFCC0 (data
// 0x1E4) and the dispatch of that message stopped, so a death caption ended by the same OK never
// respawned the ship (docs/re/space.md 7.2.1). Without a planet there is nothing to clear: skip it.
using PlanetClearFn = void(__thiscall*)(void*);
static PlanetClearFn g_planet_clear_orig;
static std::atomic<unsigned> g_planet_clear_skipped{0};

static void __fastcall planet_clear_guard(void* sim, void*)
{
    if (sim) {
        g_planet_clear_orig(sim);
        return;
    }
    if (++g_planet_clear_skipped <= 4)
        H->log(M, SK_WARN, "game fixes: the end of the respawn caption without a planet (cleanup skipped)");
}

// Alt+Shift+Enter is the game's "next display adapter" key (AppShortcuts_OnKeyDown 0x814180 ->
// cAppSystem vt 0x68, flag +0x172). With two or more adapters (two monitors, or an integrated and
// a dedicated GPU) cAppSystem::Update then recreates the D3D9 device on the next one
// (Renderer_SetVideoMode 0x75E9A0), and Spore's own code for that is broken: ReleaseShaders
// (0x70E550) visits StandardShaders that its list holds twice and faults at 0x7777BE reading
// 0xFFFE0310 (a players' crash report, 2026-10-25; reproduced with SporeKit alone, no mod), and
// with that fixed the next frame calls Lock on a vertex buffer of the old device (a call through 0
// at 0x11F36A1). docs/re/graphics.md. A player gets there with Shift still held on Alt+Enter (Alt+
// Shift is also Windows' keyboard layout switch): the key toggles fullscreen like Alt+Enter instead
// (cAppSystem vt 0x64, flag +0x171).
using AppFlagFn = void(__thiscall*)(void*);
static AppFlagFn g_next_adapter_orig, g_toggle_fullscreen;
static std::atomic<unsigned> g_adapter_keys{0};
static bool g_adapter_fix = true; // off: the game's own next-adapter switch (faults at 0x7777BE, tests)
COOP_FIX("game.adapter_key", "BUG-0012", g_adapter_fix, "gamefixes: Alt+Shift+Enter toggles fullscreen, not the game's next-adapter switch");

static void __fastcall next_adapter_guard(void* app, void*)
{
    if (!g_adapter_fix) {
        g_next_adapter_orig(app);
        return;
    }
    if (++g_adapter_keys <= 4)
        H->log(M, SK_WARN, "game fixes: Alt+Shift+Enter (next display adapter) crashes Spore: toggling fullscreen instead");
    g_toggle_fullscreen(app);
}

// BUG-0270: the window manager's modal window (cWindowManager +0x82C, the window PushModal (WM vt
// 0x74) set; +0x830 its handler; +0x81C the list of modal windows under it) can point to a window
// the game already destroyed. Then the first mouse message (cWindowManager::Update -> ProcessMessage
// -> UpdateHover 0x958FB0) calls through the dead object's vtable: a guest loading a shared Space
// world after a forced leave to the galaxy menu faulted at 0x65 (eax 0x65: the destroyed object's
// vtable is the refcounted base 0x13EB92C, whose slot 0xF8 is past its end) and the rest of that
// frame's UI messages were lost. Before each UpdateHover the guard checks that the modal window's
// vtable slots 0xC4 and 0xF8 (the two UpdateHover calls) lead into executable code; when not, the
// modal window is dropped (as PopModal of the last one does, without calling its dead handler).
using HoverFn = void*(__thiscall*)(void*, void*, int);
static HoverFn g_hover_orig;
static std::atomic<unsigned> g_dead_modal{0};
static bool g_dead_modal_fix = true;
COOP_FIX("game.dead_modal", "BUG-0270", g_dead_modal_fix,
         "gamefixes: a destroyed modal window left in the window manager is dropped before a mouse message uses it");
static uintptr_t g_code_lo, g_code_hi; // the game's image (no VirtualQuery for its addresses)
static uintptr_t g_test_vt[0x100 / 4]; // tests: a vtable as the destroyed object's (slots past the code)
static uintptr_t g_test_obj[4];

// code of the game or of a DLL (a window class of another mod): executable committed memory
static bool executable(uintptr_t a)
{
    if (a >= g_code_lo && a < g_code_hi)
        return true;
    MEMORY_BASIC_INFORMATION mbi;
    if (a < 0x10000 || !VirtualQuery((const void*)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT)
        return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

static bool modal_alive(const void* w)
{
    if (!readable(w, 4))
        return false;
    uintptr_t vt = *(const uintptr_t*)w;
    if (!readable((const void*)vt, 0xFC))
        return false;
    return executable(((const uintptr_t*)vt)[0xC4 / 4]) && executable(((const uintptr_t*)vt)[0xF8 / 4]);
}

static void* __fastcall hover_guard(void* wm, void*, void* out, int flag)
{
    uint8_t* m = (uint8_t*)wm;
    void* modal = m ? *(void**)(m + 0x82C) : nullptr;
    if (g_dead_modal_fix && modal && !modal_alive(modal)) {
        uintptr_t* list = (uintptr_t*)(m + 0x81C);
        bool more = *list != (uintptr_t)list; // other modal windows under it (left in the list)
        *(void**)(m + 0x82C) = nullptr;
        *(void**)(m + 0x830) = nullptr;
        unsigned n = ++g_dead_modal;
        if (n <= 8)
            H->log(M, SK_WARN, "game fixes: the window manager's modal window %p was destroyed (vtable %08X)%s: dropped it",
                   modal, readable(modal, 4) ? (unsigned)*(uintptr_t*)modal : 0u, more ? ", more modal windows listed" : "");
    }
    return g_hover_orig(wm, out, flag);
}

// tests (BUG-0270): make the window manager's modal window a destroyed object, as the guest had it
std::string fake_dead_modal(void* wm)
{
    uint8_t* m = (uint8_t*)wm;
    if (!m || !g_hover_orig)
        return "no window manager or no guard\n";
    if (*(void**)(m + 0x82C))
        return "a modal window is open: not replaced\n";
    if (*(uintptr_t*)(m + 0x81C) != (uintptr_t)(m + 0x81C))
        return "modal windows listed: not replaced\n";
    for (auto& v : g_test_vt)
        v = 0x65; // what the destroyed object's slots held (UTF-16 text past the end of its vtable)
    g_test_obj[0] = (uintptr_t)g_test_vt;
    *(void**)(m + 0x82C) = g_test_obj;
    *(void**)(m + 0x830) = nullptr;
    return "the modal window is now a destroyed object\n";
}

// BUG-0285 / BUG-0310: Citizen_OffStageCheck (0xACA2A0, thiscall(mgr, object), ret 4) sends a
// citizen (noun 0x18EB4B7) on a new idle walk (0xD57680 -> 0xC27B00 -> DoAction -> WalkTo) when the
// game mode is no longer Tribe. In the frame the Tribe stage is left for the galaxy menu it was run
// for a citizen the stage had already cleaned up (CreatureBase_Cleanup 0xC1CA40 sets +0xB54 to 0),
// and WalkTo's CreatureBase_SetMoveScale (0xC0DB10) read +0x3C of that null pointer: a first-chance
// fault the game's own handler caught. That handler unwound past the rest of the mode change, and
// the next load from the menu crashed or never finished (BUG-0284: the player reset; BUG-0310: the
// space graphics init in cPlanet::GetRingEffect / cSpaceGfx_EnableHeatEffects). A cleaned-up citizen
// has nowhere to walk: the guard skips the check for it. docs/re/tribe.md#tribe-leave-walk-fault.
using OffStageFn = void(__thiscall*)(void*, void*);
using IdleWalkFn = void(__cdecl*)(void*);
static OffStageFn g_offstage_orig;
static IdleWalkFn g_idle_walk; // 0xD57680, what the check calls for a citizen with nowhere to go
static bool g_leave_walk_fix = true;
COOP_FIX("tribe.leave_walk", "BUG-0285 BUG-0310", g_leave_walk_fix,
         "gamefixes: a citizen already cleaned up while the Tribe stage is left is not sent on a new walk (fault in SetMoveScale)");
constexpr uint32_t kCitizenNoun = 0x18EB4B7;
constexpr uint32_t kTribeMode = 0x1654C02;
static std::atomic<unsigned> g_offstage_calls{0}, g_offstage_citizens{0}, g_leave_walk_skipped{0};
static std::atomic<bool> g_leave_walk_test{false}; // tests: the next citizen checked while Tribe is left is made a cleaned-up one
static unsigned g_leave_walk_test_done;

static void __fastcall offstage_guard(void* mgr, void*, void* obj)
{
    g_offstage_calls++;
    uint8_t* c = (uint8_t*)obj;
    if (!c || skg::vcall<uint32_t>(c, 0x20 / 4) != kCitizenNoun) {
        g_offstage_orig(mgr, obj);
        return;
    }
    uint32_t mode = game::mode_id();
    bool leaving = mode != kTribeMode || game::leaving_stage();
    if (leaving) {
        unsigned n = ++g_offstage_citizens;
        if (n <= 8)
            H->log(M, SK_INFO, "game fixes: citizen %p checked off-stage while the stage is left (mode %08X, +0xB54 %p)", c,
                   mode, *(void**)(c + 0xB54));
    }
    void* saved = nullptr;
    bool test = leaving && g_leave_walk_test.exchange(false);
    if (test) { // the stand-in for the campaign's cleaned-up citizen: +0xB54 as the cleanup leaves it
        saved = *(void**)(c + 0xB54);
        *(void**)(c + 0xB54) = nullptr;
        g_leave_walk_test_done++;
        H->log(M, SK_WARN, "game fixes: tests: citizen %p made a cleaned-up one (+0xB54 %p -> 0) as the Tribe stage is left", c, saved);
    }
    if (*(void**)(c + 0xB54)) {
        g_offstage_orig(mgr, obj);
        return;
    }
    if (g_leave_walk_fix) { // (in the Tribe mode the check does nothing for a citizen: skipping it is the same)
        if (++g_leave_walk_skipped <= 8)
            H->log(M, SK_WARN, "game fixes: citizen %p was cleaned up (+0xB54 0) but is checked for a new walk: skipped (mode %08X)",
                   c, mode);
    } else if (test && g_idle_walk) {
        g_idle_walk(c); // the old code's path for this citizen (the check's own end: no place in range)
    } else {
        g_offstage_orig(mgr, obj);
    }
    if (test)
        *(void**)(c + 0xB54) = saved;
}

// tests: the game's own path to the fault is a citizen in a certain action as the mode changes (a
// long session); none came in the lab's leaves (docs/re/tribe.md#tribe-offstage-action). So in the
// first frame the Tribe stage is being left (quit posted) the armed stand-in puts one player member
// through the guard as the game would have: +0xB54 cleared, then the guard (fix on: skipped) or the
// old code's walk (fix off: WalkTo faults in SetMoveScale, caught by the game's handler as in the
// campaign). Returns whether it fired.
bool leave_walk_armed() { return g_leave_walk_test; }

bool leave_walk_tick(void* citizen)
{
    if (!g_leave_walk_test || !citizen || !g_offstage_orig || !game::leaving_stage() || game::mode_id() != kTribeMode)
        return false;
    offstage_guard(nullptr, nullptr, citizen); // mgr is only read by the original, not on these paths
    return true;
}

std::string arm_leave_walk()
{
    if (!g_offstage_orig || !g_idle_walk)
        return "no citizen off-stage guard\n";
    g_leave_walk_test = true;
    return "armed: the next citizen checked while the Tribe stage is left becomes a cleaned-up one\n";
}

// BUG-0449: the physics update (SporeCoop::PhysicsManager_Update 0xB4C370, thiscall(mgr, dt ms) ret 4,
// vtable slot 0x38 of the Simulator subsystem "Simulator/cPhysics") walks the body pool of the physics
// world (SporeCoop::PhysicsWorld_ptr, pool at +0x3C: base, free head, generation, count, high water,
// element size 0x138, capacity 0x1000). A body of kind 0 or 1 (+4) is read through its owner (+0x40)
// without a check: `mov eax,[esi+0x40]; cmp byte [eax+0x75],0` at 0xB4C5B4. A guest in tribe_walk had
// a live body (index word +0 matching its slot, as cObjectPool_::Iterate tests) of kind 0 with a null
// owner: the game faulted at 0xB4C5B4 in every frame from then on (1819 caught faults in 30 s), its
// handler unwound the rest of the physics update each frame. Who left the body without an owner is not
// known (docs/re/tribe.md#tribe-physics-orphan). The guard looks for such bodies before the update and
// gives them kind 3, which no physics loop handles (0xB4C370, 0xB46E10 and the contact check 0xB3FD80
// skip any kind but 0..2): the body is left out, the rest of the world is updated.
using PhysUpdateFn = void(__thiscall*)(void*, uint32_t);
static PhysUpdateFn g_phys_orig;
static uintptr_t g_phys_world; // address of the physics world pointer
static bool g_phys_owner_fix = true;
COOP_FIX("game.physics_owner", "BUG-0449", g_phys_owner_fix,
         "gamefixes: a physics body without an owner is left out of the physics update (the game faulted on it in every frame)");
constexpr uint32_t kBodySize = 0x138, kBodyKindOff = 0x4, kBodyOwnerOff = 0x40, kBodyKindSkip = 3;
static std::atomic<unsigned> g_phys_orphans{0}, g_phys_scans{0};
static std::atomic<bool> g_phys_test{false}; // tests: the next update first gets a body without an owner

struct BodyPool {
    uint32_t* f; // the pool's fields (+0 base ... +0x18 capacity)
    uint8_t* base;
    uint32_t head, count, high, size, cap;
};

static bool body_pool(BodyPool& p)
{
    uint8_t* w = g_phys_world && readable((void*)g_phys_world, 4) ? *(uint8_t**)g_phys_world : nullptr;
    if (!w || !readable(w + 0x3C, 7 * 4))
        return false;
    uint32_t* f = (uint32_t*)(w + 0x3C);
    p = {f, (uint8_t*)(uintptr_t)f[0], f[1], f[3], f[4], f[5], f[6]};
    return p.base && p.size == kBodySize && p.high <= p.cap && p.cap <= 0x10000 &&
           readable(p.base, (size_t)p.high * p.size);
}

static bool body_live(const BodyPool& p, uint32_t i) { return (*(uint32_t*)(p.base + (size_t)i * p.size) & 0xFFFF) == i; }

// tests: a live body of kind 0 without an owner that no owner refers to, as the one the guest's game faulted
// on: the next slot past the pool's high water made live as cObjectPool_::CreateObject does (zeroed, handle
// generation << 16 | index), kind 0, owner 0. No hash entry or broadphase proxy knows it (one slot is lost).
static void make_orphan(BodyPool& p)
{
    uint32_t i = p.high;
    if (i >= p.cap || !readable(p.base + (size_t)i * p.size, p.size)) { // (count and high go up together: free slots stay)
        H->log(M, SK_WARN, "game fixes: tests: no slot for a physics body without an owner (count %u high %u cap %u)", p.count,
               p.high, p.cap);
        return;
    }
    uint8_t* e = p.base + (size_t)i * p.size;
    memset(e, 0, p.size);
    uint32_t gen = ++p.f[2];
    *(uint32_t*)e = gen << 16 | i;
    p.f[3] = ++p.count;
    p.f[4] = ++p.high;
    H->log(M, SK_WARN, "game fixes: tests: physics body %u (word %08X, kind 0) made without an owner (BUG-0449)", i, *(uint32_t*)e);
}

static void __fastcall phys_update_guard(void* mgr, void*, uint32_t dt)
{
    BodyPool p;
    if ((g_phys_owner_fix || g_phys_test) && body_pool(p)) {
        if (g_phys_test.exchange(false))
            make_orphan(p);
        if (g_phys_owner_fix) {
            g_phys_scans++;
            for (uint32_t i = 0; i < p.high; i++) {
                uint8_t* e = p.base + (size_t)i * p.size;
                int kind = at<int>(e, kBodyKindOff);
                if ((kind != 0 && kind != 1) || at<void*>(e, kBodyOwnerOff) || !body_live(p, i))
                    continue;
                unsigned n = ++g_phys_orphans;
                if (n <= 8) // (head == the slot: freed twice, the pool's free list points at itself)
                    H->log(M, SK_WARN, "game fixes: physics body %u (word %08X, kind %d) has no owner: left out of the physics "
                           "update (pool head %u count %u high %u%s)", i, at<uint32_t>(e, 0), kind, p.head, p.count, p.high,
                           p.head == i ? ", the free list's head: freed twice?" : "");
                *(int*)(e + kBodyKindOff) = kBodyKindSkip;
            }
        }
    }
    g_phys_orig(mgr, dt);
}

std::string arm_physics_orphan()
{
    if (!g_phys_orig)
        return "no physics update guard\n";
    g_phys_test = true;
    return "armed: the next physics update gets a body of kind 0 without an owner\n";
}

// BUG-0453: the tick of the creature attack node 0x02D852E6 (SporeCoop::BT_AttackTick 0xD67CD0, cdecl,
// BT action table .data 0x1583F30 {id, enter 0xD67980, tick, exit 0xD672A0}; tree owner first) reads
// the NPC's target (+0xE7C, the target's combatant, set by enter through vt 0x84). Its own check for a
// missing target (0xD65980: null -> the node ends) is skipped while the node's attack animation
// (data +0x30) still plays, and then 0xD65BE0 calls target->vt 8 on null at 0xD65D23 (host crashes on
// 0.7.3.3 and 0.7.3.4). The game clears the target of every NPC aimed at a creature that is destroyed
// (seen in the lab when a partner's puppet went on the partner's death): an attack on our puppet that
// is cut by the puppet's removal hits this. With no target the guard ends the node as the game's own
// check does (return 0), animation or not.
using AttackTickFn = int(__cdecl*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*);
static AttackTickFn g_attack_orig;
static bool g_attack_target_fix = true;
COOP_FIX("npc.attack_lost_target", "BUG-0453", g_attack_target_fix,
         "creature: an NPC whose target is removed during its attack animation ends the attack (the game read the missing target)");
static std::atomic<unsigned> g_attack_ended{0};
static std::atomic<int> g_attack_cut_test{0}; // tests: armed by coop.attackcut
static unsigned g_attack_cuts = 0;

// tests: the state the hosts crashed in, made on purpose: the node's attack animation (+0x30) plays, no
// other animation of it (+0x34 / +0x38 / +0x3C: 0xD65BE0 returns before the target is read), the AI's
// node data (+0x600) set; the target is cleared by the creature's own SetTarget (vt 0x84, (null, 1, 0))
static void attack_cut(void* c, const uint8_t* data)
{
    uint8_t* ai = at<uint8_t*>(c, 0xB4C);
    if (!data || !readable(data, 0x48) || !at<uint32_t>(data, 0x30) || at<uint32_t>(data, 0x34) ||
        at<uint32_t>(data, 0x38) || at<uint32_t>(data, 0x3C) || !ai || !readable(ai + 0x600, 4) || !at<void*>(ai, 0x600))
        return;
    g_attack_cut_test = 0;
    g_attack_cuts++;
    H->log(M, SK_WARN, "game fixes: tests: NPC %p loses its target %p in its attack animation %08X (BUG-0453)", c,
           at<void*>(c, 0xE7C), at<uint32_t>(data, 0x30));
    skg::vcall<void>(c, 0x84 / 4, (void*)nullptr, 1, 0);
}

static int __cdecl attack_tick_guard(void* c, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8,
                                     void* a9, void* a10)
{
    if (g_attack_cut_test && c && readable(c, 0xE80) && at<void*>(c, 0xE7C))
        attack_cut(c, (const uint8_t*)a6);
    if (g_attack_target_fix && c && readable(c, 0xE80) && !at<void*>(c, 0xE7C)) {
        unsigned n = ++g_attack_ended;
        if (n <= 8 || n % 100 == 0)
            H->log(M, SK_INFO, "game fixes: NPC %p is in its attack with no target (removed): the attack ends (BUG-0453, %u)",
                   c, n);
        return 0;
    }
    // (the game's tick is cdecl: the caller cleans the stack, the extra arguments are its own words)
    return g_attack_orig(c, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

// BUG-0456 (class G9, BUG-0426): a creature's behaviour tree keeps up to 8 stimuli (AI block +0xB4C, +8:
// slots of 0x38 bytes, masks +0 / +4, value +8, sender +0x2C with a reference; union of the masks at
// +0x1C0 / +0x1C4; BT_PostStimulus 0xBC9960, the game's clear 0xBC9A60). A stimulus outlives its sender
// by seconds (it decays): the game never destroys a creature in the middle of a fight (corpses stay
// 60 s), co-op does (a partner's puppet when the partner dies, hides, leaves or rejoins). In the lab the
// "noticed the player" stimulus named a destroyed puppet for 4 s after its destroy; a host aborted with
// a pure virtual call (0x40000015 at 0xD8F2AD) where the herd-help node 0xD8F1C0 read the sender of an
// "attacked" stimulus whose object had been destroyed (its vtables were the base class's). Before any
// creature or citizen is destroyed (cGameNounManager::DestroyInstance), every stimulus naming it is
// cleared as 0xBC9A60 clears one (masks, value, the sender released), the others stay.
using DestroyFn = void(__thiscall*)(void*, void*);
static DestroyFn g_destroy_orig;
static bool g_stale_sender_fix = true;
COOP_FIX("npc.stale_sender", "BUG-0456", g_stale_sender_fix,
         "creature: a destroyed creature is taken out of every NPC's stimuli (the AI read it after it was gone)");
static std::atomic<unsigned> g_senders_cleared{0}, g_destroys_scanned{0};
constexpr uint32_t kAnimalNoun = 0x018EB45Eu; // (kCitizenNoun 0x18EB4B7: above)
constexpr size_t kAiOff = 0xB4C, kStimOff = 8, kStimSlot = 0x38, kStimSender = 0x2C, kStimUnion = 0x1C0;
constexpr size_t kCreatureSpan = 0x1700; // the sender may be a sub-object (combatant +0x5A8, spatial +0xC0)

struct Scrub {
    uint8_t* dying;
    unsigned cleared;
};

static unsigned scrub_stimuli(uint8_t* stim, const uint8_t* dying)
{
    unsigned n = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t* s = stim + i * kStimSlot;
        uint8_t* snd = at<uint8_t*>(s, kStimSender);
        if (!(at<uint32_t>(s, 0) | at<uint32_t>(s, 4)) || snd < dying || snd >= dying + kCreatureSpan)
            continue;
        *(uint32_t*)s = 0;
        *(uint32_t*)(s + 4) = 0;
        *(uint32_t*)(s + 8) = 0;
        *(void**)(s + kStimSender) = nullptr;
        if (readable(snd, 4) && readable(*(void**)snd, 8)) // Release, as the game's clear does
            ((void(__thiscall*)(void*))(*(void***)snd)[1])(snd);
        n++;
    }
    if (n) { // the union of what is left
        uint32_t lo = 0, hi = 0;
        for (int i = 0; i < 8; i++) {
            lo |= at<uint32_t>(stim + i * kStimSlot, 0);
            hi |= at<uint32_t>(stim + i * kStimSlot, 4);
        }
        *(uint32_t*)(stim + kStimUnion) = lo;
        *(uint32_t*)(stim + kStimUnion + 4) = hi;
    }
    return n;
}

unsigned clear_stimuli_from(void* c, void* sender)
{
    if (!c || !sender || !readable(c, kAiOff + 4))
        return 0;
    uint8_t* ai = at<uint8_t*>(c, kAiOff);
    return ai && readable(ai + kStimOff, kStimUnion + 8) ? scrub_stimuli(ai + kStimOff, (const uint8_t*)sender) : 0;
}

static void forget_sender(void* dying)
{
    Scrub sc{(uint8_t*)dying, 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* sc = (Scrub*)u;
            if ((noun != kAnimalNoun && noun != kCitizenNoun) || o == sc->dying || !game::object_alive(o) ||
                !readable(o, kAiOff + 4))
                return true;
            uint8_t* ai = at<uint8_t*>(o, kAiOff);
            if (ai && readable(ai + kStimOff, kStimUnion + 8))
                sc->cleared += scrub_stimuli(ai + kStimOff, sc->dying);
            return true;
        },
        &sc);
    g_destroys_scanned++;
    if (sc.cleared) {
        unsigned n = g_senders_cleared += sc.cleared;
        if (n <= 16 || n % 100 < sc.cleared)
            H->log(M, SK_INFO, "game fixes: %u stimulus(es) of NPCs named %p, destroyed now: cleared (BUG-0456, %u in all)",
                   sc.cleared, dying, n);
    }
}

static void __fastcall destroy_guard(void* nm, void*, void* obj)
{
    if (g_stale_sender_fix && obj && readable(obj, 0x30)) {
        uint32_t noun = game::noun_id(obj);
        if (noun == kAnimalNoun || noun == kCitizenNoun)
            forget_sender(obj);
    }
    g_destroy_orig(nm, obj);
}

// BUG-0454 (BUG-0371): the host's creature editor crashed three times at RenderWare::Raster::Fill+0x7
// (0x11F02C7, read 0x4): the skin painter's mip builder (0x7AFFC0, from ExecutePipeline 0x51CCE2) got a
// texture from the texture manager (vt 0x3C) whose raster (+0) was null and filled each level of that
// null raster. Fill (thiscall(raster, source, level) ret 8) has its own failure return (0, when the raster
// cannot be locked); a null raster takes that return instead of the fault. The editor then has a skin
// texture without pixels (not seen: the lab's GPU never failed a texture), not a crash.
using FillFn = int(__thiscall*)(void*, void*, int);
static FillFn g_fill_orig;
static bool g_null_raster_fix = true;
COOP_FIX("game.null_raster_fill", "BUG-0454", g_null_raster_fix,
         "gamefixes: filling a texture whose raster the graphics driver did not make fails like a raster that cannot be locked (the editor crashed at 0x11F02C7)");
static std::atomic<unsigned> g_null_fills{0};

static int __fastcall fill_guard(void* raster, void*, void* src, int level)
{
    if (!raster && g_null_raster_fix) {
        unsigned n = ++g_null_fills;
        if (n <= 4 || n % 100 == 0)
            H->log(M, SK_WARN, "game fixes: a texture without a raster was to be filled (level %d): skipped (BUG-0454, %u)",
                   level, n);
        return 0;
    }
    return g_fill_orig(raster, src, level);
}

// BUG-0546: an NPC empire's ship whose model this game's resources do not have. The ship gfx's SetModel
// (SporeCoop::UfoGfx_SetModel 0xE9A920, thiscall(gfx, const Key* model, bool bake) ret 8) asks the model world
// for the key (vt 0xC) and keeps a model only when one is made, then calls SetVisible (0xE98D90), which writes
// [GetModel()]+4 without a null check: a read of 0x4 at 0xE98DD0. The game's own init (0xE9B180) puts in the
// default UFO {0x9D5DB21A, 0x476A98C7, 0x40656200} only for a ship with no key at all; an NPC empire's ship whose
// key names a creation of the player's Sporepedia cache (Pollination.package) without its model record (the
// cache holds only the summary 0x02D5C9AF) keeps that key. Seen with one game and no co-op session (lab lane 5's
// cache). The guard gives such a key the default UFO when this game has it, else the last ship model key it had
// (the player's own ship is set up first); SetVisible on a gfx without a model is skipped (a backstop).
using UfoSetModelFn = void(__thiscall*)(void*, const uint32_t*, uint32_t);
using UfoSetVisibleFn = void(__thiscall*)(void*, uint32_t);
static UfoSetModelFn g_ufo_model_orig;
static UfoSetVisibleFn g_ufo_visible_orig;
static bool g_ufo_model_fix = true;
COOP_FIX("game.ufo_model", "BUG-0546", g_ufo_model_fix,
         "gamefixes: a ship whose model this game lacks (an incomplete Sporepedia cache) gets the default UFO model instead of a crash at 0xE98DD0");
static std::atomic<unsigned> g_ufo_defaulted{0}, g_ufo_invisible_skipped{0};
static const uint32_t kUfoModelType = 0x476A98C7, kDefaultUfo[3] = {0x9D5DB21A, kUfoModelType, 0x40656200};

static uint32_t g_ufo_good[3]; // the last ship model key this game had (the player's own ship comes first)
static bool g_default_known, g_default_checked;

static bool ufo_key_known(const uint32_t* key)
{
    creations::Key k;
    k.instance = key[0];
    k.type = key[1] ? key[1] : kUfoModelType;
    k.group = key[2];
    return creations::key_known(k);
}

static void __fastcall ufo_set_model_guard(void* gfx, void*, const uint32_t* key, uint32_t bake)
{
    if (g_ufo_model_fix && key && readable(key, 12) && (key[0] || key[2])) {
        if (ufo_key_known(key)) {
            memcpy(g_ufo_good, key, sizeof g_ufo_good);
        } else {
            if (!g_default_checked) { // the game's own default UFO is not in every install (lane 5: not built)
                g_default_checked = true;
                g_default_known = ufo_key_known(kDefaultUfo);
            }
            const uint32_t* sub = g_default_known ? kDefaultUfo : (g_ufo_good[0] || g_ufo_good[2]) ? g_ufo_good : nullptr;
            unsigned n = ++g_ufo_defaulted;
            if (n <= 8 || n % 100 == 0)
                H->log(M, SK_WARN, "game fixes: ship model %08X/%08X/%08X is not in this game: %s %08X/%08X/%08X (BUG-0546, %u)",
                       key[0], key[1], key[2], g_default_known ? "the default UFO" : sub ? "the last known ship model" : "none known, kept",
                       sub ? sub[0] : 0, sub ? sub[1] : 0, sub ? sub[2] : 0, n);
            if (sub) {
                uint32_t k[3] = {sub[0], sub[1], sub[2]};
                g_ufo_model_orig(gfx, k, bake);
                return;
            }
        }
    }
    g_ufo_model_orig(gfx, key, bake);
}

static void __fastcall ufo_set_visible_guard(void* gfx, void*, uint32_t visible)
{
    if (g_ufo_model_fix && gfx && readable(gfx, 4)) {
        void** vt = *(void***)gfx;
        using GetModelFn = void*(__thiscall*)(void*);
        if (vt && readable(vt + 0x2C / 4, 4) && !((GetModelFn)vt[0x2C / 4])(gfx)) {
            unsigned n = ++g_ufo_invisible_skipped;
            if (n <= 8 || n % 100 == 0)
                H->log(M, SK_WARN, "game fixes: a ship gfx without a model: SetVisible skipped (BUG-0546, %u)", n);
            return;
        }
    }
    g_ufo_visible_orig(gfx, visible);
}

std::string arm_attack_cut()
{
    if (!g_attack_orig)
        return "no attack node guard\n";
    g_attack_cut_test = 1;
    return "armed: the next NPC in its attack animation loses its target (" + std::to_string(g_attack_cuts) + " so far)\n";
}

// BUG-0582: the Tribe creature attack's attackers (behaviour update 0xD73FD0, its state 3: walk to a spot round
// the attacked place) take a free spot with 0xD72720 cdecl(bool, const Vector3* a, const Vector3* b, rec*, u32* out):
// rec is the spot record of the place's planet cell from a global sorted table (0xC945C0 by the cell id the
// behaviour's start 0xD73930 stored at +0xC0, null when the cell has no record) and is read without a check
// (rec+0x40 / +0x44 the spots, 0x1C bytes each). Its sibling 0xD72690 (giving a spot back) checks the null. A host
// faulted there every frame after its own creature attack (lab, tribe_bounce_world). With no record the guard
// answers "no free spot" (out = -1, false), the game's own branch that ends the attacker's behaviour.
// `coop.fix tribe.attack_spot off`: the game's read as before.
using TakeSpotFn = uint32_t(__cdecl*)(const float*, const float*, void*, uint32_t*);
static TakeSpotFn g_spot_orig;
static bool g_attack_spot_fix = true;
COOP_FIX("tribe.attack_spot", "BUG-0582", g_attack_spot_fix,
         "tribe: an animal of a creature attack whose village spot is gone ends its attack (the game read the missing spot)");
static std::atomic<unsigned> g_spots_missing{0};
static std::atomic<int> g_spot_cut_test{0}; // tests: coop.spotcut (the next lookup finds no record)
static unsigned g_spot_cuts = 0;

static uint32_t __cdecl take_spot_guard(const float* a, const float* b, void* rec, uint32_t* out)
{
    if (g_spot_cut_test && rec) {
        g_spot_cut_test = 0;
        g_spot_cuts++;
        H->log(M, SK_WARN, "game fixes: tests: an attacker's spot record %p is left out (BUG-0582)", rec);
        rec = nullptr;
    }
    if (!rec && g_attack_spot_fix) {
        unsigned n = ++g_spots_missing;
        if (n <= 8 || n % 100 == 0)
            H->log(M, SK_INFO, "game fixes: a creature attack's attacker has no spot record: its attack ends (BUG-0582, %u)", n);
        if (out)
            *out = 0xFFFFFFFFu;
        return 0xFFFFFF00u; // AL = 0, as the game's own "no free spot"
    }
    return g_spot_orig(a, b, rec, out);
}

std::string arm_spot_cut()
{
    if (!g_spot_orig)
        return "no attack spot guard\n";
    g_spot_cut_test = 1;
    return "armed: the next attacker's spot lookup finds no record (" + std::to_string(g_spot_cuts) + " so far)\n";
}

// ---- BUG-0575: an attached effect of a model whose effect object is broken ----
// A model (a creature's spatial at +0xC0, a projectile's view, ...) keeps its attached effects in a vector at +0xC0 /
// +0xC4 (0x3C bytes each: +0 key, +4 effect id, +8 enabled, +9 / +0xA restart flags, +0xC distance threshold, +0x38
// the effect, a counted reference; SpatialEffects_Add 0xC8AFB0). SpatialEffects_UpdateByDistance 0xC8ABF0
// (thiscall(owner), every view update) starts an effect that came into the camera's range (vt 8(0) =
// VisualEffect_Start 0xA82660) and stops one that left it (vt 0xC(0)); SpatialEffects_SetTransform 0xC89EF0 moves
// them. Start calls vt 8 of every component of the effect (vector +0x144..+0x148, 0x5C bytes, object at +0x38)
// without a check. A guest crashed there after the home camera jump (0xA826F3, read of 0x94: the first component's
// call returned through a freed object's vtable with esi = 1, edi = 0). Before either loop the guard checks every
// listed effect: its vtable and slots in the game, and for a VisualEffect its components (vector sane, each object's
// vtable in the game, slots 2 / 3 in its code). A broken one leaves the list without a call (no Release on freed
// memory: the reference is lost, not dropped twice); the game makes the effect again when it wants it.
// Not verified against the players' crash: who freed the component is not known (docs/re/tribe.md#tribe-model-fx-guard).
// `coop.fix game.model_fx off`: the loops run on whatever is listed (0.7.7).
using OwnerFn = void(__thiscall*)(void*);
using OwnerXfFn = void(__thiscall*)(void*, void*);
static OwnerFn g_fx_dist_orig;
static OwnerXfFn g_fx_xf_orig;
static bool g_model_fx_fix = true;
COOP_FIX("game.model_fx", "BUG-0575", g_model_fx_fix,
         "gamefixes: a model's attached effect whose parts were freed leaves the model's list before the camera starts it (the guest crashed at 0xA826F3)");
static std::atomic<unsigned> g_model_fx_dropped{0};
static uintptr_t g_text_lo, g_text_hi, g_fx_vt, g_fx_start;
static uint8_t* g_fxcut_owner; // tests (coop.fxcut): the planted effect's list owner, entry index, effect
static int g_fxcut_entry = -1, g_fxcut_stage;
static uint8_t* g_fxcut_fx;
static uint32_t g_fxcut_thr, g_fxcut_saved[2];
static double g_fxcut_at;

static uintptr_t g_fxcut_vt[16];              // the freed component's "vtable": nulls (vt 8 = a call through null)
static uintptr_t g_fxcut_obj[0x60 / 4];       // the freed component
static uintptr_t g_fxcut_comp[0x5C / 4];      // one component record of 0x5C bytes, +0x38 = the object
static unsigned g_fxcut_done, g_fxcut_planted;

static bool heap_ptr(uintptr_t p) { return p >= 0x10000 && p < 0xFFFE0000u && !(p & 3); }
static bool in_image(uintptr_t p) { return p >= g_code_lo && p < g_code_hi; }
static bool in_text(uintptr_t p) { return p >= g_text_lo && p < g_text_hi; }
static bool vt_ok(uintptr_t obj, int slot_a, int slot_b)
{
    if (!heap_ptr(obj))
        return false;
    uintptr_t vt = *(const uintptr_t*)obj;
    return in_image(vt) && !(vt & 3) && in_text(((const uintptr_t*)vt)[slot_a]) && in_text(((const uintptr_t*)vt)[slot_b]);
}

// the listed effect is one the loops may call (Start / Stop / IsRunning / SetTransform)
static bool model_fx_ok(uintptr_t fx)
{
    if (!vt_ok(fx, 2, 3) || !in_text(((const uintptr_t*)*(const uintptr_t*)fx)[4]) ||
        !in_text(((const uintptr_t*)*(const uintptr_t*)fx)[6]))
        return false;
    if (*(const uintptr_t*)fx != g_fx_vt) // another effect class: its components are not known
        return true;
    uintptr_t b = at<uintptr_t>((void*)fx, 0x144), e = at<uintptr_t>((void*)fx, 0x148);
    if (!b && !e)
        return true;
    if (!heap_ptr(b) || e < b || (e - b) % 0x5C || (e - b) / 0x5C > 512)
        return false;
    for (uintptr_t c = b; c < e; c += 0x5C)
        if (!vt_ok(at<uintptr_t>((void*)c, 0x38), 2, 3))
            return false;
    return true;
}

static void model_fx_scrub(void* owner, const char* where)
{
    if (!g_model_fx_fix || !owner || !g_text_hi)
        return;
    uintptr_t b = at<uintptr_t>(owner, 0xC0), e = at<uintptr_t>(owner, 0xC4);
    if (b == e || !heap_ptr(b) || e < b || (e - b) % 0x3C || (e - b) / 0x3C > 256)
        return;
    for (uintptr_t ent = b; ent < e; ent += 0x3C) {
        uintptr_t fx = at<uintptr_t>((void*)ent, 0x38);
        if (!fx || model_fx_ok(fx))
            continue;
        *(uintptr_t*)(ent + 0x38) = 0; // the loops skip (or make again) an entry without an effect
        if ((uint8_t*)fx == g_fxcut_fx) { // tests: the planted parts go, the real effect is whole again (leaked)
            *(uint32_t*)(fx + 0x144) = g_fxcut_saved[0];
            *(uint32_t*)(fx + 0x148) = g_fxcut_saved[1];
            g_fxcut_fx = nullptr;
            g_fxcut_done++;
        }
        unsigned n = ++g_model_fx_dropped;
        if (n <= 8 || n % 100 == 0)
            H->log(M, SK_WARN, "game fixes: model %p: attached effect %08X (%p) has freed parts: dropped before %s (BUG-0575, %u)",
                   owner, at<uint32_t>((void*)ent, 4), (void*)fx, where, n);
    }
}

// tests (coop.fxcut): the state of the guest's crash, made on purpose, on the next list owner the range check sees
// with a running VisualEffect that may restart (+8 and +9 or +0xA). Its threshold is set to 0 for a moment (the
// game's own loop stops it); once stopped, its component vector is pointed at one component whose object has a
// vtable of nulls (a freed component) and the threshold is put back: the same range check starts it (as after the
// home camera jump) and calls through null in VisualEffect_Start, unless the guard drops it first.
static uint8_t* fxcut_entry(void* owner, int i)
{
    uintptr_t b = at<uintptr_t>(owner, 0xC0), e = at<uintptr_t>(owner, 0xC4);
    if (!heap_ptr(b) || e < b || (e - b) % 0x3C || i < 0 || b + (i + 1) * 0x3C > e)
        return nullptr;
    return (uint8_t*)b + i * 0x3C;
}

static void fxcut_step(void* owner)
{
    if (g_fxcut_stage == 1) { // find an owner and an entry
        uintptr_t b = at<uintptr_t>(owner, 0xC0), e = at<uintptr_t>(owner, 0xC4);
        if (b == e || !heap_ptr(b) || e < b || (e - b) % 0x3C || (e - b) / 0x3C > 256)
            return;
        int i = 0;
        for (uintptr_t ent = b; ent < e; ent += 0x3C, i++) {
            uintptr_t fx = at<uintptr_t>((void*)ent, 0x38);
            if (!fx || *(const uintptr_t*)fx != g_fx_vt || !at<uint8_t>((void*)ent, 8) ||
                !(at<uint8_t>((void*)ent, 9) | at<uint8_t>((void*)ent, 0xA)) || !model_fx_ok(fx) ||
                !skg::vcall<bool>((void*)fx, 0x10 / 4))
                continue;
            g_fxcut_owner = (uint8_t*)owner;
            g_fxcut_entry = i;
            g_fxcut_thr = at<uint32_t>((void*)ent, 0xC);
            *(float*)(ent + 0xC) = 0.0f; // out of range: this loop stops it
            g_fxcut_stage = 2;
            H->log(M, SK_WARN, "game fixes: tests: model %p effect %08X (%p, entry %d, threshold %.0f) is stopped for the test (BUG-0575)",
                   owner, at<uint32_t>((void*)ent, 4), (void*)fx, i, *(float*)&g_fxcut_thr);
            return;
        }
        return;
    }
    if (g_fxcut_stage != 2 || owner != g_fxcut_owner)
        return;
    uint8_t* ent = fxcut_entry(owner, g_fxcut_entry);
    uint8_t* fx = ent ? (uint8_t*)at<uintptr_t>(ent, 0x38) : nullptr;
    if (!fx || *(const uintptr_t*)fx != g_fx_vt) {
        g_fxcut_stage = 0;
        return;
    }
    if (skg::vcall<bool>(fx, 0x10 / 4)) // still fading out
        return;
    g_fxcut_vt[0] = 0; // all nulls
    g_fxcut_obj[0] = (uintptr_t)g_fxcut_vt;
    memset(g_fxcut_comp, 0, sizeof g_fxcut_comp);
    g_fxcut_comp[0x38 / 4] = (uintptr_t)g_fxcut_obj;
    g_fxcut_saved[0] = at<uint32_t>(fx, 0x144);
    g_fxcut_saved[1] = at<uint32_t>(fx, 0x148);
    *(uint32_t*)(fx + 0x144) = (uint32_t)(uintptr_t)g_fxcut_comp;
    *(uint32_t*)(fx + 0x148) = (uint32_t)(uintptr_t)g_fxcut_comp + 0x5C;
    g_fxcut_fx = fx;
    *(uint32_t*)(ent + 0xC) = g_fxcut_thr; // back in range: this check starts it
    g_fxcut_stage = 0;
    g_fxcut_planted++;
    H->log(M, SK_WARN, "game fixes: tests: model %p effect %p is stopped and has a freed part now: the range check starts it (BUG-0575)",
           owner, fx);
}

static void __fastcall fx_dist_guard(void* owner, void*)
{
    if (g_fxcut_stage)
        fxcut_step(owner);
    model_fx_scrub(owner, "the camera range check");
    g_fx_dist_orig(owner);
}

static void __fastcall fx_xf_guard(void* owner, void*, void* xf)
{
    model_fx_scrub(owner, "its move");
    g_fx_xf_orig(owner, xf);
}

static void fxcut_frame()
{
    if (g_fxcut_stage && H->time_seconds() - g_fxcut_at > 20) {
        if (g_fxcut_stage == 2)
            if (uint8_t* ent = fxcut_entry(g_fxcut_owner, g_fxcut_entry))
                *(uint32_t*)(ent + 0xC) = g_fxcut_thr;
        H->log(M, SK_WARN, "game fixes: tests: no model effect was planted in 20 s (stage %d, BUG-0575)", g_fxcut_stage);
        g_fxcut_stage = 0;
    }
}

static std::string fxcut_arm()
{
    if (!g_fx_dist_orig || !g_fx_vt)
        return "no model effect guard\n";
    g_fxcut_stage = 1;
    g_fxcut_at = H->time_seconds();
    return "armed: the next model with a running attached effect gets a freed part (" + std::to_string(g_fxcut_planted) +
           " planted so far)\n";
}

static int c_fxcut(void*, int, const char* const*, SkOut* out)
{
    H->out_printf(out, "%s", fxcut_arm().c_str());
    return 0;
}

// coop.fxlist <creature>: its attached effects (tests, RE)
static int c_fxlist(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "all")) { // every creature with attached effects: pointer, noun, count, running
        struct Acc { SkOut* out; unsigned shown; } acc{out, 0};
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* a = (Acc*)u;
                if ((noun != kCitizenNoun && noun != kAnimalNoun) || !game::object_alive(o) || a->shown >= 64)
                    return true;
                uint8_t* owner = (uint8_t*)o + 0xC0;
                uintptr_t b = at<uintptr_t>(owner, 0xC0), e = at<uintptr_t>(owner, 0xC4);
                if (!heap_ptr(b) || e <= b)
                    return true;
                unsigned run = 0, n = (unsigned)((e - b) / 0x3C);
                for (uintptr_t ent = b; ent < e; ent += 0x3C) {
                    uintptr_t fx = at<uintptr_t>((void*)ent, 0x38);
                    run += fx && model_fx_ok(fx) && skg::vcall<bool>((void*)fx, 0x10 / 4);
                }
                H->out_printf(a->out, "%p noun=%08X effects=%u running=%u\n", o, noun, n, run);
                a->shown++;
                return true;
            },
            &acc);
        H->out_printf(out, "%u creatures with attached effects\n", acc.shown);
        return 0;
    }
    void* c = argc > 1 ? (void*)strtoul(argv[1], nullptr, 16) : game::avatar();
    if (!c || !readable(c, 0x200)) {
        H->out_printf(out, "no such creature\n");
        return 1;
    }
    uint8_t* owner = (uint8_t*)c + 0xC0;
    uintptr_t b = at<uintptr_t>(owner, 0xC0), e = at<uintptr_t>(owner, 0xC4);
    H->out_printf(out, "creature %p: %u attached effects (dropped %u, planted %u, tests done %u)\n", c,
                  heap_ptr(b) && e >= b ? (unsigned)((e - b) / 0x3C) : 0u, g_model_fx_dropped.load(), g_fxcut_fx ? 1u : 0u,
                  g_fxcut_done);
    int i = 0;
    for (uintptr_t ent = b; heap_ptr(b) && ent < e && i < 32; ent += 0x3C, i++) {
        uintptr_t fx = at<uintptr_t>((void*)ent, 0x38);
        bool ok = fx && model_fx_ok(fx);
        H->out_printf(out, "  %d key=%08X id=%08X on=%u restart=%u/%u thr=%.0f fx=%p vfx=%d ok=%d running=%d parts=%d\n", i,
                      at<uint32_t>((void*)ent, 0), at<uint32_t>((void*)ent, 4), at<uint8_t>((void*)ent, 8),
                      at<uint8_t>((void*)ent, 9), at<uint8_t>((void*)ent, 0xA), at<float>((void*)ent, 0xC), (void*)fx,
                      fx && *(const uintptr_t*)fx == g_fx_vt, ok, ok ? (int)skg::vcall<bool>((void*)fx, 0x10 / 4) : -1,
                      ok && *(const uintptr_t*)fx == g_fx_vt ? (int)((at<uintptr_t>((void*)fx, 0x148) - at<uintptr_t>((void*)fx, 0x144)) / 0x5C) : -1);
    }
    return 0;
}

// ---- BUG-0584: a free fruit the plants' fruit take would free ----
// The flora (Flora_Get) keeps two counted vectors of cFruit (docs/re/creature.md#creature-fruit-pool-guard): free fruit
// +0x1B8F44..+0x1B8F4C (eaten, kept for reuse) and loose fruit +0x1B905C..+0x1B9064. Its drop maker 0xB09790 takes
// one with Flora_TakeFruitForDrop 0xB08DF0 (thiscall(flora) -> cFruit*: the last free one, else the loose one
// farthest from the camera) and calls its AddRef (vt 0) at once. The free path's pop drops the pool's reference and
// its own before it returns: a free fruit whose only reference was the pool's (+0x8 == 1: a fruit of the save eaten
// without the eat node's DestroyInstance, worldobj's wobj.fruit_saved) is freed there and the caller called through
// the freed memory (a host at 0xB097C2, lab 20261105-100942). Before the take the guard takes such free fruit, and any
// entry of the two vectors that is not a cFruit (its vtable), out of its vector without a Release (kept alive: a few
// hundred bytes lost); a null take is the game's own "no fruit" (nothing falls this time).
// `coop.fix game.fruit_pool off`: the take reads whatever is listed (0.7.7).
using TakeFruitFn = void*(__thiscall*)(void*);
static TakeFruitFn g_take_fruit_orig;
static bool g_fruit_pool_fix = true;
COOP_FIX("game.fruit_pool", "BUG-0584", g_fruit_pool_fix,
         "gamefixes: a freed fruit, or a free one the take would free, leaves the plants' fruit pool before a fruit falls (a host faulted at 0xB097C2)");
static std::atomic<unsigned> g_fruit_dropped{0};
static uintptr_t g_fruit_vt;
constexpr size_t kFreeFruit = 0x1B8F44, kLooseFruit = 0x1B905C;

static unsigned fruit_vec_scrub(uint8_t* flora, size_t off)
{
    uintptr_t* b = at<uintptr_t*>(flora, off);
    uintptr_t* e = at<uintptr_t*>(flora, off + 4);
    if (!b || !heap_ptr((uintptr_t)b) || e < b || e - b > 100000)
        return 0;
    unsigned n = 0;
    for (uintptr_t* p = b; p < e;) {
        // a free fruit whose only reference is the pool's is freed by the take's pop (it releases the pool's
        // reference and its own): it leaves the pool without a Release (kept alive, a few hundred bytes lost)
        if (heap_ptr(*p) && *(const uintptr_t*)*p == g_fruit_vt && (off != kFreeFruit || at<uint32_t>((void*)*p, 8) > 1)) {
            p++;
            continue;
        }
        memmove(p, p + 1, (e - p - 1) * sizeof *p);
        e--;
        n++;
    }
    *(uintptr_t**)(flora + off + 4) = e;
    return n;
}

static void* __fastcall take_fruit_guard(void* flora, void*)
{
    if (g_fruit_pool_fix && flora && g_fruit_vt) {
        unsigned f = fruit_vec_scrub((uint8_t*)flora, kFreeFruit), l = fruit_vec_scrub((uint8_t*)flora, kLooseFruit);
        if (f + l) {
            unsigned n = g_fruit_dropped += f + l;
            H->log(M, SK_WARN, "game fixes: %u freed fruit in the fruit pool (free %u, loose %u): taken out before a fruit falls (BUG-0584, %u)",
                   f + l, f, l, n);
        }
    }
    return g_take_fruit_orig(flora);
}

// coop.fruitpool: the plants' free and loose fruit (pointer, vtable ok, references +0x8, flags +0x148, state +0x16C)
static int c_fruitpool(void*, int, const char* const*, SkOut* out)
{
    static uintptr_t flora_get = H->sym("SporeCoop::Flora_Get");
    uint8_t* fl = flora_get ? ((uint8_t*(__cdecl*)())flora_get)() : nullptr;
    if (!fl) {
        H->out_printf(out, "no flora\n");
        return 1;
    }
    for (size_t off : {kFreeFruit, kLooseFruit}) {
        uintptr_t* b = at<uintptr_t*>(fl, off);
        uintptr_t* e = at<uintptr_t*>(fl, off + 4);
        H->out_printf(out, "%s %d:", off == kFreeFruit ? "free" : "loose", b && e >= b ? (int)(e - b) : -1);
        for (uintptr_t* p = b; p && p < e && p - b < 64; p++) {
            bool ok = heap_ptr(*p) && *(const uintptr_t*)*p == g_fruit_vt;
            if (ok)
                H->out_printf(out, " %08X/r%u/f%X/s%u", (unsigned)*p, at<uint32_t>((void*)*p, 8), at<uint32_t>((void*)*p, 0x148),
                              at<uint32_t>((void*)*p, 0x16C));
            else
                H->out_printf(out, " %08X/bad", (unsigned)*p);
        }
        H->out_printf(out, "\n");
    }
    return 0;
}

static bool patch_slot(uintptr_t* slot, uintptr_t value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old))
        return false;
    *slot = value;
    VirtualProtect(slot, sizeof *slot, old, &old);
    return true;
}

// BUG-0506 (and GitHub issue #190): the game's Pictures / Movies / Animations save areas (0x11AC196..0x11AC198) are
// directory databases in Documents\My Spore Creations (dir id 0xA0214B), made once by the app's init
// (0x6B3760). When that folder cannot be made (Documents missing, redirected, blocked by Windows'
// controlled folder access, or a file of that name) the areas are never registered, and the game
// settings window (its builder 0x602070, from GameSettings::Show) calls
// Resource::GetDatabaseByID(0x11AC197)->vt 0x28 without a null check: a read of 0 at 0x6025A9.
// The editors read Pictures the same way (PlayModeBackgrounds 0x633750: GetDatabaseByID(0x11AC196)->vt 0x5C).
// Seen with the ru-ru Steam install, whose installer writes PlayerDir "??? ????????" (the Russian name lost
// in a code page): a folder name with '?' cannot be made. Right after the app's init has made them
// (SporeCoop::SaveAreas_Init 0x6B3760), a missing area gets a fallback directory database in the game's
// AppData folder (dir id 0xA02151, as Games\Game0 and Preferences), the way that init makes them; the
// first frame and the settings builder check again (a hot reload, an init that ran before the hook).
using SettingsBuildFn = void(__thiscall*)(void*, void*, void*, void*);
using CreateSaveAreaFn = bool(__cdecl*)(uint32_t dir, const wchar_t* name, void** out, uint32_t id);
using RegisterSaveAreaFn = void(__cdecl*)(uint32_t id, void* db, void* extra);
using GetDatabaseFn = void*(__cdecl*)(uint32_t id);
static SettingsBuildFn g_settings_orig;
using AreasInitFn = void(__cdecl*)();
static AreasInitFn g_areas_init_orig;
static bool g_areas_checked; // the startup check ran (after the app's init or in the first frame)
static CreateSaveAreaFn g_create_area;
static RegisterSaveAreaFn g_register_area;
static GetDatabaseFn g_get_db;
static std::atomic<unsigned> g_areas_made{0}, g_areas_failed{0};
static bool g_save_areas_fix = true;
COOP_FIX("game.save_areas", "BUG-0506", g_save_areas_fix,
         "gamefixes: missing Pictures / Movies / Animations save areas get a fallback folder right after the game's startup (settings, editors, screenshots)");

static void ensure_save_areas()
{
    static const struct {
        uint32_t id;
        const wchar_t* name;
    } kAreas[] = {{0x011AC196, L"Pictures"}, {0x011AC197, L"Movies"}, {0x011AC198, L"Animations"}};
    if (!g_get_db || !g_create_area || !g_register_area)
        return;
    for (const auto& a : kAreas) {
        if (g_get_db(a.id))
            continue;
        void* db = nullptr;
        bool ok = g_create_area(0x00A02151, a.name, &db, a.id) && db;
        if (ok) {
            g_register_area(a.id, db, nullptr);
            // the creator's reference, released as the app's init does (secondary interface at +4, slot 1)
            void* ref = (uint8_t*)db + 4;
            ((void(__thiscall*)(void*))(*(uintptr_t**)ref)[1])(ref);
        }
        ok = ok && g_get_db(a.id);
        unsigned n = ok ? ++g_areas_made : ++g_areas_failed;
        if (n <= 4)
            H->log(M, ok ? SK_WARN : SK_ERROR, "game fixes: save area 0x%08X (%s) was missing (Documents\\<PlayerDir> "
                   "could not be made: a PlayerDir with '?' as the ru-ru Steam install writes, or a blocked Documents): %s", a.id, std::string(a.name, a.name + wcslen(a.name)).c_str(),
                   ok ? "made in the game's AppData folder" : "the fallback failed too");
    }
}

static void log_areas(const char* when)
{
    if (!g_get_db)
        return;
    H->log(M, SK_INFO, "game fixes: save areas %s: Pictures=%p Movies=%p Animations=%p (fix %s)", when, g_get_db(0x011AC196),
           g_get_db(0x011AC197), g_get_db(0x011AC198), g_save_areas_fix ? "on" : "off");
}

static void __cdecl areas_init_guard()
{
    g_areas_init_orig();
    g_areas_checked = true;
    log_areas("after the game's startup");
    if (g_save_areas_fix)
        ensure_save_areas();
}

void frame()
{
    fxcut_frame();
    if (g_areas_checked || !g_get_db)
        return;
    g_areas_checked = true; // the init ran before our hook (hot reload, a late load): check once now
    H->run_outside_frame([](void*) {
        log_areas("at the first frame");
        if (g_save_areas_fix)
            ensure_save_areas();
    }, nullptr);
}

static void __fastcall settings_build_guard(void* self, void*, void* a, void* b, void* c)
{
    if (g_save_areas_fix)
        ensure_save_areas();
    g_settings_orig(self, a, b, c);
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    uintptr_t vt = H->sym("Terrain::cTerrainSphereTextureJob::vftable");
    uintptr_t fn = H->sym("Terrain::cTerrainSphereTextureJob::DrawLayer");
    uintptr_t* slot = vt ? (uintptr_t*)(vt + 3 * 4) : nullptr;
    bool ok = slot && fn && !IsBadReadPtr(slot, 4) && *slot == fn && patch_slot(slot, (uintptr_t)&draw_layer_guard);
    if (ok) {
        g_draw_slot = slot;
        g_draw_orig = (DrawLayerFn)fn;
    }
    H->log(M, SK_INFO, "game fixes: planet texture job guard %s", ok ? "on" : "off (unknown code)");
    uintptr_t la = H->sym("SporeCoop::SimCamera_LookAt"), cu = H->sym("SporeCoop::SimCamera_Update");
    bool cam = la && cu && H->hook_create(M, (void*)la, (void*)&lookat_guard, (void**)&g_lookat_orig) == 0 &&
               H->hook_create(M, (void*)cu, (void*)&camupd_guard, (void**)&g_camupd_orig) == 0;
    H->log(M, SK_INFO, "game fixes: tribe camera guard %s", cam ? "on" : "off");
    uintptr_t pc = H->sym("SporeCoop::cSimPlanetHighLOD_EndRespawnCaption");
    bool planet = pc && H->hook_create(M, (void*)pc, (void*)&planet_clear_guard, (void**)&g_planet_clear_orig) == 0;
    H->log(M, SK_INFO, "game fixes: respawn caption guard %s", planet ? "on" : "off");
    uintptr_t na = H->sym("SporeCoop::AppSystem_NextAdapter"), tf = H->sym("SporeCoop::AppSystem_ToggleFullscreen");
    g_toggle_fullscreen = (AppFlagFn)tf;
    bool keys = na && tf && H->hook_create(M, (void*)na, (void*)&next_adapter_guard, (void**)&g_next_adapter_orig) == 0;
    H->log(M, SK_INFO, "game fixes: Alt+Shift+Enter guard %s", keys ? "on" : "off");
    if (HMODULE exe = GetModuleHandleW(nullptr)) {
        auto* dos = (IMAGE_DOS_HEADER*)exe;
        auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + dos->e_lfanew);
        g_code_lo = (uintptr_t)exe;
        g_code_hi = g_code_lo + nt->OptionalHeader.SizeOfImage;
    }
    uintptr_t uh = H->sym("UTFWin::cWindowManager::UpdateHover");
    bool hover = uh && g_code_hi && H->hook_create(M, (void*)uh, (void*)&hover_guard, (void**)&g_hover_orig) == 0;
    H->log(M, SK_INFO, "game fixes: dead modal window guard %s", hover ? "on" : "off");
    uintptr_t os = H->sym("SporeCoop::Citizen_OffStageCheck");
    g_idle_walk = (IdleWalkFn)H->sym("SporeCoop::Citizen_IdleWalk");
    bool walk = os && g_idle_walk && H->hook_create(M, (void*)os, (void*)&offstage_guard, (void**)&g_offstage_orig) == 0;
    if (!walk)
        g_offstage_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: cleaned-up citizen walk guard %s", walk ? "on" : "off");
    uintptr_t pu = H->sym("SporeCoop::PhysicsManager_Update");
    g_phys_world = H->sym("SporeCoop::PhysicsWorld_ptr");
    bool phys = pu && g_phys_world && H->hook_create(M, (void*)pu, (void*)&phys_update_guard, (void**)&g_phys_orig) == 0;
    if (!phys)
        g_phys_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: physics body owner guard %s", phys ? "on" : "off");
    uintptr_t at_ = H->sym("SporeCoop::BT_AttackTick");
    bool attack = at_ && H->hook_create(M, (void*)at_, (void*)&attack_tick_guard, (void**)&g_attack_orig) == 0;
    if (!attack)
        g_attack_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: attack without a target guard %s", attack ? "on" : "off");
    uintptr_t rf = H->sym("RenderWare::Raster::Fill");
    bool fill = rf && H->hook_create(M, (void*)rf, (void*)&fill_guard, (void**)&g_fill_orig) == 0;
    if (!fill)
        g_fill_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: null raster fill guard %s", fill ? "on" : "off");
    uintptr_t di = H->sym("Simulator::cGameNounManager::DestroyInstance");
    bool dest = di && H->hook_create(M, (void*)di, (void*)&destroy_guard, (void**)&g_destroy_orig) == 0;
    if (!dest)
        g_destroy_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: stale stimulus sender guard %s", dest ? "on" : "off");
    uintptr_t ts = H->sym("SporeCoop::TribeAttack_TakeSpot");
    bool spot = ts && H->hook_create(M, (void*)ts, (void*)&take_spot_guard, (void**)&g_spot_orig) == 0;
    if (!spot)
        g_spot_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: creature attack spot guard %s", spot ? "on" : "off");
    uintptr_t sb = H->sym("SporeCoop::GameSettings_Build");
    g_get_db = (GetDatabaseFn)H->sym("Resource::GetDatabaseByID");
    g_create_area = (CreateSaveAreaFn)H->sym("Resource::Paths::CreateSaveAreaDirectoryDatabase");
    g_register_area = (RegisterSaveAreaFn)H->sym("Resource::Paths::RegisterSaveArea");
    bool areas = sb && g_get_db && g_create_area && g_register_area &&
                 H->hook_create(M, (void*)sb, (void*)&settings_build_guard, (void**)&g_settings_orig) == 0;
    if (!areas)
        g_settings_orig = nullptr;
    uintptr_t ai = H->sym("SporeCoop::SaveAreas_Init");
    if (!(ai && g_get_db && g_create_area && g_register_area &&
          H->hook_create(M, (void*)ai, (void*)&areas_init_guard, (void**)&g_areas_init_orig) == 0))
        g_areas_init_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: missing save area guard %s (startup hook %s)", areas ? "on" : "off",
           g_areas_init_orig ? "on" : "off");
    uintptr_t um = H->sym("SporeCoop::UfoGfx_SetModel"), uv = H->sym("SporeCoop::UfoGfx_SetVisible");
    bool ufo = um && uv && H->hook_create(M, (void*)um, (void*)&ufo_set_model_guard, (void**)&g_ufo_model_orig) == 0 &&
               H->hook_create(M, (void*)uv, (void*)&ufo_set_visible_guard, (void**)&g_ufo_visible_orig) == 0;
    H->log(M, SK_INFO, "game fixes: ship model guard %s", ufo ? "on" : "off");
    if (HMODULE exe = GetModuleHandleW(nullptr)) { // the game's code section (the vtable slot checks of BUG-0575)
        auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        g_text_lo = (uintptr_t)exe + sec->VirtualAddress;
        g_text_hi = g_text_lo + sec->Misc.VirtualSize;
    }
    uintptr_t fd = H->sym("SporeCoop::SpatialEffects_UpdateByDistance"), fs = H->sym("SporeCoop::SpatialEffects_SetTransform");
    g_fx_vt = H->sym("SporeCoop::VisualEffect_vftable");
    g_fx_start = H->sym("SporeCoop::VisualEffect_Start");
    bool fxok = fd && fs && g_fx_vt && g_fx_start && g_text_hi && in_image(g_fx_vt) &&
                ((const uintptr_t*)g_fx_vt)[2] == g_fx_start && // the vtable is the one this build's Start is in
                H->hook_create(M, (void*)fd, (void*)&fx_dist_guard, (void**)&g_fx_dist_orig) == 0 &&
                H->hook_create(M, (void*)fs, (void*)&fx_xf_guard, (void**)&g_fx_xf_orig) == 0;
    if (!fxok)
        g_fx_dist_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: model attached effect guard %s", fxok ? "on" : "off");
    uintptr_t tf_ = H->sym("SporeCoop::Flora_TakeFruitForDrop");
    g_fruit_vt = H->sym("SporeCoop::cFruit_vftable");
    bool fruit = tf_ && g_fruit_vt && in_image(g_fruit_vt) &&
                 H->hook_create(M, (void*)tf_, (void*)&take_fruit_guard, (void**)&g_take_fruit_orig) == 0;
    if (!fruit)
        g_take_fruit_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: fruit pool guard %s", fruit ? "on" : "off");
    H->command_add(M, "coop.fxlist", "coop.fxlist [creature] - a creature's attached effects (BUG-0575)", c_fxlist, nullptr,
                   SK_CMD_MAIN_THREAD);
    H->command_add(M, "coop.fxcut", "coop.fxcut - tests (BUG-0575): the next model with a running attached effect has it stopped and given a freed part",
                   c_fxcut, nullptr, SK_CMD_MAIN_THREAD);
    H->command_add(M, "coop.fruitpool", "coop.fruitpool - the plants' free and loose fruit: references, flags, state (BUG-0584)",
                   c_fruitpool, nullptr, SK_CMD_MAIN_THREAD);
}

void shutdown()
{
    if (g_draw_slot && patch_slot(g_draw_slot, (uintptr_t)g_draw_orig))
        g_draw_slot = nullptr;
}

std::string status()
{
    return std::string("game fixes: planet texture job guard ") + (g_draw_slot ? "on" : "off") + ", skipped " +
           std::to_string(g_skipped.load()) + (g_last.empty() ? "" : " (last: " + g_last + ")") +
           ", tribe camera skipped " + std::to_string(g_cam_skipped.load()) + ", respawn caption without a planet " +
           std::to_string(g_planet_clear_skipped.load()) + ", Alt+Shift+Enter turned into Alt+Enter " +
           std::to_string(g_adapter_keys.load()) + ", dead modal windows dropped " + std::to_string(g_dead_modal.load()) +
           ", off-stage checks " + std::to_string(g_offstage_calls.load()) + " (citizens while the stage is left " +
           std::to_string(g_offstage_citizens.load()) + "), cleaned-up citizen walks skipped " +
           std::to_string(g_leave_walk_skipped.load()) + ", test citizens " + std::to_string(g_leave_walk_test_done) +
           ", physics bodies without an owner left out " + std::to_string(g_phys_orphans.load()) + " (scans " +
           std::to_string(g_phys_scans.load()) + "), attacks ended without a target " + std::to_string(g_attack_ended.load()) +
           " (test cuts " + std::to_string(g_attack_cuts) + (g_attack_cut_test ? ", armed" : "") + ")" +
           ", null raster fills skipped " + std::to_string(g_null_fills.load()) +
           ", stale stimulus senders cleared " + std::to_string(g_senders_cleared.load()) + " (destroys " +
           std::to_string(g_destroys_scanned.load()) + "), missing save areas made " +
           std::to_string(g_areas_made.load()) + " (failed " + std::to_string(g_areas_failed.load()) + ")" +
           ", attack spots missing " + std::to_string(g_spots_missing.load()) + " (test cuts " +
           std::to_string(g_spot_cuts) + (g_spot_cut_test ? ", armed" : "") + ")" +
           ", ship models defaulted " + std::to_string(g_ufo_defaulted.load()) + " (SetVisible without a model skipped " +
           std::to_string(g_ufo_invisible_skipped.load()) + ")" + ", broken model effects dropped " +
           std::to_string(g_model_fx_dropped.load()) + " (tests: planted " + std::to_string(g_fxcut_planted) + ", dropped " + std::to_string(g_fxcut_done) + ")" +
           ", freed fruit taken out of the pool " + std::to_string(g_fruit_dropped.load()) + "\n";
}

} // namespace gamefixes
