// D3D9 interception: Direct3DCreate9 -> CreateDevice -> Present/Reset.
// Provides the per-frame tick (main thread), screenshots and the text overlay.
#include "core.h"

#include <d3d9.h>

#include <cmath>
#include <mutex>

namespace sk {

typedef IDirect3D9*(WINAPI* Direct3DCreate9_t)(UINT);
typedef HRESULT(WINAPI* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                        D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(WINAPI* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
                                   const RGNDATA*);
typedef HRESULT(WINAPI* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT(WINAPI* SwapPresent_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND,
                                       const RGNDATA*, DWORD);

static Direct3DCreate9_t o_Direct3DCreate9;
static CreateDevice_t o_CreateDevice;
static Present_t o_Present;
static Reset_t o_Reset;
static SwapPresent_t o_SwapPresent;

static IDirect3DDevice9* g_dev;
static HWND g_hwnd;
static uint64_t g_frame;
static double g_fps, g_last_t;
static bool g_in_frame;

// ---- overlay font (D3DX from the game's own d3dx9_27.dll) ----
struct ID3DXFontVtbl; // we call through raw vtable indices
typedef HRESULT(WINAPI* D3DXCreateFontW_t)(IDirect3DDevice9*, INT, UINT, UINT, UINT, BOOL, DWORD,
                                           DWORD, DWORD, DWORD, LPCWSTR, void**);
static void* g_font;   // ID3DXFont*
static bool g_font_failed;

static void** vt(void* obj) { return *(void***)obj; }

static void font_release()
{
    if (g_font) {
        ((ULONG(WINAPI*)(void*))vt(g_font)[2])(g_font);
        g_font = nullptr;
    }
}

static void font_ensure()
{
    if (g_font || g_font_failed || !g_dev)
        return;
    HMODULE dx = GetModuleHandleW(L"d3dx9_27.dll");
    if (!dx)
        dx = LoadLibraryW(L"d3dx9_27.dll");
    auto create = dx ? (D3DXCreateFontW_t)(void*)GetProcAddress(dx, "D3DXCreateFontW") : nullptr;
    if (!create || FAILED(create(g_dev, 18, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                                 L"Arial", &g_font))) {
        g_font_failed = true;
        g_font = nullptr;
        SKLOG(SK_WARN, "overlay: D3DXCreateFontW unavailable, overlay disabled");
    }
}

static bool g_overlay_active;
void overlay_text(int x, int y, uint32_t argb, const char* utf8)
{
    if (!g_overlay_active || !g_font || !utf8)
        return;
    std::wstring w = widen(utf8);
    RECT r = {x, y, x + 2000, y + 400};
    typedef INT(WINAPI * DrawTextW_t)(void*, void*, LPCWSTR, INT, RECT*, DWORD, D3DCOLOR);
    auto draw = (DrawTextW_t)vt(g_font)[15];
    // drop shadow for readability
    RECT rs = {x + 1, y + 1, x + 2001, y + 401};
    draw(g_font, nullptr, w.c_str(), -1, &rs, DT_NOCLIP, (argb & 0xFF000000u));
    draw(g_font, nullptr, w.c_str(), -1, &r, DT_NOCLIP, argb);
}

int world_to_screen_game(const float* w, float* out);
int world_to_screen(const float* w, float* out) { return world_to_screen_game(w, out); }

static int g_bb_w, g_bb_h;
void screen_size(int* w, int* h)
{
    if (w) *w = g_bb_w;
    if (h) *h = g_bb_h;
}

// ---- screenshots ----
static std::mutex g_shot_mu;
static std::wstring g_shot_path;
static std::function<void(bool, std::string)> g_shot_done;

void request_screenshot(const std::wstring& path, std::function<void(bool, std::string)> done)
{
    std::lock_guard<std::mutex> lk(g_shot_mu);
    if (g_shot_done)
        g_shot_done(false, "superseded by a newer screenshot request");
    g_shot_path = path;
    g_shot_done = std::move(done);
}

static void capture_if_requested()
{
    std::wstring path;
    std::function<void(bool, std::string)> done;
    {
        std::lock_guard<std::mutex> lk(g_shot_mu);
        if (!g_shot_done)
            return;
        path = g_shot_path;
        done = std::move(g_shot_done);
        g_shot_done = nullptr;
    }
    IDirect3DSurface9 *bb = nullptr, *rt = nullptr, *sys = nullptr;
    std::string err;
    bool ok = false;
    D3DSURFACE_DESC d{};
    do {
        if (FAILED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) { err = "GetBackBuffer"; break; }
        bb->GetDesc(&d);
        IDirect3DSurface9* src = bb;
        if (d.MultiSampleType != D3DMULTISAMPLE_NONE) {
            if (FAILED(g_dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE,
                                                 0, FALSE, &rt, nullptr))) { err = "CreateRenderTarget"; break; }
            if (FAILED(g_dev->StretchRect(bb, nullptr, rt, nullptr, D3DTEXF_NONE))) { err = "StretchRect"; break; }
            src = rt;
        }
        if (FAILED(g_dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM,
                                                      &sys, nullptr))) { err = "CreateOffscreenPlainSurface"; break; }
        if (FAILED(g_dev->GetRenderTargetData(src, sys))) { err = "GetRenderTargetData"; break; }
        D3DLOCKED_RECT lr;
        if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) { err = "LockRect"; break; }
        std::vector<uint8_t> rgb((size_t)d.Width * d.Height * 3);
        bool fmt_ok = d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8;
        for (UINT y = 0; y < d.Height && fmt_ok; y++) {
            const uint8_t* row = (const uint8_t*)lr.pBits + (size_t)y * lr.Pitch;
            uint8_t* o = &rgb[(size_t)y * d.Width * 3];
            for (UINT x = 0; x < d.Width; x++) {
                o[x * 3 + 0] = row[x * 4 + 2];
                o[x * 3 + 1] = row[x * 4 + 1];
                o[x * 3 + 2] = row[x * 4 + 0];
            }
        }
        sys->UnlockRect();
        if (!fmt_ok) { err = "unsupported backbuffer format " + std::to_string((int)d.Format); break; }
        ok = png_write_rgb(path, (int)d.Width, (int)d.Height, rgb.data());
        if (!ok)
            err = "png write failed: " + narrow(path);
    } while (false);
    if (sys) sys->Release();
    if (rt) rt->Release();
    if (bb) bb->Release();
    done(ok, ok ? (narrow(path) + " " + std::to_string(d.Width) + "x" + std::to_string(d.Height)) : err);
}

// ---- frame source ----
// Normally every frame starts in the Present hook. Another overlay can take Present over and call
// the driver past it (seen on Windows with the Steam overlay while Steam is online: issue #4, frames
// stopped right after the menu came up): the game draws, SporeKit sees no frame, and every mod
// stops ticking. The game loop (App::Canvas::Tick, once per loop pass) is then the frame source:
// when Present has not been seen for kPresentLostSec while the window has the focus (or background
// mode is on) and is not minimized, the per-frame work runs from there (without the overlay pass,
// which needs the frame's back buffer).
static constexpr double kPresentLostSec = 2.0;
static double g_last_present_t;  // last Present seen (or the device creation)
static bool g_loop_frames;       // frames come from the game loop now
static bool g_in_loop_frame;
static int g_frames_mode = -1;   // 0 auto, 1 loop: ignore Present (tests of the fallback), -1 unread

static int frames_mode_read()
{
    if (g_frames_mode < 0) {
        std::string m = ini_str(mods_dirw() + L"\\sporekit.ini", "sporekit", "frames", "auto");
        g_frames_mode = m == "loop" ? 1 : 0;
    }
    return g_frames_mode;
}

// ---- per-frame ----
// the work of one frame that does not draw: counters, frame limit, tasks, SK_EV_FRAME
static void frame_common()
{
    g_frame++;
    double t = now_seconds();
    float dt = g_last_t > 0 ? (float)(t - g_last_t) : 0.f;
    if (dt > 0)
        g_fps = g_fps * 0.95 + (1.0 / dt) * 0.05;
    g_last_t = t;
    if (g_frame == 1)
        SKLOG(SK_INFO, "first frame (thread %lu, main %lu)", GetCurrentThreadId(), main_thread_id());
    // the game sizes/centers its window around device resets: place test windows afterwards
    if (g_frame == 2 || g_frame == 120 || g_frame == 600)
        run_outside_frame(place_test_window);
    if (g_frame % 10 == 1)
        apply_frame_limit();

    vcursor_tick();
    cheats_flush_pending();
    pump_main_tasks();
    exceptions_pump();
    report_shot_tick();
    if (g_frame == 30)
        diag_after_device();
    fire(SK_EV_FRAME, &dt);
}

static void on_frame_begin_present()
{
    if (g_in_frame || g_in_loop_frame)
        return;
    if (frames_mode_read() == 1)
        return; // frames=loop: as if another overlay had taken Present
    g_last_present_t = now_seconds();
    if (g_loop_frames) {
        g_loop_frames = false;
        SKLOG(SK_INFO, "frames: Present reaches SporeKit again; frames come from Present");
    }
    g_in_frame = true;
    frame_common();

    // overlay pass
    font_ensure();
    if (g_font) {
        IDirect3DSurface9 *bb = nullptr, *old_rt = nullptr;
        if (SUCCEEDED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
            g_dev->GetRenderTarget(0, &old_rt);
            g_dev->SetRenderTarget(0, bb);
            if (SUCCEEDED(g_dev->BeginScene())) {
                g_overlay_active = true;
                fire(SK_EV_OVERLAY, g_dev);
                g_overlay_active = false;
                g_dev->EndScene();
            }
            g_dev->SetRenderTarget(0, old_rt ? old_rt : bb);
            if (old_rt) old_rt->Release();
            bb->Release();
        }
    }
    capture_if_requested();
    report_shot_on_present(g_dev); // a report screenshot: this frame with the overlay
    g_in_frame = false;
}

static int g_present_depth; // device Present may call the swapchain Present internally

// called on the main thread after every App::Canvas::Tick (gamehooks.cpp)
void frame_from_game_loop()
{
    if (!g_dev || g_in_frame || g_in_loop_frame || g_present_depth)
        return;
    bool forced = frames_mode_read() == 1;
    if (!forced && now_seconds() - g_last_present_t < kPresentLostSec)
        return;
    // Without focus Spore pauses and stops drawing (no Present) while the loop goes on: no frames
    // then, as before, unless background mode keeps it running (hooked GetForegroundWindow says
    // so). A minimized Spore draws nothing either.
    if (!g_hwnd || IsIconic(g_hwnd) || GetForegroundWindow() != g_hwnd)
        return;
    if (!g_loop_frames) {
        g_loop_frames = true;
        if (forced)
            SKLOG(SK_INFO, "frames: [sporekit] frames=loop: frames come from the game loop (App::Canvas::Tick)");
        else
            SKLOG(SK_WARN, "frames: Present has not reached SporeKit for %.0f s (another overlay took it over?); "
                           "frames come from the game loop (App::Canvas::Tick), on-screen labels are off",
                  kPresentLostSec);
    }
    g_in_loop_frame = true;
    frame_common();
    g_in_loop_frame = false;
}

const char* frame_source()
{
    if (!g_dev)
        return "none (no device yet)";
    if (frames_mode_read() == 1)
        return g_loop_frames ? "game loop (frames=loop, test)" : "none yet (frames=loop, test)";
    return g_loop_frames ? "game loop (Present not seen)" : "Present";
}

void set_frame_source_mode(bool loop) { g_frames_mode = loop ? 1 : 0; }

static HRESULT WINAPI h_Present(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w,
                                const RGNDATA* r)
{
    if (dev == g_dev && g_present_depth == 0)
        on_frame_begin_present();
    g_present_depth++;
    HRESULT hr = o_Present(dev, a, b, w, r);
    g_present_depth--;
    return hr;
}

static HRESULT WINAPI h_SwapPresent(IDirect3DSwapChain9* sc, const RECT* a, const RECT* b, HWND w,
                                    const RGNDATA* r, DWORD f)
{
    if (g_present_depth == 0) {
        IDirect3DDevice9* dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(&dev))) {
            if (dev == g_dev)
                on_frame_begin_present();
            dev->Release();
        }
    }
    g_present_depth++;
    HRESULT hr = o_SwapPresent(sc, a, b, w, r, f);
    g_present_depth--;
    return hr;
}

static HRESULT WINAPI h_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    fire(SK_EV_DEVICE_LOST, dev);
    if (g_font)
        ((HRESULT(WINAPI*)(void*))vt(g_font)[16])(g_font); // OnLostDevice
    HRESULT hr = o_Reset(dev, pp);
    if (SUCCEEDED(hr) && pp) {
        g_bb_w = pp->BackBufferWidth;
        g_bb_h = pp->BackBufferHeight;
    }
    if (g_font && SUCCEEDED(hr))
        ((HRESULT(WINAPI*)(void*))vt(g_font)[17])(g_font); // OnResetDevice
    fire(SK_EV_DEVICE_RESET, dev);
    SKLOG(SK_INFO, "device Reset -> 0x%08lX (%ux%u)", hr, pp ? pp->BackBufferWidth : 0,
          pp ? pp->BackBufferHeight : 0);
    return hr;
}

static void hook_device(IDirect3DDevice9* dev)
{
    void** v = vt(dev);
    if (!o_Present)
        hook_create(&g_kit_mod, v[17], (void*)h_Present, (void**)&o_Present);
    if (!o_Reset)
        hook_create(&g_kit_mod, v[16], (void*)h_Reset, (void**)&o_Reset);
    IDirect3DSwapChain9* sc = nullptr;
    if (!o_SwapPresent && SUCCEEDED(dev->GetSwapChain(0, &sc))) {
        void* p = vt(sc)[3];
        if (p != v[17])
            hook_create(&g_kit_mod, p, (void*)h_SwapPresent, (void**)&o_SwapPresent);
        sc->Release();
    }
}

static HRESULT WINAPI h_CreateDevice(IDirect3D9* d3d, UINT ad, D3DDEVTYPE t, HWND w, DWORD f,
                                     D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
{
    HRESULT hr = o_CreateDevice(d3d, ad, t, w, f, pp, out);
    SKLOG(SK_INFO, "CreateDevice hwnd=%p flags=0x%lX %ux%u windowed=%d -> 0x%08lX", w, f,
          pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0, pp ? pp->Windowed : -1, hr);
    if (SUCCEEDED(hr) && out && *out) {
        g_dev = *out;
        g_last_present_t = now_seconds(); // the frame source fallback counts from here
        if (pp) {
            g_bb_w = pp->BackBufferWidth;
            g_bb_h = pp->BackBufferHeight;
        }
        g_hwnd = w ? w : (pp ? pp->hDeviceWindow : nullptr);
        focus_attach_window(g_hwnd);
        font_release();
        g_font_failed = false;
        hook_device(g_dev);
    }
    return hr;
}

static IDirect3D9* WINAPI h_Direct3DCreate9(UINT sdk)
{
    IDirect3D9* d3d = o_Direct3DCreate9(sdk);
    if (d3d && !o_CreateDevice)
        hook_create(&g_kit_mod, vt(d3d)[16], (void*)h_CreateDevice, (void**)&o_CreateDevice);
    return d3d;
}

bool d3d9_install()
{
    HMODULE m = GetModuleHandleW(L"d3d9.dll");
    if (!m)
        m = LoadLibraryW(L"d3d9.dll");
    void* p = m ? (void*)GetProcAddress(m, "Direct3DCreate9") : nullptr;
    if (!p) {
        SKLOG(SK_ERROR, "d3d9: Direct3DCreate9 not found");
        return false;
    }
    return hook_create(&g_kit_mod, p, (void*)h_Direct3DCreate9, (void**)&o_Direct3DCreate9) == 0;
}

void* d3d_device() { return g_dev; }
bool in_frame() { return g_in_frame || g_present_depth > 0; }
void* game_window() { return g_hwnd; }
uint64_t frame_index() { return g_frame; }
double fps() { return g_fps; }

} // namespace sk
