// Wider third-person FOV in Creature and Galactic Adventures (BUG-0866).
//
// Hooks App::cViewer::BeginUpdate, EndUpdate, and GetCameraToPoint:
// - In BeginUpdate, when mode is Creature or Scenario, the active camera is the follow camera
//   (0xB03AF999), and step > 0, widens camera+0x5C/+0x60 via cViewer::SetViewWindow (keeping
//   aspect ratio) and saves the stock view window.
// - In EndUpdate, restores the stock view window so native zoom/camera logic between draws
//   continues to operate on the stock lens.
// - In GetCameraToPoint, recomputes the world picking ray using the widened view window so
//   world clicks (targeting, mating, interacting) match what was drawn on screen.
#include "fov.h"
#include "fov_logic.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "fixes.h"
#include "mouse.h"
#include "sporekit_game.hpp"

namespace fov {

static const SkHost* H;
static SkMod* M;
static std::function<void(const std::string&)> g_feed;

static bool g_fov_fix = true;
COOP_FIX("camera.fov", "BUG-0866", g_fov_fix,
         "camera: wider third-person FOV in Creature and Galactic Adventures (Ctrl+Scroll / coop.fov)");
static bool g_fov_pick_fix = true;
COOP_FIX("camera.fov_pick", "BUG-0866", g_fov_pick_fix,
         "camera: cViewer::GetCameraToPoint recomputes the picking ray for the widened third-person FOV");

using BeginUpdate_t = bool(SK_THISCALL*)(void* viewer);
using EndUpdate_t = int(SK_THISCALL*)(void* viewer);
using GetCameraToPoint_t = bool(SK_THISCALL*)(void* viewer, float x, float y, float* origin, float* dir);

static BeginUpdate_t o_BeginUpdate;
static EndUpdate_t o_EndUpdate;
static GetCameraToPoint_t o_GetCameraToPoint;

static uintptr_t a_ModeMgrGet;
static uintptr_t a_GetCamMgr;
static uintptr_t a_GetActiveCamID;
static uintptr_t a_SetViewWindow;

static bool g_hooked = false;
static int g_step = 0;
static int g_wheel_rem = 0;
static uint32_t g_last_mode = 0;

// Active draw-pass state (BeginUpdate -> EndUpdate)
static bool g_wide_applied = false;
static void* g_wide_viewer = nullptr;
static fov_logic::ViewWindow g_saved_vw{};

// Last observed live values (for diagnostics and lab verification)
static fov_logic::ViewWindow g_last_draw_vw{};
static uint32_t g_applied_draws = 0;
static uint32_t g_picked_rays = 0;
static uint32_t g_wheel_steps = 0;

static void* game_mode_mgr()
{
    if (!a_ModeMgrGet)
        return nullptr;
    void* mm = skg::cdecl_call<void*>(a_ModeMgrGet);
    return mm && skg::readable(mm, 0x30) ? mm : nullptr;
}

static uint32_t current_mode_id()
{
    void* mm = game_mode_mgr();
    if (!mm || !skg::readable(*(void**)mm, 0x3C))
        return 0;
    return skg::vcall<uint32_t>(mm, 0x38 / 4); // GetActiveModeID
}

static void* main_viewer()
{
    void* mm = game_mode_mgr();
    if (!mm || !skg::readable(*(void**)mm, 0x5C))
        return nullptr;
    return skg::vcall<void*>(mm, 0x58 / 4); // GetViewer
}

static uint32_t active_camera_id()
{
    if (!a_GetCamMgr || !a_GetActiveCamID)
        return 0;
    void* mm = game_mode_mgr();
    if (!mm)
        return 0;
    void* cm = skg::thiscall<void*>(a_GetCamMgr, mm);
    if (!cm || !skg::readable(cm, 4) || !skg::readable(*(void**)cm, 0x5C))
        return 0;
    return skg::thiscall<uint32_t>(a_GetActiveCamID, cm);
}

static uint8_t* perspective_camera(void* viewer)
{
    if (!viewer || !skg::readable(viewer, 0x174))
        return nullptr;
    uint8_t* cam = *(uint8_t**)((uint8_t*)viewer + 0x170);
    if (!cam || !skg::readable(cam, 0x88))
        return nullptr;
    if (*(int*)(cam + 0x6C) != 1) // 1 = perspective projection
        return nullptr;
    return cam;
}

static bool follow_camera_ready(void* viewer)
{
    if (!g_fov_fix || g_step <= 0 || !viewer)
        return false;
    uint32_t mode = current_mode_id();
    if (!fov_logic::is_fov_stage(mode))
        return false;
    if (viewer != main_viewer())
        return false;
    return active_camera_id() == fov_logic::kFollowCameraId;
}

static void restore_view_window(void* viewer, fov_logic::ViewWindow vw)
{
    if (!a_SetViewWindow || !viewer || vw.x <= 0.f || vw.y <= 0.f)
        return;
    if (perspective_camera(viewer))
        skg::thiscall<void>(a_SetViewWindow, viewer, vw.x, vw.y);
}

static bool SK_THISCALL h_BeginUpdate(void* self)
{
    // Guard against an earlier BeginUpdate that unwound past EndUpdate
    if (g_wide_applied) {
        if (g_wide_viewer == self)
            restore_view_window(g_wide_viewer, g_saved_vw);
        g_wide_applied = false;
        g_wide_viewer = nullptr;
    }

    if (follow_camera_ready(self) && a_SetViewWindow) {
        if (uint8_t* cam = perspective_camera(self)) {
            fov_logic::ViewWindow stock{*(float*)(cam + 0x5C), *(float*)(cam + 0x60)};
            fov_logic::ViewWindow wide = fov_logic::widen_view_window(stock, g_step);
            if (wide.x > stock.x && wide.y > stock.y) {
                g_saved_vw = stock;
                g_wide_viewer = self;
                g_wide_applied = true;
                skg::thiscall<void>(a_SetViewWindow, self, wide.x, wide.y);
                // Read back the live values written into Camera+0x5C/+0x60 for diagnostics
                g_last_draw_vw = {*(float*)(cam + 0x5C), *(float*)(cam + 0x60)};
                g_applied_draws++;
            }
        }
    }

    bool ok = o_BeginUpdate(self);
    if (!ok && g_wide_applied && g_wide_viewer == self) {
        restore_view_window(self, g_saved_vw);
        g_wide_applied = false;
        g_wide_viewer = nullptr;
    }
    return ok;
}

static int SK_THISCALL h_EndUpdate(void* self)
{
    int r = o_EndUpdate(self);
    if (g_wide_applied && self == g_wide_viewer) {
        restore_view_window(self, g_saved_vw);
        g_wide_applied = false;
        g_wide_viewer = nullptr;
    }
    return r;
}

static bool SK_THISCALL h_GetCameraToPoint(void* self, float x, float y, float* origin, float* dir)
{
    bool ok = o_GetCameraToPoint(self, x, y, origin, dir);
    if (!ok || !origin || !dir)
        return ok;
    if (!g_fov_pick_fix || !follow_camera_ready(self))
        return ok;
    // If called inside BeginUpdate..EndUpdate, the viewer already holds the wide projection
    if (g_wide_applied && self == g_wide_viewer)
        return ok;
    uint8_t* cam = perspective_camera(self);
    if (!cam)
        return ok;
    fov_logic::ViewWindow stock{*(float*)(cam + 0x5C), *(float*)(cam + 0x60)};
    fov_logic::ViewWindow wide = fov_logic::widen_view_window(stock, g_step);
    float off_x = *(float*)(cam + 0x54);
    float off_y = *(float*)(cam + 0x58);
    fov_logic::Viewport vp{
        *(uint16_t*)(cam + 0x78),
        *(uint16_t*)(cam + 0x7C),
        *(uint16_t*)(cam + 0x80),
        *(uint16_t*)(cam + 0x84),
    };
    fov_logic::Vec3 o{}, d{};
    if (fov_logic::compute_pick_ray((const float*)self, wide, off_x, off_y, vp, x, y, o, d)) {
        origin[0] = o.x;
        origin[1] = o.y;
        origin[2] = o.z;
        dir[0] = d.x;
        dir[1] = d.y;
        dir[2] = d.z;
        g_picked_rays++;
    }
    return ok;
}

static void notify_step(int step)
{
    if (!g_feed)
        return;
    char b[64];
    if (step == 0)
        snprintf(b, sizeof b, "Camera FOV: default");
    else
        snprintf(b, sizeof b, "Camera FOV: +%d deg (%d/%d)", (int)fov_logic::extra_deg(step), step,
                 fov_logic::kMaxStep);
    g_feed(b);
}

static bool on_wheel(int delta, uint16_t keys)
{
    if (!g_fov_fix)
        return false;
    bool ctrl = ((keys & MK_CONTROL) != 0) && ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
    if (!ctrl)
        return false;
    uint32_t mode = current_mode_id();
    if (!fov_logic::is_fov_stage(mode))
        return false;
    if (active_camera_id() != fov_logic::kFollowCameraId)
        return false;
    int next = fov_logic::apply_wheel_delta(g_step, delta, g_wheel_rem);
    if (next != g_step) {
        g_step = next;
        g_wheel_steps++;
        H->log(M, SK_INFO, "fov: Ctrl+Scroll -> step %d (+%.0f deg)", g_step, fov_logic::extra_deg(g_step));
        notify_step(g_step);
    }
    return true; // swallow Ctrl+Scroll so Spore does not also zoom
}

void init(const SkHost* host, SkMod* mod, std::function<void(const std::string&)> feed)
{
    H = host;
    M = mod;
    g_feed = std::move(feed);
    g_step = 0;
    g_wheel_rem = 0;
    g_wide_applied = false;
    g_wide_viewer = nullptr;
    g_last_draw_vw = {};
    g_applied_draws = 0;
    g_picked_rays = 0;
    g_wheel_steps = 0;

    a_ModeMgrGet = H->sym("App::IGameModeManager::Get");
    a_GetCamMgr = H->sym("App::cGameModeManager::GetCameraManager");
    a_GetActiveCamID = H->sym("App::cCameraManager::GetActiveCameraID");
    a_SetViewWindow = H->sym("App::cViewer::SetViewWindow");

    uintptr_t a_begin = H->sym("App::cViewer::BeginUpdate");
    uintptr_t a_end = H->sym("App::cViewer::EndUpdate");
    uintptr_t a_pick = H->sym("App::cViewer::GetCameraToPoint");
    if (!a_ModeMgrGet || !a_GetCamMgr || !a_GetActiveCamID || !a_SetViewWindow || !a_begin || !a_end || !a_pick) {
        H->log(M, SK_WARN, "fov: missing viewer/camera symbols, third-person FOV disabled");
        return;
    }
    int r1 = H->hook_create(M, (void*)a_begin, (void*)&h_BeginUpdate, (void**)&o_BeginUpdate);
    int r2 = H->hook_create(M, (void*)a_end, (void*)&h_EndUpdate, (void**)&o_EndUpdate);
    int r3 = H->hook_create(M, (void*)a_pick, (void*)&h_GetCameraToPoint, (void**)&o_GetCameraToPoint);
    if (r1 != 0 || r2 != 0 || r3 != 0) {
        H->log(M, SK_WARN, "fov: hook_create failed (%d/%d/%d)", r1, r2, r3);
        return;
    }
    g_hooked = true;
    mouse::set_wheel_handler(on_wheel);
    H->log(M, SK_INFO, "fov: third-person FOV ready (Creature / Adventure follow camera 0x%08X)",
           fov_logic::kFollowCameraId);
}

void tick(uint32_t mode)
{
    if (mode != g_last_mode) {
        g_last_mode = mode;
        if (g_step > 0 && !fov_logic::keeps_fov_mode(mode)) {
            H->log(M, SK_INFO, "fov: reset to 0 on leaving Creature/Adventure (mode 0x%08X)", mode);
            g_step = 0;
            g_wheel_rem = 0;
            g_last_draw_vw = {};
        }
    }
}

void shutdown()
{
    mouse::set_wheel_handler(nullptr);
    if (g_wide_applied && g_wide_viewer) {
        restore_view_window(g_wide_viewer, g_saved_vw);
        g_wide_applied = false;
        g_wide_viewer = nullptr;
    }
    g_hooked = false;
}

int step() { return g_step; }

int set_step(int s)
{
    s = fov_logic::clamp_step(s);
    if (s != g_step) {
        g_step = s;
        g_wheel_rem = 0;
        if (g_step == 0)
            g_last_draw_vw = {};
        if (H && M)
            H->log(M, SK_INFO, "fov: step %d (+%.0f deg)", g_step, fov_logic::extra_deg(g_step));
        notify_step(g_step);
    }
    return g_step;
}

int cycle_step()
{
    int next = g_step >= fov_logic::kMaxStep ? 0 : g_step + 1;
    return set_step(next);
}

bool active_now() { return follow_camera_ready(main_viewer()); }

int command(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc >= 2 && !strcmp(argv[1], "ray")) {
        if (argc < 4) {
            H->out_printf(out, "usage: coop.fov ray <x> <y>");
            return 1;
        }
        void* v = main_viewer();
        uintptr_t a_pick = H->sym("App::cViewer::GetCameraToPoint");
        if (!v || !a_pick) {
            H->out_printf(out, "fov: no viewer");
            return 1;
        }
        float sx = (float)atof(argv[2]), sy = (float)atof(argv[3]);
        float o[3]{}, d[3]{};
        bool ok = skg::thiscall<bool>(a_pick, v, sx, sy, o, d);
        H->out_printf(out, "ray ok=%d sx=%.1f sy=%.1f origin=(%.4f %.4f %.4f) dir=(%.6f %.6f %.6f)", ok ? 1 : 0, sx,
                      sy, o[0], o[1], o[2], d[0], d[1], d[2]);
        return ok ? 0 : 1;
    }
    if (argc >= 2) {
        int s = 0;
        if (!fov_logic::parse_step_arg(argv[1], s)) {
            H->out_printf(out, "usage: coop.fov [0-6|reset|ray <x> <y>]");
            return 1;
        }
        set_step(s);
    }
    void* v = main_viewer();
    uint8_t* cam = perspective_camera(v);
    float stock_x = cam ? *(float*)(cam + 0x5C) : 0.f;
    float stock_y = cam ? *(float*)(cam + 0x60) : 0.f;
    uint16_t vpx = cam ? *(uint16_t*)(cam + 0x78) : 0;
    uint16_t vpy = cam ? *(uint16_t*)(cam + 0x7C) : 0;
    uint16_t vpw = cam ? *(uint16_t*)(cam + 0x80) : 0;
    uint16_t vph = cam ? *(uint16_t*)(cam + 0x84) : 0;
    uint32_t mode = current_mode_id();
    uint32_t cid = active_camera_id();
    H->out_printf(out,
                  "fov hooked=%d step=%d extra=%.0fdeg active=%d mode=0x%08X cam=0x%08X fix=%d pick_fix=%d "
                  "stock=(%.4f %.4f) draw=(%.4f %.4f) vp=(%u %u %u %u) applied=%u picked=%u wheel=%u",
                  g_hooked ? 1 : 0, g_step, fov_logic::extra_deg(g_step), active_now() ? 1 : 0, mode, cid,
                  g_fov_fix ? 1 : 0, g_fov_pick_fix ? 1 : 0, stock_x, stock_y, g_last_draw_vw.x, g_last_draw_vw.y,
                  (unsigned)vpx, (unsigned)vpy, (unsigned)vpw, (unsigned)vph, g_applied_draws, g_picked_rays,
                  g_wheel_steps);
    return 0;
}

} // namespace fov
