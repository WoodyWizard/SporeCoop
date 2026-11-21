#include "partner_marker.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <string>
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

// the plane across a ring's up: unit up and two unit tangents a, b; false for a bad ring
struct Basis { float up[3], a[3], b[3]; };
static bool basis(const Ring& g, Basis& o)
{
    const float* p = g.at;
    const float ul = std::sqrt(g.up[0] * g.up[0] + g.up[1] * g.up[1] + g.up[2] * g.up[2]);
    if (!std::isfinite(ul) || ul < 1e-4f || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
        return false;
    for (int i = 0; i < 3; ++i) o.up[i] = g.up[i] / ul;
    const float* up = o.up;
    // a tangent: up x (the axis least along up)
    float ax[3] = {0, 0, 0};
    const float fx = std::fabs(up[0]), fy = std::fabs(up[1]), fz = std::fabs(up[2]);
    ax[fx <= fy && fx <= fz ? 0 : fy <= fz ? 1 : 2] = 1;
    float a[3] = {up[1] * ax[2] - up[2] * ax[1], up[2] * ax[0] - up[0] * ax[2], up[0] * ax[1] - up[1] * ax[0]};
    const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    for (int i = 0; i < 3; ++i) o.a[i] = a[i] / al;
    o.b[0] = up[1] * o.a[2] - up[2] * o.a[1];
    o.b[1] = up[2] * o.a[0] - up[0] * o.a[2];
    o.b[2] = up[0] * o.a[1] - up[1] * o.a[0];
    return true;
}
// the world point `rad` metres out at angle `ang`, `lift` metres over the ground
static void ring_point(const Ring& g, const Basis& q, float rad, float ang, float lift, float w[3])
{
    const float ca = std::cos(ang) * rad, sa = std::sin(ang) * rad;
    for (int i = 0; i < 3; ++i) w[i] = g.at[i] + q.up[i] * lift + q.a[i] * ca + q.b[i] * sa;
}

// a circle of kSeg points round g.at on the plane across g.up, `out` radii out, projected; false when a point (or
// the centre) is behind the camera
template <int kSeg>
static bool project_circle(const SkHost* host, const Ring& g, const Basis& q, float out, float c[2], float (*rim)[2])
{
    bool ok = true;
    auto project = [&](float rad, float ang, float o[2]) {
        float w[3];
        ring_point(g, q, rad, ang, .12f, w); // just over the ground
        o[0] = o[1] = NAN;
        host->world_to_screen(w, o); // (0 also off screen, with the point written: only an unwritten one is behind)
        ok = ok && std::isfinite(o[0]) && std::isfinite(o[1]) && std::fabs(o[0]) < 1e5f && std::fabs(o[1]) < 1e5f;
    };
    project(0, 0, c);
    for (int s = 0; s < kSeg && ok; ++s)
        project(g.radius * out, 6.2831853f * s / kSeg, rim[s]);
    return ok;
}

// colour c (its alpha the ring's opacity) with `white` of white mixed in, at alpha `a` (0..1) of that opacity
static DWORD tint(unsigned c, float white, float a)
{
    const float op = ((c >> 24) & 0xFF) / 255.f;
    auto ch = [&](int sh) { return (DWORD)std::lround(((c >> sh) & 0xFF) * (1 - white) + 255.f * white) & 0xFF; };
    const DWORD al = (DWORD)std::lround(std::clamp(a * op, 0.f, 1.f) * 255.f);
    return al << 24 | ch(16) << 16 | ch(8) << 8 | ch(0);
}

static bool identity(const D3DMATRIX& m)
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (std::fabs(m.m[r][c] - (r == c ? 1.f : 0.f)) > 1e-6f) return false;
    return true;
}

// The scene's depth at the overlay pass (docs/re/graphics.md#graphics-overlay-depth): in some frames the game's last
// pass before Present is a 2D one that leaves no depth-stencil bound and an orthographic view and projection (Cell:
// about every other frame), in the others the scene's depth-stencil and its perspective transforms are still set. The
// last scene state seen is kept: its depth-stencil surface (one reference, released before a device Reset, at the
// unload and when another surface is seen) and its view and projection (only for each vertex's depth: its x and y come
// from world_to_screen, the camera the frame was drawn with).
static IDirect3DSurface9* g_scene_ds;
static D3DMATRIX g_scene_view, g_scene_proj;
static uint32_t g_scene_seen, g_scene_kept;
void release_depth()
{
    if (g_scene_ds) g_scene_ds->Release();
    g_scene_ds = nullptr;
}
// tests (coop.rings ztest): the depth compare and the near plane the ring's depth is computed with (0: the projection's)
static int g_zfunc = D3DCMP_LESSEQUAL;
static float g_near_override = 0;
void set_depth_test(int zfunc, float near_plane)
{
    g_zfunc = zfunc >= 1 && zfunc <= 8 ? zfunc : D3DCMP_LESSEQUAL;
    g_near_override = near_plane > 0 ? near_plane : 0;
}
static bool perspective(const D3DMATRIX& p) { return p._34 == 1.f && p._44 == 0.f && p._33 > 0 && p._43 < 0; }
// the current frame's scene depth (true) or the last one kept; false: none known
static bool scene_depth(IDirect3DDevice9* device)
{
    IDirect3DSurface9* ds = nullptr;
    D3DMATRIX view{}, proj{};
    device->GetDepthStencilSurface(&ds);
    device->GetTransform(D3DTS_VIEW, &view);
    device->GetTransform(D3DTS_PROJECTION, &proj);
    if (ds && perspective(proj) && !identity(view)) {
        g_scene_seen++;
        if (ds != g_scene_ds) {
            release_depth();
            g_scene_ds = ds; // (keeps GetDepthStencilSurface's reference)
        } else
            ds->Release();
        g_scene_view = view;
        g_scene_proj = proj;
        return true;
    }
    if (ds) ds->Release();
    if (!g_scene_ds) return false;
    g_scene_kept++;
    return true;
}

std::string device_report(const SkHost* host)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device) return "no device";
    char b[1024];
    std::string out;
    IDirect3DSurface9* ds = nullptr;
    IDirect3DSurface9* rt = nullptr;
    D3DSURFACE_DESC dd{}, rd{};
    const bool has_ds = SUCCEEDED(device->GetDepthStencilSurface(&ds)) && ds && SUCCEEDED(ds->GetDesc(&dd));
    if (SUCCEEDED(device->GetRenderTarget(0, &rt)) && rt) rt->GetDesc(&rd);
    snprintf(b, sizeof b, "depth-stencil: %s %p %ux%u format=%d ms=%d | render target 0: %p %ux%u format=%d\n",
             has_ds ? "bound" : "none", (void*)ds, dd.Width, dd.Height, (int)dd.Format, (int)dd.MultiSampleType, (void*)rt,
             rd.Width, rd.Height, (int)rd.Format);
    out += b;
    if (ds) ds->Release();
    if (rt) rt->Release();
    DWORD z = 0, zw = 0, zf = 0;
    device->GetRenderState(D3DRS_ZENABLE, &z);
    device->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
    device->GetRenderState(D3DRS_ZFUNC, &zf);
    D3DVIEWPORT9 vp{};
    device->GetViewport(&vp);
    snprintf(b, sizeof b, "states: ZENABLE=%lu ZWRITE=%lu ZFUNC=%lu viewport=%lu %lu %lux%lu z %.2f..%.2f\n", z, zw, zf,
             vp.X, vp.Y, vp.Width, vp.Height, vp.MinZ, vp.MaxZ);
    out += b;
    snprintf(b, sizeof b, "scene depth: kept=%p frames with it=%u frames with the kept one=%u\n", (void*)g_scene_ds,
             g_scene_seen, g_scene_kept);
    out += b;
    const D3DTRANSFORMSTATETYPE ts[3] = {D3DTS_VIEW, D3DTS_PROJECTION, D3DTS_WORLD};
    const char* names[3] = {"view", "projection", "world"};
    for (int i = 0; i < 3; ++i) {
        D3DMATRIX m{};
        device->GetTransform(ts[i], &m);
        snprintf(b, sizeof b, "%s: [%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.2f %.2f %.2f %.4f]\n",
                 names[i], m._11, m._12, m._13, m._14, m._21, m._22, m._23, m._24, m._31, m._32, m._33, m._34, m._41,
                 m._42, m._43, m._44);
        out += b;
    }
    return out;
}

int rings(const SkHost* host, const Ring* in, int n, bool soft, bool depth)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (!device || !host->world_to_screen || !in || n <= 0) return 0;
    // 48 points round (BUG-9321: round up close; the old ring had 24). Flat (no depth): every band is drawn between two
    // circles taken from the outermost one projected (a screen-space blend towards the projected centre: exact enough
    // for a ring this small against the camera's distance, and 49 projections a ring instead of one per band).
    // depth (BUG-9321, the owner: a creature in front of its ring hides that part): each vertex projected on its own
    // (world_to_screen) with its depth from the scene's view and projection, tested against the scene's depth buffer
    // (scene_depth above), not written.
    constexpr int kSeg = 48;
    const float kOut = soft ? 1.3f : 1.1f; // the outermost circle, in radii
    if (depth && !scene_depth(device)) depth = false; // (nothing to test against: drawn flat)
    std::vector<Vertex> v;
    std::vector<uint16_t> idx;
    IDirect3DStateBlock9* saved = nullptr;
    IDirect3DSurface9* old_ds = nullptr;
    int drawn = 0;
    for (int r = 0; r < n && r < 512; ++r) {
        const Ring& g = in[r];
        if (!(g.radius > 0) || g.radius > 500.f) continue;
        Basis q;
        float c[2], rim[kSeg][2];
        if (!basis(g, q) || !project_circle<kSeg>(host, g, q, kOut, c, rim)) continue;
        float px = 0; // the ring's radius on the screen
        for (int s = 0; s < kSeg; ++s) px += std::hypot(rim[s][0] - c[0], rim[s][1] - c[1]);
        px /= kSeg * kOut;
        if (!(px > .25f) || px > 1e4f) continue;
        float k = 1; // BUG-9321: kept between min_px and max_px on the screen (a marker at every zoom)
        if (g.max_px > 0 && px > g.max_px) k = g.max_px / px;
        else if (g.min_px > 0 && px < g.min_px) k = g.min_px / px;
        // (depth: a little higher for a bigger ring, so a gentle slope under it does not cut it)
        const float lift = std::isfinite(g.lift) ? (g.lift_fixed ? g.lift : g.lift * k) : .12f + .06f * g.radius * k;
        v.clear();
        idx.clear();
        bool behind = false;
        auto point = [&](float t, int s, DWORD col) {
            if (depth) { // x, y: this frame's camera; z, rhw: the scene's view depth and projection
                float w[3], o[2] = {NAN, NAN};
                ring_point(g, q, g.radius * k * t, 6.2831853f * s / kSeg, lift, w);
                host->world_to_screen(w, o);
                const D3DMATRIX& V = g_scene_view;
                const float zv = w[0] * V._13 + w[1] * V._23 + w[2] * V._33 + V._43;
                behind |= !(zv > .05f) || !std::isfinite(o[0]) || !std::isfinite(o[1]);
                float za = g_scene_proj._33, zb = g_scene_proj._43;
                if (g_near_override > 0 && za > 1.f) { // (tests) the same far plane, another near one
                    const float n = -zb / za, f = za * n / (za - 1.f);
                    za = f / (f - g_near_override), zb = -g_near_override * za;
                }
                const float z = std::clamp(za + zb / std::max(zv, .05f), 0.f, 1.f);
                v.push_back({o[0], o[1], z, 1.f / std::max(zv, .05f), col});
            } else {
                const float m = k * t / kOut;
                v.push_back({c[0] + (rim[s][0] - c[0]) * m, c[1] + (rim[s][1] - c[1]) * m, 0, 1, col});
            }
        };
        auto count = [&]() { return (uint16_t)v.size(); };
        auto circle = [&](float t, DWORD col) { // its first vertex's index
            const uint16_t first = count();
            for (int s = 0; s < kSeg; ++s) point(t, s, col);
            return first;
        };
        auto band = [&](float t0, DWORD c0, float t1, DWORD c1) {
            const uint16_t i0 = circle(t0, c0), i1 = circle(t1, c1);
            for (int s = 0; s < kSeg; ++s) {
                const uint16_t n0 = (uint16_t)((s + 1) % kSeg);
                idx.insert(idx.end(), {(uint16_t)(i0 + s), (uint16_t)(i1 + s), (uint16_t)(i0 + n0),
                                       (uint16_t)(i0 + n0), (uint16_t)(i1 + s), (uint16_t)(i1 + n0)});
            }
        };
        if (soft) {
            // a very faint dark halo just outside the rim (still seen on light ground), under the glow
            band(.88f, 0, 1.04f, 0x26000000);
            band(1.04f, 0x26000000, 1.3f, 0);
            // the glow (the owner's reference picture of 2026-11-21): almost clear and white-ish in the middle (the
            // unit stands there), the player's colour strongest near 0.9 of the radius, then a soft fade to nothing
            // just outside it. {radius, white mixed in, alpha}
            static const float kStops[][3] = {{0, .85f, .10f},   {.30f, .70f, .10f}, {.55f, .45f, .20f},
                                              {.72f, .20f, .45f}, {.84f, 0, .80f},    {.92f, 0, .85f},
                                              {1.f, 0, .55f},    {1.08f, 0, .18f},   {1.18f, 0, 0}};
            const DWORD mid = tint(g.color, kStops[0][1], kStops[0][2]);
            const uint16_t centre = count();
            point(0, 0, mid);
            uint16_t prev = circle(kStops[1][0], tint(g.color, kStops[1][1], kStops[1][2]));
            for (int s = 0; s < kSeg; ++s)
                idx.insert(idx.end(), {centre, (uint16_t)(prev + s), (uint16_t)(prev + (s + 1) % kSeg)});
            for (size_t i = 1; i + 1 < sizeof kStops / sizeof kStops[0]; ++i)
                band(kStops[i][0], tint(g.color, kStops[i][1], kStops[i][2]), kStops[i + 1][0],
                     tint(g.color, kStops[i + 1][1], kStops[i + 1][2]));
        } else { // decision 7K A's first look (BUG-9302): a hard band 0.78..1.0 over a dark one 0.68..1.1
            band(.68f, 0x70000000, 1.1f, 0x70000000);
            band(.78f, g.color, 1.f, g.color);
        }
        if (behind) continue;
        if (!saved) {
            if (!(saved = begin(device))) return drawn;
            device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
            if (depth) { // (the state block does not hold the depth-stencil binding: put back below)
                device->GetDepthStencilSurface(&old_ds);
                if (old_ds != g_scene_ds) device->SetDepthStencilSurface(g_scene_ds);
                device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
                device->SetRenderState(D3DRS_ZFUNC, g_zfunc);
                device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            }
        }
        device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, (UINT)v.size(), (UINT)(idx.size() / 3), idx.data(),
                                       D3DFMT_INDEX16, v.data(), sizeof(Vertex));
        drawn++;
    }
    if (saved) {
        if (depth) {
            device->SetDepthStencilSurface(old_ds);
            if (old_ds) old_ds->Release();
        }
        saved->Apply();
        saved->Release();
    }
    return drawn;
}
}

// tests (coop.rings trace, BUG-9321 in Civ): the frame's render target / depth-stencil switches, clears and draw counts
// between them, recorded for a few frames from hooks on the device's methods (installed by the first trace)
namespace partner_marker {
using Clear_t = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
using SetDS_t = HRESULT(__stdcall*)(IDirect3DDevice9*, IDirect3DSurface9*);
using SetRT_t = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
using DIP_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
static Clear_t o_Clear;
static SetDS_t o_SetDS;
static SetRT_t o_SetRT;
static DIP_t o_DIP;
static int g_trace_frames;
static unsigned g_trace_draws;
static std::string g_trace;
static void trace_line(const char* s)
{
    if (g_trace_draws) {
        char b[48];
        snprintf(b, sizeof b, "  %u draws\n", g_trace_draws);
        g_trace += b;
        g_trace_draws = 0;
    }
    if (g_trace.size() < 60000) g_trace += s;
}
static std::string surf(IDirect3DSurface9* s)
{
    char b[64];
    D3DSURFACE_DESC d{};
    if (s) s->GetDesc(&d);
    snprintf(b, sizeof b, "%p %ux%u f=%d", (void*)s, d.Width, d.Height, (int)d.Format);
    return b;
}
// BUG-9327: Civ and Tribe draw the world into another render target, put it on the back buffer and then clear the
// depth-stencil (Z and stencil, not the colour) before the game's UI: at the overlay pass the depth buffer is empty and
// the ring was drawn over the unit (docs/re/graphics.md#graphics-ui-depth-clear). The rings of the last overlay pass
// are drawn right before that clear: the scene's depth is still there, and the game's UI comes after (over the ring).
static const SkHost* g_early_host;
static std::vector<Ring> g_early;
static bool g_early_soft = true;
static bool g_early_armed;     // the last overlay pass gave a list for this frame
static int g_early_drawn = -1; // rings drawn at the clear this frame (-1: none drawn there)
static uint32_t g_early_frames;
static void early_draw(IDirect3DDevice9* d, DWORD n, DWORD f)
{
    if (!g_early_armed || g_early.empty() || n != 0 || !(f & D3DCLEAR_ZBUFFER) || (f & D3DCLEAR_TARGET) || g_early_drawn >= 0)
        return;
    IDirect3DSurface9 *ds = nullptr, *rt = nullptr, *bb = nullptr;
    d->GetDepthStencilSurface(&ds);
    d->GetRenderTarget(0, &rt);
    d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    D3DSURFACE_DESC dd{}, bd{};
    if (ds) ds->GetDesc(&dd);
    if (bb) bb->GetDesc(&bd);
    D3DVIEWPORT9 vp{};
    d->GetViewport(&vp);
    // the back buffer is the target (the world is on it already) and the clear is of the whole screen-size depth
    const bool ok = ds && rt && rt == bb && dd.Width == bd.Width && dd.Height == bd.Height && vp.Width == bd.Width &&
                    vp.Height == bd.Height;
    if (ds) ds->Release();
    if (rt) rt->Release();
    if (bb) bb->Release();
    if (!ok) return;
    g_early_armed = false;
    g_early_drawn = rings(g_early_host, g_early.data(), (int)g_early.size(), g_early_soft, true);
    g_early_frames++;
}
static HRESULT __stdcall h_Clear(IDirect3DDevice9* d, DWORD n, const D3DRECT* r, DWORD f, D3DCOLOR c, float z, DWORD st)
{
    early_draw(d, n, f);
    if (g_trace_frames > 0) {
        IDirect3DSurface9* ds = nullptr;
        d->GetDepthStencilSurface(&ds);
        D3DVIEWPORT9 vp{};
        d->GetViewport(&vp);
        D3DMATRIX p{};
        d->GetTransform(D3DTS_PROJECTION, &p);
        char b[256];
        snprintf(b, sizeof b, "clear flags=%lx rects=%lu z=%.2f viewport=%lu %lu %lux%lu ds=%s proj33=%.4f proj43=%.3f\n", f,
                 n, z, vp.X, vp.Y, vp.Width, vp.Height, surf(ds).c_str(), p._33, p._43);
        if (ds) ds->Release();
        trace_line(b);
    }
    return o_Clear(d, n, r, f, c, z, st);
}
static HRESULT __stdcall h_SetDS(IDirect3DDevice9* d, IDirect3DSurface9* s)
{
    if (g_trace_frames > 0) trace_line(("set depth-stencil " + surf(s) + "\n").c_str());
    return o_SetDS(d, s);
}
static HRESULT __stdcall h_SetRT(IDirect3DDevice9* d, DWORD i, IDirect3DSurface9* s)
{
    if (g_trace_frames > 0) {
        char b[32];
        snprintf(b, sizeof b, "set render target %lu ", i);
        trace_line((b + surf(s) + "\n").c_str());
    }
    return o_SetRT(d, i, s);
}
static HRESULT __stdcall h_DIP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT b, UINT m, UINT n, UINT s, UINT c)
{
    if (g_trace_frames > 0) g_trace_draws++;
    return o_DIP(d, t, b, m, n, s, c);
}
static int g_hooked = -1; // 1 installed, 0 failed
bool install(const SkHost* host, SkMod* mod)
{
    auto* device = static_cast<IDirect3DDevice9*>(host->d3d_device());
    if (g_hooked >= 0 || !device) return g_hooked == 1;
    g_early_host = host;
    void** vt = *(void***)device; // IDirect3DDevice9::Clear is slot 43
    g_hooked = host->hook_create(mod, vt[43], (void*)h_Clear, (void**)&o_Clear) == 0;
    return g_hooked == 1;
}
int early_drawn()
{
    const int drawn = g_early_drawn;
    g_early_drawn = -1;
    g_early_armed = false;
    return drawn;
}
void early_arm(const Ring* in, int n, bool soft)
{
    g_early.assign(in, in + std::max(0, n));
    g_early_soft = soft;
    g_early_armed = n > 0;
}
uint32_t early_frames() { return g_early_frames; }
std::string trace(const SkHost* host, SkMod* mod, int frames)
{
    if (!install(host, mod)) return "hooks failed" "\n";
    static int traced = -1; // the trace's own hooks (only for tests): SetDepthStencilSurface 39, SetRenderTarget 37, DIP 82
    if (traced < 0) {
        void** vt = *(void***)host->d3d_device();
        traced = host->hook_create(mod, vt[39], (void*)h_SetDS, (void**)&o_SetDS) == 0 &&
                 host->hook_create(mod, vt[37], (void*)h_SetRT, (void**)&o_SetRT) == 0 &&
                 host->hook_create(mod, vt[82], (void*)h_DIP, (void**)&o_DIP) == 0;
    }
    if (traced != 1) return "trace hooks failed" "\n";
    std::string out = g_trace;
    g_trace.clear();
    g_trace_draws = 0;
    g_trace_frames = frames;
    return out.empty() ? "(armed: ask again)\n" : out;
}
void trace_frame()
{
    if (g_trace_frames > 0) {
        trace_line("--- overlay pass (Present)\n");
        g_trace_frames--;
    }
}
}
