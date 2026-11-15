// Thin wrappers over Spore's own UI system (UTFWin): window tree access, layouts from the
// game packages, native buttons/labels/text fields styled like existing game windows, and
// click handlers. Layouts follow ModAPI's headers (Spore/UTFWin/*.h) with the differences of
// this build noted in docs/08-ui.md. Everything that creates windows must run outside
// IDirect3DDevice9::Present (SkHost::run_outside_frame / SK_CMD_OUTSIDE_FRAME).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sporekit.h"

namespace sui {

using Win = void*; // UTFWin::IWindow*

struct Rect {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float w() const { return x2 - x1; }
    float h() const { return y2 - y1; }
};

// UTFWin::Message (0x1C bytes)
struct Msg {
    Win source;
    int field_04;
    int type;
    uint32_t a, b, c, d;
};
constexpr int kMsgTransformed = 0x15; // d: the Matrix4* the window manager is building (InflateEffect scales it)
constexpr int kMsgKeyDown = 1, kMsgKeyUp = 2, kMsgMouseDown = 6, kMsgMouseUp = 7, kMsgButtonClick = 0x17,
              kMsgButtonSelect = 0x18, kMsgMouseEnter = 0x1B, kMsgMouseLeave = 0x1C,
              kMsgTextChanged = (int)0x9B1552DA, kMsgComponentActivated = 0x287259F6;
// a button was pressed: every button sends ComponentActivated once; standard buttons also send
// ButtonClick and menu style buttons ButtonSelect, so only ComponentActivated counts
inline bool is_click(const Msg& m) { return m.type == kMsgComponentActivated; }
constexpr int kAlignLeft = 1, kAlignRight = 2, kAlignCenter = 3;
constexpr uint32_t kFlagVisible = 1, kFlagEnabled = 2, kFlagIgnoreMouse = 0x10, kFlagAlwaysInFront = 0x40;
// not in ModAPI: hit tests (the mouse) go through the window's transform (seen live 2026-11-13: without it a scaled
// window drew larger and took clicks at its old size)
constexpr uint32_t kFlagTransformHits = 0x20;

bool init(const SkHost* host, SkMod* mod);
void register_commands();

// ---- reading ----
Win main_window();
// the window the game's own hit test finds at a screen point (IWindowManager vt 0x44, GetWindowAtPosition(Point&))
Win window_at(float x, float y);
std::vector<Win> children(Win w);
Win parent(Win w);
const char* type_name(Win w); // GetComponentName
uint32_t control_id(Win w);
uint32_t command_id(Win w);
uint32_t flags(Win w);
std::string caption(Win w); // UTF-8
Rect area(Win w);           // relative to the parent
Rect real_area(Win w);
Rect screen_rect(Win w);    // in screen pixels (sum of the parents' offsets)
void* drawable(Win w);
bool visible(Win w);
Win find(Win root, uint32_t control_id);
Win find_caption(Win root, const std::string& text); // caption contains `text`
std::string dump(Win root, int max_depth, bool only_visible);

// ---- changing ----
void add_ref(Win w);
void release(Win w);
void set_caption(Win w, const std::string& utf8);
void set_area(Win w, const Rect& r);
// the area the window has after its layout procs (anchors): they are reverted first (IWindow vt 0x6C, SetLayoutArea)
void set_layout_area(Win w, const Rect& r);
void set_visible(Win w, bool on);
void set_enabled(Win w, bool on);
void set_flag(Win w, uint32_t flag, bool on);
void set_ids(Win w, uint32_t control_id, uint32_t command_id);
void set_font(Win w, uint32_t font_id);
// a copied button's caption in the middle of the button: no caption offset, even borders (button_like keeps the
// model's, made for a text beside an edge: a one-glyph caption sat right of the middle)
void center_caption(Win w);
void set_shade_color(Win w, uint32_t argb);
void set_fill_color(Win w, uint32_t argb); // the window's own background (Window vt 0xAC; 0 alpha: none)
uint32_t fill_color(Win w);                 // (vt 0xA4)
uint32_t font(Win w);
void add_child(Win parent, Win child);
void remove_child(Win parent, Win child);
void bring_to_front(Win parent, Win child);

// ---- layouts (.spui from the game packages) ----
// loads a layout into the main UI layer; returns its top window (nullptr on failure)
Win load_layout(uint32_t instance, uint32_t group = 0x40464100);

// ---- widgets ----
void* as_button(Win w); // IButton* or nullptr
// a native button that looks like `like` (same drawable object, font, caption colors)
Win button_like(Win parent, Win like, const Rect& r, const std::string& caption);
// a text label: a button without a drawable that ignores the mouse
Win label(Win parent, const Rect& r, const std::string& text, uint32_t font_id, uint32_t argb, int align);
void set_label_color(Win label, uint32_t argb);
// a one-line text field
Win text_edit(Win parent, const Rect& r, const std::string& text, uint32_t font_id, int max_len,
              bool draw_background = true);
void set_edit_masked(Win edit, bool masked);
// a native slider (UTFWin WinSlider, as the Audio page's): `drawable` an ISliderDrawable (e.g. slider_drawable_of(another
// slider)), horizontal or vertical; values min..max
void* as_slider(Win w); // ISlider* or nullptr
Win slider(Win parent, const Rect& r, int min, int max, int value, bool horizontal, void* drawable);
void* slider_drawable_of(Win slider);
// a new SliderDrawable of `like`'s class (a SliderDrawable) with our images (background groove, thumb, thumb container;
// a null keeps `like`'s)
void* slider_drawable_with(void* like, void* background, void* thumb, void* container);
int slider_value(Win w);
// a toggle button's (check box's) ticked state (IButton state flag 4, kBtnStateSelected)
bool button_selected(Win w);
void set_button_selected(Win w, bool on);
// a window that only draws `drawable` (a box's background), ignoring the mouse
Win box(Win parent, const Rect& r, void* drawable);
void set_slider_value(Win w, int value);
// the window manager builds the window's transform again (IWindow vt 0x98, InvalidateTransform)
void invalidate_transform(Win w);
// the window's real area again from its area and layout procs (anchors; IWindow vt 0x94, Revalidate)
void revalidate(Win w);
// the window's transform as drawn (screen = local * scale + offset; Window +0x148, its type at +0x188: 0 identity,
// 1 translation and scale, read live 2026-11-13); false for other types
bool render_xform(Win w, float& sx, float& sy, float& tx, float& ty);
// the window has a transform of its own (its local transform, Window +0xC0, type at +0x100, is not the identity: the
// game's InflateEffect buttons while they grow); the game hit-tests such a window through its full transform
bool has_own_transform(Win w);
// a native combo box (UTFWin IComboBox, cast 0x2F5528D9; ModAPI's order): its values, the selected one, changes
void* as_combo(Win w);
std::vector<std::string> combo_values(Win w); // UTF-8
int combo_selected(Win w);
void combo_select(Win w, int index);
void combo_set_values(Win w, const std::vector<std::string>& values);
std::string edit_text(Win edit);
void set_edit_text(Win edit, const std::string& text);

// ---- images and button looks ----
// Image* from a package (png), nullptr if missing. The caller owns one reference.
void* image(uint32_t instance, uint32_t type, uint32_t group);
// a window showing a png from a package at its own size (IImageDrawable::AddImageWindow)
Win image_window(Win parent, uint32_t instance, uint32_t group, float x, float y);
bool has_image(uint32_t instance, uint32_t group);
// a new SporeStdDrawable copying `src` (a SporeStdDrawable) with another icon image
void* std_drawable_with_icon(void* src, void* icon);
// a new StdDrawable drawing `image` stretched like Spore's tooltip background (the native
// tooltip drawable's class and layout values, read at runtime 2026-11-10)
void* std_drawable_tooltip(void* image);
void* std_drawable_image(void* image, uint32_t mode); // the tooltip drawable with another layout mode
// a new StdDrawable with the layout of `src` (a StdDrawable: the game's button) and `n` images of ours for its states
// (images[0..3]: normal, disabled, highlighted, active); null if `src` is no StdDrawable or an image is missing
void* std_drawable_states(void* src, void* const* images, int n);
void set_drawable(Win w, void* drawable);

// ---- events ----
// fn(window, message) -> handled. The handler object lives in sporekit (safe on hot reload).
void* add_handler(Win w, std::function<bool(Win, const Msg&)> fn, int event_flags = 0); // 0: input + advanced (0x81)
void remove_handler(Win w, void* handler);

uint32_t fnv(const char* s); // Spore id()

} // namespace sui
