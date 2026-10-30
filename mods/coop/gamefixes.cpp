#include "gamefixes.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include "fixes.h"
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
           "\n";
}

} // namespace gamefixes
