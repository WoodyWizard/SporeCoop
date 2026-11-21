#include "partner_marker.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <vector>

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

void plate(const SkHost* host, float x1, float y1, float x2, float y2, float radius, unsigned fill, unsigned edge, float width)
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
    width = std::clamp(width, 1.f, 4.f);
    const float in = edge >> 24 ? width : 0.f;
    Vertex fan[kPts + 2];
    fan[0] = {(x1 + x2) * .5f, (y1 + y2) * .5f, 0, 1, fill};
    outline(in, fan + 1, fill);
    fan[kPts + 1] = fan[1];
    device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, kPts, fan, sizeof(Vertex));
    if (edge >> 24) { // a ring, so the see-through fill does not show the edge's colour
        Vertex outer[kPts], inner[kPts], strip[2 * kPts + 2];
        outline(0, outer, edge);
        outline(width, inner, edge);
        for (int i = 0; i < kPts; ++i) { strip[2 * i] = outer[i]; strip[2 * i + 1] = inner[i]; }
        strip[2 * kPts] = outer[0];
        strip[2 * kPts + 1] = inner[0];
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2 * kPts, strip, sizeof(Vertex));
    }
    saved->Apply(); saved->Release();
}

int ground_rings(const SkHost* host, const float* centres, const float* radii, const unsigned* colors, int n)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || !host->world_to_screen || n <= 0) return 0;
    constexpr int kSeg = 24;
    // per ring: the band's outer and inner edge and the dark edge's, projected (x, y) at kSeg angles
    struct Proj { float o[kSeg][2], i[kSeg][2], eo[kSeg][2], ei[kSeg][2]; };
    std::vector<Proj> rings;
    std::vector<unsigned> cols;
    rings.reserve(n);
    for (int r = 0; r < n; ++r) {
        const float* c = centres + 3 * r;
        const float radius = radii[r];
        if (!(radius > 0) || radius > 50.f) continue;
        const float len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (!std::isfinite(len) || len < 1.f) continue;
        const float up[3] = {c[0] / len, c[1] / len, c[2] / len};
        // a tangent: up x (the axis least along up)
        float ax[3] = {0, 0, 0};
        const float fx = std::fabs(up[0]), fy = std::fabs(up[1]), fz = std::fabs(up[2]);
        ax[fx <= fy && fx <= fz ? 0 : fy <= fz ? 1 : 2] = 1;
        float a[3] = {up[1] * ax[2] - up[2] * ax[1], up[2] * ax[0] - up[0] * ax[2], up[0] * ax[1] - up[1] * ax[0]};
        const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        a[0] /= al; a[1] /= al; a[2] /= al;
        const float b[3] = {up[1] * a[2] - up[2] * a[1], up[2] * a[0] - up[0] * a[2], up[0] * a[1] - up[1] * a[0]};
        Proj p{};
        bool ok = true;
        auto project = [&](float rad, float ang, float out[2]) {
            const float lift = .12f, ca = std::cos(ang) * rad, sa = std::sin(ang) * rad; // just over the ground
            float w[3] = {c[0] + up[0] * lift + a[0] * ca + b[0] * sa, c[1] + up[1] * lift + a[1] * ca + b[1] * sa,
                          c[2] + up[2] * lift + a[2] * ca + b[2] * sa};
            out[0] = out[1] = NAN;
            host->world_to_screen(w, out); // (0 also off screen, with the point written: only an unwritten one is behind)
            ok = ok && std::isfinite(out[0]) && std::isfinite(out[1]) && std::fabs(out[0]) < 1e5f && std::fabs(out[1]) < 1e5f;
        };
        for (int s = 0; s < kSeg && ok; ++s) {
            const float ang = 6.2831853f * s / kSeg;
            project(radius, ang, p.o[s]);
            project(radius * .78f, ang, p.i[s]);
            project(radius * 1.1f, ang, p.eo[s]);
            project(radius * .68f, ang, p.ei[s]);
        }
        if (!ok) continue;
        rings.push_back(p);
        cols.push_back(colors[r]);
    }
    if (rings.empty()) return 0;
    IDirect3DStateBlock9* saved = begin(device);
    if (!saved) return 0;
    Vertex strip[2 * kSeg + 2];
    auto band = [&](const float (*o)[2], const float (*in)[2], DWORD argb) {
        for (int s = 0; s <= kSeg; ++s) {
            const int k = s % kSeg;
            strip[2 * s] = {o[k][0], o[k][1], 0, 1, argb};
            strip[2 * s + 1] = {in[k][0], in[k][1], 0, 1, argb};
        }
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2 * kSeg, strip, sizeof(Vertex));
    };
    for (size_t r = 0; r < rings.size(); ++r) {
        band(rings[r].eo, rings[r].ei, 0x70000000); // the dark edge (seen on light ground)
        band(rings[r].o, rings[r].i, (DWORD)cols[r]);
    }
    saved->Apply(); saved->Release();
    return (int)rings.size();
}
}
