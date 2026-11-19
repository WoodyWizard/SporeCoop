#include "ring_timer.h"

#include <d3d9.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include "fixes.h"

namespace ring_timer {
namespace {
bool g_on = true;
COOP_FIX("ui.revive_ring", "BUG-9141", g_on,
         "cell / creature / adventure / tribe: a downed player's countdown is a draining ring (off: the text countdowns)");

struct Vertex {
    float x, y, z, rhw;
    DWORD color;
};

void paint(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, const std::vector<Pt>& points, uint32_t argb)
{
    const UINT per = type == D3DPT_TRIANGLESTRIP ? 1 : 3;
    if (points.size() < 3) return;
    std::vector<Vertex> v;
    v.reserve(points.size());
    for (const Pt& p : points) v.push_back({p.x - 0.5f, p.y - 0.5f, 0, 1, (DWORD)argb});
    const UINT count = type == D3DPT_TRIANGLESTRIP ? (UINT)v.size() - 2 : (UINT)v.size() / per;
    device->DrawPrimitiveUP(type, count, v.data(), sizeof(Vertex));
}
} // namespace

bool enabled() { return g_on; }

static float g_luma = -1;
static double g_luma_at = -100;
float scene_luma(double now) { return now - g_luma_at < 0.5 ? g_luma : -1.f; }

void loading(const SkHost* host, int sw, int sh, float x, float y, float radius, float progress, double now)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    // radius 0: the cover only, no ring (BUG-9227: "Paused by host")
    if (!device || sw <= 0 || sh <= 0 || !std::isfinite(x) || !std::isfinite(y) || (radius != 0 && !(radius > 2))) return;
    // the blur: the frame down to a 1/4 and a 1/16 copy and back up, each step filtered (a render target made per frame,
    // as progress_debt's: nothing of ours in D3DPOOL_DEFAULT when the device resets); a failed step leaves it sharp
    IDirect3DSurface9* back = nullptr;
    D3DSURFACE_DESC bd{};
    if (SUCCEEDED(device->GetRenderTarget(0, &back)) && SUCCEEDED(back->GetDesc(&bd))) {
        // BUG-9261 (owner 2026-11-18: the loading screens "seem to be low resolution"): the frame went to 1/4 and 1/16
        // and back in two steps: at 2560x1440 the 1/16 copy (160x90) came back as blocks. Now it halves step by step to
        // 1/16 and comes back the same way (each step filtered: a smooth blur at any size)
        constexpr int kLevels = 4; // 1/2, 1/4, 1/8, 1/16
        IDirect3DSurface9* lv[kLevels] = {};
        bool ok = true;
        for (int i = 0; i < kLevels && ok; i++) {
            const UINT w = std::max<UINT>(1, bd.Width >> (i + 1)), h = std::max<UINT>(1, bd.Height >> (i + 1));
            ok = SUCCEEDED(device->CreateRenderTarget(w, h, bd.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &lv[i], nullptr)) &&
                 SUCCEEDED(device->StretchRect(i ? lv[i - 1] : back, nullptr, lv[i], nullptr, D3DTEXF_LINEAR));
        }
        if (ok) {
            IDirect3DSurface9* tiny = lv[kLevels - 1];
            D3DSURFACE_DESC td{};
            tiny->GetDesc(&td);
            const UINT tw = td.Width, th = td.Height;
            // BUG-9202: how bright the game's own frame is under the screen (the 1/16 copy read back, 32-bit formats):
            // the game is in the stage mode while it still shows black for up to ~10 s
            IDirect3DSurface9* sys = nullptr;
            if ((bd.Format == D3DFMT_X8R8G8B8 || bd.Format == D3DFMT_A8R8G8B8) &&
                SUCCEEDED(device->CreateOffscreenPlainSurface(tw, th, bd.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
                SUCCEEDED(device->GetRenderTargetData(tiny, sys))) {
                D3DLOCKED_RECT lr{};
                if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                    double sum = 0;
                    for (UINT y = 0; y < th; y++) {
                        const uint8_t* row = (const uint8_t*)lr.pBits + y * lr.Pitch;
                        for (UINT x = 0; x < tw; x++) sum += (row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2]) / 3.0;
                    }
                    sys->UnlockRect();
                    g_luma = (float)(sum / (tw * th));
                    g_luma_at = now;
                }
            }
            if (sys) sys->Release();
            bool up = true;
            for (int i = kLevels - 1; i > 0 && up; i--)
                up = SUCCEEDED(device->StretchRect(lv[i], nullptr, lv[i - 1], nullptr, D3DTEXF_LINEAR));
            if (up)
                device->StretchRect(lv[0], nullptr, back, nullptr, D3DTEXF_LINEAR);
        }
        for (auto* l : lv)
            if (l) l->Release();
    }
    if (back) back->Release();
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE); // BUG-9202: the Tribe night fog tinted the ring dark blue
    device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    const float w = (float)sw, h = (float)sh;
    const DWORD dim = 0x80081018; // the Co-op window's dark blue, half see-through
    Vertex q[4] = {{0, 0, 0, 1, dim}, {w, 0, 0, 1, dim}, {w, h, 0, 1, dim}, {0, h, 0, 1, dim}};
    device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, q, sizeof(Vertex));
    if (radius == 0) {
        saved->Apply();
        saved->Release();
        return;
    }
    const float r_out = radius, r_in = radius * 0.78f, p = std::clamp(progress, 0.f, 1.f);
    paint(device, D3DPT_TRIANGLELIST, pie(x, y, r_out + 3.f, 1.f), kDisc);
    paint(device, D3DPT_TRIANGLESTRIP, arc_strip(x, y, r_in, r_out, 0.f, 1.f), 0xFF20303Cu);
    if (p > 0.001f) paint(device, D3DPT_TRIANGLESTRIP, arc_strip(x, y, r_in, r_out, 0.f, p), 0xFFF0C040u); // the Co-op gold
    const float spin = (float)std::fmod(now * 0.6, 1.0); // one turn in 1.7 s
    paint(device, D3DPT_TRIANGLESTRIP, arc_strip(x, y, r_out + 4.f, r_out + 7.f, spin, 0.12f), 0xC0FFFFFFu);
    saved->Apply();
    saved->Release();
}

// BUG-9156: a red edge round the screen (a downed player spectating): four bands from the edge, opaque to clear
void vignette(const SkHost* host, int sw, int sh, float strength)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || sw <= 0 || sh <= 0 || !(strength > 0)) return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE); // BUG-9202: the Tribe night fog tinted the ring dark blue
    device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    const float w = (float)sw, h = (float)sh, band = std::min(w, h) * 0.18f;
    const DWORD edge = (DWORD)(std::clamp(strength, 0.f, 1.f) * 150.f) << 24 | 0x00B00000, clear = 0x00B00000;
    auto quad = [&](float x0, float y0, DWORD c0, float x1, float y1, DWORD c1, float x2, float y2, DWORD c2, float x3, float y3, DWORD c3) {
        Vertex v[4] = {{x0, y0, 0, 1, c0}, {x1, y1, 0, 1, c1}, {x2, y2, 0, 1, c2}, {x3, y3, 0, 1, c3}};
        device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(Vertex));
    };
    quad(0, 0, edge, w, 0, edge, w - band, band, clear, band, band, clear);         // top
    quad(0, h, edge, band, h - band, clear, w - band, h - band, clear, w, h, edge); // bottom
    quad(0, 0, edge, band, band, clear, band, h - band, clear, 0, h, edge);         // left
    quad(w, 0, edge, w, h, edge, w - band, h - band, clear, w - band, band, clear); // right
    saved->Apply();
    saved->Release();
}

// BUG-9156: a rounded panel from (x0, y0) to (x1, y1): `fill`, with a `border` edge of 2 px (border 0: none)
void panel(const SkHost* host, float x0, float y0, float x1, float y1, float radius, uint32_t fill, uint32_t border)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || !(x1 > x0) || !(y1 > y0)) return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE); // BUG-9202: the Tribe night fog tinted the ring dark blue
    device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    auto shape = [&](float a0, float b0, float a1, float b1, float r, DWORD c) {
        r = std::min(r, std::min(a1 - a0, b1 - b0) / 2.f);
        std::vector<Vertex> v;
        const float cx = (a0 + a1) / 2.f, cy = (b0 + b1) / 2.f;
        v.push_back({cx, cy, 0, 1, c});
        const float corners[4][2] = {{a1 - r, b0 + r}, {a1 - r, b1 - r}, {a0 + r, b1 - r}, {a0 + r, b0 + r}};
        const float start[4] = {-1.5708f, 0.f, 1.5708f, 3.14159f};
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i <= 6; ++i) {
                const float t = start[k] + 1.5708f * i / 6.f;
                v.push_back({corners[k][0] + r * std::cos(t), corners[k][1] + r * std::sin(t), 0, 1, c});
            }
        v.push_back(v[1]);
        device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, (UINT)v.size() - 2, v.data(), sizeof(Vertex));
    };
    if (border) shape(x0, y0, x1, y1, radius, (DWORD)border);
    shape(border ? x0 + 2 : x0, border ? y0 + 2 : y0, border ? x1 - 2 : x1, border ? y1 - 2 : y1,
          border ? std::max(1.f, radius - 2) : radius, (DWORD)fill);
    saved->Apply();
    saved->Release();
}

// BUG-9156: a filled arrow (left or right) with a dark edge, centred at (x, y)
void arrow(const SkHost* host, float x, float y, float size, bool left, uint32_t argb)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || !(size > 1)) return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE); // BUG-9202: the Tribe night fog tinted the ring dark blue
    device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    const float d = left ? -1.f : 1.f;
    auto tri = [&](float s, DWORD c) {
        Vertex v[3] = {{x + d * s, y, 0, 1, c}, {x - d * s * 0.7f, y - s, 0, 1, c}, {x - d * s * 0.7f, y + s, 0, 1, c}};
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof(Vertex));
    };
    tri(size + 2.f, 0xC0000000);
    tri(size, (DWORD)argb);
    saved->Apply();
    saved->Release();
}

void draw(const SkHost* host, float x, float y, float radius, float left, float revive, double now)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || !std::isfinite(x) || !std::isfinite(y) || !(radius > 2)) return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE); // BUG-9202: the Tribe night fog tinted the ring dark blue
    device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); // the disc is see-through
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    const float r_out = radius, r_in = radius * 0.68f;
    left = std::clamp(left, 0.f, 1.f);
    paint(device, D3DPT_TRIANGLELIST, pie(x, y, r_out + 3.f, 1.f), kDisc);                  // the dark disc
    paint(device, D3DPT_TRIANGLESTRIP, arc_strip(x, y, r_in, r_out, 0.f, 1.f), kTrack);       // the empty track
    const bool blink_on = std::fmod(now, 0.8) < 0.5;
    paint(device, D3DPT_TRIANGLESTRIP, arc_strip(x, y, r_in, r_out, left_start(left), left), time_color(left, blink_on));
    if (revive > 0.01f)
        paint(device, D3DPT_TRIANGLELIST, pie(x, y, r_in - 2.f, revive), kRevive); // a revive under way
    // the "+" (a downed one waiting for help)
    const float arm = r_in * 0.55f, half = std::max(1.5f, r_in * 0.13f);
    const std::vector<Pt> cross{{x - half, y - arm}, {x + half, y - arm}, {x - half, y + arm},
                                {x + half, y - arm}, {x + half, y + arm}, {x - half, y + arm},
                                {x - arm, y - half}, {x + arm, y - half}, {x - arm, y + half},
                                {x + arm, y - half}, {x + arm, y + half}, {x - arm, y + half}};
    paint(device, D3DPT_TRIANGLELIST, cross, kCross);
    saved->Apply();
    saved->Release();
}
} // namespace ring_timer
