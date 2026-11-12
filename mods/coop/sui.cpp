#include "sui.h"

#include <cctype>
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
void set_caption(Win w, const std::string& t)
{
    std::u16string u = utf16(t);
    vcall<void>(w, 0x80 / 4, u.c_str());
}
void set_area(Win w, const Rect& r) { vcall<void>(w, 0x60 / 4, &r); }
void set_flag(Win w, uint32_t f, bool on) { vcall<void>(w, 0x7C / 4, f, on); }
void set_visible(Win w, bool on) { set_flag(w, kFlagVisible, on); }
void set_enabled(Win w, bool on) { set_flag(w, kFlagEnabled, on); }
void set_ids(Win w, uint32_t cid, uint32_t cmd)
{
    vcall<void>(w, 0x50 / 4, cid);
    vcall<void>(w, 0x54 / 4, cmd);
}
void set_font(Win w, uint32_t f) { vcall<void>(w, 0x84 / 4, f); }
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
    // the UILayout object stays alive for the whole session (the windows belong to it)
    void* l = calloc(1, 0x40);
    thiscall<void*>(a_layout_ctor, l);
    vcall<int>(l, 1); // AddRef (DefaultRefCounted: [0] is the destructor)
    uint32_t key[3] = {instance, 0x0510A95B, group};
    if (!o_LayoutLoad(l, key, true, 0x5B598FA))
        return nullptr;
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
    vcall<void>(w, 0x5C / 4, vcall<uint32_t>(like, 0x30 / 4)); // shade color
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

Win text_edit(Win parent, const Rect& r, const std::string& text, uint32_t font_id, int max_len)
{
    Win w = ((CreateFn)a_te_create)(0, nullptr);
    void* e = as_edit(w);
    if (!e)
        return nullptr;
    add_ref(w);
    if (font_id)
        vcall<void>(e, 0x18 / 4, font_id); // SetTextStyleID
    static const uint32_t colors[8] = {0xFFFFFFFF, 0xD0081828, 0xFF808080, 0x80081828,
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

// ---------------- events ----------------
struct HandlerBox {
    std::function<bool(Win, const Msg&)> fn;
};
static int tramp(void* user, void* window, const void* msg)
{
    auto* b = (HandlerBox*)user;
    return b->fn && msg ? b->fn(window, *(const Msg*)msg) : 0;
}

void* add_handler(Win w, std::function<bool(Win, const Msg&)> fn)
{
    // the box is never freed: after a hot reload the old handler is disabled, not deleted
    auto* box = new HandlerBox{std::move(fn)};
    void* p = H->ui_winproc(M, tramp, box, 0);
    vcall<void>(w, 0x104 / 4, p);
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
                   "coop.ui tree [depth] [all] [root] | find <caption> | where <hex id>... | at <x> <y> | layouts | load <inst> [group] | "
                   "show <win> [0|1] - Spore UI windows",
                   c_ui, nullptr, SK_CMD_OUTSIDE_FRAME);
}

} // namespace sui
