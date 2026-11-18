#include "progress_debt.h"

#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "cellsync.h"
#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "sui.h"
#include "sporekit_game.hpp"

namespace progress_debt {
namespace {
// `coop.fix core.progress_debt off`: the old loss (Creature: 10% of the DNA taken away; Cell: none)
bool g_fix = true;
COOP_FIX("core.progress_debt", "BUG-9119", g_fix,
         "creature / cell: a downed player who was not revived leaves a progress debt (gains repay it first) instead of losing DNA");

const SkHost* H;
net::Session* S;
Ledger g_ledger;           // host: the party's debt
Ledger g_heard;            // guest: the host's last word
double g_last_sent = -1e9;
float g_sent = -1;
uint32_t g_mode = 0, g_world = 0;
void (*g_notice)(const std::string&, int);
bool g_repaid = false; // host: the last gain cleared the debt (a notice on the next tick)
float g_tribe_goal = -1; // host, Tribe: the progress before the first unrepaid wipe (the debt: this minus the progress)

// the bar: the HUD's progress strip (control 0x051B5478, its fill window 0x0652A628) in screen
// pixels, found outside the frame (UI reads); the fill inside it is the stage's progress 0..1
// (Creature: cPlayer +0x10F0 / +0x10F4, Cell: food S+0x1C / 1000), docs/re/creature.md#creature-progress-bar
// The Creature bar is not one straight line: each brain level has a stretch of the same width and
// its own DNA, so the fill's end is read from the game's own marker (0x051B5168, a zero-width window
// at the fill's end, the green arrow) and the debt is drawn back from it at the bar's average rate.
constexpr uint32_t kBarId = 0x051B5478, kFillId = 0x0652A628, kMarkerId = 0x051B5168;
// Tribe (runtime 2026-11-12, 1280x960): the same bar 0x051B5478 holds three sections 0x051B54C0..C2
// (233-422, 423-804, 808-1179 of 231-1202) for its 5 wins (1, 2 and 2 steps: the widths' share); the
// fill has no window there (0x0652A628 and the marker are missing), it fills the sections' steps in turn
constexpr uint32_t kTribeSections[3] = {0x051B54C0, 0x051B54C1, 0x051B54C2};
struct Bar {
    bool shown = false;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float edge = -1; // the marker's x (-1: not found)
    float sx1[3] = {}, sx2[3] = {}; // Tribe: the sections (x2 <= x1: missing)
};
Bar g_bar;
double g_bar_asked = -1e9;
bool g_bar_busy = false;
uint32_t g_draws = 0;
// 2026-11-12 (owner: "A connected player doesn't see the Debt in the progress bar"; not reproduced on two PCs): why a
// debt was not drawn, logged once per debt (the first 5 s without a stretch), so a player's log names the cause
const char* g_skip = "";
double g_owing_since = -1;
uint32_t g_draws_at_owing = 0;
bool g_skip_logged = false;

bool here(uint32_t mode) { return mode == modes::kCreature || mode == modes::kCell; }
bool drawn(uint32_t mode) { return here(mode) || mode == modes::kTribe; } // where the debt shows on the bar
bool is_host() { return S && S->connected() && S->role() == net::Role::Host; }
bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }

const Ledger& current() { return is_host() ? g_ledger : g_heard; }

void send(double now)
{
    net::Writer w;
    w.u32(g_ledger.mode);
    w.u32(g_ledger.world);
    w.f32(g_ledger.owed);
    S->broadcast_reliable(net::MSG_PROGRESS_DEBT, w);
    g_sent = g_ledger.owed;
    g_last_sent = now;
}

// the stage's progress now: done / total (false outside Creature / Cell)
bool progress(uint32_t mode, float& done, float& total)
{
    if (mode == modes::kCreature) {
        done = game::evolution_bar();
        total = game::goal_total();
        return std::isfinite(done) && std::isfinite(total) && total > 0;
    }
    if (mode == modes::kCell) {
        int food = 0;
        if (!cellsync::food_total(food))
            return false;
        done = (float)food;
        total = 1000.f;
        return true;
    }
    if (mode == modes::kTribe && game::mode_id() == modes::kTribe) { // the wins: cPlayer +0x10F0 / +0x10F4
        void* nm = game::noun_manager();
        void* p = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? *(void**)((uint8_t*)nm + 0x74) : nullptr;
        if (!p || !skg::readable((uint8_t*)p + 0x10F0, 8))
            return false;
        done = *(float*)((uint8_t*)p + 0x10F0);
        total = *(float*)((uint8_t*)p + 0x10F4);
        return std::isfinite(done) && std::isfinite(total) && total > 0 && total < 100;
    }
    return false;
}

// outside the frame: where the bar is now (the HUD can be hidden: editors, cinematics, menus)
void find_bar(void*)
{
    Bar b;
    std::vector<sui::Win> stack{sui::main_window()};
    sui::Win bar = nullptr;
    while (!stack.empty() && !bar) { // only through visible parents (hidden copies are skipped)
        sui::Win w = stack.back();
        stack.pop_back();
        if (!sui::visible(w))
            continue;
        if (sui::control_id(w) == kBarId)
            bar = w;
        else
            for (sui::Win c : sui::children(w))
                stack.push_back(c);
    }
    sui::Win fill = bar && g_mode != modes::kTribe ? sui::find(bar, kFillId) : nullptr;
    if (bar && g_mode == modes::kTribe) {
        for (int i = 0; i < 3; ++i)
            if (sui::Win s = sui::find(bar, kTribeSections[i])) {
                const sui::Rect r = sui::drawn_rect(s); // BUG-9238: where it shows
                b.sx1[i] = r.x1;
                b.sx2[i] = r.x2;
                if (i == 0) { b.x1 = r.x1; b.y1 = r.y1; b.y2 = r.y2; }
                b.x2 = std::max(b.x2, r.x2);
            }
        b.shown = b.sx2[0] > b.sx1[0] && b.x2 - b.x1 > 50 && b.y2 - b.y1 > 4;
    }
    if (fill) {
        sui::Rect r = sui::drawn_rect(fill); // BUG-9238
        // the green fill inside the window, measured on screenshots at 1440x1080 (both stages:
        // Creature 237..1354 in 236..1357, Cell 231..1345 in 231..1349)
        b.x1 = r.x1 + (g_mode == modes::kCreature ? 1.f : 0.f);
        b.x2 = r.x2 - (g_mode == modes::kCreature ? 3.f : 4.f);
        b.y1 = std::max(r.y1, r.y1 + (g_mode == modes::kCreature ? 1.f : 0.f));
        b.y2 = r.y2 - (g_mode == modes::kCreature ? 2.f : 0.f);
        b.shown = b.x2 - b.x1 > 50 && b.y2 - b.y1 > 4;
        if (sui::Win marker = sui::find(bar, kMarkerId)) {
            sui::Rect m = sui::drawn_rect(marker);
            if (m.x1 >= b.x1 - 2 && m.x1 <= b.x2 + 4)
                b.edge = std::clamp(m.x1, b.x1, b.x2);
        }
    }
    g_bar = b;
    g_bar_busy = false;
}
} // namespace

void init(const SkHost* host, net::Session* session, void (*notify)(const std::string&, int))
{
    H = host;
    S = session;
    g_notice = notify;
}

void reset()
{
    g_tribe_goal = -1;
    g_ledger = {};
    g_heard = {};
    g_sent = -1;
    g_bar = {};
}

bool enabled() { return g_fix; }

void tick(uint32_t mode, uint32_t world, double now)
{
    // only a stage moves the ledger: an editor, a load screen or a menu between two moments of the
    // same stage and world keeps the debt (another save or another stage clears it)
    if (here(mode) || mode == modes::kTribe || mode == modes::kCiv || mode == modes::kSpace || mode == modes::kScenario) {
        g_mode = mode;
        g_world = world;
    }
    if (!S || !S->connected()) {
        if (g_ledger.owed > 0 || g_heard.owed > 0)
            reset();
        return;
    }
    if (is_host()) {
        if (g_ledger.place(g_mode, g_world)) {
            g_tribe_goal = -1;
            if (g_sent > 0)
                send(now); // the old debt is gone
        }
        // Tribe: won back step by step (only lowered, and only read in the stage itself: values passing
        // during a reload never raise it)
        float done = 0, total = 0;
        if (g_fix && mode == modes::kTribe && g_ledger.mode == modes::kTribe && g_tribe_goal > 0 && g_ledger.owed > 0 &&
            progress(modes::kTribe, done, total)) {
            const float owe = std::max(0.f, std::min(g_tribe_goal, total) - done);
            if (owe < g_ledger.owed) {
                g_ledger.owed = owe < 0.001f ? 0.f : owe;
                if (g_ledger.owed <= 0) {
                    g_tribe_goal = -1;
                    if (g_notice) g_notice("The lost step is won back.", -1);
                }
            }
        }
        if (g_ledger.owed != g_sent && now - g_last_sent > 0.25)
            send(now);
        else if (g_ledger.owed > 0 && now - g_last_sent > 2)
            send(now); // a late joiner sees it too
        if (g_repaid) {
            g_repaid = false;
            if (g_notice && g_ledger.owed <= 0)
                g_notice("Progress debt refilled: progress counts again.", -1);
        }
    }
    if (current().owed > 0 && drawn(g_mode)) {
        if (g_owing_since < 0) {
            g_owing_since = now;
            g_draws_at_owing = g_draws;
            g_skip_logged = false;
        } else if (!g_skip_logged && now - g_owing_since > 5 && g_draws == g_draws_at_owing) {
            g_skip_logged = true;
            float done = 0, total = 0;
            const bool p = progress(g_mode, done, total);
            H->log(nullptr, SK_WARN, "coop: progress debt %.0f (%s) not drawn for 5 s: %s (bar found %d [%.0f %.0f %.0f %.0f] "
                   "edge %.0f, progress %s%.1f/%.1f, stage %08X)", current().owed, is_host() ? "host" : "guest",
                   *g_skip ? g_skip : "no draw call", (int)g_bar.shown, g_bar.x1, g_bar.y1, g_bar.x2, g_bar.y2, g_bar.edge,
                   p ? "" : "(none) ", done, total, g_mode);
        }
    } else
        g_owing_since = -1;
    if (g_fix && drawn(g_mode) && current().owed > 0 && !g_bar_busy && now - g_bar_asked > 0.25) {
        g_bar_busy = true;
        g_bar_asked = now;
        H->run_outside_frame(find_bar, nullptr);
    } else if (current().owed <= 0)
        g_bar.shown = false;
}

float add(float amount)
{
    if (!g_fix || !is_host() || !here(g_mode) || !(amount > 0))
        return 0;
    g_ledger.place(g_mode, g_world);
    float done = 0, total = 0;
    if (progress(g_mode, done, total)) // never more than the bar holds: the red stays on the bar
        amount = std::min(amount, std::max(0.f, done - g_ledger.owed));
    amount = std::floor(amount + 0.5f);
    if (amount <= 0)
        return 0;
    g_ledger.add(amount);
    return amount;
}

float add_death()
{
    float done = 0, total = 0;
    return progress(g_mode, done, total) ? add(progress_cost(done)) : 0.f;
}

float absorb(float gain)
{
    if (!g_fix || !is_host() || gain <= 0 || g_ledger.owed <= 0 || g_ledger.mode != g_mode || !here(g_mode))
        return gain;
    float left = g_ledger.absorb(gain);
    if (g_ledger.owed <= 0)
        g_repaid = true;
    return left;
}

int absorb_food(int gain)
{
    float left = absorb((float)gain);
    return (int)std::lround(left);
}

void step_lost(float before)
{
    if (!g_fix || !is_host() || g_mode != modes::kTribe || !(before > 0))
        return;
    g_ledger.place(g_mode, g_world);
    float done = 0, total = 0;
    if (!progress(modes::kTribe, done, total))
        done = before - 1;
    g_tribe_goal = std::max(g_tribe_goal, std::min(before, total > 0 ? total : before));
    g_ledger.owed = std::max(0.f, g_tribe_goal - std::max(0.f, done));
}

bool owing(uint32_t mode)
{
    const Ledger& l = current();
    return g_fix && l.owed > 0 && l.mode == mode && mode == g_mode;
}

float owed()
{
    const Ledger& l = current();
    return g_fix && l.mode == g_mode && drawn(g_mode) ? l.owed : 0.f;
}

void message(uint8_t from, net::Reader& r)
{
    uint32_t mode = r.u32(), world = r.u32();
    float debt = r.f32();
    if (!r.ok() || from != 0 || !is_guest() || !std::isfinite(debt) || debt < 0 || debt > 100000)
        return;
    g_heard.mode = mode;
    g_heard.world = world;
    g_heard.owed = debt;
}

// The tint shader (ps_2_0), compiled once with the system's d3dcompiler_47 (Windows 10+, Wine).
// Brightness from the green fill (green dominant, a little of the highlights' white), then a deep,
// slightly desaturated red: the bar's bright green 0.75 -> about (139, 44, 48), a tick mark darker,
// the shine lighter.
const char kTintHlsl[] =
    "sampler s0 : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float4 c = tex2D(s0, uv);\n"
    "  float l = c.g * 1.1 + c.r * 0.3;\n"
    "  float green = saturate((c.g - max(c.r, c.b)) * 4);\n" // only the fill: a grey clip or the white arrow stay
    "  return float4(lerp(c.rgb, float3(l * 0.66, l * 0.21, l * 0.23), green), 1);\n"
    "}\n";
// Tribe: the lost step's empty slot (dark blue) in the Creature debt's red (navy (27, 34, 85) -> about
// (146, 40, 43)); the slot's icons and the section borders keep their light and dark
const char kTintBlueHlsl[] =
    "sampler s0 : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float4 c = tex2D(s0, uv);\n"
    "  float l = c.b * 1.3 + c.g * 0.4;\n"
    "  float blue = saturate((c.b - max(c.r, c.g)) * 10);\n"
    "  return float4(lerp(c.rgb, float3(l * 1.2, l * 0.33, l * 0.35), blue), 1);\n"
    "}\n";
namespace {
IDirect3DPixelShader9* g_ps = nullptr;      // the green fill's tint
IDirect3DPixelShader9* g_ps_blue = nullptr; // Tribe: the empty slot's tint
IDirect3DDevice9* g_ps_device = nullptr;
bool g_ps_failed = false;
uint32_t g_tinted = 0;
IDirect3DPixelShader9* compile_ps(IDirect3DDevice9* device, const char* src, size_t len)
{
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    if (!dc)
        dc = LoadLibraryA("d3dcompiler_43.dll");
    using Compile_t = HRESULT(WINAPI*)(const void*, SIZE_T, const char*, const void*, void*, const char*, const char*,
                                      UINT, UINT, IUnknown**, IUnknown**);
    auto compile = dc ? (Compile_t)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
    if (!compile)
        return nullptr;
    IUnknown *code = nullptr, *errors = nullptr;
    HRESULT hr = compile(src, len, "debt_tint", nullptr, nullptr, "main", "ps_2_0", 0, 0, &code, &errors);
    if (errors)
        errors->Release();
    if (FAILED(hr) || !code)
        return nullptr;
    // ID3DBlob: IUnknown, then GetBufferPointer (slot 3), GetBufferSize (slot 4)
    void** vt = *(void***)code;
    auto ptr = ((void*(WINAPI*)(IUnknown*))vt[3])(code);
    IDirect3DPixelShader9* ps = nullptr;
    if (!ptr || FAILED(device->CreatePixelShader((const DWORD*)ptr, &ps)))
        ps = nullptr;
    code->Release();
    return ps;
}
IDirect3DPixelShader9* tint_shader(IDirect3DDevice9* device, bool blue)
{
    if (g_ps_device == device && g_ps)
        return blue ? g_ps_blue : g_ps;
    if (g_ps_failed)
        return nullptr;
    g_ps_failed = true; // one try (a missing compiler: the flat strip)
    g_ps = compile_ps(device, kTintHlsl, sizeof kTintHlsl - 1);
    g_ps_blue = g_ps ? compile_ps(device, kTintBlueHlsl, sizeof kTintBlueHlsl - 1) : nullptr;
    if (g_ps) {
        g_ps_device = device;
        g_ps_failed = false;
    }
    return blue ? g_ps_blue : g_ps;
}

// The bar's own pixels in [x1, x2) x [y1, y2), recoloured: the stretch is copied from the back buffer and
// drawn again with a small pixel shader that turns its green (or Tribe's empty blue) into a deep, slightly
// desaturated red with the same light and dark (the tick marks and the shine stay). Without a shader: a
// flat red strip.
void tint(IDirect3DDevice9* device, bool blue, float x1f, float y1f, float x2f, float y2f)
{
    const int ix1 = (int)std::floor(x1f), ix2 = (int)std::ceil(x2f);
    const int iy1 = (int)std::floor(y1f), iy2 = (int)std::ceil(y2f);
    if (ix2 - ix1 < 1 || iy2 - iy1 < 1)
        return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)))
        return;
    IDirect3DTexture9* copy = nullptr;
    IDirect3DPixelShader9* ps = tint_shader(device, blue);
    if (ps) {
        IDirect3DSurface9* back = nullptr;
        D3DSURFACE_DESC bd{};
        if (SUCCEEDED(device->GetRenderTarget(0, &back)) && SUCCEEDED(back->GetDesc(&bd)) && ix2 <= (int)bd.Width &&
            iy2 <= (int)bd.Height && ix1 >= 0 && iy1 >= 0 &&
            SUCCEEDED(device->CreateTexture(ix2 - ix1, iy2 - iy1, 1, D3DUSAGE_RENDERTARGET, bd.Format, D3DPOOL_DEFAULT, &copy, nullptr))) {
            IDirect3DSurface9* dst = nullptr;
            RECT src{ix1, iy1, ix2, iy2};
            bool ok = SUCCEEDED(copy->GetSurfaceLevel(0, &dst)) && SUCCEEDED(device->StretchRect(back, &src, dst, nullptr, D3DTEXF_POINT));
            if (dst) dst->Release();
            if (!ok) { copy->Release(); copy = nullptr; }
        }
        if (back) back->Release();
    }
    device->SetVertexShader(nullptr);
    device->SetPixelShader(copy ? ps : nullptr);
    device->SetTexture(0, copy);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    if (copy) {
        device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        struct TV { float x, y, z, rhw, u, v; };
        const float x1 = ix1 - 0.5f, x2 = ix2 - 0.5f, y1 = iy1 - 0.5f, y2 = iy2 - 0.5f; // texel centres on pixel centres
        TV q[] = {{x1, y1, 0, 1, 0, 0}, {x2, y1, 0, 1, 1, 0}, {x1, y2, 0, 1, 0, 1}, {x2, y2, 0, 1, 1, 1}};
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(TV));
        g_tinted++;
    } else {
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        struct Vertex { float x, y, z, rhw; DWORD color; };
        const DWORD c = 0xFF8A2E33;
        Vertex q[] = {{x1f, y1f, 0, 1, c}, {x2f, y1f, 0, 1, c}, {x1f, y2f, 0, 1, c}, {x2f, y2f, 0, 1, c}};
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vertex));
    }
    saved->Apply();
    saved->Release();
    if (copy) copy->Release(); // made per frame: nothing of ours in D3DPOOL_DEFAULT when the device resets
    g_draws++;
}

} // namespace

void draw()
{
    if (!g_fix || !drawn(g_mode))
        return;
    if (!g_bar.shown) {
        g_skip = "the progress bar is not on screen (not found or hidden)";
        return;
    }
    const float debt = owed();
    float done = 0, total = 0;
    if (debt <= 0)
        return;
    if (!progress(g_mode, done, total)) {
        g_skip = "no progress value for this stage";
        return;
    }
    auto* device = static_cast<IDirect3DDevice9*>(H->d3d_device());
    if (!device) {
        g_skip = "no device";
        return;
    }
    if (g_mode == modes::kTribe) { // the lost steps after the fill, each its own slot
        const int from = (int)std::floor(done + 0.001f), to = std::min((int)std::lround(total), from + (int)std::lround(debt));
        for (int k = from; k < to; ++k) {
            float x1 = 0, x2 = 0;
            if (step_slot(g_bar.sx1, g_bar.sx2, 3, k, total, x1, x2))
                tint(device, true, x1, g_bar.y1, x2, g_bar.y2);
        }
        return;
    }
    const float w = g_bar.x2 - g_bar.x1;
    const float end = g_bar.edge >= 0 ? g_bar.edge : g_bar.x1 + w * std::clamp(done / total, 0.f, 1.f);
    if (end - g_bar.x1 < 2.f || done <= 0) {
        g_skip = "the bar's fill is empty here";
        return;
    }
    const float rate = (end - g_bar.x1) / done; // pixels per point, the bar's average up to here
    // never thinner than 4 px (a few DNA on a 1000-point bar), never past the bar's start
    const float start = std::max(g_bar.x1, end - std::max(4.f, debt * rate));
    tint(device, false, start, g_bar.y1, end, g_bar.y2);
}

std::string debug()
{
    char b[320];
    const Ledger& l = current();
    float done = 0, total = 0;
    bool p = progress(g_mode, done, total);
    std::snprintf(b, sizeof b,
                  "progress debt: %s owed=%.1f (mode %08X world %08X; stage now %08X world %08X) progress=%s%.1f/%.1f "
                  "bar=%d [%.0f %.0f %.0f %.0f] edge=%.0f draws=%u tinted=%u shader=%d fix=%d\n",
                  is_host() ? "host" : is_guest() ? "guest" : "offline", l.owed, l.mode, l.world, g_mode, g_world,
                  p ? "" : "(none) ", done, total, g_bar.shown ? 1 : 0, g_bar.x1, g_bar.y1, g_bar.x2, g_bar.y2, g_bar.edge, g_draws, g_tinted, g_ps ? 1 : 0,
                  g_fix ? 1 : 0);
    return b;
}

int command(int argc, const char* const* argv, std::string& out)
{
    if (argc > 2 && !std::strcmp(argv[1], "add")) { // tests: a debt as a timeout makes it
        float got = add((float)std::atof(argv[2]));
        out = "added " + std::to_string((int)got) + "\n" + debug();
        return got > 0 ? 0 : 1;
    }
    if (argc > 1 && !std::strcmp(argv[1], "clear")) {
        if (is_host())
            g_ledger.owed = 0;
        out = debug();
        return 0;
    }
    out = debug();
    return 0;
}

} // namespace progress_debt
