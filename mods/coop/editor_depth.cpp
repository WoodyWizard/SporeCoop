// Creature editor depth handle (BUG-0871, docs/re/creations.md#creations-fork-editor-depth).
// Moves non-spine, non-limb, non-hand, non-foot parts along their outward skin normal (EditorRigblock +0xA8
// negated 2nd row, with nearest-spine fallback) by updating EditorRigblock +0x48 and its Graphics::Model +0x0C
// (plus its mirrored twin at +0x3E4), marking EditorModel +0x51 changed, and calling cEditor::CommitEditHistory
// once on release so Undo restores it and Save / Replace keep the moved position in the .bem.
// Does NOT touch IModelWorld::GetDeformationHandles (which crashes the editor's handle build).
#include "editor_depth.h"
#include "editor_depth_logic.h"
#include "fixes.h"
#include "sporekit_game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace editor_depth {

static const SkHost* H = nullptr;
static SkMod* M = nullptr;

static bool g_depth_fix = true;
COOP_FIX("editor.depth", "BUG-0871", g_depth_fix,
         "editor: depth handle in the Creature editor pushes details and weapons in or out along the skin normal");

static bool g_depth_cube_fix = true;
COOP_FIX("editor.depth_cube", "BUG-0871", g_depth_cube_fix,
         "editor: draw the Creature editor depth handle as a lit 3D cube on the overlay");

// Verified EP1 SporeApp.exe (dc04aee5) offsets & constants
constexpr uint32_t kCreatureEditorModelType = 0x9EA3031Au;
constexpr int kEditorModeBuild = 0;
constexpr int kMouseButtonLeft = 0x3E8; // 1000

// EditorRigblock property IDs (verified in EditorRigblock::LoadProperties 0x4425E4..0x442682 & Spore_Graphics.package)
constexpr uint32_t kPropIsVertebra = 0x2FF325CDu;       // rigblock flag bit 0
constexpr uint32_t kPropSocketConnector = 0x4FF31EECu;  // rigblock flag bit 31
constexpr uint32_t kPropBallConnector = 0x6FF31F12u;    // rigblock flag bit 32
constexpr uint32_t kPropCapLimb = 0xB00F0FDFu;
constexpr uint32_t kPropCapHand = 0xB00F0FE2u;
constexpr uint32_t kPropCapFoot = 0xB00F0FEFu;

// Resolved symbols
static uintptr_t a_EditorPtr = 0;
static uintptr_t a_CommitHistory = 0;
static uintptr_t a_GetBool = 0;
static uintptr_t a_GetInt32 = 0;
static uintptr_t a_WinMgrGet = 0;

// Mouse hooks on Editors::cEditor
typedef bool(SK_THISCALL* OnMouseDown_t)(void* ed, int button, float mx, float my, int state);
typedef bool(SK_THISCALL* OnMouseMove_t)(void* ed, float mx, float my, int state);
typedef bool(SK_THISCALL* OnMouseUp_t)(void* ed, int button, float mx, float my, int state);
static OnMouseDown_t o_OnMouseDown = nullptr;
static OnMouseMove_t o_OnMouseMove = nullptr;
static OnMouseUp_t o_OnMouseUp = nullptr;

// Live handle state (main thread only)
struct ActiveHandle {
    bool visible = false;
    void* editor = nullptr;
    void* model = nullptr;
    void* rigblock = nullptr;
    void* twin = nullptr;
    uint32_t part_inst = 0;
    uint32_t part_group = 0;
    Vec3 part_pos{};
    Vec3 normal{};
    Vec3 handle_pos{};
    float scale = 1.0f;
    DepthLimits limits{};
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float axis_dx = 0.0f;
    float axis_dy = 0.0f;
    float hit_radius = 14.0f;
    bool projected = false;
};

static ActiveHandle g_cur{};
static DepthTracker g_tracker{};
static bool g_hovered = false;
static bool g_dragging = false;
static bool g_drag_moved = false;
static float g_drag_start_mx = 0.0f;
static float g_drag_start_my = 0.0f;
static float g_drag_start_offset = 0.0f;
static float g_drag_axis_dx = 0.0f;
static float g_drag_axis_dy = 0.0f;
static Vec3 g_drag_normal{};
static DepthLimits g_drag_limits{};
static void* g_drag_rigblock = nullptr;
static unsigned g_commits = 0;
static unsigned g_cube_draws = 0;

static void cancel_drag()
{
    g_dragging = false;
    g_drag_moved = false;
    g_drag_rigblock = nullptr;
}

static bool modal_dialog_up()
{
    if (!a_WinMgrGet)
        return false;
    void* wm = skg::cdecl_call<void*>(a_WinMgrGet);
    if (!wm || !skg::readable((uint8_t*)wm + 0x82C, 4))
        return false;
    return *(void**)((uint8_t*)wm + 0x82C) != nullptr;
}

static void* current_editor()
{
    if (!a_EditorPtr || !skg::readable((void*)a_EditorPtr, 4))
        return nullptr;
    void* ed = *(void**)a_EditorPtr;
    if (!ed || !skg::readable((uint8_t*)ed, 0x320))
        return nullptr;
    return ed;
}

static bool collect_rigblocks(void* editor_model, std::vector<void*>& out)
{
    out.clear();
    if (!editor_model || !skg::readable((uint8_t*)editor_model, 0x60))
        return false;
    if (*(uint8_t*)((uint8_t*)editor_model + 0x2C) == 0)
        return false;
    uintptr_t beg = *(uintptr_t*)((uint8_t*)editor_model + 0x18);
    uintptr_t end = *(uintptr_t*)((uint8_t*)editor_model + 0x1C);
    if (!beg || end < beg || ((end - beg) & 3u) != 0)
        return false;
    size_t count = (end - beg) / 4;
    if (count == 0 || count > 2048)
        return false;
    if (!skg::readable((void*)beg, count * 4))
        return false;
    auto* ptrs = (void**)beg;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (ptrs[i] && skg::readable(ptrs[i], 0xDD0))
            out.push_back(ptrs[i]);
    }
    return !out.empty();
}

static bool rigblock_flag(void* rb, uint32_t bit)
{
    if (!rb || bit >= 60)
        return false;
    auto* words = (uint32_t*)((uint8_t*)rb + 0xDC8);
    return (words[bit >> 5] & (1u << (bit & 31u))) != 0;
}

static bool prop_bool_true(void* prop_list, uint32_t pid)
{
    if (!prop_list || !a_GetBool)
        return false;
    bool val = false;
    return skg::cdecl_call<bool>(a_GetBool, prop_list, pid, &val) && val;
}

static bool prop_int_nonzero(void* prop_list, uint32_t pid)
{
    if (!prop_list || !a_GetInt32)
        return false;
    int32_t val = 0;
    return skg::cdecl_call<bool>(a_GetInt32, prop_list, pid, &val) && val != 0;
}

static bool is_vertebra(void* rb)
{
    if (rigblock_flag(rb, 0))
        return true;
    void* pl = *(void**)((uint8_t*)rb + 0x0C);
    return prop_bool_true(pl, kPropIsVertebra);
}

static bool is_part_eligible(void* rb)
{
    if (!rb || !skg::readable(rb, 0xDD0))
        return false;
    void* pl = *(void**)((uint8_t*)rb + 0x0C);
    bool vert = is_vertebra(rb);
    bool sock = rigblock_flag(rb, 31) || prop_bool_true(pl, kPropSocketConnector);
    bool ball = rigblock_flag(rb, 32) || prop_bool_true(pl, kPropBallConnector);
    bool cap_limb = prop_int_nonzero(pl, kPropCapLimb);
    bool cap_hand = prop_int_nonzero(pl, kPropCapHand);
    bool cap_foot = prop_int_nonzero(pl, kPropCapFoot);
    return part_eligible(vert, ball, sock, cap_limb, cap_hand, cap_foot);
}

static Vec3 read_rigblock_pos(void* rb)
{
    auto* p = (float*)((uint8_t*)rb + 0x48);
    return {p[0], p[1], p[2]};
}

static void write_rigblock_pos(void* rb, Vec3 pos)
{
    auto* p = (float*)((uint8_t*)rb + 0x48);
    p[0] = pos.x;
    p[1] = pos.y;
    p[2] = pos.z;
    void* gfx = *(void**)((uint8_t*)rb + 0x10);
    if (gfx && skg::readable((uint8_t*)gfx + 0x08, 0x10)) {
        auto* mp = (float*)((uint8_t*)gfx + 0x0C);
        mp[0] = pos.x;
        mp[1] = pos.y;
        mp[2] = pos.z;
        *(uint16_t*)((uint8_t*)gfx + 0x08) |= 1u;
        *(uint16_t*)((uint8_t*)gfx + 0x0A) += 1u;
    }
}

static bool apply_depth_offset(void* editor, void* editor_model, void* rb, void* twin, float target_offset,
                               Vec3 normal, DepthLimits lim, bool commit)
{
    if (!editor || !editor_model || !rb)
        return false;
    Vec3 old_pos = read_rigblock_pos(rb);
    Vec3 new_pos{};
    g_tracker.apply(target_offset, normal, lim, new_pos);
    Vec3 delta = new_pos - old_pos;
    if (len_sq(delta) < 1e-10f && !commit)
        return false;
    write_rigblock_pos(rb, new_pos);
    if (twin && twin != rb && skg::readable(twin, 0xDD0)) {
        Vec3 twin_old = read_rigblock_pos(twin);
        Vec3 twin_new = twin_old + mirror_delta(delta);
        write_rigblock_pos(twin, twin_new);
    }
    *(uint8_t*)((uint8_t*)editor_model + 0x51) = 1;
    if (commit && a_CommitHistory) {
        skg::thiscall<void>(a_CommitHistory, editor, (uint32_t)1, (void*)nullptr);
        g_commits++;
    }
    return true;
}

static void refresh_handle()
{
    g_cur = ActiveHandle{};
    if (!g_depth_fix || modal_dialog_up()) {
        cancel_drag();
        return;
    }
    void* ed = current_editor();
    if (!ed) {
        cancel_drag();
        g_tracker.reset();
        return;
    }
    int mode = *(int*)((uint8_t*)ed + 0x31C);
    void* em = *(void**)((uint8_t*)ed + 0x98);
    if (mode != kEditorModeBuild || !em || !skg::readable((uint8_t*)em, 0x60)) {
        cancel_drag();
        return;
    }
    if (*(uint32_t*)((uint8_t*)em + 0x58) != kCreatureEditorModelType) {
        cancel_drag();
        return;
    }
    void* sel = *(void**)((uint8_t*)ed + 0xD4);
    if (!sel) {
        cancel_drag();
        return;
    }
    std::vector<void*> all;
    if (!collect_rigblocks(em, all)) {
        cancel_drag();
        return;
    }
    if (std::find(all.begin(), all.end(), sel) == all.end()) {
        cancel_drag();
        return;
    }
    if (!is_part_eligible(sel)) {
        cancel_drag();
        return;
    }
    std::vector<Vec3> spine;
    for (void* rb : all) {
        if (is_vertebra(rb))
            spine.push_back(read_rigblock_pos(rb));
    }
    Vec3 pos = read_rigblock_pos(sel);
    float scale = *(float*)((uint8_t*)sel + 0x1D8);
    if (!std::isfinite(scale) || scale < 0.05f || scale > 10.0f)
        scale = 1.0f;
    uint32_t pinst = *(uint32_t*)((uint8_t*)sel + 0x1C);
    uint32_t pgrp = *(uint32_t*)((uint8_t*)sel + 0x20);
    auto* pin = (const float*)((uint8_t*)sel + 0xA8);
    Vec3 norm = outward_normal(pin, pos, spine);
    float spine_dist = distance_to_spine(pos, spine);

    g_tracker.sync((uintptr_t)sel, pinst, pos, spine_dist);
    DepthLimits lim = compute_limits(g_tracker.base_depth, scale);

    void* twin = *(void**)((uint8_t*)sel + 0x3E4);
    if (twin == sel || std::find(all.begin(), all.end(), twin) == all.end())
        twin = nullptr;

    g_cur.visible = true;
    g_cur.editor = ed;
    g_cur.model = em;
    g_cur.rigblock = sel;
    g_cur.twin = twin;
    g_cur.part_inst = pinst;
    g_cur.part_group = pgrp;
    g_cur.part_pos = pos;
    g_cur.normal = norm;
    g_cur.scale = scale;
    g_cur.limits = lim;
    g_cur.handle_pos = handle_world_pos(pos, norm, scale);

    float sc_h[2] = {}, sc_p[2] = {}, sc_step[2] = {};
    float wp[3] = {pos.x, pos.y, pos.z};
    float wh[3] = {g_cur.handle_pos.x, g_cur.handle_pos.y, g_cur.handle_pos.z};
    Vec3 step_pt = pos + norm * 0.25f;
    float ws[3] = {step_pt.x, step_pt.y, step_pt.z};
    if (H && H->world_to_screen(wh, sc_h) && H->world_to_screen(wp, sc_p)) {
        g_cur.projected = true;
        g_cur.screen_x = sc_h[0];
        g_cur.screen_y = sc_h[1];
        if (H->world_to_screen(ws, sc_step)) {
            g_cur.axis_dx = sc_step[0] - sc_p[0];
            g_cur.axis_dy = sc_step[1] - sc_p[1];
        } else {
            g_cur.axis_dx = sc_h[0] - sc_p[0];
            g_cur.axis_dy = sc_h[1] - sc_p[1];
        }
        float mx = *(float*)((uint8_t*)ed + 0x28);
        float my = *(float*)((uint8_t*)ed + 0x2C);
        float dx = mx - g_cur.screen_x;
        float dy = my - g_cur.screen_y;
        g_hovered = (dx * dx + dy * dy) <= (g_cur.hit_radius * g_cur.hit_radius);
    } else {
        g_hovered = false;
    }

    if (g_dragging && g_drag_rigblock != sel)
        cancel_drag();
}

static bool hit_handle(float mx, float my)
{
    if (!g_cur.visible || !g_cur.projected)
        return false;
    float dx = mx - g_cur.screen_x;
    float dy = my - g_cur.screen_y;
    return (dx * dx + dy * dy) <= (g_cur.hit_radius * g_cur.hit_radius);
}

static bool SK_THISCALL h_OnMouseDown(void* ed, int button, float mx, float my, int state)
{
    if (g_depth_fix && button == kMouseButtonLeft) {
        refresh_handle();
        if (ed == g_cur.editor && hit_handle(mx, my)) {
            g_dragging = true;
            g_drag_moved = false;
            g_drag_rigblock = g_cur.rigblock;
            g_drag_start_mx = mx;
            g_drag_start_my = my;
            g_drag_start_offset = g_tracker.offset;
            g_drag_axis_dx = g_cur.axis_dx;
            g_drag_axis_dy = g_cur.axis_dy;
            g_drag_normal = g_cur.normal;
            g_drag_limits = g_cur.limits;
            return true; // swallow so the part stays selected
        }
    }
    return o_OnMouseDown(ed, button, mx, my, state);
}

static bool SK_THISCALL h_OnMouseMove(void* ed, float mx, float my, int state)
{
    if (g_depth_fix && g_dragging && ed == g_cur.editor) {
        refresh_handle();
        if (g_dragging && g_cur.visible && g_cur.rigblock == g_drag_rigblock) {
            float delta = drag_delta(mx - g_drag_start_mx, my - g_drag_start_my, g_drag_axis_dx, g_drag_axis_dy, 0.25f);
            float target = g_drag_start_offset + delta;
            if (apply_depth_offset(g_cur.editor, g_cur.model, g_cur.rigblock, g_cur.twin, target, g_drag_normal,
                                   g_drag_limits, false)) {
                g_drag_moved = true;
            }
            return true;
        }
    }
    bool r = o_OnMouseMove(ed, mx, my, state);
    if (g_depth_fix && g_cur.visible && g_cur.projected && ed == g_cur.editor) {
        g_hovered = hit_handle(mx, my);
    }
    return r;
}

static bool SK_THISCALL h_OnMouseUp(void* ed, int button, float mx, float my, int state)
{
    if (g_dragging && button == kMouseButtonLeft) {
        bool moved = g_drag_moved;
        void* commit_ed = g_cur.editor;
        void* commit_em = g_cur.model;
        void* commit_rb = g_cur.rigblock;
        void* commit_twin = g_cur.twin;
        float final_off = g_tracker.offset;
        Vec3 norm = g_drag_normal;
        DepthLimits lim = g_drag_limits;
        cancel_drag();
        if (g_depth_fix && moved && commit_ed == ed && commit_em && commit_rb) {
            apply_depth_offset(commit_ed, commit_em, commit_rb, commit_twin, final_off, norm, lim, true);
        }
        return true;
    }
    return o_OnMouseUp(ed, button, mx, my, state);
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    a_EditorPtr = H->sym("Editors::cEditor::ptr");
    a_CommitHistory = H->sym("Editors::cEditor::CommitEditHistory");
    a_GetBool = H->sym("App::Property::GetBool");
    a_GetInt32 = H->sym("App::Property::GetInt32");
    a_WinMgrGet = H->sym("UTFWin::IWindowManager::Get");

    if (uintptr_t a = H->sym("Editors::cEditor::OnMouseDown"))
        H->hook_create(M, (void*)a, (void*)h_OnMouseDown, (void**)&o_OnMouseDown);
    if (uintptr_t a = H->sym("Editors::cEditor::OnMouseMove"))
        H->hook_create(M, (void*)a, (void*)h_OnMouseMove, (void**)&o_OnMouseMove);
    if (uintptr_t a = H->sym("Editors::cEditor::OnMouseUp"))
        H->hook_create(M, (void*)a, (void*)h_OnMouseUp, (void**)&o_OnMouseUp);

    H->log(M, SK_INFO, "editor_depth: ready (editor_ptr=%u commit=%u mouse=%u)",
           (unsigned)(a_EditorPtr != 0), (unsigned)(a_CommitHistory != 0), (unsigned)(o_OnMouseDown != nullptr));
    return true;
}

void tick() { refresh_handle(); }

void draw()
{
    if (!H || !H->overlay_tris || !g_depth_fix || !g_cur.visible || !g_cur.projected)
        return;

    HandleState st = g_dragging ? kHeld : (g_hovered ? kHover : kDefault);

    // Subtle stem connecting part to handle when hovered or held
    if (g_hovered || g_dragging) {
        float sp[2] = {};
        float wp[3] = {g_cur.part_pos.x, g_cur.part_pos.y, g_cur.part_pos.z};
        if (H->world_to_screen(wp, sp)) {
            float dx = g_cur.screen_x - sp[0];
            float dy = g_cur.screen_y - sp[1];
            float d = std::sqrt(dx * dx + dy * dy);
            if (d > 2.0f) {
                float nx = -dy / d * 1.5f;
                float ny = dx / d * 1.5f;
                float quad[12] = {
                    sp[0] + nx,          sp[1] + ny,          sp[0] - nx,          sp[1] - ny,
                    g_cur.screen_x - nx, g_cur.screen_y - ny, sp[0] + nx,          sp[1] + ny,
                    g_cur.screen_x - nx, g_cur.screen_y - ny, g_cur.screen_x + nx, g_cur.screen_y + ny,
                };
                H->overlay_tris(quad, 2, cube_face_color(st, 0.2f));
            }
        }
    }

    if (!g_depth_cube_fix) {
        // Fallback flat 2D diamond when editor.depth_cube is off
        float r = 7.0f;
        float cx = g_cur.screen_x, cy = g_cur.screen_y;
        float tris[12] = {
            cx, cy - r, cx + r, cy, cx, cy + r,
            cx, cy - r, cx, cy + r, cx - r, cy,
        };
        H->overlay_tris(tris, 2, cube_face_color(st, 0.5f));
        return;
    }

    float half = 0.035f * std::clamp(g_cur.scale, 0.5f, 2.0f);
    Vec3 verts[8], normals[6];
    build_cube(g_cur.handle_pos, g_cur.normal, half, verts, normals);
    float proj[8][2] = {};
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    for (int i = 0; i < 8; ++i) {
        float w[3] = {verts[i].x, verts[i].y, verts[i].z};
        if (!H->world_to_screen(w, proj[i]))
            return;
        min_x = std::min(min_x, proj[i][0]);
        min_y = std::min(min_y, proj[i][1]);
        max_x = std::max(max_x, proj[i][0]);
        max_y = std::max(max_y, proj[i][1]);
    }
    float span = std::max(max_x - min_x, max_y - min_y);
    g_cur.hit_radius = std::clamp(span * 0.65f + 6.0f, 12.0f, 36.0f);

    Vec3 light_dir = normalize_or({0.45f, 0.75f, 0.48f}, {0.0f, 1.0f, 0.0f});
    for (int f = 0; f < 6; ++f) {
        const auto& idx = kCubeFaces[f];
        float x0 = proj[idx[0]][0], y0 = proj[idx[0]][1];
        float x1 = proj[idx[1]][0], y1 = proj[idx[1]][1];
        float x2 = proj[idx[2]][0], y2 = proj[idx[2]][1];
        float x3 = proj[idx[3]][0], y3 = proj[idx[3]][1];
        // In Y-down screen space, outward-CCW front faces have negative 2D cross product
        float cross2d = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (cross2d >= 0.0f)
            continue;
        float tris[12] = {x0, y0, x1, y1, x2, y2, x0, y0, x2, y2, x3, y3};
        uint32_t col = cube_face_color(st, dot(normals[f], light_dir));
        H->overlay_tris(tris, 2, col);
    }
    g_cube_draws++;
}

void shutdown()
{
    cancel_drag();
    g_tracker.reset();
    g_cur = ActiveHandle{};
}

std::string debug(const std::vector<std::string>& args)
{
    refresh_handle();
    if (!args.empty() && args[0] == "list") {
        void* ed = current_editor();
        void* em = ed ? *(void**)((uint8_t*)ed + 0x98) : nullptr;
        std::vector<void*> all;
        if (!em || !collect_rigblocks(em, all))
            return "editor_depth: no creature editor model";
        std::string out;
        char line[256];
        snprintf(line, sizeof(line), "editor_depth: model_type=0x%08X rigblocks=%u\n",
                 *(uint32_t*)((uint8_t*)em + 0x58), (unsigned)all.size());
        out += line;
        for (size_t i = 0; i < all.size(); ++i) {
            void* rb = all[i];
            Vec3 p = read_rigblock_pos(rb);
            float sc[2] = {};
            float w[3] = {p.x, p.y, p.z};
            bool on_sc = H && H->world_to_screen(w, sc);
            void* tw = *(void**)((uint8_t*)rb + 0x3E4);
            int tw_idx = -1;
            if (tw && tw != rb) {
                auto it = std::find(all.begin(), all.end(), tw);
                if (it != all.end())
                    tw_idx = (int)(it - all.begin());
            }
            snprintf(line, sizeof(line),
                     "  [%u] part=%08X!%08X elig=%d vert=%d pos=(%.4f,%.4f,%.4f) twin=%d screen=%s(%.1f,%.1f)\n",
                     (unsigned)i, *(uint32_t*)((uint8_t*)rb + 0x20), *(uint32_t*)((uint8_t*)rb + 0x1C),
                     (int)is_part_eligible(rb), (int)is_vertebra(rb), p.x, p.y, p.z, tw_idx, on_sc ? "ok" : "off",
                     sc[0], sc[1]);
            out += line;
        }
        return out;
    }

    if (!args.empty() && args[0] == "select" && args.size() >= 2) {
        void* ed = current_editor();
        void* em = ed ? *(void**)((uint8_t*)ed + 0x98) : nullptr;
        std::vector<void*> all;
        if (!em || !collect_rigblocks(em, all))
            return "editor_depth: no creature editor model";
        size_t idx = (size_t)std::strtoul(args[1].c_str(), nullptr, 10);
        if (idx >= all.size())
            return "editor_depth: index out of range";
        // cEditor +0xD4 is an intrusive_ptr<EditorRigblock> (Object vtable: +0x04 AddRef, +0x08 Release)
        void* old_sel = *(void**)((uint8_t*)ed + 0xD4);
        void* new_sel = all[idx];
        if (old_sel != new_sel) {
            if (new_sel && skg::readable(new_sel, 4)) {
                auto* vt = *(uintptr_t**)new_sel;
                if (vt && skg::readable(vt, 12) && vt[1])
                    skg::thiscall<int>(vt[1], new_sel);
            }
            *(void**)((uint8_t*)ed + 0xD4) = new_sel;
            if (old_sel && skg::readable(old_sel, 4)) {
                auto* vt = *(uintptr_t**)old_sel;
                if (vt && skg::readable(vt, 12) && vt[2])
                    skg::thiscall<int>(vt[2], old_sel);
            }
        }
        refresh_handle();
        char buf[192];
        snprintf(buf, sizeof(buf), "editor_depth: selected [%u] part=%08X visible=%d pos=(%.4f,%.4f,%.4f)",
                 (unsigned)idx, *(uint32_t*)((uint8_t*)all[idx] + 0x1C), (int)g_cur.visible, g_cur.part_pos.x,
                 g_cur.part_pos.y, g_cur.part_pos.z);
        return buf;
    }

    if (!args.empty() && args[0] == "nudge" && args.size() >= 2) {
        if (!g_depth_fix)
            return "editor_depth: disabled (editor.depth off)";
        if (!g_cur.visible)
            return "editor_depth: no eligible selected part";
        float delta = std::strtof(args[1].c_str(), nullptr);
        if (!std::isfinite(delta))
            return "editor_depth: invalid delta";
        float target = g_tracker.offset + delta;
        apply_depth_offset(g_cur.editor, g_cur.model, g_cur.rigblock, g_cur.twin, target, g_cur.normal, g_cur.limits,
                           true);
        refresh_handle();
        Vec3 tp = g_cur.twin ? read_rigblock_pos(g_cur.twin) : Vec3{};
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "editor_depth: nudged offset=%.4f pos=(%.4f,%.4f,%.4f) twin=%d twin_pos=(%.4f,%.4f,%.4f) commits=%u",
                 g_tracker.offset, g_cur.part_pos.x, g_cur.part_pos.y, g_cur.part_pos.z, (int)(g_cur.twin != nullptr),
                 tp.x, tp.y, tp.z, g_commits);
        return buf;
    }

    void* ed = current_editor();
    void* em = ed ? *(void**)((uint8_t*)ed + 0x98) : nullptr;
    uint32_t mtype = (em && skg::readable((uint8_t*)em + 0x58, 4)) ? *(uint32_t*)((uint8_t*)em + 0x58) : 0;
    int emode = (ed && skg::readable((uint8_t*)ed + 0x31C, 4)) ? *(int*)((uint8_t*)ed + 0x31C) : -1;
    Vec3 tp = g_cur.twin ? read_rigblock_pos(g_cur.twin) : Vec3{};
    Vec3 mp{};
    if (g_cur.rigblock) {
        void* gfx = *(void**)((uint8_t*)g_cur.rigblock + 0x10);
        if (gfx && skg::readable((uint8_t*)gfx + 0x0C, 12)) {
            auto* f = (float*)((uint8_t*)gfx + 0x0C);
            mp = {f[0], f[1], f[2]};
        }
    }
    char buf[512];
    snprintf(buf, sizeof(buf),
             "editor_depth: fix=%d cube=%d visible=%d editor=%d mode=%d model_type=0x%08X part=%08X "
             "pos=(%.4f,%.4f,%.4f) model_pos=(%.4f,%.4f,%.4f) normal=(%.4f,%.4f,%.4f) "
             "twin=%d twin_pos=(%.4f,%.4f,%.4f) offset=%.4f min=%.4f max=%.4f "
             "handle_screen=%d(%.1f,%.1f) axis_screen=(%.1f,%.1f) dragging=%d commits=%u draws=%u",
             (int)g_depth_fix, (int)g_depth_cube_fix, (int)g_cur.visible, (int)(ed != nullptr), emode, mtype,
             g_cur.part_inst, g_cur.part_pos.x, g_cur.part_pos.y, g_cur.part_pos.z, mp.x, mp.y, mp.z, g_cur.normal.x,
             g_cur.normal.y, g_cur.normal.z, (int)(g_cur.twin != nullptr), tp.x, tp.y, tp.z, g_tracker.offset,
             g_cur.limits.min_offset, g_cur.limits.max_offset, (int)g_cur.projected, g_cur.screen_x, g_cur.screen_y,
             g_cur.axis_dx, g_cur.axis_dy, (int)g_dragging, g_commits, g_cube_draws);
    return buf;
}

} // namespace editor_depth
