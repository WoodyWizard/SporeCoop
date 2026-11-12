#include "gamefixes.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "creations.h"
#include "fixes.h"
#include "game.h"
#include "sporekit_game.hpp"
#include "world.h"

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
// BUG-0167 (0.7.11 N1): the guard as a lab switch; off calls the game's LookAt / 0xB13A60 on the null camera as before
// d605e96 (an access violation in the tribe setup: only for the FAIL side of coop_tribe_camera_null.py)
static bool g_cam_guard_fix = true;
COOP_FIX_UNSAFE("tribe.camera_guard", "BUG-0167", g_cam_guard_fix,
                "gamefixes: the tribe setup's LookAt / camera update are skipped while the simulator camera is null");

static void __fastcall lookat_guard(void* obj, void*, void* target, int a, int instant)
{
    if (!obj && g_cam_guard_fix) {
        // BUG-0656: the reason in the line: all 7 lab cases (2026-11-01..05) had the galaxy menu intro (8915F9BB)
        // playing into the stage (BUG-0607 / BUG-0626; none in 1905 Tribe loads since that fix)
        if (++g_cam_skipped <= 4)
            H->log(M, SK_WARN, "game fixes: skipped the tribe camera set-up without a simulator camera (mode %08X, cinematic %08X)",
                   game::mode_id(), world::cinematic_id());
        return;
    }
    if (!obj)
        H->log(M, SK_WARN, "game fixes: tribe camera guard off: the tribe setup points a null simulator camera (mode %08X)",
               game::mode_id());
    g_lookat_orig(obj, target, a, instant);
}

static void __fastcall camupd_guard(void* obj, void*)
{
    if (obj || !g_cam_guard_fix)
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

// BUG-0956: the windows under the modal one (the list +0x81C, node {next, prev, window, handler}) can hold a destroyed
// window as well. PopModal of the top one makes the next listed window the modal one and calls its vt 0x10 at once:
// a guest that had earned a Space badge and loaded the host's world from the same game had the refcounted base's
// vtable 0x13EB92C there (slot 0x10 reads 0x00420065, the tail `mov esp,ebp; pop ebp; ret 4` of 0x420050), so the
// game ran on with esp in the heap and Wine ended it without a report (the fault could not be dispatched). Before
// each PopModal (and UpdateHover) such entries are unlinked (the node is left to the game's allocator: 16 bytes).
// `coop.fix game.dead_modal_list off`: not checked (0.7.12).
static bool g_dead_modal_list_fix = true;
COOP_FIX("game.dead_modal_list", "BUG-0956", g_dead_modal_list_fix,
         "gamefixes: a destroyed window in the window manager's list of modal windows is dropped before PopModal makes it the modal one");
static std::atomic<unsigned> g_dead_modal_listed{0};
static bool modal_alive(const void* w);
static void drop_dead_listed(uint8_t* m, const char* when)
{
    if (!g_dead_modal_list_fix || !m)
        return;
    uintptr_t* head = (uintptr_t*)(m + 0x81C);
    int guard = 0;
    for (uintptr_t* n = (uintptr_t*)head[0]; n && n != head && guard < 256; guard++) {
        if (!readable(n, 16))
            break;
        uintptr_t* next = (uintptr_t*)n[0];
        void* w = (void*)n[2];
        if (w && !modal_alive(w)) {
            uintptr_t* prev = (uintptr_t*)n[1];
            if (!readable(prev, 8) || !readable(next, 8))
                break;
            prev[0] = (uintptr_t)next; // unlink, as PopModal does (0x9594A8)
            next[1] = (uintptr_t)prev;
            unsigned k = ++g_dead_modal_listed;
            if (k <= 8)
                H->log(M, SK_WARN, "game fixes: a destroyed window %p (vtable %08X) was listed under the modal window (%s): "
                       "dropped it (BUG-0956)", w, readable(w, 4) ? (unsigned)*(uintptr_t*)w : 0u, when);
        }
        n = next;
    }
}

using PopModalFn = bool(__thiscall*)(void*, void*, void*);
static PopModalFn g_popmodal_orig;
static bool __fastcall popmodal_guard(void* wm, void*, void* w, void* arg)
{
    drop_dead_listed((uint8_t*)wm, "PopModal");
    return g_popmodal_orig(wm, w, arg);
}

static void* __fastcall hover_guard(void* wm, void*, void* out, int flag)
{
    drop_dead_listed((uint8_t*)wm, "UpdateHover");
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

// tests (BUG-0956): a destroyed window listed under a modal window, then that modal window popped (WM vt 0x78), as
// the game's mode switch does. The listed object's vtable slots hold 0x65 (the destroyed object's slot 0x10 led to
// 0x00420065 and the game ran on in the heap; a fault at 0x65 shows the same call without killing the stack)
static uintptr_t g_test_vt2[0x100 / 4];
static uintptr_t g_test_dead[4], g_test_top[4], g_test_node[4];
std::string fake_dead_listed(void* wm)
{
    uint8_t* m = (uint8_t*)wm;
    if (!m)
        return "no window manager\n";
    if (*(void**)(m + 0x82C))
        return "a modal window is open: not replaced\n";
    uintptr_t* head = (uintptr_t*)(m + 0x81C);
    if (head[0] != (uintptr_t)head)
        return "modal windows listed: not replaced\n";
    for (auto& v : g_test_vt2)
        v = 0x65;
    g_test_dead[0] = (uintptr_t)g_test_vt2;
    g_test_top[0] = (uintptr_t)g_test_vt2; // never called: PopModal calls only the window it makes modal
    g_test_node[0] = (uintptr_t)head;
    g_test_node[1] = (uintptr_t)head;
    g_test_node[2] = (uintptr_t)g_test_dead;
    g_test_node[3] = 0;
    head[0] = head[1] = (uintptr_t)g_test_node;
    *(void**)(m + 0x82C) = g_test_top;
    *(void**)(m + 0x830) = nullptr;
    void** vt = *(void***)m;
    using Pop = bool(__thiscall*)(void*, void*, void*);
    bool r = ((Pop)vt[0x78 / 4])(m, g_test_top, nullptr);
    bool clean = !*(void**)(m + 0x82C) && head[0] == (uintptr_t)head;
    if (!clean) { // the fake objects must not stay in the game's window manager
        *(void**)(m + 0x82C) = nullptr;
        head[0] = head[1] = (uintptr_t)head;
    }
    return std::string("popped the modal window over a destroyed listed one: ") + (r ? "1" : "0") +
           (clean ? ", the window manager has no modal window and nothing listed\n" : ", left over (cleared)\n");
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
    game::after_physics(); // BUG-0434 (creature.culled_ground)
}

// BUG-0719 (class BUG-0432, tests): the fill of the game's fixed object pools every stage uses (the physics
// world's body pool +0x3C, 0x1000 slots, and contact pool +0x08, 0x800; docs/re/tribe.md#tribe-physics-orphan) and the
// world's objects by kind (game::for_each_object), so a stress run can show how far co-op object counts take them
std::string pools_report()
{
    std::string out;
    char b[200];
    uint8_t* w = g_phys_world && readable((void*)g_phys_world, 4) ? *(uint8_t**)g_phys_world : nullptr;
    if (w && readable(w, 0xBC)) {
        for (int off : {0x3C, 0x08}) {
            uint8_t* p = w + off;
            snprintf(b, sizeof b, "pool %s: count=%u high=%u cap=%u\n", off == 0x3C ? "physics_bodies" : "physics_contacts",
                     *(uint32_t*)(p + 0xC), *(uint32_t*)(p + 0x10), *(uint32_t*)(p + 0x18));
            out += b;
        }
    } else
        out += "pool physics_bodies: none\n";
    struct Acc {
        uint32_t creatures = 0, all = 0;
    } a;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* a = (Acc*)u;
            (void)o;
            a->all++;
            a->creatures += noun == 0x18EB45Eu;
            return true;
        },
        &a);
    snprintf(b, sizeof b, "objects: all=%u creatures=%u\n", a.all, a.creatures);
    return out + b;
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

// BUG-0926: a stimulus also names a second creature, at +0x10, with no reference: in the "attacked" stimulus 0x200
// that TakeDamage posts to the victim's herd the sender (+0x2C) is the victim and +0x10 is the ATTACKER. The scrub of
// BUG-0456 looked at the sender only, so the herd mates of an NPC the partner had hit kept naming the partner's
// destroyed puppet as the attacker; the puppet's block lives on while references to it remain and is freed when the
// last one goes, and the herd-help node (BT_HerdHelpEval 0xD8F1C0) then called +0xC0 vt 0x58 / +0x5A8 vt 0xC of the
// freed creature: a pure virtual call (R6025, the game ends; lab 20261111-064457 and the 0.7.12 final check's
// long_creature, 0.4-2 s after the partner's death; on Windows 0x40000015 at 0xD8F2AD, BUG-0456's own signature).
// A stimulus whose +0x10 names the creature being destroyed is cleared as a whole, like one sent by it.
// Old (`coop.fix npc.stale_attacker off`): the sender only.
static bool g_stale_attacker_fix = true;
COOP_FIX("npc.stale_attacker", "BUG-0926", g_stale_attacker_fix,
         "creature: a destroyed creature is also taken out of the NPCs' stimuli that name it as the attacker (the herd-help AI called it after it was freed: runtime error R6025, the game ended)");
static std::atomic<unsigned> g_attackers_cleared{0};
constexpr size_t kStimOther = 0x10;

static unsigned scrub_stimuli(uint8_t* stim, const uint8_t* dying)
{
    unsigned n = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t* s = stim + i * kStimSlot;
        uint8_t* snd = at<uint8_t*>(s, kStimSender);
        uint8_t* other = at<uint8_t*>(s, kStimOther);
        bool by_sender = snd >= dying && snd < dying + kCreatureSpan;
        bool by_other = g_stale_attacker_fix && other >= dying && other < dying + kCreatureSpan;
        if (by_other) // (no reference to drop; also in a slot that is not live: the next post need not write it)
            *(void**)(s + kStimOther) = nullptr;
        if (!(at<uint32_t>(s, 0) | at<uint32_t>(s, 4)) || (!by_sender && !by_other))
            continue;
        if (!by_sender)
            g_attackers_cleared++;
        *(uint32_t*)s = 0;
        *(uint32_t*)(s + 4) = 0;
        *(uint32_t*)(s + 8) = 0;
        *(void**)(s + kStimSender) = nullptr;
        if (snd && readable(snd, 4) && readable(*(void**)snd, 8)) // Release, as the game's clear does
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

// BUG-0918 (found with BUG-0917): the avatar's combat target (+0xE7C, the target's combatant) outlives the target. The game clears the
// target of every NPC aimed at a creature it destroys, not the avatar's (vanilla never destroys a creature the player
// fights: corpses stay 60 s); co-op destroys a partner's puppet (the partner dies, hides, leaves) and a guest's
// proxies. Lab 20261111-030815: the host's avatar, its target the partner's puppet (a click on it), named the
// removed puppet for the whole 1.5 s watched, and the puppet's memory was given to a later puppet. The game's own
// ability use (Ability_Use 0xD1E930, a combat key) casts that pointer (vt 0x5C) and measures the gap to it
// (Ability_TargetGap): a use of freed memory. Before a creature is destroyed, an avatar aimed at it loses its target
// through the creature's own SetTarget (vt 0x84, null: the reference is released while the target still lives).
static bool g_avatar_target_fix = true;
COOP_FIX("game.avatar_stale_target", "BUG-0918", g_avatar_target_fix,
         "creature: the avatar's combat target is cleared when the creature it names is destroyed (a combat key then used freed memory)");
static std::atomic<unsigned> g_avatar_targets_cleared{0};
static void forget_avatar_target(void* dying)
{
    void* av = game::in_creature_stage() ? game::avatar() : nullptr;
    if (!av || av == dying || !readable(av, 0xE80))
        return;
    const uint8_t* t = at<const uint8_t*>(av, 0xE7C);
    if (!t || t < (const uint8_t*)dying || t >= (const uint8_t*)dying + kCreatureSpan)
        return;
    skg::vcall<void>(av, 0x84 / 4, (void*)nullptr, 1, 0);
    unsigned n = ++g_avatar_targets_cleared;
    if (n <= 16 || n % 100 == 0)
        H->log(M, SK_INFO, "game fixes: the avatar's target was %p, destroyed now: cleared (BUG-0918, %u in all)", dying, n);
}

// BUG-0930: the creature a creature was hit by last is kept at its combatant +0x28 (creature +0x5D0) with a reference.
// When co-op destroys a partner's puppet in a fight, the NPC it hit last keeps that reference: the destroyed puppet's
// block lives on (vtables intact, destroyed flag set, count 1) until that NPC is hit by another creature or goes, 25 s
// and more in the lab (lab lane 2, coop_stale_refs.py --watch). Vanilla never destroys a creature in a fight (corpses
// stay 60 s). Before a creature is destroyed, every listed creature whose last attacker is it lets go of it (the
// reference released, the field null, as a creature never hit has it), so the block is freed with its other references.
// Old (`coop.fix npc.stale_last_attacker off`): the reference stays until the game replaces it.
static bool g_last_attacker_fix = true;
COOP_FIX("npc.stale_last_attacker", "BUG-0930", g_last_attacker_fix,
         "creature: an NPC lets go of a destroyed partner's puppet that hit it last (its block was kept alive by that NPC for 25 s and more)");
static std::atomic<unsigned> g_last_attackers_cleared{0};
constexpr size_t kLastAttackerOff = 0x5A8 + 0x28; // combatant (+0x5A8) +0x28

static void forget_last_attacker(void* dying)
{
    struct LA {
        uint8_t* dying;
        unsigned n;
    } la{(uint8_t*)dying, 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* la = (LA*)u;
            if ((noun != kAnimalNoun && noun != kCitizenNoun) || o == la->dying || !readable(o, kLastAttackerOff + 4))
                return true;
            uint8_t* a = at<uint8_t*>(o, kLastAttackerOff);
            if (a < la->dying || a >= la->dying + kCreatureSpan)
                return true;
            *(void**)((uint8_t*)o + kLastAttackerOff) = nullptr;
            if (readable(a, 4) && readable(*(void**)a, 8)) // Release, as the game does when it replaces it
                ((void(__thiscall*)(void*))(*(void***)a)[1])(a);
            la->n++;
            return true;
        },
        &la);
    if (la.n) {
        unsigned n = g_last_attackers_cleared += la.n;
        if (n <= 16 || n % 100 < la.n)
            H->log(M, SK_INFO, "game fixes: %u NPC(s) were last hit by %p, destroyed now: let go (BUG-0930, %u in all)", la.n,
                   dying, n);
    }
}

static DestroyListener g_destroy_listener; // BUG-0623 (civ.cpp, BUG-0539)
void set_destroy_listener(DestroyListener fn) { g_destroy_listener = fn; }
static void* g_count_obj; // tests (BUG-0850)
static std::atomic<unsigned> g_count_n{0};
bool count_destroys(void* obj)
{
    g_count_obj = obj;
    g_count_n = 0;
    return g_destroy_orig != nullptr;
}
unsigned destroys_counted() { return g_count_n; }
bool destroy_hooked() { return g_destroy_orig != nullptr; }
// BUG-0926 (RE, tests): who still names a destroyed creature. The last creatures destroyed (address, when, whether a
// puppet of ours) are kept; `coop.staleref [index]` scans the process's writable memory for pointers into such a
// creature's block and says where each one sits: inside a listed object (noun, offset), inside a creature's AI block,
// in a module's data (our own tables), elsewhere on the heap. A pointer into a freed creature that a live object
// still holds is a use of freed memory waiting for its reader (BUG-0453, BUG-0456, BUG-0918 were found one by one).
struct Destroyed {
    uint8_t* at;
    double when;
    bool puppet;
    uint32_t serial;
};
static Destroyed g_destroyed[8];
static unsigned g_destroyed_n;
static bool g_staleref_on;

struct StaleHit {
    uint32_t noun; // 0: not in a listed object
    uint32_t off;
    uint8_t kind;  // 0 object, 1 AI block, 2 module, 3 other, 4 cModifiers (BUG-0930)
    void* first;
    unsigned n;
    char mod[24];
};

static int c_staleref(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))) {
        g_staleref_on = argv[1][1] == 'n';
        H->out_printf(out, "staleref %s\n", g_staleref_on ? "on" : "off");
        return 0;
    }
    if (!g_destroyed_n) {
        H->out_printf(out, "no creature destroyed since `coop.staleref on`\n");
        return 0;
    }
    uint8_t* want = argc > 1 ? (uint8_t*)strtoul(argv[1], nullptr, 16) : nullptr; // a creature's address: that one
    const Destroyed* d = nullptr;
    for (unsigned k = 0; k < g_destroyed_n && k < 8 && !d; k++) { // the newest first
        const Destroyed& c = g_destroyed[(g_destroyed_n - 1 - k) % 8];
        if (!want || c.at == want)
            d = &c;
    }
    if (!d) {
        H->out_printf(out, "%p was not destroyed since `coop.staleref on` (or 8 creatures ago)\n", want);
        return 0;
    }
    // the listed objects and the creatures' AI blocks, sorted by address
    struct Blk {
        uint8_t* at;
        uint32_t size, noun;
        uint8_t kind;
    };
    static std::vector<Blk> blks;
    blks.clear();
    game::for_each_object(
        [](void* o, uint32_t noun, void*) {
            bool creature = noun == kAnimalNoun || noun == kCitizenNoun;
            blks.push_back({(uint8_t*)o, creature ? (uint32_t)kCreatureSpan : 0x400u, noun, 0});
            // BUG-0930: the AI block (cBehaviorTreeData) is 0x670 bytes (GeneralAllocator::new_ at 0xC1FF76); the next
            // heap block is usually the creature's cModifiers (+0xB48, 0x230 bytes, 0xC0B797): its own kind here
            if (creature && readable(o, kAiOff + 4))
                if (uint8_t* ai = at<uint8_t*>(o, kAiOff); ai && readable(ai, 0x670))
                    blks.push_back({ai, 0x670, noun, 1});
            if (creature && readable(o, 0xB48 + 4))
                if (uint8_t* md = at<uint8_t*>(o, 0xB48); md && readable(md, 0x230))
                    blks.push_back({md, 0x230, noun, 4});
            return true;
        },
        nullptr);
    std::sort(blks.begin(), blks.end(), [](const Blk& a, const Blk& b) { return a.at < b.at; });
    std::vector<StaleHit> hits;
    unsigned total = 0, in_self = 0;
    uintptr_t lo = (uintptr_t)d->at, hi = lo + kCreatureSpan;
    volatile uint32_t marker = 0;
    MEMORY_BASIC_INFORMATION mi;
    for (uint8_t* a = (uint8_t*)0x10000; a < (uint8_t*)0x7FFF0000 && VirtualQuery(a, &mi, sizeof mi) == sizeof mi;
         a = (uint8_t*)mi.BaseAddress + mi.RegionSize) {
        if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)))
            continue;
        uint32_t* p = (uint32_t*)mi.BaseAddress;
        uint32_t* e = (uint32_t*)((uint8_t*)mi.BaseAddress + mi.RegionSize);
        if ((uint8_t*)&marker >= (uint8_t*)p && (uint8_t*)&marker < (uint8_t*)e) // our own stack
            continue;
        for (; p < e; p++) {
            uint32_t v = *p;
            if (v < lo || v >= hi)
                continue;
            total++;
            if ((uintptr_t)p >= lo && (uintptr_t)p < hi) { // the block's own pointers into itself
                in_self++;
                continue;
            }
            StaleHit h{0, 0, 3, p, 1, ""};
            auto it = std::upper_bound(blks.begin(), blks.end(), (uint8_t*)p, [](uint8_t* x, const Blk& b) { return x < b.at; });
            if (it != blks.begin() && (uint8_t*)p < (it - 1)->at + (it - 1)->size) {
                h.noun = (it - 1)->noun;
                h.kind = (it - 1)->kind;
                h.off = (uint32_t)((uint8_t*)p - (it - 1)->at);
            } else if (mi.Type == MEM_IMAGE) {
                h.kind = 2;
                h.off = (uint32_t)((uint8_t*)p - (uint8_t*)mi.AllocationBase);
                char path[MAX_PATH] = "";
                GetModuleFileNameA((HMODULE)mi.AllocationBase, path, sizeof path);
                const char* b = strrchr(path, '\\');
                snprintf(h.mod, sizeof h.mod, "%s", b ? b + 1 : path);
            } else
                h.off = v - (uint32_t)lo; // elsewhere: grouped by what part of the creature is named
            bool merged = false;
            for (auto& o : hits)
                if (o.kind == h.kind && o.noun == h.noun && o.off == h.off && !strcmp(o.mod, h.mod)) {
                    o.n++;
                    merged = true;
                    break;
                }
            if (!merged && hits.size() < 400)
                hits.push_back(h);
        }
    }
    H->out_printf(out, "staleref %p puppet=%d serial=%u destroyed %.2f s ago: %u pointers into it (%u its own), listed now %d\n", d->at,
                  (int)d->puppet, d->serial, H->time_seconds() - d->when, total, in_self, (int)game::creature_listed(d->at));
    if (readable(d->at, 0x5B0)) // what the block is now: the creature's vtables (alive or a zombie), a base class's (destructed), other
        H->out_printf(out, "  block now: vt %08X +4 %08X +8 %08X destroyed byte %u, +0xC0 vt %08X +0xC4 %08X, +0x5A8 vt %08X +0x5AC %08X\n",
                      at<uint32_t>(d->at, 0), at<uint32_t>(d->at, 4), at<uint32_t>(d->at, 8), (unsigned)at<uint8_t>(d->at, 0x21),
                      at<uint32_t>(d->at, 0xC0), at<uint32_t>(d->at, 0xC4), at<uint32_t>(d->at, 0x5A8), at<uint32_t>(d->at, 0x5AC));
    static const uint8_t rank[] = {0, 1, 3, 4, 2}; // object, ai, modifiers, module, other
    std::sort(hits.begin(), hits.end(), [](const StaleHit& a, const StaleHit& b) {
        return a.kind != b.kind ? rank[a.kind] < rank[b.kind] : a.off < b.off;
    });
    static const char* kinds[] = {"object", "ai", "module", "other", "modifiers"};
    unsigned shown = 0;
    for (auto& h : hits) {
        if (h.kind == 3 && shown >= 80)
            break;
        if (h.kind == 3)
            H->out_printf(out, "  other names +0x%X x%u first at %p\n", h.off, h.n, h.first);
        else if (h.kind == 2)
            H->out_printf(out, "  module %s+0x%X x%u\n", h.mod, h.off, h.n);
        else
            H->out_printf(out, "  %s noun=%08X +0x%X x%u first at %p -> +0x%X\n", kinds[h.kind], h.noun, h.off, h.n, h.first,
                          (unsigned)(*(uint32_t*)h.first - lo));
        shown++;
    }
    return 0;
}

static void __fastcall destroy_guard(void* nm, void*, void* obj)
{
    if (g_staleref_on && obj && readable(obj, 0x30) && game::noun_id(obj) == kAnimalNoun)
        g_destroyed[g_destroyed_n++ % 8] = {(uint8_t*)obj, H->time_seconds(), game::is_puppet(obj), game::object_serial(obj)};
    if (obj && obj == g_count_obj)
        g_count_n++;
    game::note_destroyed(obj); // (BUG-0426: the object list of this frame, game::listed)
    if (g_destroy_listener && obj)
        g_destroy_listener(obj, (uintptr_t)__builtin_return_address(0));
    if (g_stale_sender_fix && obj && readable(obj, 0x30)) {
        uint32_t noun = game::noun_id(obj);
        if (noun == kAnimalNoun || noun == kCitizenNoun)
            forget_sender(obj);
    }
    if (g_avatar_target_fix && obj && readable(obj, 0x30) && game::noun_id(obj) == kAnimalNoun)
        forget_avatar_target(obj);
    if (g_last_attacker_fix && obj && readable(obj, 0x30)) {
        uint32_t noun = game::noun_id(obj);
        if (noun == kAnimalNoun || noun == kCitizenNoun)
            forget_last_attacker(obj);
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

// BUG-0686 (docs/re/space.md#space-ufo-gfx-null): the ship gfx factory (SporeCoop::UfoGfx_Create 0xE994D0, slot 2 of
// the gfx vtable 0x1486F6C, thiscall(any, cUFO*) ret 4) news a cUFOGfx (0xD8 bytes, ctor 0xE993A0), runs its
// InitFromUfo (vt 0x24), then, outside game mode 0x1654C10, writes GetModel() (vt 0x2C) +4 |= 0x20 without a null
// check: a read of 0x4 at 0xE99524 when the model world made no model for the ship's key (SetModel keeps none; the
// game.ufo_model substitute needs a known ship model, which a Creature game has not). Seen on the host when the
// Creature Drama Manager's UFO visit started while the evolution vote held the editor request (lab lanes 2 and 3,
// whose Sporepedia caches hold UFO summaries only). The gfx's message listener (this = gfx + 0x3C,
// SporeCoop::UfoGfx_HandleMessage 0xE9A7A0, thiscall(listener, id, data) ret 8) writes GetModel()+0x18 on message
// 0x604C6F4 without a check too (0xE9A908, data 0x18). The guard makes the gfx the game's way and skips the flag
// on a null model; the listener does nothing for 0x604C6F4 on a gfx without a model. Off: the game's code (0.7.10).
using UfoGfxCreateFn = void*(__thiscall*)(void*, void*);
using UfoGfxMsgFn = uint8_t(__thiscall*)(void*, uint32_t, void*);
using UfoGfxCtorFn = void*(__thiscall*)(void*);
using NewFn = void*(__cdecl*)(size_t, const char*, int, int, int, int);
using ModeFn = uint32_t(__cdecl*)();
static UfoGfxCreateFn g_ufo_create_orig;
static UfoGfxMsgFn g_ufo_msg_orig;
static UfoGfxCtorFn g_ufo_ctor;
static NewFn g_game_new;
static ModeFn g_game_mode;
static bool g_ufo_gfx_fix = true;
COOP_FIX("game.ufo_gfx", "BUG-0686", g_ufo_gfx_fix,
         "gamefixes: a ship gfx the model world made no model for is made and kept without the null model's writes (no fault at 0xE99524 / 0xE9A908)");
static std::atomic<unsigned> g_ufo_gfx_null{0}, g_ufo_msg_skipped{0};
static constexpr uint32_t kUfoGfxModeNoFlag = 0x1654C10, kUfoMsgModel = 0x604C6F4;

static void* ufo_gfx_model(void* gfx)
{
    if (!gfx || !readable(gfx, 4))
        return nullptr;
    void** vt = *(void***)gfx;
    if (!vt || !readable(vt + 0x2C / 4, 4))
        return nullptr;
    using GetModelFn = void*(__thiscall*)(void*);
    return ((GetModelFn)vt[0x2C / 4])(gfx);
}

static void* __fastcall ufo_gfx_create_guard(void* self, void*, void* ufo)
{
    if (!g_ufo_gfx_fix || !g_ufo_ctor || !g_game_new || !g_game_mode)
        return g_ufo_create_orig(self, ufo);
    void* mem = g_game_new(0xD8, "Simulator/cUFOGfx", 0, 0, 0, 0);
    if (!mem)
        return g_ufo_create_orig(self, ufo); // (the game's own path: it faults on a null gfx too)
    void* gfx = g_ufo_ctor(mem);
    void** vt = *(void***)gfx;
    using InitFn = void(__thiscall*)(void*, void*);
    ((InitFn)vt[0x24 / 4])(gfx, ufo);
    void* model = ufo_gfx_model(gfx);
    if (model) {
        if (g_game_mode() != kUfoGfxModeNoFlag)
            *(uint32_t*)((uint8_t*)model + 4) |= 0x20;
    } else {
        unsigned n = ++g_ufo_gfx_null;
        if (n <= 8 || n % 100 == 0)
            H->log(M, SK_WARN, "game fixes: a ship gfx was made without a model (ship %p): kept, no fault (BUG-0686, %u)", ufo, n);
    }
    return gfx;
}

static uint8_t __fastcall ufo_gfx_msg_guard(void* listener, void*, uint32_t id, void* data)
{
    if (g_ufo_gfx_fix && id == kUfoMsgModel && listener && !ufo_gfx_model((uint8_t*)listener - 0x3C)) {
        unsigned n = ++g_ufo_msg_skipped;
        if (n <= 8 || n % 100 == 0)
            H->log(M, SK_WARN, "game fixes: a ship gfx without a model: its message %08X skipped (BUG-0686, %u)", id, n);
        return 1;
    }
    return g_ufo_msg_orig(listener, id, data);
}

std::string ufo_forget()
{
    char b[96];
    snprintf(b, sizeof b, "forgot the last known ship model %08X/%08X/%08X\n", g_ufo_good[0], g_ufo_good[1], g_ufo_good[2]);
    memset(g_ufo_good, 0, sizeof g_ufo_good);
    return b;
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

// BUG-0676: the galaxy menu (GalaxyGameEntry, global [0x16A1344]) keeps the planet under the mouse at +0xF8 (the
// rollover, set by the planet set's vt 0x5C 0xDF7BF0) and the picked one at +0xFC, both pointers into the planet set's
// entry vector ([0x16A15FC] +0x08..+0x0C, 0xBC bytes each, +0xB8 the entry's info panel). Its planet rebuild
// (SporeCoop::GGE_RebuildPlanets 0xDF95A0, also on 0x625D27D, which cStarManager::ReadStarsFromSaveDB sends) clears every
// entry (+0xB8 = 0, the panels freed), resizes the vector (a reallocation frees the old one) and refills it, but leaves
// both pointers alone. Leaving the menu (its exit 0xDE8160) then rolls the old planet out (vt 0x5C(0)) and calls
// 0xDF6D80 on its panel without a check: a guest that loaded a received world while the mouse rested on a planet faulted
// at 0xDF6D93 (data 0x2C); the exit stopped half-way and the menu's frame faulted at 0xE01323 in every frame after
// (chaos_tribe_awful, 22939 faults). The guard rolls the planet out the game's own way before the rebuild and drops a
// pointer that names no entry of the new vector after it. Off: the game's code alone (0.7.9).
using MenuRebuildFn = void(__thiscall*)(void*);
static MenuRebuildFn g_menu_rebuild_orig;
static uintptr_t g_gge_ptr;
static bool g_menu_hover_fix = true;
COOP_FIX("game.menu_hover", "BUG-0676", g_menu_hover_fix,
         "gamefixes: the galaxy menu forgets the planet under the mouse when it rebuilds its planets (a load after it faulted at 0xDF6D93)");
static std::atomic<unsigned> g_menu_rollouts{0}, g_menu_dropped{0}, g_menu_rebuilds{0};
static int g_menu_hover_arm = -1; // tests (coop.menuhover arm <i>): the next rebuild first puts the mouse on entry i
static bool g_menu_hover_watch;   // tests: the armed rebuild logs the rollover before and after

struct MenuPlanets {
    uint8_t* gge = nullptr;
    void* set = nullptr;
    uint8_t *begin = nullptr, *end = nullptr, *cap = nullptr;
    size_t count() const { return begin && end > begin ? (size_t)(end - begin) / 0xBC : 0; }
    bool entry(const uint8_t* p) const // a live entry of the vector with its panel
    {
        return p && p >= begin && p < end && (size_t)(p - begin) % 0xBC == 0 && readable(p + 0xB8, 4) && at<void*>(p, 0xB8);
    }
};

static MenuPlanets menu_planets(void* set)
{
    MenuPlanets m;
    m.gge = g_gge_ptr && readable((void*)g_gge_ptr, 4) ? *(uint8_t**)g_gge_ptr : nullptr;
    if (m.gge && !readable(m.gge + 0xF8, 8))
        m.gge = nullptr;
    m.set = set;
    if (set && readable(set, 0x14)) {
        m.begin = at<uint8_t*>(set, 8);
        m.end = at<uint8_t*>(set, 0xC);
        m.cap = at<uint8_t*>(set, 0x10);
    }
    return m;
}

static void* menu_planet_set()
{
    uintptr_t p = H->sym("SporeCoop::GGE_PlanetSet_ptr");
    return p && readable((void*)p, 4) ? *(void**)p : nullptr;
}

static void __fastcall menu_rebuild_guard(void* set, void*)
{
    g_menu_rebuilds++;
    MenuPlanets m = menu_planets(set);
    if (g_menu_hover_arm >= 0 && m.gge && (size_t)g_menu_hover_arm < m.count() && !at<void*>(m.gge, 0xFC)) {
        uint8_t* e = m.begin + (size_t)g_menu_hover_arm * 0xBC;
        if (m.entry(e)) {
            skg::vcall<void>(set, 0x5C / 4, (void*)e); // the rollover, as the mouse over the planet
            H->log(M, SK_WARN, "game fixes: tests: the mouse is on menu planet %d (%p) before the rebuild (%u entries, room for %u)",
                   g_menu_hover_arm, e, (unsigned)m.count(), m.cap > m.begin ? (unsigned)((m.cap - m.begin) / 0xBC) : 0u);
        }
        g_menu_hover_arm = -1;
        g_menu_hover_watch = true;
    }
    void* hover = m.gge ? at<void*>(m.gge, 0xF8) : nullptr;
    if (g_menu_hover_fix && hover && !at<void*>(m.gge, 0xFC) && m.entry((uint8_t*)hover)) {
        skg::vcall<void>(set, 0x5C / 4, (void*)nullptr); // the game's roll-out: hides the panel, +0xF8 = 0
        unsigned n = ++g_menu_rollouts;
        if (n <= 8 || n % 100 == 0)
            H->log(M, SK_INFO, "game fixes: the galaxy menu rebuilds its planets with the mouse on planet %p: rolled out first (BUG-0676, %u)",
                   hover, n);
    }
    void* hover_in = hover;
    g_menu_rebuild_orig(set);
    if (g_menu_hover_watch && m.gge) {
        g_menu_hover_watch = false;
        MenuPlanets now = menu_planets(set);
        uint8_t* h = at<uint8_t*>(now.gge, 0xF8);
        H->log(M, SK_WARN, "game fixes: tests: menu rollover %p before the rebuild, %p after (%s; panel %p; %u entries at %p, were at %p)",
               hover_in, h, now.entry(h) ? "a planet of the list" : "no planet of the list", h && readable(h + 0xB8, 4) ? at<void*>(h, 0xB8) : nullptr,
               (unsigned)now.count(), now.begin, m.begin);
    }
    if (!g_menu_hover_fix || !m.gge)
        return;
    MenuPlanets now = menu_planets(set);
    for (uint32_t off : {0xF8u, 0xFCu}) {
        uint8_t* p = at<uint8_t*>(now.gge, off);
        if (!p || now.entry(p))
            continue;
        *(void**)(now.gge + off) = nullptr;
        unsigned n = ++g_menu_dropped;
        if (n <= 8 || n % 100 == 0)
            H->log(M, SK_WARN, "game fixes: the galaxy menu's %s planet %p is no planet of the rebuilt list (%u entries at %p): dropped (BUG-0676, %u)",
                   off == 0xF8 ? "rollover" : "picked", p, (unsigned)now.count(), now.begin, n);
    }
}

// coop.menuhover [arm <i> | off]: the galaxy menu's planets, its rollover / picked planet; tests: arm the rollover
static int c_menuhover(void*, int argc, const char* const* argv, SkOut* out)
{
    char b[512];
    if (argc >= 3 && !strcmp(argv[1], "arm")) {
        g_menu_hover_arm = atoi(argv[2]);
        snprintf(b, sizeof b, "armed: the next planet rebuild of the galaxy menu puts the mouse on planet %d first\n", g_menu_hover_arm);
        H->out_printf(out, "%s", b);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "off"))
        g_menu_hover_arm = -1;
    MenuPlanets m = menu_planets(menu_planet_set());
    int panels = 0;
    for (size_t i = 0; i < m.count(); i++)
        panels += m.entry(m.begin + i * 0xBC);
    snprintf(b, sizeof b,
             "menu=%p set=%p planets=%u (panels %d, room %u) begin=%p rollover=%p picked=%p | rebuilds %u rolled out %u dropped %u%s\n",
             m.gge, m.set, (unsigned)m.count(), panels, m.cap > m.begin ? (unsigned)((m.cap - m.begin) / 0xBC) : 0u, m.begin,
             m.gge ? at<void*>(m.gge, 0xF8) : nullptr, m.gge ? at<void*>(m.gge, 0xFC) : nullptr, g_menu_rebuilds.load(),
             g_menu_rollouts.load(), g_menu_dropped.load(), g_menu_hover_arm >= 0 ? " (armed)" : "");
    H->out_printf(out, "%s", b);
    return 0;
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
    uintptr_t pm = H->sym("UTFWin::cWindowManager::PopModal");
    bool pop = pm && g_code_hi && H->hook_create(M, (void*)pm, (void*)&popmodal_guard, (void**)&g_popmodal_orig) == 0;
    H->log(M, SK_INFO, "game fixes: dead listed modal window guard %s (BUG-0956)", pop ? "on" : "off");
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
    uintptr_t ugc = H->sym("SporeCoop::UfoGfx_Create"), ugm = H->sym("SporeCoop::UfoGfx_HandleMessage");
    g_ufo_ctor = (UfoGfxCtorFn)H->sym("SporeCoop::UfoGfx_ctor");
    g_game_new = (NewFn)H->sym("GeneralAllocator::new_");
    g_game_mode = (ModeFn)H->sym("Simulator_GetCurrentGameMode");
    bool ufo_gfx = ugc && ugm && g_ufo_ctor && g_game_new && g_game_mode &&
                   H->hook_create(M, (void*)ugc, (void*)&ufo_gfx_create_guard, (void**)&g_ufo_create_orig) == 0 &&
                   H->hook_create(M, (void*)ugm, (void*)&ufo_gfx_msg_guard, (void**)&g_ufo_msg_orig) == 0;
    H->log(M, SK_INFO, "game fixes: ship gfx null model guard %s (BUG-0686)", ufo_gfx ? "on" : "off");
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
    uintptr_t mr = H->sym("SporeCoop::GGE_RebuildPlanets");
    g_gge_ptr = H->sym("SporeCoop::GalaxyGameEntry_ptr");
    bool menu = mr && g_gge_ptr && H->sym("SporeCoop::GGE_PlanetSet_ptr") &&
                H->hook_create(M, (void*)mr, (void*)&menu_rebuild_guard, (void**)&g_menu_rebuild_orig) == 0;
    if (!menu)
        g_menu_rebuild_orig = nullptr;
    H->log(M, SK_INFO, "game fixes: galaxy menu rollover guard %s", menu ? "on" : "off");
    H->command_add(M, "coop.menuhover", "coop.menuhover [arm <i> | off] - the galaxy menu's planets and its rollover; tests: the next rebuild puts the mouse on planet i first (BUG-0676)",
                   c_menuhover, nullptr, SK_CMD_MAIN_THREAD);
    H->command_add(M, "coop.staleref", "coop.staleref on | off | [creature] - RE / tests (BUG-0926): pointers into the creature destroyed last (or the one at that address), by where they sit",
                   c_staleref, nullptr, SK_CMD_MAIN_THREAD);
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
           std::to_string(g_adapter_keys.load()) + ", dead modal windows dropped " + std::to_string(g_dead_modal.load()) + ", dead listed modal windows dropped " + std::to_string(g_dead_modal_listed.load()) +
           ", off-stage checks " + std::to_string(g_offstage_calls.load()) + " (citizens while the stage is left " +
           std::to_string(g_offstage_citizens.load()) + "), cleaned-up citizen walks skipped " +
           std::to_string(g_leave_walk_skipped.load()) + ", test citizens " + std::to_string(g_leave_walk_test_done) +
           ", physics bodies without an owner left out " + std::to_string(g_phys_orphans.load()) + " (scans " +
           std::to_string(g_phys_scans.load()) + "), attacks ended without a target " + std::to_string(g_attack_ended.load()) +
           " (test cuts " + std::to_string(g_attack_cuts) + (g_attack_cut_test ? ", armed" : "") + ")" +
           ", null raster fills skipped " + std::to_string(g_null_fills.load()) +
           ", avatar targets cleared " + std::to_string(g_avatar_targets_cleared.load()) +
           ", stale stimulus senders cleared " + std::to_string(g_senders_cleared.load()) + " (as the attacker " +
           std::to_string(g_attackers_cleared.load()) + ", destroys " +
           std::to_string(g_destroys_scanned.load()) + "), missing save areas made " +
           std::to_string(g_areas_made.load()) + " (failed " + std::to_string(g_areas_failed.load()) + ")" +
           ", attack spots missing " + std::to_string(g_spots_missing.load()) + " (test cuts " +
           std::to_string(g_spot_cuts) + (g_spot_cut_test ? ", armed" : "") + ")" +
           ", ship models defaulted " + std::to_string(g_ufo_defaulted.load()) + " (SetVisible without a model skipped " +
           std::to_string(g_ufo_invisible_skipped.load()) + "), ship gfx made without a model " +
           std::to_string(g_ufo_gfx_null.load()) + " (their messages skipped " + std::to_string(g_ufo_msg_skipped.load()) +
           ")" + ", broken model effects dropped " +
           std::to_string(g_model_fx_dropped.load()) + " (tests: planted " + std::to_string(g_fxcut_planted) + ", dropped " + std::to_string(g_fxcut_done) + ")" +
           ", freed fruit taken out of the pool " + std::to_string(g_fruit_dropped.load()) +
           ", galaxy menu rollovers rolled out before a rebuild " + std::to_string(g_menu_rollouts.load()) + " (stale dropped " +
           std::to_string(g_menu_dropped.load()) + ")\n";
}

} // namespace gamefixes
