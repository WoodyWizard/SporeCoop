#include "gamefixes.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

static bool patch_slot(uintptr_t* slot, uintptr_t value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old))
        return false;
    *slot = value;
    VirtualProtect(slot, sizeof *slot, old, &old);
    return true;
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
           std::to_string(g_phys_scans.load()) + ")\n";
}

} // namespace gamefixes
