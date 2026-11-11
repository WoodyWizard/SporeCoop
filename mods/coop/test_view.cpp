// coop.view: a fixed camera for test screenshots (tests only, docs/06-testing.md#testing-same-view).
//
// The game's active camera (cCameraManager's current ICamera) writes the main viewer's transform every frame
// (cViewer::SetViewTransform from its Update). While coop.view is set, a hook on cCameraManager::Update lets the game's
// camera run and then puts our transform (an eye, a point it looks at, the planet's up) and a field of view on the
// viewer, so the frame is drawn from there. Nothing of the player's game changes: the hook is made at the first
// `coop.view set` (a test command) and passes through while the view is off.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <windows.h>

#include "sporekit.h"
#include "sporekit_game.hpp"

namespace testview {

static const SkHost* H;
static SkMod* M;
#define SAY(...) H->out_printf(out, __VA_ARGS__)

// Math::Transform as cViewer::GetViewTransform 0x7C41C0 fills it: flags (2 rotation, 4 offset), a change counter,
// the offset, the scale, the rotation's rows (the viewer's right, forward, up)
struct Transform {
    uint16_t flags, count;
    float off[3];
    float scale;
    float rot[9];
};
static_assert(sizeof(Transform) == 0x38, "Math::Transform");

using UpdateFn = void(SK_THISCALL*)(void* mgr, uint32_t dt);
static UpdateFn o_Update;
static bool g_hooked, g_on;
static float g_eye[3], g_at[3], g_up[3], g_fov;
static bool g_up_set;
static float g_vw0[2];
static bool g_have_vw0;
static unsigned g_applied, g_failed;

static void* viewer()
{
    uintptr_t get = H->sym("App::IGameModeManager::Get");
    void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
    return mm ? skg::vcall<void*>(mm, 0x58 / 4) : nullptr;
}

static float* cam_of(void* v)
{
    float* c = v ? *(float**)((uint8_t*)v + 0x170) : nullptr;
    return c && !IsBadReadPtr(c, 0x88) ? c : nullptr;
}

static void norm(float* a)
{
    float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (l > 1e-6f)
        for (int i = 0; i < 3; i++)
            a[i] /= l;
}

static void cross(const float* a, const float* b, float* o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static float det_rows(const float* m) // rows m[0..2], m[4..6], m[8..10] of the viewer's matrix
{
    float c[3];
    cross(m + 4, m + 8, c);
    return m[0] * c[0] + m[1] * c[1] + m[2] * c[2];
}

// our transform onto the viewer; false when there is no viewer
static bool apply()
{
    void* v = viewer();
    float* cam = cam_of(v);
    if (!v || !cam)
        return false;
    float* m = (float*)v; // camera-to-world: rows right, forward, up, position (sporekit gamehooks.cpp)
    float f[3] = {g_at[0] - g_eye[0], g_at[1] - g_eye[1], g_at[2] - g_eye[2]};
    norm(f);
    float up[3] = {g_up[0], g_up[1], g_up[2]};
    if (!g_up_set) { // on a planet: away from its centre at the eye
        memcpy(up, g_eye, sizeof up);
        norm(up);
    }
    float r[3], u[3];
    cross(f, up, r);
    norm(r);
    cross(r, f, u);
    if (det_rows(m) < 0) // the game's own rows are left-handed: the same handedness for ours
        for (float& x : r)
            x = -x;
    Transform t{};
    skg::thiscall<void>(H->sym("App::cViewer::GetViewTransform"), v, &t);
    t.flags |= 2 | 4;
    t.scale = 1.f;
    memcpy(t.off, g_eye, sizeof t.off);
    memcpy(t.rot, r, 12);
    memcpy(t.rot + 3, f, 12);
    memcpy(t.rot + 6, u, 12);
    skg::thiscall<void>(H->sym("App::cViewer::SetViewTransform"), v, &t);
    if (g_fov > 0.f && *(int*)&cam[0x6C / 4] == 1) { // a perspective camera only
        if (!g_have_vw0) {
            g_vw0[0] = cam[0x5C / 4];
            g_vw0[1] = cam[0x60 / 4];
            g_have_vw0 = true;
        }
        skg::thiscall<void>(H->sym("App::cViewer::SetPerspectiveProjection"), v, g_fov);
    }
    return true;
}

static void SK_THISCALL h_Update(void* mgr, uint32_t dt)
{
    o_Update(mgr, dt);
    if (g_on) {
        if (apply())
            g_applied++;
        else
            g_failed++;
    }
}

static void restore()
{
    void* v = viewer();
    if (g_have_vw0 && cam_of(v))
        skg::thiscall<void>(H->sym("App::cViewer::SetViewWindow"), v, g_vw0[0], g_vw0[1]);
    g_have_vw0 = false;
}

static int c_view(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc >= 8 && !strcmp(argv[1], "set")) {
        if (!g_hooked) {
            uintptr_t a = H->sym("App::cCameraManager::Update");
            if (!a || H->hook_create(M, (void*)a, (void*)&h_Update, (void**)&o_Update) != 0) {
                SAY("view: no hook on cCameraManager::Update");
                return 1;
            }
            g_hooked = true;
        }
        for (int i = 0; i < 3; i++) {
            g_eye[i] = (float)atof(argv[2 + i]);
            g_at[i] = (float)atof(argv[5 + i]);
        }
        g_fov = argc >= 9 ? (float)atof(argv[8]) : 0.f;
        g_up_set = argc >= 12;
        for (int i = 0; i < 3 && g_up_set; i++)
            g_up[i] = (float)atof(argv[9 + i]);
        if (g_on && g_fov <= 0.f)
            restore();
        g_on = true;
        g_applied = g_failed = 0;
        bool ok = apply(); // at once too: a shot right after the command sees it
        SAY("view set eye=(%.2f %.2f %.2f) at=(%.2f %.2f %.2f) fov=%.1f now=%d\n", g_eye[0], g_eye[1], g_eye[2],
            g_at[0], g_at[1], g_at[2], g_fov, ok ? 1 : 0);
        return ok ? 0 : 1;
    }
    if (argc >= 2 && !strcmp(argv[1], "off")) {
        bool was = g_on;
        g_on = false;
        restore();
        SAY("view off (was %s, applied %u frames)\n", was ? "on" : "off", g_applied);
        return 0;
    }
    void* v = viewer();
    float* m = (float*)v;
    float* cam = cam_of(v);
    SAY("view %s hooked=%d applied=%u failed=%u eye=(%.2f %.2f %.2f) at=(%.2f %.2f %.2f) fov=%.1f\n", g_on ? "on" : "off",
        g_hooked ? 1 : 0, g_applied, g_failed, g_eye[0], g_eye[1], g_eye[2], g_at[0], g_at[1], g_at[2], g_fov);
    if (m && cam)
        SAY("viewer pos=(%.2f %.2f %.2f) fwd=(%.4f %.4f %.4f) up=(%.4f %.4f %.4f) right=(%.4f %.4f %.4f) "
            "window=(%.4f %.4f) det=%.3f\n",
            m[12], m[13], m[14], m[4], m[5], m[6], m[8], m[9], m[10], m[0], m[1], m[2], cam[0x5C / 4], cam[0x60 / 4],
            det_rows(m));
    return 0;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    host->command_add(mod, "coop.view",
                      "coop.view [set ex ey ez tx ty tz [fov] [ux uy uz] | off] - tests: the camera at a fixed place looking "
                      "at a point (fov in degrees, vertical; up: the planet's by default), for the same view on two games",
                      c_view, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace testview
