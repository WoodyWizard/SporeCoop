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
constexpr int kMsgKeyDown = 1, kMsgKeyUp = 2, kMsgMouseDown = 6, kMsgMouseUp = 7, kMsgButtonClick = 0x17,
              kMsgButtonSelect = 0x18, kMsgMouseEnter = 0x1B, kMsgMouseLeave = 0x1C,
              kMsgTextChanged = (int)0x9B1552DA, kMsgComponentActivated = 0x287259F6;
// a button was pressed: every button sends ComponentActivated once; standard buttons also send
// ButtonClick and menu style buttons ButtonSelect, so only ComponentActivated counts
inline bool is_click(const Msg& m) { return m.type == kMsgComponentActivated; }
constexpr int kAlignLeft = 1, kAlignRight = 2, kAlignCenter = 3;
constexpr uint32_t kFlagVisible = 1, kFlagEnabled = 2, kFlagIgnoreMouse = 0x10, kFlagAlwaysInFront = 0x40;

bool init(const SkHost* host, SkMod* mod);
void register_commands();

// ---- reading ----
Win main_window();
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
int refs(void* object);   // the game's reference count of a live window / image / drawable (BUG-1025)
extern bool g_release_fix; // core.ui_release (BUG-1025): what the mod drops is given back
void set_caption(Win w, const std::string& utf8);
void set_area(Win w, const Rect& r);
void set_visible(Win w, bool on);
void set_enabled(Win w, bool on);
void set_flag(Win w, uint32_t flag, bool on);
void set_ids(Win w, uint32_t control_id, uint32_t command_id);
void set_font(Win w, uint32_t font_id);
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
Win text_edit(Win parent, const Rect& r, const std::string& text, uint32_t font_id, int max_len);
std::string edit_text(Win edit);
void set_edit_text(Win edit, const std::string& text);

// ---- images and button looks ----
// Image* from a package (png), nullptr if missing. The caller owns one reference.
void* image(uint32_t instance, uint32_t type, uint32_t group);
// a window showing a png from a package at its own size (IImageDrawable::AddImageWindow)
Win image_window(Win parent, uint32_t instance, uint32_t group, float x, float y);
// a new SporeStdDrawable copying `src` (a SporeStdDrawable) with another icon image
void* std_drawable_with_icon(void* src, void* icon);
void set_drawable(Win w, void* drawable);

// ---- events ----
// fn(window, message) -> handled. The handler object lives in sporekit (safe on hot reload).
void* add_handler(Win w, std::function<bool(Win, const Msg&)> fn);
void remove_handler(Win w, void* handler);

uint32_t fnv(const char* s); // Spore id()

} // namespace sui
