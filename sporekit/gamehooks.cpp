// Game lifecycle hooks and a few game-aware helpers used by the devkit.
#include "core.h"

#include <atomic>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../sdk/include/sporekit_game.hpp"

using namespace skg;

namespace sk {

typedef int(SK_THISCALL* PreInit_t)(void*, int, int);
typedef int(SK_THISCALL* Init1_t)(void*, int);
typedef int(SK_THISCALL* Shutdown_t)(void*);

static PreInit_t o_PreInit;
static Init1_t o_AppSysInit, o_SporeAppInit;
static Shutdown_t o_Shutdown;

static int SK_THISCALL h_PreInit(void* self, int a, int b)
{
    watchdog_mark("PreInit");
    int r = o_PreInit(self, a, b);
    watchdog_mark("PreInit done");
    SKLOG(SK_INFO, "App::cAppSystem::PreInit -> %d", r);
    fire(SK_EV_APP_PREINIT, nullptr);
    return r;
}

static int SK_THISCALL h_AppSysInit(void* self, int a)
{
    watchdog_mark("AppSystem::Init");
    int r = o_AppSysInit(self, a);
    watchdog_mark("AppSystem::Init done");
    SKLOG(SK_INFO, "App::cAppSystem::Init -> %d", r);
    fire(SK_EV_APP_INIT, nullptr);
    return r;
}

static int SK_THISCALL h_SporeAppInit(void* self, int a)
{
    // tests (BUG-0521): mods\test-initfail-<instance> (read and removed here) makes the game's start fail
    // as it did on a player's PC: Init returns 0 before doing anything (a 0 after a full Init makes the
    // game's teardown fault, not the clean ExitProcess(0) of that PC). "off" in it: kit.fix initfail off
    bool fail_test = false;
    std::wstring marker = mods_dirw() + L"\\test-initfail-" + std::to_wstring(instance_id());
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        char b[16] = {};
        if (FILE* f = _wfopen(marker.c_str(), L"rb")) {
            fread(b, 1, sizeof b - 1, f);
            fclose(f);
        }
        DeleteFileW(marker.c_str());
        if (!strncmp(b, "off", 3))
            g_fix_initfail.on = 0;
        fail_test = true;
        SKLOG(SK_WARN, "tests: this start is made to fail (BUG-0521, initfail %s)", g_fix_initfail.on ? "on" : "off");
    }
    watchdog_mark("SporeApp::Init");
    int r = fail_test ? 0 : o_SporeAppInit(self, a);
    watchdog_mark("SporeApp::Init done");
    SKLOG(SK_INFO, "App::cSporeApp::Init -> %d", r);
    if (!r)
        diag_init_failed(r); // BUG-0521
    fire(SK_EV_POST_INIT, nullptr);
    return r;
}

// App::Canvas::Tick: thiscall(canvas), plain ret, once per pass of the game loop (it pumps the
// window messages; minimized it sleeps 200 ms). The frame source when Present is lost (d3d9hook.cpp).
typedef int(SK_THISCALL* CanvasTick_t)(void*);
static CanvasTick_t o_CanvasTick;
static int SK_THISCALL h_CanvasTick(void* self)
{
    int r = o_CanvasTick(self);
    watchdog_loop_tick();
    frame_from_game_loop();
    return r;
}

static int SK_THISCALL h_Shutdown(void* self)
{
    SKLOG(SK_INFO, "App::cSporeApp::Shutdown");
    fire(SK_EV_SHUTDOWN, nullptr);
    mods_shutdown();
    return o_Shutdown(self);
}

static void try_hook(const char* name, void* detour, void** orig)
{
    uintptr_t a = sym(name);
    if (!a) {
        SKLOG(SK_WARN, "lifecycle hook skipped, symbol missing: %s", name);
        return;
    }
    hook_create(&g_kit_mod, (void*)a, detour, orig);
}

bool gamehooks_install()
{
    try_hook("App::cAppSystem::PreInit", (void*)h_PreInit, (void**)&o_PreInit);
    try_hook("App::cAppSystem::Init", (void*)h_AppSysInit, (void**)&o_AppSysInit);
    try_hook("App::cSporeApp::Init", (void*)h_SporeAppInit, (void**)&o_SporeAppInit);
    try_hook("App::cSporeApp::Shutdown", (void*)h_Shutdown, (void**)&o_Shutdown);
    try_hook("App::Canvas::Tick", (void*)h_CanvasTick, (void**)&o_CanvasTick);
    return true;
}

// ---- helpers ----
uint32_t game_mode_id()
{
    uintptr_t get = sym("App::IGameModeManager::Get");
    if (!get)
        return 0;
    void* mgr = cdecl_call<void*>(get);
    if (!mgr)
        return 0;
    return vcall<uint32_t>(mgr, 0x38 / 4); // GetActiveModeID
}

// BUG-1022: the mode for reports written off the main thread, cached by the main thread (frame_common)
static std::atomic<uint32_t> g_mode_cached{0};
void game_mode_cache_tick() { g_mode_cached = game_mode_id(); }
uint32_t game_mode_cached() { return g_mode_cached; }

const char* game_mode_name(uint32_t id)
{
    switch (id) {
    case 0: return "none";
    case 0x1654C00: return "Cell";
    case 0x1654C01: return "Creature";
    case 0x1654C02: return "Tribe";
    case 0x1654C04: return "Civ";
    case 0x1654C05: return "Space";
    case 0x1654C06: return "GameEditMode";
    case 0x1654C08: return "LoadGame";
    case 0x1654C10: return "Scenario";
    case 0x00DBDBA1: return "Editor";
    case 0x2CCD1D2: return "GalaxyGameEntry";
    case 0x8916F92D: return "Cinematics";
    default: return "unknown";
    }
}

// cViewer (IGameModeManager vtable 0x58): +0x00 camera-to-world matrix, rows are
// right, forward, up, position. cViewer+0x170 -> Camera: viewWindow (half size at
// distance 1) at +0x5C, D3DVIEWPORT9 at +0x78. See docs/05-re-notes.md.
struct GameView {
    float V[16];
    float vw[2];
    uint32_t vp[4];
};

static bool read_view(GameView& g)
{
    uintptr_t get = sym("App::IGameModeManager::Get");
    if (!get)
        return false;
    void* mm = cdecl_call<void*>(get);
    if (!mm || !readable(mm, 4) || !readable(*(void**)mm, 0x5C))
        return false;
    void* viewer = vcall<void*>(mm, 0x58 / 4);
    if (!viewer || !readable(viewer, 0x174))
        return false;
    const float* cam = field<const float*>(viewer, 0x170);
    if (!cam || !readable(cam, 0x88))
        return false;
    memcpy(g.V, viewer, sizeof g.V);
    g.vw[0] = cam[0x5C / 4], g.vw[1] = cam[0x60 / 4];
    memcpy(g.vp, &cam[0x78 / 4], sizeof g.vp);
    return true;
}

static int project_view(const GameView& g, const float* w, float* out)
{
    const float* V = g.V;
    float d[3] = {w[0] - V[12], w[1] - V[13], w[2] - V[14]};
    float x = d[0] * V[0] + d[1] * V[1] + d[2] * V[2];
    float z = d[0] * V[4] + d[1] * V[5] + d[2] * V[6];
    float y = d[0] * V[8] + d[1] * V[9] + d[2] * V[10];
    if (z < 0.1f)
        return 0;
    float vwx = g.vw[0], vwy = g.vw[1];
    const uint32_t* vp = g.vp;
    if (vwx <= 0 || vwy <= 0 || !vp[2] || !vp[3])
        return 0;
    float nx = x / (z * vwx), ny = y / (z * vwy);
    out[0] = vp[0] + (nx + 1.f) * 0.5f * vp[2];
    out[1] = vp[1] + (1.f - ny) * 0.5f * vp[3];
    return nx > -1.2f && nx < 1.2f && ny > -1.2f && ny < 1.2f;
}

int world_to_screen_game(const float* w, float* out)
{
    GameView g;
    if (!read_view(g))
        return 0;
    return project_view(g, w, out);
}

// ---- BUG-0948: the camera the frame was drawn with ----
// The overlay pass runs in Present after the game drew the frame. While the camera turned, the name tags stood 230 px
// and more beside their creature or over empty ground: the game draws a frame with the viewer as it was at the frame
// before's Present (every D3D view matrix it set in a frame was that viewer, lab 2026-11-12 lane 4: kit.cam) and moves
// the viewer to the next camera step before Present, so the viewer read in the overlay pass is one step ahead of the
// picture. The viewer is copied at the game's first SetTransform(VIEW) after a Present (d3d9hook.cpp), and the overlay
// pass projects with that copy. `kit.fix drawncam 0`: the viewer at Present, as before.
static GameView g_drawn;
static bool g_drawn_ok;
static uint64_t g_drawn_takes, g_drawn_frames, g_drawn_moved;
static float g_drawn_last_px, g_drawn_max_px;

void drawn_view_take()
{
    g_drawn_ok = read_view(g_drawn);
    g_drawn_takes++;
}
void drawn_view_drop() { g_drawn_ok = false; }

// at the overlay pass: how far the viewer at Present is from the copy (kit.cam): a point 10 m ahead of the copy and off
// its middle (the orbit camera looks at the avatar: a point straight ahead stays in the middle), projected with both
void drawn_view_measure()
{
    if (!g_drawn_ok)
        return;
    GameView now;
    if (!read_view(now))
        return;
    g_drawn_frames++;
    const float* V = g_drawn.V;
    float p[3], a[2] = {0, 0}, b[2] = {0, 0};
    for (int i = 0; i < 3; i++)
        p[i] = V[12 + i] + V[4 + i] * 10.f + V[i] * 3.f + V[8 + i] * 2.f;
    project_view(g_drawn, p, a);
    if (!project_view(now, p, b) && memcmp(now.V, g_drawn.V, sizeof now.V)) {
        g_drawn_moved++;
        g_drawn_last_px = 1e4f;
        return;
    }
    float d = std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]));
    g_drawn_last_px = d;
    if (d > 0.5f)
        g_drawn_moved++;
    g_drawn_max_px = std::max(g_drawn_max_px, d);
}

std::string drawn_view_text(bool reset)
{
    char b[320];
    snprintf(b, sizeof b, "drawncam fix=%d copies=%llu overlay_frames=%llu moved=%llu last_px=%.1f max_px=%.1f have=%d",
             (int)fixed(g_fix_drawncam), (unsigned long long)g_drawn_takes, (unsigned long long)g_drawn_frames,
             (unsigned long long)g_drawn_moved, g_drawn_last_px, g_drawn_max_px, (int)g_drawn_ok);
    if (reset)
        g_drawn_frames = g_drawn_moved = 0, g_drawn_max_px = 0;
    return b;
}

int world_to_screen_drawn(const float* w, float* out)
{
    if (!g_drawn_ok)
        return world_to_screen_game(w, out);
    return project_view(g_drawn, w, out);
}

bool cheat_exec(const char* line)
{
    uintptr_t get = sym("App::ICheatManager::Get");
    if (!get)
        return false;
    void* cm = cdecl_call<void*>(get);
    if (!cm)
        return false;
    return vcall<bool>(cm, 0x20 / 4, line); // ProcessLine(const char*)
}

} // namespace sk
