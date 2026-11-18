#include "partner_marker.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>

namespace partner_marker {
void draw(const SkHost* host, float x, float y, float dx, float dy, float size, unsigned color)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    const float length = std::hypot(dx,dy);
    if (!device || !std::isfinite(length) || length < .001f || !std::isfinite(x) || !std::isfinite(y)) return;
    dx /= length; dy /= length;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL,&saved))) return;
    device->SetVertexShader(nullptr); device->SetPixelShader(nullptr);
    device->SetTexture(0,nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE,FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_LIGHTING,FALSE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    struct Vertex { float x,y,z,rhw; DWORD color; };
    auto paint = [&](float scale, DWORD argb) {
        auto v = [&](float along,float across) -> Vertex {
            return {x+(dx*along-dy*across)*size*scale,y+(dy*along+dx*across)*size*scale,0,1,argb};
        };
        Vertex vertices[] = {v(.65f,0),v(0,.38f),v(0,-.38f),
            v(-.55f,.14f),v(.08f,.14f),v(.08f,-.14f),
            v(-.55f,.14f),v(.08f,-.14f),v(-.55f,-.14f)};
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,3,vertices,sizeof(Vertex));
    };
    paint(1.2f,0xFF102018); paint(1.f,color);
    saved->Apply(); saved->Release();
}

// the device state for flat, alpha-blended triangles; returns the saved state (nullptr: do not draw)
static IDirect3DStateBlock9* begin(IDirect3DDevice9* device)
{
    IDirect3DStateBlock9* saved = nullptr;
    if (!device || FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return nullptr;
    device->SetVertexShader(nullptr); device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    return saved;
}
struct Vertex { float x, y, z, rhw; DWORD color; };

void chevron(const SkHost* host, float x, float y, float dx, float dy, float size, unsigned fill)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    const float length = std::hypot(dx, dy);
    if (!device || !std::isfinite(length) || length < .001f || !std::isfinite(x) || !std::isfinite(y)) return;
    dx /= length; dy /= length;
    IDirect3DStateBlock9* saved = begin(device);
    if (!saved) return;
    // the mockup's chevron, in units of its width: two arms meeting at the tip (along +), 0.27 thick
    auto paint = [&](float grow, DWORD argb) {
        auto v = [&](float along, float across) -> Vertex {
            return {x + (dx * along - dy * across) * size, y + (dy * along + dx * across) * size, 0, 1, argb};
        };
        const float a0 = -.30f - grow, a1 = .20f - grow * 1.4f, a2 = .47f + grow * 1.4f, a3 = -.03f + grow, w = .5f + grow;
        Vertex t[] = {v(a0, -w), v(a1, 0), v(a2, 0), v(a0, -w), v(a2, 0), v(a3, -w),   // left arm
                      v(a1, 0), v(a0, w), v(a3, w), v(a1, 0), v(a3, w), v(a2, 0)};      // right arm
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 4, t, sizeof(Vertex));
    };
    paint(.07f, 0x60000000);  // a soft shadow
    paint(.045f, 0xFFFFFFFF); // the white edge
    paint(0, fill);
    saved->Apply(); saved->Release();
}

void plate(const SkHost* host, float x1, float y1, float x2, float y2, float radius, unsigned fill, unsigned edge)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || !(x2 > x1) || !(y2 > y1)) return;
    IDirect3DStateBlock9* saved = begin(device);
    if (!saved) return;
    radius = std::min(radius, std::min(x2 - x1, y2 - y1) * .5f);
    constexpr int kSeg = 8, kPts = 4 * (kSeg + 1);
    auto outline = [&](float in, Vertex* out, DWORD argb) { // the rounded rectangle's edge, `in` pixels inside
        const float r = std::max(0.f, radius - in);
        const float corners[4][3] = {{x2 - in - r, y1 + in + r, -1.5708f}, {x2 - in - r, y2 - in - r, 0.f},
                                     {x1 + in + r, y2 - in - r, 1.5708f}, {x1 + in + r, y1 + in + r, 3.1416f}};
        int n = 0;
        for (auto& c : corners)
            for (int i = 0; i <= kSeg; ++i) {
                const float ang = c[2] + 1.5708f * i / kSeg;
                out[n++] = {c[0] + std::cos(ang) * r, c[1] + std::sin(ang) * r, 0, 1, argb};
            }
    };
    const float in = edge >> 24 ? 1.f : 0.f;
    Vertex fan[kPts + 2];
    fan[0] = {(x1 + x2) * .5f, (y1 + y2) * .5f, 0, 1, fill};
    outline(in, fan + 1, fill);
    fan[kPts + 1] = fan[1];
    device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, kPts, fan, sizeof(Vertex));
    if (edge >> 24) { // a ring, so the see-through fill does not show the edge's colour
        Vertex outer[kPts], inner[kPts], strip[2 * kPts + 2];
        outline(0, outer, edge);
        outline(1, inner, edge);
        for (int i = 0; i < kPts; ++i) { strip[2 * i] = outer[i]; strip[2 * i + 1] = inner[i]; }
        strip[2 * kPts] = outer[0];
        strip[2 * kPts + 1] = inner[0];
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2 * kPts, strip, sizeof(Vertex));
    }
    saved->Apply(); saved->Release();
}
}
