#include "sui.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fixes.h"
#include "sporekit_game.hpp"

using skg::thiscall;
using skg::vcall;

namespace sui {

static const SkHost* H;
static SkMod* M;
static uintptr_t a_slider_create;
static uintptr_t a_wm_get, a_btn_create, a_te_create, a_layout_ctor, a_layout_load, a_layout_container,
    a_image_get, a_obj_new, a_get_alloc, a_std_ctor, a_info_ctor;

// ---- UILayout::Load hook: which layouts the game loads (devkit "coop.ui layouts") ----
struct LayoutRec {
    uint32_t instance, type, group;
};
static std::vector<LayoutRec> g_layouts;
using LayoutLoad_t = bool(SK_THISCALL*)(void*, const uint32_t*, bool, uint32_t);
static LayoutLoad_t o_LayoutLoad;
static bool SK_THISCALL h_LayoutLoad(void* self, const uint32_t* key, bool insert, uint32_t param)
{
    if (key) {
        g_layouts.push_back({key[0], key[1], key[2]});
        if (g_layouts.size() > 400)
            g_layouts.erase(g_layouts.begin());
    }
    return o_LayoutLoad(self, key, insert, param);
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    a_wm_get = H->sym("UTFWin::IWindowManager::Get");
    a_btn_create = H->sym("UTFWin::IButton::Create");
    a_te_create = H->sym("UTFWin::ITextEdit::Create");
    a_slider_create = H->sym("UTFWin::ISlider::Create");
    a_layout_ctor = H->sym("UTFWin::UILayout::UILayout");
    a_layout_load = H->sym("UTFWin::UILayout::Load");
    a_layout_container = H->sym("UTFWin::UILayout::GetContainerWindow");
    a_image_get = H->sym("UTFWin::Image::GetImage");
    a_obj_new = H->sym("UTFWin::UTFWinObject::new_");
    a_get_alloc = H->sym("UTFWin::GetAllocator");
    a_std_ctor = H->sym("UTFWin::SporeStdDrawable::SporeStdDrawable");
    a_info_ctor = H->sym("UTFWin::SporeStdDrawableImageInfo::SporeStdDrawableImageInfo");
    if (a_layout_load)
        H->hook_create(M, (void*)a_layout_load, (void*)h_LayoutLoad, (void**)&o_LayoutLoad);
    bool ok = a_wm_get && a_btn_create && a_te_create && a_layout_ctor && o_LayoutLoad && a_image_get &&
              a_obj_new && a_get_alloc && a_std_ctor && a_info_ctor && H->size >= sizeof(SkHost) &&
              H->ui_winproc;
    if (!ok)
        H->log(M, SK_WARN, "sui: Spore UI symbols missing, native UI disabled");
    return ok;
}

uint32_t fnv(const char* s)
{
    uint32_t h = 0x811C9DC5;
    for (; *s; s++) {
        h *= 0x1000193;
        h ^= (uint8_t)tolower((unsigned char)*s);
    }
    return h;
}

static std::string utf8(const char16_t* s)
{
    if (!s || !*s)
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)s, -1, nullptr, 0, nullptr, nullptr);
    std::string r(n > 0 ? n - 1 : 0, '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)s, -1, r.data(), n, nullptr, nullptr);
    return r;
}

static std::u16string utf16(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::u16string r(n > 0 ? n - 1 : 0, u'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, (wchar_t*)r.data(), n);
    return r;
}

// a UTFWin object has its vtable inside SporeApp.exe
static bool valid(Win w)
{
    if (!w || IsBadReadPtr(w, 8))
        return false;
    uintptr_t vt = *(uintptr_t*)w, base = H->image_base();
    return vt > base && vt < base + 0x2000000;
}

// ---------------- reading ----------------
Win main_window()
{
    void* wm = a_wm_get ? skg::cdecl_call<void*>(a_wm_get) : nullptr;
    return wm ? vcall<Win>(wm, 0x04 / 4) : nullptr;
}

Win window_at(float x, float y)
{
    void* wm = a_wm_get ? skg::cdecl_call<void*>(a_wm_get) : nullptr;
    float pt[2] = {x, y};
    return wm ? vcall<Win>(wm, 0x44 / 4, (void*)pt) : nullptr;
}

// Window::mChildrenWindows: intrusive list anchor at IWindow+0x38 in this build (ModAPI: +0x3C);
// a child's list node is at child+4
std::vector<Win> children(Win w)
{
    std::vector<Win> r;
    if (!w)
        return r;
    uint8_t* anchor = (uint8_t*)w + 0x38;
    uint8_t* n = *(uint8_t**)anchor;
    for (int i = 0; n && n != anchor && i < 4096 && !IsBadReadPtr(n, 8); i++) {
        if (valid(n - 4))
            r.push_back(n - 4);
        n = *(uint8_t**)n;
    }
    return r;
}

Win parent(Win w) { return w ? vcall<Win>(w, 0x10 / 4) : nullptr; }
const char* type_name(Win w)
{
    const char* n = w ? vcall<const char*>(w, 0x118 / 4) : nullptr;
    return n ? n : "-";
}
uint32_t control_id(Win w) { return w ? vcall<uint32_t>(w, 0x1C / 4) : 0; }
uint32_t command_id(Win w) { return w ? vcall<uint32_t>(w, 0x20 / 4) : 0; }
uint32_t flags(Win w) { return w ? vcall<uint32_t>(w, 0x28 / 4) : 0; }
std::string caption(Win w) { return w ? utf8(vcall<const char16_t*>(w, 0x3C / 4)) : std::string(); }
Rect area(Win w) { return w ? *vcall<const Rect*>(w, 0x34 / 4) : Rect{}; }
Rect real_area(Win w) { return w ? *vcall<const Rect*>(w, 0x38 / 4) : Rect{}; }
void* drawable(Win w) { return w ? vcall<void*>(w, 0xA8 / 4) : nullptr; }
// real_area is the laid out position relative to the parent; area is the layout input
// (anchored windows can have it relative to the right/bottom edge)
Rect screen_rect(Win w)
{
    Rect r = real_area(w);
    for (Win p = parent(w); p; p = parent(p)) {
        Rect a = real_area(p);
        r.x1 += a.x1; r.x2 += a.x1;
        r.y1 += a.y1; r.y2 += a.y1;
    }
    return r;
}
bool visible(Win w) { return w && (flags(w) & kFlagVisible); }
uint32_t font(Win w) { return w ? vcall<uint32_t>(w, 0x40 / 4) : 0; }

Win find(Win root, uint32_t id)
{
    if (!root)
        return nullptr;
    if (control_id(root) == id)
        return root;
    for (Win c : children(root))
        if (Win f = find(c, id))
            return f;
    return nullptr;
}

Win find_caption(Win root, const std::string& text)
{
    if (!root)
        return nullptr;
    if (caption(root).find(text) != std::string::npos)
        return root;
    for (Win c : children(root))
        if (Win f = find_caption(c, text))
            return f;
    return nullptr;
}

static void dump_rec(Win w, int depth, int max_depth, bool only_visible, std::string& out)
{
    if (only_visible && !visible(w))
        return;
    Rect a = real_area(w);
    char b[512];
    void* d = drawable(w);
    snprintf(b, sizeof b, "%*s%s %p id=0x%08X cmd=0x%08X fl=0x%X [%.0f %.0f %.0f %.0f] drw=%p vt=%p font=%08X '%s'\n",
             depth * 2, "", type_name(w), w, control_id(w), command_id(w), flags(w), a.x1, a.y1, a.x2, a.y2, d,
             d ? *(void**)d : nullptr, font(w), caption(w).c_str());
    out += b;
    if (depth >= max_depth)
        return;
    for (Win c : children(w))
        dump_rec(c, depth + 1, max_depth, only_visible, out);
}

std::string dump(Win root, int max_depth, bool only_visible)
{
    std::string s;
    if (root)
        dump_rec(root, 0, max_depth, only_visible, s);
    return s;
}

// ---------------- changing ----------------
void add_ref(Win w) { if (w) vcall<int>(w, 0); }
void release(Win w) { if (w) vcall<int>(w, 1); }
// the object's reference count now (AddRef's answer minus the one it added, given back at once): the game's own
// count of a live window, image or drawable (Object: AddRef slot 0, Release slot 1)
int refs(void* o)
{
    if (!o)
        return 0;
    int n = vcall<int>(o, 0) - 1;
    vcall<int>(o, 1);
    return n;
}

// BUG-1025: every window the mod made kept the mod's own reference for good (new_button / text_edit take one more
// on top of the creation's, the keep-alive of the mod's tables), a handler's creation reference was never given
// back and a layout object of a failed load leaked. With the switch the mod gives back what it holds when it drops
// a window (the notification panels' labels, the main menu's item and its version line, an older pause menu
// button), the handler's own reference once the window took one, the menu icon's image reference after the
// drawable took its own, and a failed layout object is used again. Off (`coop.fix core.ui_release off`): 0.7.13.
bool g_release_fix = true;
COOP_FIX("core.ui_release", "BUG-1025", g_release_fix, "ui: windows, handlers and images the mod drops are given back to the game (no leak per notification)");
void set_caption(Win w, const std::string& t)
{
    std::u16string u = utf16(t);
    vcall<void>(w, 0x80 / 4, u.c_str());
}
void set_area(Win w, const Rect& r) { vcall<void>(w, 0x60 / 4, &r); }
void set_layout_area(Win w, const Rect& r) { vcall<void>(w, 0x6C / 4, &r); }
void set_flag(Win w, uint32_t f, bool on) { vcall<void>(w, 0x7C / 4, f, on); }
void set_visible(Win w, bool on) { set_flag(w, kFlagVisible, on); }
void set_enabled(Win w, bool on) { set_flag(w, kFlagEnabled, on); }
void set_ids(Win w, uint32_t cid, uint32_t cmd)
{
    vcall<void>(w, 0x50 / 4, cid);
    vcall<void>(w, 0x54 / 4, cmd);
}
void set_font(Win w, uint32_t f) { vcall<void>(w, 0x84 / 4, f); }
void set_shade_color(Win w, uint32_t argb) { vcall<void>(w, 0x5C / 4, argb); }
void set_fill_color(Win w, uint32_t argb) { vcall<void>(w, 0xAC / 4, argb); }
uint32_t fill_color(Win w) { return vcall<uint32_t>(w, 0xA4 / 4); }
void add_child(Win p, Win c) { vcall<void>(p, 0xD8 / 4, c); }
void remove_child(Win p, Win c) { vcall<void>(p, 0xDC / 4, c); }
void bring_to_front(Win p, Win c) { vcall<void>(p, 0xE8 / 4, c); }
void set_drawable(Win w, void* d) { vcall<void>(w, 0xB0 / 4, d); }

// ---------------- layouts ----------------
Win load_layout(uint32_t instance, uint32_t group)
{
    if (!o_LayoutLoad)
        return nullptr;
    Win layer = nullptr;
    // the default parameter puts the layout into the main UI layer: remember its children
    for (Win c : children(main_window()))
        if (control_id(c) == 0x05B598FA)
            layer = c;
    std::vector<Win> before = children(layer);
    // the UILayout object stays alive for the whole session (the windows belong to it). BUG-1025: one whose load
    // failed is used for the next load (it was calloc'd: never given to the game's allocator to free)
    static void* s_spare;
    void* l = g_release_fix && s_spare ? s_spare : nullptr;
    s_spare = nullptr;
    if (!l) {
        l = calloc(1, 0x40);
        thiscall<void*>(a_layout_ctor, l);
        vcall<int>(l, 1); // AddRef (DefaultRefCounted: [0] is the destructor)
    }
    uint32_t key[3] = {instance, 0x0510A95B, group};
    if (!o_LayoutLoad(l, key, true, 0x5B598FA)) {
        s_spare = l;
        return nullptr;
    }
    if (!layer)
        layer = thiscall<Win>(a_layout_container, l);
    for (Win c : children(layer)) {
        bool old = false;
        for (Win b : before)
            old |= b == c;
        if (!old)
            return c;
    }
    return nullptr;
}

// ---------------- widgets ----------------
void* as_button(Win w) { return w ? vcall<void*>(w, 0x0C / 4, 0x8ED27E7Au) : nullptr; }
static void* as_edit(Win w) { return w ? vcall<void*>(w, 0x0C / 4, 0xCF428691u) : nullptr; }

// this build: IWindow* __stdcall Create(int, ICoreAllocator* = nullptr -> UTFWin default)
using CreateFn = void*(SK_STDCALL*)(int, void*);

static Win new_button()
{
    Win w = ((CreateFn)a_btn_create)(0, nullptr);
    if (w)
        add_ref(w); // ours; the parent takes its own
    return w;
}

Win button_like(Win parent, Win like, const Rect& r, const std::string& text)
{
    void* src = as_button(like);
    Win w = src ? new_button() : nullptr;
    void* b = as_button(w);
    if (!b)
        return nullptr;
    vcall<void>(b, 0x1C / 4, vcall<int>(src, 0x18 / 4)); // button type
    vcall<void>(b, 0x30 / 4, vcall<int>(src, 0x2C / 4)); // button flags
    vcall<void>(b, 0x3C / 4, vcall<int>(src, 0x38 / 4)); // alignment
    for (uint32_t i = 0; i < 8; i++)
        vcall<void>(b, 0x4C / 4, i, vcall<uint32_t>(src, 0x48 / 4, i)); // caption colors
    // this build: borders/offset as separate floats (ModAPI: Rectangle&/Point&)
    float bl = 0, bt = 0, br = 0, bb = 0;
    vcall<void>(src, 0x50 / 4, &bl, &bt, &br, &bb);
    vcall<void>(b, 0x54 / 4, bl, bt, br, bb);
    float ox = 0, oy = 0;
    vcall<void>(src, 0x58 / 4, &ox, &oy);
    vcall<void>(b, 0x5C / 4, ox, oy);
    vcall<void>(b, 0x6C / 4, vcall<void*>(src, 0x68 / 4)); // text outline
    if (void* d = drawable(like))
        set_drawable(w, d); // the same drawable object (images per state), ref counted
    set_font(w, font(like));
    set_shade_color(w, vcall<uint32_t>(like, 0x30 / 4));
    vcall<void>(w, 0xAC / 4, vcall<uint32_t>(like, 0xA4 / 4)); // fill color
    vcall<void>(w, 0x78 / 4, vcall<uint32_t>(like, 0x24 / 4)); // cursor
    set_area(w, r);
    set_caption(w, text);
    set_visible(w, true);
    set_enabled(w, true);
    if (parent)
        add_child(parent, w);
    return w;
}

void set_label_color(Win w, uint32_t argb)
{
    if (void* b = as_button(w))
        for (uint32_t i = 0; i < 8; i++)
            vcall<void>(b, 0x4C / 4, i, argb);
}

void center_caption(Win w)
{
    void* b = as_button(w);
    if (!b)
        return;
    vcall<void>(b, 0x54 / 4, 1.f, 1.f, 1.f, 1.f); // borders
    vcall<void>(b, 0x5C / 4, 0.f, 0.f);           // caption offset
}

Win label(Win parent, const Rect& r, const std::string& text, uint32_t font_id, uint32_t argb, int align)
{
    Win w = new_button();
    void* b = as_button(w);
    if (!b)
        return nullptr;
    // type 3 (like the main menu items) honours the alignment; type 1 always centers
    vcall<void>(b, 0x1C / 4, align == kAlignCenter ? 1 : 3);
    vcall<void>(b, 0x3C / 4, align);
    vcall<void>(b, 0x54 / 4, 1.f, 1.f, 1.f, 1.f);
    vcall<void>(b, 0x5C / 4, 0.f, 0.f);
    set_label_color(w, argb);
    if (font_id)
        set_font(w, font_id);
    set_area(w, r);
    set_caption(w, text);
    set_flag(w, kFlagIgnoreMouse, true);
    set_visible(w, true);
    set_enabled(w, true);
    if (parent)
        add_child(parent, w);
    return w;
}

Win text_edit(Win parent, const Rect& r, const std::string& text, uint32_t font_id, int max_len, bool draw_background)
{
    Win w = ((CreateFn)a_te_create)(0, nullptr);
    void* e = as_edit(w);
    if (!e)
        return nullptr;
    add_ref(w);
    if (font_id)
        vcall<void>(e, 0x18 / 4, font_id); // SetTextStyleID
    const uint32_t colors[8] = {0xFFFFFFFF, draw_background ? 0xD0081828u : 0u,
                                       0xFF808080, draw_background ? 0x80081828u : 0u,
                                       0xFF000000, 0xFF9BD8FF, 0xFFFFFFFF, 0xFFFFFFFF};
    for (uint32_t i = 0; i < 8; i++)
        vcall<void>(e, 0x20 / 4, i, colors[i]);
    Rect border{8, 5, 8, 5};
    vcall<void>(e, 0x28 / 4, &border);
    vcall<void>(e, 0x68 / 4, max_len);
    set_area(w, r);
    set_visible(w, true);
    set_enabled(w, true);
    if (parent)
        add_child(parent, w);
    set_edit_text(w, text);
    return w;
}

// ---------------- sliders ----------------
static void* utf_new(size_t n);
static void obj_addref(void* o);
// ISlider (ModAPI order; checked live 2026-11-13 on the Audio page's sliders): 0x14 SetOrientation(1 horizontal, 2
// vertical), 0x18 GetOrientation, 0x1C SetValue(v, notify), 0x20 GetValue, 0x24 SetMinValue, 0x28 GetMinValue, 0x2C
// SetMaxValue, 0x30 GetMaxValue, 0x34 SetSliderDrawable, 0x38 GetSliderDrawable
void* as_slider(Win w) { return w ? vcall<void*>(w, 0x0C / 4, 0x0F00A8A0u) : nullptr; }

Win slider(Win parent, const Rect& r, int min, int max, int value, bool horizontal, void* drawable)
{
    if (!a_slider_create)
        return nullptr;
    Win w = ((CreateFn)a_slider_create)(0, nullptr);
    void* s = as_slider(w);
    if (!s)
        return nullptr;
    add_ref(w);
    vcall<void>(s, 0x14 / 4, horizontal ? 1 : 2);
    if (drawable)
        vcall<void>(s, 0x34 / 4, drawable);
    vcall<void>(s, 0x24 / 4, min, 0);
    vcall<void>(s, 0x2C / 4, max, 0);
    vcall<void>(s, 0x1C / 4, value, 0);
    set_area(w, r);
    set_visible(w, true);
    set_enabled(w, true);
    if (parent)
        add_child(parent, w);
    return w;
}

void* slider_drawable_of(Win w)
{
    void* s = as_slider(w);
    return s ? vcall<void*>(s, 0x38 / 4) : nullptr;
}

void* slider_drawable_with(void* like, void* background, void* thumb, void* container)
{
    // the ISliderDrawable interface sits at +0xC of a SliderDrawable (read live 2026-11-13: +0 IDrawable vtable, +4
    // DefaultLayoutElement vtable, +8 the reference count, +0xC ISliderDrawable vtable, +0x10 images[3], +0x1C a float
    // 1.4, +0x20 / +0x24 zero); ModAPI's 0x1C bytes are too few in this build: 0x28 copied into a 0x40 block
    if (!like || IsBadReadPtr((uint8_t*)like - 0xC, 0x28))
        return nullptr;
    uint8_t* src = (uint8_t*)like - 0xC;
    uint8_t* d = (uint8_t*)utf_new(0x40);
    if (!d)
        return nullptr;
    memset(d, 0, 0x40);
    memcpy(d, src, 0x28);
    *(int*)(d + 8) = 0; // nobody holds it yet (set_slider_drawable takes a reference)
    void* const images[3] = {background, thumb, container};
    for (int i = 0; i < 3; i++) {
        void* img = images[i] ? images[i] : *(void**)(src + 0x10 + i * 4);
        if (!images[i])
            obj_addref(img); // `like`'s, now held twice; ours come with the caller's reference
        *(void**)(d + 0x10 + i * 4) = img;
    }
    return d + 0xC;
}

void invalidate_transform(Win w)
{
    if (w) vcall<int>(w, 0x98 / 4);
}

void revalidate(Win w)
{
    if (w) vcall<void>(w, 0x94 / 4);
}

void* as_combo(Win w) { return w ? vcall<void*>(w, 0x0C / 4, 0x2F5528D9u) : nullptr; }

std::vector<std::string> combo_values(Win w)
{
    std::vector<std::string> out;
    void* c = as_combo(w);
    if (!c) return out;
    const int n = vcall<int>(c, 0x3C / 4);
    for (int i = 0; i < n && i < 512; i++) {
        // GetValue returns an eastl::string16 by value (a hidden result pointer first); its buffer is the game's
        // allocator's and stays (a few bytes per call, only while the Settings dialog is built)
        uint32_t buf[8] = {};
        vcall<void*>(c, 0x34 / 4, (void*)buf, i);
        const char16_t* b = (const char16_t*)(uintptr_t)buf[0];
        const char16_t* e = (const char16_t*)(uintptr_t)buf[1];
        if (b && e >= b && e - b < 256 && !IsBadReadPtr(b, (e - b) * 2))
            out.push_back(utf8(std::u16string(b, e).c_str()));
        else
            out.emplace_back();
    }
    return out;
}

int combo_selected(Win w)
{
    void* c = as_combo(w);
    return c ? vcall<int>(c, 0x40 / 4) : -1;
}

void combo_select(Win w, int index)
{
    if (void* c = as_combo(w)) vcall<void>(c, 0x44 / 4, index, true);
}

void combo_set_values(Win w, const std::vector<std::string>& values)
{
    void* c = as_combo(w);
    if (!c) return;
    vcall<void>(c, 0x2C / 4); // ClearValues
    for (auto& v : values) {
        const std::u16string u = utf16(v);
        vcall<void>(c, 0x20 / 4, u.c_str()); // AddValue
    }
}

bool has_own_transform(Win w)
{
    const uint8_t* p = (const uint8_t*)w;
    if (!w || IsBadReadPtr(p + 0xC0, 0x44)) return false;
    return *(const int*)(p + 0x100) != 0;
}

bool render_xform(Win w, float& sx, float& sy, float& tx, float& ty)
{
    const uint8_t* p = (const uint8_t*)w;
    if (!w || IsBadReadPtr(p + 0x148, 0x44)) return false;
    const float* m = (const float*)(p + 0x148);
    const int type = *(const int*)(p + 0x188);
    if (type != 0 && type != 1) return false;
    sx = m[0];
    sy = m[5];
    tx = m[12];
    ty = m[13];
    return sx > 0.01f && sy > 0.01f;
}

static bool g_drawn_rect_fix = true;
COOP_FIX("ui.drawn_rect", "BUG-9238", g_drawn_rect_fix,
         "ui: the mod places its marks and clicks by where Spore's windows are drawn (Interface Size), not by their layout");
Rect drawn_rect(Win w)
{
    const Rect r = screen_rect(w);
    if (!g_drawn_rect_fix || !w) return r;
    const uint8_t* p = (const uint8_t*)w;
    if (IsBadReadPtr(p + 0x148, 0x44) || *(const int*)(p + 0x188) != 1) return r; // (identity: drawn at its layout)
    float sx, sy, tx, ty;
    if (!render_xform(w, sx, sy, tx, ty) || !std::isfinite(tx) || !std::isfinite(ty)) return r;
    const Rect a = real_area(w);
    return Rect{tx, ty, tx + a.w() * sx, ty + a.h() * sy};
}

bool button_selected(Win w)
{
    void* b = as_button(w);
    return b && (vcall<int>(b, 0x20 / 4) & 4);
}

void set_button_selected(Win w, bool on)
{
    if (void* b = as_button(w))
        vcall<void>(b, 0x28 / 4, 4, on);
}

Win box(Win parent, const Rect& r, void* drawable)
{
    Win w = label(parent, r, "", 0, 0, kAlignLeft);
    if (w && drawable)
        set_drawable(w, drawable);
    return w;
}

int slider_value(Win w)
{
    void* s = as_slider(w);
    return s ? vcall<int>(s, 0x20 / 4) : 0;
}

void set_slider_value(Win w, int v)
{
    if (void* s = as_slider(w))
        vcall<void>(s, 0x1C / 4, v, 0);
}

void set_edit_masked(Win w, bool masked)
{
    constexpr int kPasswordMode = 0x80;
    if (void* e = as_edit(w))
        vcall<void>(e, 0x44 / 4, kPasswordMode, masked); // SetTextEditFlag
}

std::string edit_text(Win w)
{
    void* e = as_edit(w);
    return e ? utf8(vcall<const char16_t*>(e, 0x54 / 4)) : std::string();
}

void set_edit_text(Win w, const std::string& t)
{
    if (void* e = as_edit(w)) {
        std::u16string u = utf16(t);
        vcall<int>(e, 0x60 / 4, u.c_str(), 0);
    }
}

// ---------------- images / drawables ----------------
Win image_window(Win parent, uint32_t instance, uint32_t group, float x, float y)
{
    uintptr_t a = H->sym("UTFWin::IImageDrawable::AddImageWindow");
    if (!a || !parent)
        return nullptr;
    uint32_t key[3] = {instance, 0x2F7D0004, group};
    return skg::cdecl_call<Win>(a, key, x, y, parent);
}

void* image(uint32_t instance, uint32_t type, uint32_t group)
{
    uint32_t key[3] = {instance, type, group};
    void* img = nullptr;
    // static bool GetImage(const ResourceKey&, ImagePtr& dst, bool, int height, int width)
    bool ok = skg::cdecl_call<bool>(a_image_get, key, &img, false, -1, -1);
    return ok ? img : nullptr;
}

static void* utf_new(size_t n)
{
    void* alloc = skg::cdecl_call<void*>(a_get_alloc);
    return skg::cdecl_call<void*>(a_obj_new, n, (size_t)4, "SporeCoop", alloc);
}

static void obj_addref(void* o) { if (o) vcall<int>(o, 0); }
static void obj_release(void* o) { if (o) vcall<int>(o, 1); }

bool has_image(uint32_t instance, uint32_t group)
{
    void* resource = image(instance, 0x2F7D0004, group);
    obj_release(resource);
    return resource != nullptr;
}

// SporeStdDrawableImageInfo (0x98): vtables +0/+4, ref count +8, background image +0xC,
// icon image +0x10, the rest plain values
static void* copy_info(void* src, void* icon)
{
    uint8_t* d = (uint8_t*)utf_new(0x98);
    thiscall<void*>(a_info_ctor, d);
    void* old_bg = *(void**)(d + 0xC);
    void* old_icon = *(void**)(d + 0x10);
    memcpy(d + 0xC, (uint8_t*)src + 0xC, 0x98 - 0xC);
    obj_addref(*(void**)(d + 0xC));
    *(void**)(d + 0x10) = icon;
    obj_addref(icon);
    obj_release(old_bg);
    obj_release(old_icon);
    return d;
}

void* std_drawable_with_icon(void* src, void* icon)
{
    if (!src || *(uintptr_t*)src != H->sym("UTFWin::SporeStdDrawable::vftable")) // must be a SporeStdDrawable
        return nullptr;
    uint8_t* d = (uint8_t*)utf_new(0x160);
    thiscall<void*>(a_std_ctor, d);
    uint8_t* s = (uint8_t*)src;
    // StdDrawable: images[8] +0x10, values, hit mask +0x4C, values up to +0x7C
    for (int i = 0; i < 8; i++) {
        void** slot = (void**)(d + 0x10 + i * 4);
        void* old = *slot;
        *slot = *(void**)(s + 0x10 + i * 4);
        obj_addref(*slot);
        obj_release(old);
    }
    memcpy(d + 0x30, s + 0x30, 0x4C - 0x30);
    {
        void** slot = (void**)(d + 0x4C);
        void* old = *slot;
        *slot = *(void**)(s + 0x4C);
        obj_addref(*slot);
        obj_release(old);
    }
    memcpy(d + 0x50, s + 0x50, 0x7C - 0x50);
    // current info (+0x7C, embedded): copy values, keep our vtables/ref count
    {
        uint8_t* ci = d + 0x7C;
        void* old_bg = *(void**)(ci + 0xC);
        void* old_icon = *(void**)(ci + 0x10);
        memcpy(ci + 0xC, s + 0x7C + 0xC, 0x98 - 0xC);
        obj_addref(*(void**)(ci + 0xC));
        *(void**)(ci + 0x10) = icon;
        obj_addref(icon);
        obj_release(old_bg);
        obj_release(old_icon);
    }
    // per state infos (+0x114): own copies with our icon
    for (int i = 0; i < 8; i++) {
        void* si = *(void**)(s + 0x114 + i * 4);
        void** slot = (void**)(d + 0x114 + i * 4);
        void* mine = (si && si != s + 0x7C) ? copy_info(si, icon) : (void*)(d + 0x7C);
        vcall<int>(mine, 0); // the slot's reference
        void* old = *slot;
        *slot = mine;
        if (old)
            vcall<int>(old, 1);
    }
    memcpy(d + 0x134, s + 0x134, 0x160 - 0x134);
    return d;
}

// the same drawable with another layout mode in every state: `mode` 2 is the tooltip's (cut in thirds, the corners
// keep their size); the Party panel's portrait takes the whole image into the window (tried live, see coop_ui.cpp)
void* std_drawable_image(void* image, uint32_t mode)
{
    uint8_t* d = (uint8_t*)std_drawable_tooltip(image);
    if (d) *(uint32_t*)(d + 0x30) = mode;
    return d;
}

void* std_drawable_tooltip(void* image)
{
    // UTFWin::StdDrawable (0x7C bytes, vtable 0x1A85238 at runtime), the class of Spore's own
    // tooltip background: images[8] at +0x10, layout values +0x30..+0x78, hit mask +0x4C
    static uintptr_t ctor = H->sym("UTFWin::StdDrawable::StdDrawable");
    if (!ctor || !image)
        return nullptr;
    uint8_t* d = (uint8_t*)utf_new(0x7C);
    if (!d)
        return nullptr;
    thiscall<void*>(ctor, d);
    for (int i = 0; i < 8; i++) { // the pill in every state slot
        *(void**)(d + 0x10 + i * 4) = image;
        obj_addref(image);
    }
    // the native tooltip's values (window 03754E6C, read at runtime 2026-11-10): mode 2, the image
    // cut in thirds (0.333 x4), scale 1 / 1; +0x4C (hit mask) stays the constructor's
    static const uint32_t kHead[7] = {2, 0x3EAAAAAB, 0x3EAAAAAB, 0x3EAAAAAB, 0x3EAAAAAB, 0x3F800000, 0x3F800000};
    static const uint32_t kTail[11] = {0, 0x40C00000, 0, 2, 2, 0, 0, 0, 0x399D4952, 0x3F000000, 0};
    memcpy(d + 0x30, kHead, sizeof kHead);
    memcpy(d + 0x50, kTail, sizeof kTail);
    return d;
}

void* std_drawable_states(void* src, void* const* images, int n)
{
    static uintptr_t ctor = H->sym("UTFWin::StdDrawable::StdDrawable");
    if (!ctor || !src || n < 1 || n > 8 || IsBadReadPtr(src, 0x7C))
        return nullptr;
    for (int i = 0; i < n; i++)
        if (!images[i])
            return nullptr;
    uint8_t* d = (uint8_t*)utf_new(0x7C);
    if (!d)
        return nullptr;
    thiscall<void*>(ctor, d);
    if (*(uintptr_t*)d != *(uintptr_t*)src) { // not the class of the game's button: leave its look alone
        obj_release(d);
        return nullptr;
    }
    for (int i = 0; i < n; i++) {
        *(void**)(d + 0x10 + i * 4) = images[i];
        obj_addref(images[i]);
    }
    const uint8_t* s = (const uint8_t*)src; // the layout (mode, cuts, scale) and the values after the hit mask
    memcpy(d + 0x30, s + 0x30, 0x4C - 0x30);
    memcpy(d + 0x50, s + 0x50, 0x7C - 0x50);
    return d;
}

// ---------------- events ----------------
struct HandlerBox {
    std::function<bool(Win, const Msg&)> fn;
};
static int tramp(void* user, void* window, const void* msg)
{
    auto* b = (HandlerBox*)user;
    return b->fn && msg ? b->fn(window, *(const Msg*)msg) : 0;
}

void* add_handler(Win w, std::function<bool(Win, const Msg&)> fn, int event_flags)
{
    // the box is never freed: after a hot reload the old handler is disabled, not deleted
    auto* box = new HandlerBox{std::move(fn)};
    void* p = H->ui_winproc(M, tramp, box, event_flags);
    vcall<void>(w, 0x104 / 4, p);
    if (g_release_fix && refs(p) >= 2) { // the window holds its own (AddWinProc): ours is not needed (BUG-1025)
        vcall<int>(p, 1);
        return nullptr; // (no caller keeps it; remove_handler would give back a reference not ours)
    }
    return p;
}

void remove_handler(Win w, void* h)
{
    if (!h)
        return;
    H->ui_winproc_disable(h);
    if (w)
        vcall<void>(w, 0x108 / 4, h);
    obj_release(h);
}

// ---------------- devkit ----------------
namespace {
// BUG-0812: `coop.ui show|btn|font|caption|area|align|tree <hex>` took the number as a window and called its
// vtable: a control id (06318E78) or any other number was a wild pointer and the game faulted. The number is
// a window only when the live UI tree has a window at that address; else the first window with that control
// id; else nothing ("no window"). `coop.fix core.ui_window_arg off`: the number as it is (before).
static bool g_ui_arg_fix = true;
COOP_FIX("core.ui_window_arg", "BUG-0812", g_ui_arg_fix, "ui: coop.ui takes a window by its address or control id only when the UI tree has it (a wrong number faulted the game)");
Win arg_win(const char* text, SkOut* out)
{
    uintptr_t v = strtoul(text, nullptr, 16);
    if (!g_ui_arg_fix)
        return (Win)v;
    Win by_id = nullptr;
    std::vector<Win> stack{main_window()};
    for (int n = 0; !stack.empty() && n < 50000; n++) {
        Win w = stack.back();
        stack.pop_back();
        if (!w)
            continue;
        if ((uintptr_t)w == v)
            return w;
        if (!by_id && control_id(w) == (uint32_t)v)
            by_id = w;
        for (Win c : children(w))
            stack.push_back(c);
    }
    if (!by_id)
        H->out_printf(out, "no window %08X (no window of the UI tree has that address or control id)\n", (unsigned)v);
    return by_id;
}

int c_ui(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string sub = argc > 1 ? argv[1] : "tree";
    if (sub == "find" && argc > 2) {
        std::string t = argv[2];
        for (int i = 3; i < argc; i++)
            t += std::string(" ") + argv[i];
        Win w = find_caption(main_window(), t);
        if (!w) {
            H->out_printf(out, "no window with caption '%s'", t.c_str());
            return 1;
        }
        for (Win p = w; p; p = parent(p))
            H->out_printf(out, "%s", dump(p, 0, false).c_str());
        return 0;
    }
    if (sub == "path" && argc > 2) { // path <hex address | control id>: the window and its parents (BUG-9238 tests)
        Win w = arg_win(argv[2], out);
        for (Win p = w; p; p = parent(p))
            H->out_printf(out, "%s", dump(p, 0, false).c_str());
        return w ? 0 : 1;
    }
    if (sub == "at" && argc > 3) { // at <x> <y>: every visible window under a screen point (game bots)
        float x = (float)atof(argv[2]), y = (float)atof(argv[3]);
        std::vector<Win> stack{main_window()};
        while (!stack.empty()) {
            Win w = stack.back();
            stack.pop_back();
            if (!visible(w))
                continue;
            Rect r = screen_rect(w);
            bool layer = r.x1 <= 0 && r.y1 <= 0 && r.x2 - r.x1 >= 1000 && r.y2 - r.y1 >= 700; // full-screen layers
            if (x >= r.x1 && x < r.x2 && y >= r.y1 && y < r.y2 && parent(w) && !layer)
                H->out_printf(out, "%08X %.0f %.0f %.0f %.0f enabled=%d %s '%s'\n", control_id(w), r.x1, r.y1, r.x2, r.y2,
                              (flags(w) & kFlagEnabled) ? 1 : 0, type_name(w), caption(w).c_str());
            for (Win c : children(w))
                stack.push_back(c);
        }
        return 0;
    }
    if (sub == "where" && argc > 2) { // where <hex id>...: screen rect of each visible window (game bots)
        for (int i = 2; i < argc; i++) {
            uint32_t id = (uint32_t)strtoul(argv[i], nullptr, 16);
            std::vector<Win> stack{main_window()};
            Win hit = nullptr;
            while (!stack.empty() && !hit) { // only through visible parents: hidden copies are skipped
                Win w = stack.back();
                stack.pop_back();
                if (!visible(w))
                    continue;
                if (control_id(w) == id)
                    hit = w;
                for (Win c : children(w))
                    stack.push_back(c);
            }
            if (!hit) {
                H->out_printf(out, "%08X none\n", id);
                continue;
            }
            Rect r = screen_rect(hit);
            H->out_printf(out, "%08X %.0f %.0f %.0f %.0f enabled=%d\n", id, r.x1, r.y1, r.x2, r.y2,
                          (flags(hit) & kFlagEnabled) ? 1 : 0);
        }
        return 0;
    }
    if (sub == "layouts") {
        for (auto& l : g_layouts)
            H->out_printf(out, "%08X.%08X group %08X\n", l.instance, l.type, l.group);
        return 0;
    }
    if (sub == "refs" && argc > 2) { // the game's reference count of a window (BUG-1025)
        Win w = arg_win(argv[2], out);
        if (!w)
            return 1;
        H->out_printf(out, "refs %p %d\n", w, refs(w));
        return 0;
    }
    if (sub == "reftest") { // RE (BUG-1025): the reference counts of a window the mod makes, step by step
        Win parent = main_window();
        Win l = label(nullptr, {0, 0, 10, 10}, "reftest", 0, 0xFFFFFFFF, kAlignLeft);
        if (!l || !parent)
            return 1;
        int made = refs(l);
        add_child(parent, l);
        int added = refs(l);
        void* p = H->ui_winproc(M, tramp, new HandlerBox{}, 0);
        int proc0 = refs(p);
        vcall<void>(l, 0x104 / 4, p);
        int proc1 = refs(p);
        remove_child(parent, l);
        int removed = refs(l);
        H->out_printf(out, "label: made %d (with ours), in a parent %d, taken out %d; handler: made %d, on the window %d\n",
                      made, added, removed, proc0, proc1);
        vcall<int>(p, 1); // ours
        release(l);       // ours: the label and its handler go
        return 0;
    }
    if (sub == "load" && argc > 2) {
        Win w = load_layout((uint32_t)strtoul(argv[2], nullptr, 16),
                            argc > 3 ? (uint32_t)strtoul(argv[3], nullptr, 16) : 0x40464100u);
        H->out_printf(out, "%s", w ? dump(w, 1, false).c_str() : "load failed");
        return w ? 0 : 1;
    }
    if (sub == "btn" && argc > 2) { // button properties
        Win w = arg_win(argv[2], out);
        if (!w)
            return 1;
        void* b = as_button(w);
        if (!b) {
            H->out_printf(out, "not a button");
            return 1;
        }
        float bl, bt, br, bb, ox, oy;
        vcall<void>(b, 0x50 / 4, &bl, &bt, &br, &bb);
        vcall<void>(b, 0x58 / 4, &ox, &oy);
        H->out_printf(out, "type=%d flags=0x%X align=%d borders=%.0f %.0f %.0f %.0f offset=%.0f %.0f font=%08X colors",
                      vcall<int>(b, 0x18 / 4), vcall<int>(b, 0x2C / 4), vcall<int>(b, 0x38 / 4), bl, bt, br, bb, ox, oy,
                      font(w));
        for (uint32_t i = 0; i < 8; i++)
            H->out_printf(out, " %08X", vcall<uint32_t>(b, 0x48 / 4, i));
        return 0;
    }
    if (sub == "combo" && argc > 2) { // combo <win> [sel <i>]: a combo box's values (tests)
        Win w = arg_win(argv[2], out);
        if (!as_combo(w)) {
            H->out_printf(out, "not a combo box");
            return 1;
        }
        if (argc > 4 && !strcmp(argv[3], "sel")) combo_select(w, atoi(argv[4]));
        const auto v = combo_values(w);
        H->out_printf(out, "%zu values, selected %d\n", v.size(), combo_selected(w));
        for (size_t i = 0; i < v.size(); i++) H->out_printf(out, "%zu '%s'\n", i, v[i].c_str());
        return 0;
    }
    if (sub == "slider" && argc > 2) { // slider <win>: its values and drawable (tests)
        Win w = arg_win(argv[2], out);
        void* s = as_slider(w);
        if (!s) {
            H->out_printf(out, "not a slider");
            return 1;
        }
        void* d = vcall<void*>(s, 0x38 / 4);
        H->out_printf(out, "slider %p orient=%d value=%d min=%d max=%d drawable=%p", s, vcall<int>(s, 0x18 / 4),
                      vcall<int>(s, 0x20 / 4), vcall<int>(s, 0x28 / 4), vcall<int>(s, 0x30 / 4), d);
        if (d && !IsBadReadPtr(d, 0x1C))
            for (int i = 0; i < 7; i++)
                H->out_printf(out, " %08X", ((uint32_t*)d)[i]);
        return 0;
    }
    if (sub == "mkslider" && argc > 7) { // mkslider <parent> x1 y1 x2 y2 <like slider> [h|v]: a test slider
        Win p = arg_win(argv[2], out), like = arg_win(argv[7], out);
        if (!p || !like)
            return 1;
        Rect r{(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]), (float)atof(argv[6])};
        Win w = slider(p, r, 50, 200, 100, argc < 9 || argv[8][0] != 'v', slider_drawable_of(like));
        H->out_printf(out, "%s", w ? dump(w, 0, false).c_str() : "failed");
        return w ? 0 : 1;
    }
    if (sub == "scale" && argc > 3) { // scale <win> <factor> [event flags hex]: tests of a native window's size
        Win w = arg_win(argv[2], out);
        if (!w)
            return 1;
        static float s_factor = 1.f;
        static int s_seen = 0;
        s_factor = (float)atof(argv[3]);
        const int flags = argc > 4 ? (int)strtoul(argv[4], nullptr, 16) : 0xFFFF;
        add_handler(w, [](Win win, const Msg& m) {
            if (s_seen < 40) {
                s_seen++;
                H->log(M, SK_INFO, "sui: scale test: %p message %X a=%X b=%X c=%X d=%X", win, m.type, m.a, m.b, m.c, m.d);
            }
            if (m.type == kMsgTransformed && m.d && !IsBadReadPtr((void*)(uintptr_t)m.d, 64)) {
                float* x = (float*)(uintptr_t)m.d;
                x[0] *= s_factor;
                x[5] *= s_factor;
            }
            return false;
        }, flags);
        invalidate_transform(w);
        H->out_printf(out, "scale %.2f on %p (flags %X)", s_factor, w, flags);
        return 0;
    }
    if (sub == "align" && argc > 3) { // experiments: a button's caption alignment (+0x3C) and type (+0x1C)
        Win w = arg_win(argv[2], out);
        void* b = w ? as_button(w) : nullptr;
        if (!b)
            return 1;
        if (argc > 4)
            vcall<void>(b, 0x1C / 4, atoi(argv[4]));
        vcall<void>(b, 0x3C / 4, atoi(argv[3]));
        return 0;
    }
    if (sub == "area" && argc > 6) { // experiments: a window's area (parent coordinates)
        Win w = arg_win(argv[2], out);
        if (!w)
            return 1;
        set_area(w, {(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]),
                                                       (float)atof(argv[6])});
        return 0;
    }
    if (sub == "caption" && argc > 3) { // experiments: a window's caption (glyph widths: docs/08-ui.md#ui-text-width)
        Win w = arg_win(argv[2], out);
        if (!w)
            return 1;
        set_caption(w, argv[3]);
        return 0;
    }
    if (sub == "font" && argc > 3) {
        Win w = arg_win(argv[2], out);
        if (!w)
            return 1;
        set_font(w, (uint32_t)strtoul(argv[3], nullptr, 16));
        return 0;
    }
    if (sub == "show" && argc > 2) {
        Win w = arg_win(argv[2], out);
        if (!w)
            return 1;
        set_visible(w, argc < 4 || atoi(argv[3]));
        return 0;
    }
    int depth = argc > 2 ? atoi(argv[2]) : 4;
    bool all = argc > 3 && !strcmp(argv[3], "all");
    Win root = argc > 4 ? arg_win(argv[4], out) : main_window();
    if (!root)
        return 1;
    std::string s = dump(root, depth, !all);
    if (s.size() > 60000)
        s.resize(60000);
    H->out_printf(out, "%s", s.c_str());
    return 0;
}
} // namespace

void register_commands()
{
    H->command_add(M, "coop.ui",
                   "coop.ui tree [depth] [all] [root] | find <caption> | path <win> | where <hex id>... | at <x> <y> | layouts | load <inst> [group] | "
                   "show <win> [0|1] - Spore UI windows",
                   c_ui, nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace sui
