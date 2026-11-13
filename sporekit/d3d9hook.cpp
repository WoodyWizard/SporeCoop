// D3D9 interception: Direct3DCreate9 -> CreateDevice -> Present/Reset.
// Provides the per-frame tick (main thread), screenshots and the text overlay.
#include "core.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace sk {

typedef IDirect3D9*(WINAPI* Direct3DCreate9_t)(UINT);
typedef HRESULT(WINAPI* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                        D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(WINAPI* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
                                   const RGNDATA*);
typedef HRESULT(WINAPI* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT(WINAPI* SetTransform_t)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
typedef HRESULT(WINAPI* SwapPresent_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND,
                                       const RGNDATA*, DWORD);

static Direct3DCreate9_t o_Direct3DCreate9;
static CreateDevice_t o_CreateDevice;
static Present_t o_Present;
static Reset_t o_Reset;
static SwapPresent_t o_SwapPresent;
static SetTransform_t o_SetTransform;

static IDirect3DDevice9* g_dev;
static HWND g_hwnd;
static uint64_t g_frame;
static double g_fps, g_last_t;
static bool g_in_frame;
static bool g_view_copied;    // BUG-0948: the viewer was copied for this frame (its first SetTransform(VIEW))
static uint32_t g_view_sets;  // the game's SetTransform(VIEW) calls (kit.cam)

// ---- BUG-0779: what the player's device and its hooks are, and how far apart the frames are shown ----
// the module a code address lives in ("d3d9.dll (game folder)", "d3d9.dll (system)", ...)
static std::string module_text(const void* p)
{
    HMODULE m = nullptr;
    if (!p || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR)p, &m))
        return "no module";
    std::string full = narrow(module_path(m)); // BUG-1033
    std::string name = full.substr(full.find_last_of("\\/") + 1);
    std::string low = full, game = narrow(game_dir());
    for (auto& c : low)
        c = (char)tolower((unsigned char)c);
    for (auto& c : game)
        c = (char)tolower((unsigned char)c);
    bool in_game = !game.empty() && low.rfind(game, 0) == 0;
    bool sys = low.find("\\windows\\") != std::string::npos;
    return name + (in_game ? " (game folder)" : sys ? " (system)" : " (elsewhere)");
}

static bool readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

// where a function's first instruction jumps when it starts with one (another program's hook placed before ours)
static std::string jump_text(const void* p)
{
    if (!p || !readable(p, 8))
        return "";
    const uint8_t* b = (const uint8_t*)p;
    const uint8_t* to = nullptr;
    if (b[0] == 0xE9)
        to = b + 5 + *(const int32_t*)(b + 1);
    else if (b[0] == 0xFF && b[1] == 0x25 && readable(*(void* const*)(b + 2), 4))
        to = **(const uint8_t* const* const*)(b + 2);
    else if (b[0] == 0x68 && b[5] == 0xC3) // push imm32; ret
        to = *(const uint8_t* const*)(b + 1);
    if (!to)
        return "";
    return ", starts with a jump into " + module_text(to);
}

void d3d9_note_function(const char* what, const void* p)
{
    SKLOG(SK_INFO, "d3d9: %s at %p in %s%s", what, p, module_text(p).c_str(), jump_text(p).c_str());
}

static void log_pp(const char* what, const D3DPRESENT_PARAMETERS* pp)
{
    if (!pp)
        return;
    unsigned iv = pp->PresentationInterval;
    const char* ivs = iv == D3DPRESENT_INTERVAL_IMMEDIATE ? "immediate: no vsync"
                      : iv == D3DPRESENT_INTERVAL_DEFAULT ? "default: vsync"
                      : iv == D3DPRESENT_INTERVAL_ONE     ? "one: vsync"
                                                          : "other";
    SKLOG(SK_INFO, "%s: %ux%u windowed=%d interval=0x%X (%s) refresh=%u Hz swap effect=%u back buffers=%u", what,
          pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, iv, ivs, pp->FullScreen_RefreshRateInHz,
          (unsigned)pp->SwapEffect, pp->BackBufferCount);
}

// The game asks for PresentationInterval IMMEDIATE (no vsync) in windowed and fullscreen mode (seen at CreateDevice and
// every Reset, GOG 2024): the picture tears whenever a frame is shown while the screen is drawn, the more the faster
// the game runs (BUG-0779). [sporekit] vsync = 1 asks the driver for vsync instead (interval one), 0 for none, -1 (the
// default) leaves the game's choice.
static void vsync_apply(D3DPRESENT_PARAMETERS* pp)
{
    static int want = -2;
    if (want == -2)
        want = ini_int(mods_dirw() + L"\\sporekit.ini", "sporekit", "vsync", -1);
    if (!pp || want < 0)
        return;
    UINT iv = want ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    if (pp->PresentationInterval != iv) {
        SKLOG(SK_INFO, "vsync: [sporekit] vsync=%d: presentation interval 0x%X instead of the game's 0x%X", want, iv,
              pp->PresentationInterval);
        pp->PresentationInterval = iv;
    }
}

// Present intervals: one set for the minute line of the log, one for the console (`frame.interval`)
struct Intervals {
    std::vector<float> ms;
    void add(float v)
    {
        if (ms.size() < 200000)
            ms.push_back(v);
    }
    std::string text() const
    {
        if (ms.empty())
            return "no Present intervals yet";
        std::vector<float> s = ms;
        std::sort(s.begin(), s.end());
        double sum = 0;
        for (float v : s)
            sum += v;
        char b[200];
        snprintf(b, sizeof b, "present interval mean %.1f ms p50 %.1f p95 %.1f max %.1f over %u frames (%.1f fps)",
                 sum / s.size(), s[s.size() / 2], s[std::min(s.size() - 1, s.size() * 95 / 100)], s.back(),
                 (unsigned)s.size(), sum > 0 ? 1000.0 * s.size() / sum : 0.0);
        return b;
    }
};
static Intervals g_iv_min, g_iv_con;
static double g_iv_last_t, g_iv_min_t;
static std::mutex g_iv_mu;

static void interval_tick()
{
    double t = now_seconds();
    std::lock_guard<std::mutex> lk(g_iv_mu);
    if (g_iv_last_t > 0) {
        float d = (float)((t - g_iv_last_t) * 1000.0);
        g_iv_min.add(d);
        g_iv_con.add(d);
    }
    g_iv_last_t = t;
    if (g_iv_min_t <= 0)
        g_iv_min_t = t;
    if (t - g_iv_min_t >= 60) {
        SKLOG(SK_INFO, "frames: %s this minute; frame limit %d ms (the game's own %d ms)", g_iv_min.text().c_str(),
              frame_limit_now(), frame_limit_own());
        g_iv_min.ms.clear();
        g_iv_min_t = t;
    }
}

std::string present_intervals(bool reset)
{
    std::lock_guard<std::mutex> lk(g_iv_mu);
    std::string s = g_iv_con.text();
    if (reset)
        g_iv_con.ms.clear();
    return s;
}

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

// ---- overlay shapes (since 0.3.15): filled triangles in screen pixels, drawn in the overlay pass ----
// The game's render states are saved in a state block (made once, released before a Reset) and put back
// after each call, so the game's next frame and the D3DX font see the states they had.
static int g_bb_w, g_bb_h;
static IDirect3DStateBlock9* g_tri_sb;
static bool g_tri_failed;
static bool g_tri_fail_test;  // tests (kit.test.trifail): the next CreateStateBlock is taken as failed (BUG-1026)
static uint32_t g_tri_drawn;  // overlay_tris calls that drew (tests)
static int g_tri_test_frames; // tests (kit.test.tri): SporeKit draws a test triangle for this many frames

static void tri_release()
{
    if (g_tri_sb) {
        g_tri_sb->Release();
        g_tri_sb = nullptr;
    }
}

void overlay_tris(const float* xy, int n, uint32_t argb)
{
    if (!g_overlay_active || !g_dev || !xy || n <= 0 || g_tri_failed)
        return;
    if (n > 256)
        n = 256;
    bool fail_test = !g_tri_sb && g_tri_fail_test;
    if (fail_test)
        g_tri_fail_test = false;
    if (!g_tri_sb && (fail_test || FAILED(g_dev->CreateStateBlock(D3DSBT_ALL, &g_tri_sb)))) {
        if (g_tri_sb)
            g_tri_sb->Release();
        g_tri_sb = nullptr;
        g_tri_failed = true;
        SKLOG(SK_WARN, "overlay: CreateStateBlock failed, no overlay shapes");
        return;
    }
    struct V {
        float x, y, z, rhw;
        DWORD c;
    };
    V v[256 * 3];
    for (int i = 0; i < n * 3; i++) {
        float x = xy[i * 2], y = xy[i * 2 + 1];
        if (!std::isfinite(x) || !std::isfinite(y))
            return;
        v[i] = {x - 0.5f, y - 0.5f, 0.f, 1.f, (DWORD)argb};
    }
    g_tri_sb->Capture();
    D3DVIEWPORT9 vp = {0, 0, (DWORD)(g_bb_w > 0 ? g_bb_w : 1), (DWORD)(g_bb_h > 0 ? g_bb_h : 1), 0.f, 1.f};
    g_dev->SetViewport(&vp);
    g_dev->SetVertexShader(nullptr);
    g_dev->SetPixelShader(nullptr);
    g_dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_dev->SetTexture(0, nullptr);
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    g_dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    g_dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)n, v, sizeof(V));
    g_tri_sb->Apply();
    g_tri_drawn++;
}

int world_to_screen_game(const float* w, float* out);
// BUG-0948: inside the overlay pass the labels are projected with the camera the frame was drawn with
int world_to_screen(const float* w, float* out)
{
    if (g_overlay_active && fixed(g_fix_drawncam))
        return world_to_screen_drawn(w, out);
    return world_to_screen_game(w, out);
}

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
// BUG-0653: one long frame (a 2.5 s hitch: the game saved box, a save, kit.test.hang) is not Present lost: the
// next game loop pass came 2.5 s after the frame's Present and took over for one frame (a WARN, labels off,
// then "Present reaches SporeKit again" 1 ms later). Present lost also needs kPresentLostPasses game loop
// passes without a Present between them (an overlay that took Present over: 30-60 passes a second).
// `kit.fix presentlost 0`: the time alone, as before (0.3.11).
static constexpr int kPresentLostPasses = 10;
static int g_loop_passes; // game loop passes since the last Present
static bool g_loop_frames;       // frames come from the game loop now
static bool g_in_loop_frame;
static int g_frames_mode = -1;   // 0 auto, 1 loop: ignore Present (tests of the fallback), -1 unread
// BUG-0667: a fault inside the frame's work (a mod's SK_EV_FRAME handler calling the game) that the game's own
// handler catches unwinds past this file's frames: g_in_frame / g_in_loop_frame stayed set, every later Present and
// game loop pass took itself for a nested call, and no frame ran again (no mod tick, the frame counter stopped: the
// game "froze" for the lab and its partner while it drew on). The frame's stack address is kept; an entry on the same
// thread at that depth or above it (not nested inside the frame) means the frame was cut off: the flags are cleared
// and the frames go on. `kit.fix frameunwind 0`: the flags stay (0.3.11).
static int g_present_depth; // device Present may call the swapchain Present internally
static uintptr_t g_frame_sp;     // stack address of the frame being run
static DWORD g_frame_tid;
static uint32_t g_frames_cut;
// BUG-1024: a fault inside the original Present (the driver, or a window message dispatched there that runs a task)
// that the game's handler catches unwinds past h_Present's `g_present_depth--`: the depth stayed 1, no frame started
// again (h_Present and the game loop fallback both need depth 0), in_frame() stayed true and the outside-frame tasks
// waited for ever. The game drew on, the mod was dead. The outermost Present's stack address and thread are kept; an
// entry on that thread at or above it (not nested inside it: Present -> swapchain Present is deeper) or the game loop
// pass after App::Canvas::Tick means that Present was left by an unwind: the depth goes back to 0.
// A fault in an SK_EV_OVERLAY handler skipped EndScene, the render target's restore and the Release of the back
// buffer and the old target (one reference of each leaked per fault: a later device Reset fails while the back buffer
// is held). Those are kept here and released when the frame is found cut off. `kit.fix presentunwind 0`: as before.
static uintptr_t g_present_sp;
static DWORD g_present_tid;
static uint32_t g_presents_cut;
static IDirect3DSurface9 *g_ov_bb, *g_ov_rt; // the overlay pass's back buffer and the game's render target
static bool g_ov_scene;                      // the overlay pass's BeginScene is open
static int g_present_fault;                  // tests (kit.test.presentfault): 1 inside Present, 2 in the overlay pass
static uint32_t g_overlay_cuts;

static void overlay_cut_off()
{
    if (!g_ov_bb && !g_ov_rt && !g_ov_scene)
        return;
    g_overlay_cuts++;
    SKLOG(SK_WARN, "overlay: the overlay pass was cut off by a fault (%u so far): its surfaces are released (BUG-1024)",
          g_overlay_cuts);
    // the scene is still open (the game's own BeginScene inside it fails and the game then skips its EndScene: the
    // overlay pass never began a scene again in the lab) and the render target is still the back buffer: as at the
    // pass's end. This runs at the start of the next frame, on the device's thread, before the original Present.
    if (g_ov_scene && g_dev)
        g_dev->EndScene();
    if (g_dev && (g_ov_rt || g_ov_bb))
        g_dev->SetRenderTarget(0, g_ov_rt ? g_ov_rt : g_ov_bb);
    if (g_ov_rt)
        g_ov_rt->Release();
    if (g_ov_bb)
        g_ov_bb->Release();
    g_ov_rt = g_ov_bb = nullptr;
    g_ov_scene = false;
}

static void present_cut_off(uintptr_t sp)
{
    if (!g_present_depth || !fixed(g_fix_presentunwind) || GetCurrentThreadId() != g_present_tid || sp < g_present_sp)
        return;
    g_present_depth = 0;
    g_presents_cut++;
    SKLOG(SK_WARN, "frames: Present was left by a fault the game's handler caught (%u so far); frames go on (BUG-1024)",
          g_presents_cut);
    if (!g_in_frame && !g_in_loop_frame)
        outside_tasks_resume();
}

static void present_enter(uintptr_t sp)
{
    if (g_present_depth++ == 0) {
        g_present_sp = sp;
        g_present_tid = GetCurrentThreadId();
    }
}

static void present_fault_test(int where)
{
    if (g_present_fault != where)
        return;
    g_present_fault = 0;
    SKLOG(SK_WARN, "tests: a fault %s now (kit.test.presentfault)", where == 1 ? "inside Present" : "in the overlay pass");
    test_fault_null();
}

static bool frame_cut_off(uintptr_t sp)
{
    if (!fixed(g_fix_frameunwind) || !(g_in_frame || g_in_loop_frame) || GetCurrentThreadId() != g_frame_tid ||
        sp < g_frame_sp)
        return false;
    g_in_frame = g_in_loop_frame = false;
    g_overlay_active = false;
    if (fixed(g_fix_presentunwind))
        overlay_cut_off(); // BUG-1024
    g_frames_cut++;
    SKLOG(SK_WARN, "frames: the frame %llu was cut off by a fault the game's handler caught (%u so far); frames go on "
                   "(BUG-0667)", (unsigned long long)g_frame, g_frames_cut);
    if (!g_present_depth)
        outside_tasks_resume(); // BUG-0672
    return true;
}

static void frame_enter(bool loop, uintptr_t sp)
{
    g_frame_sp = sp;
    g_frame_tid = GetCurrentThreadId();
    (loop ? g_in_loop_frame : g_in_frame) = true;
}

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
    if (g_frame % 10 == 2)
        game_mode_cache_tick(); // BUG-1022

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
    uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
    if ((g_in_frame || g_in_loop_frame) && !frame_cut_off(sp))
        return;
    if (frames_mode_read() == 1)
        return; // frames=loop: as if another overlay had taken Present
    g_last_present_t = now_seconds();
    g_loop_passes = 0;
    if (g_loop_frames) {
        g_loop_frames = false;
        SKLOG(SK_INFO, "frames: Present reaches SporeKit again; frames come from Present");
    }
    frame_enter(false, sp);
    interval_tick();
    frame_common();

    // overlay pass
    font_ensure();
    if (g_font) {
        IDirect3DSurface9 *bb = nullptr, *old_rt = nullptr;
        if (SUCCEEDED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
            g_dev->GetRenderTarget(0, &old_rt);
            bool keep = fixed(g_fix_presentunwind);
            if (keep) { // BUG-1024: released by overlay_cut_off when a fault cuts this pass off
                g_ov_bb = bb;
                g_ov_rt = old_rt;
            }
            g_dev->SetRenderTarget(0, bb);
            if (SUCCEEDED(g_dev->BeginScene())) {
                g_ov_scene = keep;
                drawn_view_measure();
                g_overlay_active = true;
                present_fault_test(2);
                fire(SK_EV_OVERLAY, g_dev);
                if (g_tri_test_frames > 0) { // tests (kit.test.tri, BUG-1026): a magenta triangle at the top left
                    g_tri_test_frames--;
                    const float xy[6] = {0.f, 0.f, 400.f, 0.f, 0.f, 400.f};
                    overlay_tris(xy, 1, 0xFFFF00FFu);
                }
                g_overlay_active = false;
                g_dev->EndScene();
                g_ov_scene = false;
            }
            g_dev->SetRenderTarget(0, old_rt ? old_rt : bb);
            g_ov_bb = g_ov_rt = nullptr;
            if (old_rt) old_rt->Release();
            bb->Release();
        }
    }
    capture_if_requested();
    report_shot_on_present(g_dev); // a report screenshot: this frame with the overlay
    g_view_copied = false;
    drawn_view_drop(); // the next frame's draw copies its own
    g_in_frame = false;
}


// called on the main thread after every App::Canvas::Tick (gamehooks.cpp)
void frame_from_game_loop()
{
    uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
    present_cut_off(sp); // BUG-1024: after App::Canvas::Tick no Present of this thread is running
    if (fixed(g_fix_taskcut))
        command_tasks_cut_off(sp); // BUG-0966: no console task of this thread is running here either
    if ((g_in_frame || g_in_loop_frame) && !g_present_depth)
        frame_cut_off(sp);
    if (!g_dev || g_in_frame || g_in_loop_frame || g_present_depth)
        return;
    bool forced = frames_mode_read() == 1;
    if (g_loop_passes < 1000000)
        g_loop_passes++;
    if (!forced && now_seconds() - g_last_present_t < kPresentLostSec)
        return;
    if (!forced && !g_loop_frames && fixed(g_fix_presentlost) && g_loop_passes < kPresentLostPasses)
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
    frame_enter(true, sp);
    frame_common();
    g_in_loop_frame = false;
    outside_tasks_resume(); // BUG-0672
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

// BUG-0948: the game's first SetTransform(VIEW) after a Present belongs to drawing the next frame; the viewer then is
// the camera that frame is drawn with (gamehooks.cpp). One read of the viewer per frame, on the game's thread.
static HRESULT WINAPI h_SetTransform(IDirect3DDevice9* dev, D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m)
{
    if (t == D3DTS_VIEW && dev == g_dev && !g_in_frame && !g_present_depth) {
        g_view_sets++;
        if (!g_view_copied && GetCurrentThreadId() == main_thread_id()) {
            g_view_copied = true;
            drawn_view_take();
        }
    }
    return o_SetTransform(dev, t, m);
}

std::string drawn_cam_command(const char* arg)
{
    bool reset = arg && !strcmp(arg, "reset");
    return drawn_view_text(reset) + " view_sets=" + std::to_string(g_view_sets) +
           (o_SetTransform ? "" : " (SetTransform not hooked)");
}

static HRESULT WINAPI h_Present(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w,
                                const RGNDATA* r)
{
    present_cut_off((uintptr_t)__builtin_frame_address(0)); // BUG-1024
    if (dev == g_dev && g_present_depth == 0)
        on_frame_begin_present();
    present_enter((uintptr_t)__builtin_frame_address(0));
    if (dev == g_dev)
        present_fault_test(1);
    HRESULT hr = o_Present(dev, a, b, w, r);
    g_present_depth--;
    if (!g_present_depth && !g_in_frame && !g_in_loop_frame)
        outside_tasks_resume(); // BUG-0672: tasks a message inside this Present held back
    return hr;
}

static HRESULT WINAPI h_SwapPresent(IDirect3DSwapChain9* sc, const RECT* a, const RECT* b, HWND w,
                                    const RGNDATA* r, DWORD f)
{
    present_cut_off((uintptr_t)__builtin_frame_address(0)); // BUG-1024
    bool ours = false;
    if (g_present_depth == 0) {
        IDirect3DDevice9* dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(&dev))) {
            ours = dev == g_dev;
            if (ours)
                on_frame_begin_present();
            dev->Release();
        }
    }
    present_enter((uintptr_t)__builtin_frame_address(0));
    if (ours)
        present_fault_test(1);
    HRESULT hr = o_SwapPresent(sc, a, b, w, r, f);
    g_present_depth--;
    if (!g_present_depth && !g_in_frame && !g_in_loop_frame)
        outside_tasks_resume(); // BUG-0672
    return hr;
}

static HRESULT WINAPI h_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    log_pp("device Reset asks for", pp);
    vsync_apply(pp);
    fire(SK_EV_DEVICE_LOST, dev);
    tri_release(); // a state block holds no D3DPOOL_DEFAULT resource, but is made anew after a Reset anyway
    if (g_font)
        ((HRESULT(WINAPI*)(void*))vt(g_font)[16])(g_font); // OnLostDevice
    HRESULT hr = o_Reset(dev, pp);
    if (SUCCEEDED(hr) && fixed(g_fix_trireset) && g_tri_failed) { // BUG-1026: the shapes get another try
        g_tri_failed = false;
        SKLOG(SK_INFO, "overlay: the shapes are tried again after the Reset (BUG-1026)");
    }
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
    if (!o_Present) {
        d3d9_note_function("Present", v[17]);
        d3d9_note_function("Reset", v[16]);
    }
    if (!o_Present)
        hook_create(&g_kit_mod, v[17], (void*)h_Present, (void**)&o_Present);
    if (!o_Reset)
        hook_create(&g_kit_mod, v[16], (void*)h_Reset, (void**)&o_Reset);
    if (!o_SetTransform) // BUG-0948
        hook_create(&g_kit_mod, v[44], (void*)h_SetTransform, (void**)&o_SetTransform);
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
    log_pp("CreateDevice asks for", pp);
    vsync_apply(pp);
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
        if (fixed(g_fix_trireset)) { // BUG-1026: the state block belonged to the old device
            tri_release();
            g_tri_failed = false;
        }
        hook_device(g_dev);
    }
    return hr;
}

static IDirect3D9* WINAPI h_Direct3DCreate9(UINT sdk)
{
    IDirect3D9* d3d = o_Direct3DCreate9(sdk);
    if (d3d && !o_CreateDevice) {
        d3d9_note_function("CreateDevice", vt(d3d)[16]);
        hook_create(&g_kit_mod, vt(d3d)[16], (void*)h_CreateDevice, (void**)&o_CreateDevice);
    }
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
    d3d9_note_function("Direct3DCreate9", p);
    return hook_create(&g_kit_mod, p, (void*)h_Direct3DCreate9, (void**)&o_Direct3DCreate9) == 0;
}

// tests (BUG-1024): kit.test.presentfault present|overlay arms one fault; state: the counters and the back buffer's
// reference count (GetBackBuffer adds one, the Release returns the count without it)
std::string present_fault_command(const char* arg)
{
    char b[256];
    if (arg && (!strcmp(arg, "present") || !strcmp(arg, "overlay"))) {
        g_present_fault = !strcmp(arg, "present") ? 1 : 2;
        snprintf(b, sizeof b, "armed: a fault %s in the next frame", g_present_fault == 1 ? "inside Present" : "in the overlay pass");
        return b;
    }
    unsigned long refs = 0;
    IDirect3DSurface9* bb = nullptr;
    if (g_dev && SUCCEEDED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
        refs = bb->Release();
    snprintf(b, sizeof b, "depth=%d presents_cut=%u frames_cut=%u overlay_cuts=%u bb_refs=%lu armed=%d frame=%llu",
             g_present_depth, g_presents_cut, g_frames_cut, g_overlay_cuts, refs, g_present_fault,
             (unsigned long long)g_frame);
    return b;
}

// tests (BUG-1026): kit.test.tri [frames|fail]: a test triangle for some frames, or the next CreateStateBlock fails
std::string tri_test_command(const char* arg)
{
    if (arg && !strcmp(arg, "fail")) {
        tri_release(); // made again at the next shape, and that fails
        g_tri_fail_test = true;
    } else if (arg && *arg)
        g_tri_test_frames = atoi(arg);
    char b[160];
    snprintf(b, sizeof b, "tri: failed=%d block=%d drawn=%u test_frames=%d fail_next=%d", g_tri_failed ? 1 : 0,
             g_tri_sb ? 1 : 0, g_tri_drawn, g_tri_test_frames, g_tri_fail_test ? 1 : 0);
    return b;
}

void* d3d_device() { return g_dev; }
bool in_frame() { return g_in_frame || g_present_depth > 0; }
void* game_window() { return g_hwnd; }
uint64_t frame_index() { return g_frame; }
double fps() { return g_fps; }

} // namespace sk
