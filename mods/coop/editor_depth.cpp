// Depth square for creature parts (roadmap 42, BUG-9103). Spore's own "nudge" handle (the
// outfitter's depth square) is a deformation of accessory models whose tuning file never shipped,
// and creature part models have none. So the mod draws its own square next to the selected detail,
// weapon or other surface part in the creature editors and moves the part in or out with it:
// docs/re/creations.md#creations-editor-depth-handle
#include "editor_depth.h"
#include "editor_depth_axis.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "mouse.h"
#include "sporekit_game.hpp"

namespace editor_depth {
using namespace skg;

static const SkHost* H;
bool g_fix = true;
COOP_FIX("editor.depth", "BUG-9103", g_fix, "editor: creature details and weapons get a depth square that pushes them in or out");
static bool g_cube = true;
COOP_FIX("editor.depth_cube", "BUG-9103", g_cube, "editor: the depth handle is a lit 3D cube like Spore's handles (off: a flat square)");

struct Vec3 {
    float x, y, z;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float k) const { return {x * k, y * k, z * k}; }
    float len() const { return std::sqrt(x * x + y * y + z * z); }
};

// Editor (cEditor, Editors::cEditor::ptr): +0x98 EditorModel, +0xD4 selected rigblock.
// EditorModel: +0x18/+0x1C rigblocks (EditorRigblock*), +0x51 changed, +0x58 model type.
// EditorRigblock: +0x0C part properties, +0x10 model, +0x1C part instance, +0x48 position.
// Model: +0x0C drawn offset (the same point as the rigblock position), +0x18 scale.
// A part is moved by writing both positions; CommitEditHistory makes it an edit (undo, save).
constexpr uint32_t kCreatureModel = 0x9EA3031A; // Creature Editor Setup editorModelTypes
static uintptr_t a_EditorPtr, a_GetBool, a_GetInt, a_Commit, a_GetProps, a_GetWM;
static void* g_list_vtable; // App::PropertyList's vtable (from a list we know)

static bool list_ok(void* list)
{
    if (!g_list_vtable && a_GetProps) {
        void* pm = cdecl_call<void*>(a_GetProps);
        void* known = nullptr;
        if (pm && vcall<bool>(pm, 0x2C / 4, uint32_t(0xCF2DC8D0), uint32_t(0x40600100), &known) && known) {
            g_list_vtable = field<void*>(known, 0);
            vcall<int>(known, 0x04 / 4);
        }
    }
    return list && g_list_vtable && readable(list, 0x38) && field<void*>(list, 0) == g_list_vtable;
}

static bool prop_bool(void* list, uint32_t id)
{
    bool v = false;
    return list_ok(list) && cdecl_call<bool>(a_GetBool, list, id, &v) && v;
}

static int prop_int(void* list, uint32_t id)
{
    int v = 0;
    return list_ok(list) && cdecl_call<bool>(a_GetInt, list, id, &v) ? v : 0;
}

// The open creature-family editor and its model (the pointer stays set outside the editor).
static void* editor_model(void** editor_out = nullptr)
{
    if (game::mode_id() != modes::kEditor || !a_EditorPtr) return nullptr;
    void* ed = *reinterpret_cast<void**>(a_EditorPtr);
    if (!ed || !readable(ed, 0x2B4)) return nullptr;
    void* em = field<void*>(ed, 0x98);
    if (!em || !readable(em, 0x5C) || field<uint32_t>(em, 0x58) != kCreatureModel) return nullptr;
    if (editor_out) *editor_out = ed;
    return em;
}

static std::vector<void*> rigblocks(void* em)
{
    std::vector<void*> out;
    void** b = field<void**>(em, 0x18);
    void** e = field<void**>(em, 0x1C);
    if (!b || e < b || e - b > 512 || !readable(b, (e - b) * 4)) return out;
    for (void** p = b; p < e; ++p)
        if (*p && readable(*p, 0x50)) out.push_back(*p);
    return out;
}

static bool is_vertebra(void* rb) { return prop_bool(field<void*>(rb, 0x0C), 0xAFFF3A14); } // modelIsVertebra

// Details, weapons, mouths, eyes and other surface parts. Limbs, hands, feet and the spine keep
// their placement (the player's rule).
static const char* why_not(void* rb)
{
    void* parts = field<void*>(rb, 0x0C);
    if (!list_ok(parts)) return "no part properties";
    struct Flag { uint32_t id; const char* name; };
    static const Flag kBools[] = {
        {0xAFFF3A14, "spine"}, {0x4FF31EEC, "limb (ball connector)"}, {0x6FF31F12, "limb (socket connector)"},
        {0x04A3386D, "hand"}, {0x04A33896, "foot"},
    };
    for (const auto& f : kBools)
        if (prop_bool(parts, f.id)) return f.name;
    static const Flag kInts[] = {{0xB00F0FDF, "limb"}, {0xB00F0FE2, "hand"}, {0xB00F0FEF, "foot"}};
    for (const auto& f : kInts)
        if (prop_int(parts, f.id) > 0) return f.name;
    return nullptr;
}

static Vec3 position(void* rb) { return field<Vec3>(rb, 0x48); }

static void set_position(void* rb, const Vec3& p)
{
    field<Vec3>(rb, 0x48) = p;
    void* model = field<void*>(rb, 0x10);
    if (model && readable(model, 0x18)) field<Vec3>(model, 0x0C) = p;
}

// "Out of the body" as the spine sees it: from the nearest spine segment through the part (and that
// distance). Used for the depth limits; the handle's direction is the skin's normal below.
static Vec3 outward(void* em, void* rb, float* dist = nullptr)
{
    const Vec3 p = position(rb);
    float best = 1e9f;
    Vec3 dir{0, 0, 1};
    for (void* other : rigblocks(em)) {
        if (other == rb || !is_vertebra(other)) continue;
        const Vec3 d = p - position(other);
        const float l = d.len();
        if (l > 1e-4f && l < best) { best = l; dir = d * (1.f / l); }
    }
    if (dist) *dist = best < 1e8f ? best : 0.f;
    return dir;
}

// BUG-9116: straight in and out of the skin. EditorRigblock +0xA8 is the attach (pin) frame, a 3x3
// rotation stored by rows; its second row, negated, points out of the skin where the part sits
// (runtime 2026-11-11: a side detail out sideways, the front detail forward, the tail details back, a
// back detail up; two details pinned at one spot share it while their own rotation +0x60 differs).
// The line from the spine through the part (the first version) was slanted wherever the skin is not
// round about the spine. `coop.fix editor.depth_normal off`: that line.
static bool g_normal = true;
COOP_FIX("editor.depth_normal", "BUG-9116", g_normal, "editor: the depth handle moves a part straight in and out of the skin (off: along the line from the spine)");

static Vec3 skin_normal(void* em, void* rb)
{
    if (g_normal && readable(rb, 0xA8 + 36)) {
        const float* m = reinterpret_cast<const float*>(static_cast<uint8_t*>(rb) + 0xA8);
        const Vec3 r0{m[0], m[1], m[2]}, r1{m[3], m[4], m[5]}, r2{m[6], m[7], m[8]};
        const float l = r1.len();
        auto unit = [](const Vec3& v) { return std::fabs(v.len() - 1.f) < 0.02f; };
        const float d01 = r0.x * r1.x + r0.y * r1.y + r0.z * r1.z, d12 = r1.x * r2.x + r1.y * r2.y + r1.z * r2.z;
        if (std::isfinite(l) && unit(r0) && unit(r1) && unit(r2) && std::fabs(d01) < 0.02f && std::fabs(d12) < 0.02f)
            return r1 * (-1.f / l);
    }
    return outward(em, rb);
}

// The mirrored twin of a symmetric part: x mirrored (left and right pieces can be different
// part ids, like a pair of ears).
static void* twin(void* em, void* rb)
{
    const Vec3 p = position(rb);
    if (std::fabs(p.x) < 1e-3f) return nullptr;
    void* best = nullptr;
    float bd = 0.02f;
    for (void* other : rigblocks(em)) {
        if (other == rb || is_vertebra(other)) continue;
        const Vec3 q = position(other);
        const float d = Vec3{p.x + q.x, p.y - q.y, p.z - q.z}.len();
        if (d < bd) { bd = d; best = other; }
    }
    return best;
}

// ---- the square ----
constexpr float kOffset = 0.18f;   // world units outside the part's anchor, times the part scale
// How far a part goes, measured as its distance from the nearest spine segment, relative to that
// distance when the editor first saw it (the part sitting on the skin): out only a little past the
// skin, in no further than short of the spine (never through to the other side of the body).
constexpr float kOutBody = 0.3f;   // at most this fraction of the body depth out...
constexpr float kOutScale = 0.2f;  // ...and at most this many times the part scale
constexpr float kInBody = 0.25f;   // stays at least this fraction of the body depth from the spine
constexpr int kHalf = 9;           // the flat square's half size in pixels (editor.depth_size off)
// BUG-9116: the cube is a thing in the scene like Spore's handle balls and arrows: a world size, so it
// grows and shrinks with the editor's zoom as they do (it was 9 pixels at every zoom). Never smaller
// than kMinHalfPx on screen, so it stays easy to grab far out.
constexpr float kHalfWorld = 0.028f;
constexpr float kMinHalfPx = 5.f;
static bool g_size = true;
COOP_FIX("editor.depth_size", "BUG-9116", g_size, "editor: the depth cube has a size in the scene and zooms with Spore's handles (off: 9 pixels)");
static float g_half_px = kHalf; // the cube's half size on screen last drawn (the click area)
static float g_cube_ppu = 0;    // BUG-9145: the cube's pixels per world unit last drawn (a head-on drag's scale)
static bool g_axis_fix = true;
COOP_FIX("editor.depth_axis", "BUG-9145", g_axis_fix,
         "editor: the depth cube can be dragged in every view (off: a part facing the camera or the screen edge ignores the press)");
static unsigned g_axis_fallbacks, g_presses_refused;
struct Drag {
    void* rb = nullptr;
    void* twin = nullptr;
    Vec3 start{}, twin_start{}, dir{};
    float px_per_unit = 0, sx = 0, sy = 0; // screen direction of +dir per world unit, unit vector
    int x0 = 0, y0 = 0;
    float depth = 0, lo = 0, hi = 0;
};
// where each part sat when the editor first saw it, and its spine distance then (drags do not add up)
struct Base { void* rb; Vec3 pos; float dist; };
static void* g_base_em;
static std::vector<Base> g_base;
static Drag g_drag;
static bool g_dragging, g_hover;
static float g_square[2] = {-1, -1};
static void* g_square_rb;
static unsigned g_moves, g_commits;

static float part_scale(void* rb)
{
    void* model = field<void*>(rb, 0x10);
    const float s = model && readable(model, 0x1C) ? field<float>(model, 0x18) : 1.f;
    return std::isfinite(s) && s > 0.05f && s < 20.f ? s : 1.f;
}

// Where the square is for the selected part, or false.
static bool square_for(void* em, void* rb, float out[2], Vec3* dir_out = nullptr, Vec3* at_out = nullptr)
{
    if (!rb || !readable(rb, 0x50) || why_not(rb)) return false;
    const Vec3 dir = skin_normal(em, rb);
    const Vec3 at = position(rb) + dir * (kOffset * part_scale(rb));
    if (!H->world_to_screen(&at.x, out) || !std::isfinite(out[0]) || !std::isfinite(out[1])) return false;
    if (dir_out) *dir_out = dir;
    if (at_out) *at_out = at;
    return true;
}

static Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static DWORD shade(DWORD argb, float k)
{
    auto c = [&](int s) { return DWORD(std::clamp(float((argb >> s) & 0xFF) * k, 0.f, 255.f)) << s; };
    return 0xFF000000u | c(16) | c(8) | c(0);
}

// The handle as a small lit cube like Spore's own handles: one face points along the part's
// outward line, its corners are projected with the game camera, the faces turned to the camera
// are drawn (darker the further they turn from the light), with a dark outline.
static bool draw_cube(IDirect3DDevice9* device, const Vec3& at, const Vec3& dir, DWORD color)
{
    // basis: u along the outward line, v and w across it
    const Vec3 up = std::fabs(dir.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    Vec3 v = cross(dir, up);
    v = v * (1.f / v.len());
    const Vec3 w = cross(dir, v);
    // world half size giving about kHalf pixels on screen at this distance
    float c0[2];
    if (!H->world_to_screen(&at.x, c0)) return false;
    float ppu = 0;
    for (const Vec3& axis : {dir, v, w}) {
        const Vec3 p = at + axis * 0.05f;
        float s[2];
        if (H->world_to_screen(&p.x, s)) ppu = std::max(ppu, std::hypot(s[0] - c0[0], s[1] - c0[1]) / 0.05f);
    }
    if (!(ppu > 1.f) || !std::isfinite(ppu)) return false;
    const float h = g_size ? std::max(kHalfWorld, kMinHalfPx / ppu) : kHalf / ppu;
    g_half_px = h * ppu;
    g_cube_ppu = ppu;
    // corners: bit 0 = +u, bit 1 = +v, bit 2 = +w
    float sc[8][2];
    for (int i = 0; i < 8; ++i) {
        const Vec3 p = at + dir * ((i & 1) ? h : -h) + v * ((i & 2) ? h : -h) + w * ((i & 4) ? h : -h);
        if (!H->world_to_screen(&p.x, sc[i]) || !std::isfinite(sc[i][0]) || !std::isfinite(sc[i][1])) return false;
    }
    struct Face { int a, b, c, d; Vec3 n; };
    const Face faces[6] = {
        {1, 3, 7, 5, dir}, {0, 4, 6, 2, dir * -1.f}, {2, 6, 7, 3, v},
        {0, 1, 5, 4, v * -1.f}, {4, 5, 7, 6, w}, {0, 2, 3, 1, w * -1.f},
    };
    // faces wound the same way seen from outside: the camera sees those with one sign of area
    auto area = [&](const Face& f) {
        const int q[4] = {f.a, f.b, f.c, f.d};
        float s = 0;
        for (int i = 0; i < 4; ++i) s += sc[q[i]][0] * sc[q[(i + 1) % 4]][1] - sc[q[(i + 1) % 4]][0] * sc[q[i]][1];
        return s;
    };
    // All faces are wound the same way seen from outside, so the three facing the camera share one
    // sign of screen area and their opposites have the other. Perspective makes the near (front)
    // faces larger, so the sum of all six signed areas has the front sign. It is a property of the
    // camera's handedness: kept from the last clear frame.
    static float front_sign = -1.f;
    float sum = 0, total = 0;
    for (const Face& f : faces) { const float a = area(f); sum += a; total += std::fabs(a); }
    if (total > 0 && std::fabs(sum) > 0.02f * total) front_sign = sum > 0 ? 1.f : -1.f;
    const float sign = front_sign;

    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return false;
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
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
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    struct Vertex { float x, y, z, rhw; DWORD color; };
    const Vec3 light = Vec3{0.35f, -0.45f, 0.82f} * (1.f / Vec3{0.35f, -0.45f, 0.82f}.len());
    // Like Spore's glossy handle balls: no outline, soft shading that runs across each face (the
    // corner normals are bent towards the corner, so edges read as rounded), a white sheen where
    // the light hits, slightly see-through
    auto lit = [&](const Face& f, int corner) {
        const Vec3 cn = dir * ((corner & 1) ? 1.f : -1.f) + v * ((corner & 2) ? 1.f : -1.f) + w * ((corner & 4) ? 1.f : -1.f);
        Vec3 n = f.n * 0.65f + cn * (0.35f / 1.7320508f);
        n = n * (1.f / n.len());
        const float d = std::max(0.f, dot(n, light));
        const float k = 0.64f + 0.4f * d + 0.25f * std::pow(d, 12.f);
        return shade(color, k);
    };
    for (const Face& f : faces) {
        if (area(f) * sign <= 0) continue; // the three faces turned to the camera
        Vertex q[4] = {
            {sc[f.a][0], sc[f.a][1], 0, 1, lit(f, f.a)}, {sc[f.b][0], sc[f.b][1], 0, 1, lit(f, f.b)},
            {sc[f.d][0], sc[f.d][1], 0, 1, lit(f, f.d)}, {sc[f.c][0], sc[f.c][1], 0, 1, lit(f, f.c)},
        };
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vertex));
    }
    saved->Apply();
    saved->Release();
    return true;
}

static void commit(void*)
{
    void* ed = nullptr;
    void* em = editor_model(&ed);
    if (!em) return;
    field<uint8_t>(em, 0x51) = 1;                       // EditorModel changed: Save/Replace on exit
    thiscall<void>(a_Commit, ed, true, (void*)nullptr); // an undo step like a native edit
    ++g_commits;
}

static bool input(unsigned message, uintptr_t, int x, int y)
{
    if (!g_fix) return false;
    if (g_dragging) {
        if (message == WM_MOUSEMOVE) {
            const float along = ((x - g_drag.x0) * g_drag.sx + (y - g_drag.y0) * g_drag.sy) / g_drag.px_per_unit;
            g_drag.depth = std::clamp(along, g_drag.lo, g_drag.hi);
            set_position(g_drag.rb, g_drag.start + g_drag.dir * g_drag.depth);
            if (g_drag.twin) {
                const Vec3 mirrored{-g_drag.dir.x, g_drag.dir.y, g_drag.dir.z};
                set_position(g_drag.twin, g_drag.twin_start + mirrored * g_drag.depth);
            }
            ++g_moves;
            return true;
        }
        if (message == WM_LBUTTONUP) {
            g_dragging = false;
            if (std::fabs(g_drag.depth) > 1e-4f) H->run_outside_frame(commit, nullptr);
            return true;
        }
        return message == WM_LBUTTONDOWN;
    }
    const float reach = g_half_px + 5.f;
    const bool over = g_square_rb && std::fabs(x - g_square[0]) <= reach && std::fabs(y - g_square[1]) <= reach;
    if (message == WM_MOUSEMOVE) { g_hover = over; return false; }
    if (message != WM_LBUTTONDOWN || !over) return false;
    void* em = editor_model();
    if (!em || field<void*>(*reinterpret_cast<void**>(a_EditorPtr), 0xD4) != g_square_rb) return false;
    Drag d;
    d.rb = g_square_rb;
    float sq[2];
    if (!square_for(em, d.rb, sq, &d.dir)) return false;
    d.start = position(d.rb);
    // screen direction and pixels per world unit along the part's outward line
    if (g_axis_fix) { // BUG-9145: a short step (on screen when the cube is), up / down when it looks at the camera
        const Vec3 a = d.start, b = d.start + d.dir * editor_depth_axis::kStep;
        float pa[2] = {NAN, NAN}, pb[2] = {NAN, NAN};
        H->world_to_screen(&a.x, pa); // off the screen the point is still written (BUG-9121); behind it is not
        H->world_to_screen(&b.x, pb);
        if (!std::isfinite(pa[0]) || !std::isfinite(pa[1])) { pa[0] = sq[0]; pa[1] = sq[1]; } // the cube is on screen
        const auto axis = editor_depth_axis::drag_axis(pa[0], pa[1], pb[0], pb[1], true, g_cube_ppu);
        if (axis.fallback) ++g_axis_fallbacks;
        d.px_per_unit = axis.px_per_unit;
        d.sx = axis.sx;
        d.sy = axis.sy;
    } else {
        const Vec3 a = d.start, b = d.start + d.dir;
        float pa[2], pb[2];
        if (!H->world_to_screen(&a.x, pa) || !H->world_to_screen(&b.x, pb)) { ++g_presses_refused; return false; }
        const float dx = pb[0] - pa[0], dy = pb[1] - pa[1], l = std::hypot(dx, dy);
        if (!(l > 1.f)) { ++g_presses_refused; return false; } // the line points at the camera: rotate the view
        d.px_per_unit = l;
        d.sx = dx / l;
        d.sy = dy / l;
    }
    d.x0 = x;
    d.y0 = y;
    float cur = 0;
    const Vec3 spine_dir = outward(em, d.rb, &cur);
    if (g_base_em != em) { g_base.clear(); g_base_em = em; }
    auto it = std::find_if(g_base.begin(), g_base.end(), [&](const Base& b) { return b.rb == d.rb; });
    if (it == g_base.end()) { g_base.push_back({d.rb, d.start, cur}); it = g_base.end() - 1; }
    const float base = it->dist;
    if (!(base > 1e-3f)) return false;
    // The limits, measured from where the part first sat, along the handle's line: out at most a little
    // past the skin, in no further than short of the spine. Along the skin normal a step in brings the
    // part closer to the spine by about cos(angle to the spine's line): the inward room grows by 1/cos.
    const float off = dot(d.start - it->pos, d.dir);
    const float out_max = std::min(kOutBody * base, kOutScale * part_scale(d.rb));
    const float cosine = std::clamp(dot(d.dir, spine_dir), 0.3f, 1.f);
    const float in_max = (1.f - kInBody) * base / cosine;
    d.hi = std::max(0.f, out_max - off);
    d.lo = std::min(0.f, -in_max - off);
    d.twin = twin(em, d.rb);
    if (d.twin) d.twin_start = position(d.twin);
    g_drag = d;
    g_dragging = true;
    return true; // the editor does not see the press: the part stays selected and is not dragged
}

void draw()
{
    g_square_rb = nullptr;
    if (!g_fix) { g_dragging = false; return; }
    void* ed = nullptr;
    void* em = editor_model(&ed);
    void* rb = em ? field<void*>(ed, 0xD4) : nullptr;
    if (!em || !rb) { g_dragging = false; return; }
    // a dialog (the exit box "Save New / Replace") is the window manager's modal window (+0x82C,
    // gamefixes.cpp BUG-0270): no square over it, and its clicks stay the dialog's
    void* wm = a_GetWM ? cdecl_call<void*>(a_GetWM) : nullptr;
    if (wm && readable(wm, 0x830) && field<void*>(wm, 0x82C)) { g_hover = false; return; }
    float sq[2];
    Vec3 dir{}, at{};
    if (!square_for(em, rb, sq, &dir, &at)) return;
    g_square[0] = sq[0];
    g_square[1] = sq[1];
    g_square_rb = rb;
    auto* device = static_cast<IDirect3DDevice9*>(H->d3d_device());
    if (!device) return;
    // Spore's editor handle colours: pale blue, green on rollover; orange while held (the cube's
    // base tones match the lit handle balls next to it: 868FD5 in shade to B5C5D8 lit)
    const DWORD fill = g_cube ? (g_dragging ? 0xFFF5802E : g_hover ? 0xFF92DC8E : 0xFFA9B4F0)
                              : (g_dragging ? 0xFFF07A29 : g_hover ? 0xFF8AD08A : 0xFFAABFFF);
    if (g_cube && draw_cube(device, at, dir, fill)) return;
    D3DRECT rects[2] = {
        {LONG(sq[0]) - kHalf - 2, LONG(sq[1]) - kHalf - 2, LONG(sq[0]) + kHalf + 2, LONG(sq[1]) + kHalf + 2},
        {LONG(sq[0]) - kHalf, LONG(sq[1]) - kHalf, LONG(sq[0]) + kHalf, LONG(sq[1]) + kHalf},
    };
    device->Clear(1, &rects[0], D3DCLEAR_TARGET, 0xFF1A2236, 1.f, 0);
    device->Clear(1, &rects[1], D3DCLEAR_TARGET, fill, 1.f, 0);
}

void tick()
{
    // the hook lives in mouse.cpp; keep it pointing here after hot reloads
    mouse::set_depth_input(input);
}

static int command(void*, int argc, const char* const* argv, SkOut* out)
{
    auto say = [&](const char* fmt, auto... a) { H->out_printf(out, fmt, a...); };
    if (argc >= 3 && !strcmp(argv[1], "open")) { // tests: open a saved creature in the stage editor
        uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit");
        uintptr_t alloc = H->sym("GeneralAllocator::new_");
        if (!ctor || !submit || !alloc) { say("not possible\n"); return 1; }
        void* req = cdecl_call<void*>(alloc, (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
        thiscall<void*>(ctor, req);
        field<uint32_t>(req, 0xC) = 0x465C50BAu;                 // CreatureEditorExtraLarge
        field<uint32_t>(req, 0x10) = (uint32_t)strtoul(argv[2], nullptr, 16);
        field<uint32_t>(req, 0x14) = 0x2B978C46u;                // creature
        field<uint32_t>(req, 0x18) = 0x40626200u;                // My Creations
        say("submitted %d\n", (int)cdecl_call<bool>(submit, req));
        return 0;
    }
    void* ed = nullptr;
    void* em = editor_model(&ed);
    if (em && argc >= 4 && !strcmp(argv[1], "nudge")) { // tests (BUG-9116): move a part along the handle's line
        void* rb = (void*)(uintptr_t)strtoul(argv[2], nullptr, 16);
        const auto parts = rigblocks(em);
        if (std::find(parts.begin(), parts.end(), rb) == parts.end() || why_not(rb)) { say("not a depth part\n"); return 1; }
        const Vec3 n = skin_normal(em, rb), spine = outward(em, rb), before = position(rb);
        const float amount = (float)atof(argv[3]);
        set_position(rb, before + n * amount);
        const Vec3 after = position(rb), moved = after - before;
        const float ml = moved.len();
        say("normal %.3f %.3f %.3f (spine line %.3f %.3f %.3f) moved %.4f %.4f %.4f, along the normal %.4f, sideways %.5f\n",
            n.x, n.y, n.z, spine.x, spine.y, spine.z, moved.x, moved.y, moved.z, dot(moved, n),
            ml > 0 ? (moved - n * dot(moved, n)).len() : 0.f);
        if (std::fabs(amount) > 1e-5f) H->run_outside_frame(commit, nullptr);
        return 0;
    }
    if (em && argc >= 3 && !strcmp(argv[1], "where")) { // tests: a part's anchor on screen (to click it)
        void* rb = (void*)(uintptr_t)strtoul(argv[2], nullptr, 16);
        const auto parts = rigblocks(em);
        if (std::find(parts.begin(), parts.end(), rb) == parts.end()) { say("no such part\n"); return 1; }
        const Vec3 p = position(rb);
        float s[2];
        if (!H->world_to_screen(&p.x, s)) { say("off screen\n"); return 1; }
        float ppu = 0; // screen pixels per world unit at the part (the camera's zoom there)
        for (const Vec3& axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
            const Vec3 q = p + axis * 0.05f;
            float t[2];
            if (H->world_to_screen(&q.x, t)) ppu = std::max(ppu, std::hypot(t[0] - s[0], t[1] - s[1]) / 0.05f);
        }
        say("at %.0f %.0f ppu %.1f\n", s[0], s[1], ppu);
        return 0;
    }
    say("fix=%d editor=%p dragging=%d moves=%u commits=%u square=%.0f %.0f last range %.3f..%.3f axis_fix=%d "
        "head_on_drags=%u presses_refused=%u\n", g_fix, em ? ed : nullptr,
        g_dragging, g_moves, g_commits, g_square_rb ? g_square[0] : -1.f, g_square_rb ? g_square[1] : -1.f, g_drag.lo, g_drag.hi,
        (int)g_axis_fix, g_axis_fallbacks, g_presses_refused);
    if (!em) return 0;
    void* sel = field<void*>(ed, 0xD4);
    if (argc >= 2 && !strcmp(argv[1], "parts")) {
        for (void* rb : rigblocks(em)) {
            const char* no = why_not(rb);
            const Vec3 p = position(rb);
            say("  %p part %08X %s pos %.3f %.3f %.3f%s\n", rb, field<uint32_t>(rb, 0x1C), no ? no : "depth", p.x, p.y, p.z,
                rb == sel ? "  (selected)" : "");
        }
        return 0;
    }
    if (sel) {
        const char* no = why_not(sel);
        const Vec3 p = position(sel), d = outward(em, sel);
        say("selected %p part %08X %s pos %.3f %.3f %.3f out %.3f %.3f %.3f twin %p\n", sel, field<uint32_t>(sel, 0x1C),
            no ? no : "depth", p.x, p.y, p.z, d.x, d.y, d.z, twin(em, sel));
    }
    return 0;
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    a_EditorPtr = H->sym("Editors::cEditor::ptr");
    a_GetBool = H->sym("App::Property::GetBool");
    a_GetInt = H->sym("App::Property::GetInt32");
    a_Commit = H->sym("Editors::cEditor::CommitEditHistory");
    a_GetProps = H->sym("App::IPropManager::Get");
    a_GetWM = H->sym("UTFWin::IWindowManager::Get"); // optional: without it the square also shows under dialogs
    H->command_add(mod, "coop.depth", "coop.depth [parts] - the creature editor depth square (tests, BUG-9103)", command,
                   nullptr, SK_CMD_MAIN_THREAD);
    const bool ok = a_EditorPtr && a_GetBool && a_GetInt && a_Commit && a_GetProps;
    if (!ok) g_fix = false;
    return ok;
}

} // namespace editor_depth
