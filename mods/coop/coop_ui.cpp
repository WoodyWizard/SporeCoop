#include "coop_ui.h"
#include "trace.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "modes.h"
#include "mouse.h"
#include "net/fixes.h"
#include "notify.h"
#include "net/udp.h"
#include "notify_text.h"
#include "space_ext.h"
#include "sui.h"
#include "world.h"
#include "tribe.h"
#include "civ.h"
#include "fixes.h"
#include "slotfs.h"
#include "lineages_graphics.h"

namespace coop_ui {

static uint32_t g_text_font;

using sui::Rect;
using sui::Win;

static const SkHost* H;
static SkMod* M;
static Api A;
static bool g_ok;      // Spore UI available
static bool g_ru;      // Russian captions
static bool g_want_open, g_pending, g_dirty = true;
static double g_last_refresh;
static std::string g_status;
static uint32_t g_status_color = 0xFFFFD866;
static double g_status_until; // the status goes away then (0 = stays until the next session event)
bool g_status_fix = true;     // BUG-0104 (`coop.corefix ui_status 0`: the status of the last button stays)
COOP_FIX("core.ui_status", "BUG-0104", g_status_fix, "ui: the co-op window shows the session's status, not the last button's");
// BUG-0597: "Send my world" pressed in the main menu (no world loaded) or with nobody joined did nothing while
// the window said "Sending the world to everyone...": players read it as "solo does not start". The button is
// greyed out then and the lobby says why (a host can start or load a game alone, friends join any time and get
// the world by themselves). Off: the button always sends and says so (0.7.7).
static bool g_share_explain_fix = true;
COOP_FIX("core.ui_share_explain", "BUG-0597", g_share_explain_fix, "ui: Send my world is greyed out with no world or nobody joined, and the lobby says why");
// BUG-0598: a guest without own creations of the stage's kind saw "My cell: the world's own" with no arrows and
// no hint, and could not tell that a choice exists (a player asked where to pick the own creation). The row says
// whose it is ("the host's species") and how to get a choice (make one in an editor). Off: "the world's own" (0.7.7).
static bool g_creation_hint_fix = true;
COOP_FIX("core.ui_creation_hint", "BUG-0598", g_creation_hint_fix, "ui: the creation row says whose creation it is and how to get a choice");
static bool g_f9_down;
static bool g_lb_down;      // the left mouse button in the last frame (the Co-op item during the menu intro)
static uint32_t g_lb_seen;  // mouse::clicks() handled (BUG-0145)
static double g_skip_since; // the Co-op item was clicked during the menu intro: open once it is over
static double g_intro_over; // when that intro was over
static bool g_raise = true; // move the window above all UI layers when it opens
bool g_log_msgs; // debug: log UI messages that reach our handlers

constexpr uint32_t kIdMenuButton = 0xC00F0001, kIdDialog = 0xC00F0100;
constexpr uint32_t kIdMainMenu = 0x0615A5FF, kIdShare = 0x0615B616;
constexpr uint32_t kDialogLayout = 0x34A6339E; // the game's account dialog (frame, logo, close)
constexpr uint32_t kGold = 0xFFFFCC4D, kWhite = 0xFFFFFFFF, kDim = 0xFF9DB8CC, kGreen = 0xFF8CF08C,
                   kRed = 0xFFFF7A6A, kYellow = 0xFFFFD866;
constexpr float kW = 760, kH = 600;
constexpr uint32_t kDangerButtonShade = 0xFFAD535D; // the red without our images (an older package)
// the owner (2026-11-13): the game's gold buttons and their shaded red "look a bit dim and grimy": every button of the
// mod (all copy D.like_button) draws data/ui/lineages_button_gold_*, the red ones lineages_button_red_* (tools/
// make_icons.py), in the game's button layout; without the images (an older package) the game's look and the shade
static void* g_red_look; // the red buttons' drawable, null: shade the gold
static bool g_bright_buttons = true;
COOP_FIX("ui.bright_buttons", "BUG-9179", g_bright_buttons,
         "ui: the mod's buttons draw their own brighter gold and red (the game's: dim and grimy, the owner 2026-11-13)");
static void* button_look(void* game_look, const char* colour)
{
    static const char* const kStates[4] = {"normal", "disabled", "hover", "pressed"};
    void* img[4] = {};
    for (int i = 0; i < 4; i++)
        img[i] = sui::image(sui::fnv((std::string("lineages_button_") + colour + "_" + kStates[i]).c_str()), 0x2F7D0004,
                            sui::fnv("sporecoop"));
    void* look = sui::std_drawable_states(game_look, img, 4);
    for (void* i : img)
        if (i) sui::release(i); // the drawable holds its own references
    return look;
}
static void apply_red(Win button)
{
    if (g_red_look)
        sui::set_drawable(button, g_red_look);
    else
        sui::set_shade_color(button, kDangerButtonShade);
}
constexpr int kSpaceRows = 12;
static bool g_space_page; // the lobby shows the Space page instead
static unsigned g_slots_shown; // the player slot count last written into the lobby's box (0: none)
static bool g_give_button = false;
COOP_FIX("ui.give_button", "BUG-9148", g_give_button,
         "co-op window: the Give To button for selected units and cargo spice (owner 2026-11-12: removed, off)");
// BUG-9153 (owner 2026-11-12, decision 2L "B, prioritize this"): no "Village >" page; a guest plays its own village with
// the game's own controls (selection and right-click orders: BUG-9152, instruments: keys 1-3). On: the old page.
static bool g_village_page = false;
COOP_FIX("ui.village_page", "BUG-9153", g_village_page,
         "co-op window: the Village page with buttons for a guest's village orders (owner 2026-11-12: removed, off)");
static int g_lan_first, g_lan_max, g_lan_wheel_delta;
static bool g_lan_dragging;
constexpr int kLanRows = 6;
constexpr size_t kServerHistoryLimit = 24;
bool g_lan_pages_fix = true;
COOP_FIX("core.lan_pages", "BUG-0183", g_lan_pages_fix, "ui: every network game is accessible through the server list scrollbar");
static int g_space_first; // the first row shown (pages of kSpaceRows rows)

static const char* T(const char* en, const char* ru) { return g_ru ? ru : en; }

// ---------------- widgets with cached state ----------------
struct Cache {
    std::string text;
    uint32_t color = 0;
    int vis = -1;
    bool left = false; // left aligned label: the box is sized to the text (labels center their text)
    bool right = false; // right aligned label: the box ends at box.x2 and is sized to the text
    Rect box;
    int enabled = -1;
};
static std::map<Win, Cache> g_cache;
static int g_chat_scroll;            // chat lines scrolled up from the newest (BUG-0129)
constexpr size_t kChatCols = 70;     // letter cells of a chat line (680 px)

// width of a text in the dialog font (average glyph width, measured at 1080p)
// BUG-9117: labels sized by measured character widths, aligned beside their fields; even rows; the
// difficulty field framed; the ping column wide enough; shorter update lines; a tidier lobby.
// `coop.fix ui.polish off`: the 0.7.8-lineages47 layout's text boxes (the estimate below).
static bool g_label_width_fix = true;
COOP_FIX("ui.polish", "BUG-9117", g_label_width_fix, "ui: the co-op window's labels are sized to their text and aligned beside their fields");
static float text_width(const std::string& t)
{
    int n = 0;
    for (unsigned char ch : t)
        n += (ch & 0xC0) != 0x80;
    return n * 9.3f + 30;
}

static float text_px(const std::string& t, bool small_font);
static std::map<Win, bool> g_small_font; // labels drawn with the button font (narrower)
constexpr float kLabelMargin = 16.f;     // a label's inner margin on each side
static void fit(Win w, const Cache& c)
{
    Rect r = c.box;
    if (!g_label_width_fix) {
        r.x2 = std::min(r.x2, r.x1 + text_width(c.text));
        sui::set_area(w, r);
        return;
    }
    // the measured width plus the label's own inner margin (about 15 px a side: a box only as wide as
    // the text wraps it), moved left by that margin so the text starts at the box's left edge
    const float m = kLabelMargin;
    r.x1 = c.box.x1 - m;
    r.x2 = std::min(c.box.x2 + m, r.x1 + text_px(c.text, g_small_font.count(w) != 0) + 2 * m);
    sui::set_area(w, r);
}

// The window's body font, measured on screen (2026-11-11, 1440x1080: "Port (UDP)" 92 px, "Password" 81,
// "Seats" 46, "Transfer" 75): a width per character class, within about 3 px. The game's labels
// centre their text whatever alignment they are given, so a label that has to end at a field's edge
// gets a box exactly as wide as its text (plus kRightPad on each side).
static float text_px(const std::string& t, bool small_font)
{
    float w = 0;
    for (unsigned char ch : t) {
        if ((ch & 0xC0) == 0x80) continue; // UTF-8 continuation
        if (ch >= 0x80) { w += 10.f; continue; }
        if (strchr("iljI!.,:;'|", ch)) w += 4.5f;
        else if (ch >= '0' && ch <= '9') w += 11.5f; // "Hosting on UDP port 47839  -  2/2 players": 370 px
        else if (strchr("rtf()[] -/", ch)) w += ch == ' ' ? 6.5f : 6.5f;
        else if (strchr("mwMW%", ch)) w += 13.5f;
        else if (strchr("OQDGHNUCA", ch)) w += 12.5f;
        else if (ch >= 'A' && ch <= 'Z') w += 11.5f;
        else w += 10.f;
    }
    w *= 1.03f; // a little room: a box short of its text wraps the text
    return small_font ? w * 0.70f : w; // the button font: chat "* hosting on UDP port 47839" 176 px
}
static void fit_right(Win w, const Cache& c, bool small_font)
{
    // the text ends at box.x2: the box reaches one margin past it (the label centres its text)
    Rect r = c.box;
    r.x2 = c.box.x2 + kLabelMargin;
    r.x1 = std::max(c.box.x1 - kLabelMargin, r.x2 - text_px(c.text, small_font) - 2 * kLabelMargin);
    sui::set_area(w, r);
}

static void text(Win w, const std::string& t)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.text != t) {
        c.text = t;
        sui::set_caption(w, t);
        if (c.left)
            fit(w, c);
        else if (c.right)
            fit_right(w, c, g_small_font.count(w) != 0);
    }
}
static void color(Win w, uint32_t argb)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.color != argb) {
        c.color = argb;
        sui::set_label_color(w, argb);
    }
}
static void enable(Win w, bool on)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.enabled != (int)on) {
        c.enabled = on;
        sui::set_enabled(w, on);
    }
}
static void show(Win w, bool on)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.vis != (int)on) {
        c.vis = on;
        sui::set_visible(w, on);
    }
}

// ---------------- main menu item ----------------
static Win g_menu_btn, g_menu_parent;
// BUG-0163: the item is a child of the galaxy menu window, which the game frees when it leaves the
// menu; the pointer was dropped only by the next refresh (up to 1 s later), and a click meanwhile
// made a virtual call on the freed item. It goes as soon as the mode is not the menu.
// `coop.corefix menu_drop 0` = the old behaviour (tests).
bool g_menu_drop_fix = true;
bool g_recenter_fix = true;
COOP_FIX_UNSAFE("core.menu_drop", "BUG-0163", g_menu_drop_fix, "ui: the galaxy menu's Co-op item pointer goes with the menu");
COOP_FIX("core.window_recenter", "BUG-0113", g_recenter_fix, "ui: the co-op window is placed again after a resolution change");

// The window is centred on the screen it opens on (BUG-0113: it is kept for the whole run, and was
// centred once; after a smaller resolution in the options its bottom row was off the screen).
static int g_placed_w = -1, g_placed_h = -1;
static void place_window(Win root)
{
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    if (sw <= 0 || sh <= 0 || (sw == g_placed_w && sh == g_placed_h))
        return;
    g_placed_w = sw;
    g_placed_h = sh;
    float x = std::max(0.f, (sw - kW) / 2), y = std::max(0.f, (sh - kH) / 2);
    sui::set_area(root, Rect{x, y, x + kW, y + kH});
}
static bool in_menu() { return A.mode && A.mode() == modes::kMenu; }
static void* g_menu_drawable;
static Win g_menu_drawable_for; // the menu window the drawable was made for

static void on_menu_click() { open(true); }

static void ensure_menu_button()
{
    Win root = sui::main_window();
    Win menu = A.mode && A.mode() == modes::kMenu ? sui::find(root, kIdMainMenu) : nullptr;
    if (!menu) {
        g_menu_btn = g_menu_parent = nullptr;
        return;
    }
    if (g_menu_btn && g_menu_parent == menu)
        return;
    Win share = sui::find(menu, kIdShare);
    if (!share)
        return;
    // a stale item from before a hot reload: remove it, a fresh one gets our handler
    if (Win old = sui::find(menu, kIdMenuButton))
        sui::remove_child(menu, old);
    Rect a = sui::area(share);
    float dy = 95; // below "Galactic Adventures"
    Rect r{a.x1, a.y1 + dy, a.x2, a.y2 + dy};
    // built detached and added to the menu last: changing the drawable of a window that is already
    // in the galaxy menu made the game render its planet view synchronously from here (outside the
    // frame), and DXVK deadlocked the main thread right after a stage was left (frames stopped)
    Win b = sui::button_like(nullptr, share, r, T("Co-op", "Кооператив"));
    if (!b)
        return;
    sui::set_ids(b, kIdMenuButton, kIdMenuButton);
    // a fresh drawable for every galaxy menu: it is a copy of the "Share" button's drawable (its
    // images and current state), and the copy from an earlier menu, set on the new button after a
    // few stage switches, hung the main thread inside SetDrawable (a render from the planet view
    // waiting in DXVK, docs/known-issues.md history). The old one stays with the old button.
    if (g_menu_drawable_for != menu) {
        g_menu_drawable = nullptr;
        g_menu_drawable_for = menu;
    }
    if (!g_menu_drawable) {
        void* icon = sui::image(sui::fnv("coop_icon"), 0x2F7D0004, sui::fnv("sporecoop"));
        g_menu_drawable = sui::std_drawable_with_icon(sui::drawable(share), icon);
        if (!icon)
            H->log(M, SK_WARN, "ui: coop_icon not found (Data/SporeCoop.package missing?)");
    }
    if (g_menu_drawable)
        sui::set_drawable(b, g_menu_drawable);
    sui::add_child(menu, b);
    sui::add_handler(b, [](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg 0x%X src=%p %08X %08X %08X %08X", m.type, m.source, m.a, m.b, m.c, m.d);
        if (sui::is_click(m)) {
            on_menu_click();
            return true;
        }
        return false;
    });
    g_menu_btn = b;
    g_menu_parent = menu;
    // the version under the item: a player's screenshot of the menu tells which SporeCoop runs
    if (Win old = sui::find(menu, kIdMenuButton + 1))
        sui::remove_child(menu, old);
    if (A.version_line) {
        std::string v = A.version_line();
        v = v.substr(0, v.find("  -  "));
        if (Win l = sui::label(menu, {r.x1 + 6, r.y2 - 4, r.x2 + 90, r.y2 + 14}, v, sui::font(share), kDim,
                               sui::kAlignLeft))
            sui::set_ids(l, kIdMenuButton + 1, 0);
    }
    H->log(M, SK_INFO, "ui: co-op item added to the main menu");
}

// ---------------- dialog ----------------
struct Dialog {
    Win root = nullptr, layer = nullptr;
    uint32_t font_body = 0, font_button = 0;
    Win like_button = nullptr; // gold "Login" button of the layout
    Win subtitle = nullptr;
    Win version = nullptr, connect_version = nullptr, report_btn = nullptr, report_caption = nullptr;
    // connect page
    std::vector<Win> page_connect;
    Win transfer_btn[2]{}, seat_btn[4]{}, difficulty_value[5]{};
    Win name_edit = nullptr, addr_edit = nullptr, port_edit = nullptr, join_btn = nullptr, host_btn = nullptr;
    Win scaling_edit = nullptr, join_port = nullptr, join_password = nullptr, host_password = nullptr;
    Win selected_info = nullptr;
    unsigned seats = 2;
    struct SavedServer { std::string address, name; };
    std::vector<SavedServer> saved_servers;
    bool servers_loaded = false;
    struct Privacy { Win button, eye, crossed; };
    std::map<std::string, Privacy> privacy;
    bool show_host_addresses = false;
    Win next_seats_edit = nullptr, queue_seats = nullptr, slots_label = nullptr;
    Win lan_hdr = nullptr, host_hint = nullptr, status = nullptr, update_btn = nullptr;
    Win lan_rows[kLanRows] = {};
    std::string lan_addr[kLanRows];
    std::map<Win, bool> hidden_fields;
    Win lan_track = nullptr, lan_thumb = nullptr;
    // lobby page
    std::vector<Win> page_lobby;
    Win session = nullptr, world = nullptr, chat_edit = nullptr, send_btn = nullptr;
    Win invite_btn = nullptr, rules = nullptr, transfer_mode = nullptr, preview_status = nullptr;
    Win ready_btn = nullptr, share_btn = nullptr, leave_btn = nullptr, leave_caption = nullptr, onward_btn = nullptr, cells_btn = nullptr;
    Win create_btn = nullptr; // BUG-9135 phase 4: a late joiner's new creature (tribe.late_create_editor)
    Win row_name[4] = {}, row_stage[4] = {}, row_ping[4] = {}, row_ready[4] = {};
    Win chat_lines[5] = {};
    Win chat_up = nullptr, chat_down = nullptr;
    Win creation = nullptr, creation_prev = nullptr, creation_next = nullptr, give_btn = nullptr, give_next = nullptr, jump_btn = nullptr;
    Win space_btn = nullptr; // lobby -> the Space page (Space stage only)
    // Space page: the rows of the space features (space_ext.h)
    std::vector<Win> page_space;
    Win space_title = nullptr, space_back = nullptr, space_prev = nullptr, space_next = nullptr, space_pageno = nullptr;
    Win space_text[kSpaceRows] = {}, space_button[kSpaceRows] = {};
    std::function<void()> space_action[kSpaceRows];
};
static Dialog D;
static std::map<std::string, Win> g_named; // for tests (coop.uistate)

static Win add_label(std::vector<Win>& page, const Rect& r, const std::string& t, uint32_t argb,
                     int align = sui::kAlignLeft)
{
    Win w = sui::label(D.root, r, t, D.font_body, argb, align);
    if (!w)
        return nullptr;
    page.push_back(w);
    Cache c{t, argb, 1, align == sui::kAlignLeft, align == sui::kAlignRight, r};
    if (c.left)
        fit(w, c);
    else if (c.right)
        fit_right(w, c, false);
    g_cache[w] = c;
    return w;
}

// a right-aligned label drawn with the smaller button font
static void use_small_font(Win w)
{
    if (!w) return;
    sui::set_font(w, D.font_button);
    g_small_font[w] = true;
    auto& c = g_cache[w];
    if (c.right) fit_right(w, c, true);
}

static Win shade_danger_button(std::vector<Win>& page, Win button, const std::string& title)
{
    if (!button) return nullptr;
    // Native window shading multiplies the caption color as well as the skin.
    // Draw the caption above the button with a mouse-transparent native label.
    Win caption = add_label(page, sui::area(button), title, kWhite, sui::kAlignCenter);
    if (caption) {
        apply_red(button);
        sui::set_label_color(button, 0);
    }
    return caption;
}

// BUG-0273: while a cinematic or a caption plays, the game holds its input lock
// (cGameInputManager +0x110 above 0) and drops every click on the UI, also on our window: a
// guest's "I'm ready" during the brain-upgrade captions of a vote did nothing, the vote ran out of
// time. The click is taken from the window's messages instead (mouse.h) and given to the button
// under it, unless the game delivered it itself within kLockedClickWait. Off: the clicks are
// dropped while the lock is held (0.7.3.0).
bool g_locked_clicks_fix = true;
COOP_FIX("core.ui_locked_clicks", "BUG-0273", g_locked_clicks_fix, "ui: co-op window buttons take clicks while a caption holds the game's input lock");
static bool g_tribal_clicks_fix = true;
COOP_FIX("core.ui_tribal_clicks", "BUG-9018", g_tribal_clicks_fix, "ui: Tribal co-op buttons recover clicks swallowed without a native input lock");
static std::map<Win, std::function<void()>> g_btn_fn; // every button of the window and its action
static std::map<Win, double> g_btn_native;
static std::map<Win, int> g_edit_max;                 // every text line of the window and its length (BUG-0482)            // when the game last delivered a click to it
static Win g_locked_btn;                              // a click under the lock waits for the game
static double g_locked_at;
static unsigned g_locked_clicks, g_locked_native;     // tests: clicks given / the game delivered itself
constexpr double kLockedClickWait = 0.5;

static int input_lock()
{
    uintptr_t get = H ? H->sym("Simulator::cGameInputManager::Get") : 0;
    using Get_t = uint8_t*(__cdecl*)();
    uint8_t* im = get ? ((Get_t)get)() : nullptr;
    return im && !IsBadReadPtr(im + 0x110, 4) ? *(int*)(im + 0x110) : 0;
}

// BUG-0482 follow-up: a modal box of the game (the "game saved" box, the window manager's modal window
// cWindowManager +0x82C, PushModal; gamefixes.cpp BUG-0270) takes the clicks like the input lock does: a
// chat line clicked while the host's "game saved" box was up never took the typing (play_creature, rel/0.7.4
// out/lab/20261103-224540). The locked paths (buttons, text lines) take any such box too. Off: only the
// input lock (0.7.4).
static bool g_modal_fix = true;
COOP_FIX("core.ui_modal_edit", "BUG-0482", g_modal_fix, "ui: the co-op window's buttons and chat line work while a modal box of the game (\"game saved\") is up");
static unsigned g_modal_seen; // tests: frames with a modal box of the game seen under a click

static void* game_modal()
{
    uintptr_t get = H ? H->sym("UTFWin::IWindowManager::Get") : 0;
    using Get_t = uint8_t*(__cdecl*)();
    uint8_t* wm = get ? ((Get_t)get)() : nullptr;
    void* m = wm && !IsBadReadPtr(wm + 0x82C, 4) ? *(void**)(wm + 0x82C) : nullptr;
    if (!m)
        return nullptr;
    for (Win w = (Win)m; w; w = sui::parent(w)) // our own window is no box of the game
        if (w == D.root)
            return nullptr;
    return m;
}

// BUG-0599: the window steps aside (hidden for a moment) while the mod clicks the game's "game saved" box under it
static double g_aside_until;
static bool root_up() { return D.root && (sui::visible(D.root) || (H && H->time_seconds() < g_aside_until)); }

// clicks on our window are dropped now: the input lock (a caption) or a modal box of the game
static bool clicks_locked()
{
    if (input_lock() > 0)
        return true;
    if (g_modal_fix && game_modal()) {
        g_modal_seen++;
        return true;
    }
    return false;
}

static Win add_button(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t,
                      std::function<void()> fn);
// The arrows of every arrow button (the owner 2026-11-13: triangles, not "<" ">", "make sure this is consistent"):
// U+25C4, U+25BA, U+25B2, U+25BC of the Co-op window's font, in the middle of the button (sui::center_caption).
static const char kArrowLeft[] = "◄", kArrowRight[] = "►", kArrowUp[] = "▲", kArrowDown[] = "▼";
static Win add_arrow_button(std::vector<Win>& page, const char* name, const Rect& r, const char* arrow, std::function<void()> fn)
{
    Win w = add_button(page, name, r, arrow, std::move(fn));
    if (w) sui::center_caption(w);
    return w;
}

static Win add_button(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t,
                      std::function<void()> fn)
{
    Win w = sui::button_like(D.root, D.like_button, r, t);
    if (!w)
        return nullptr;
    sui::set_font(w, D.font_body); // the dialog text font reads better than the small button font
    page.push_back(w);
    g_named[name] = w;
    g_cache[w] = Cache{t, 0, 1};
    sui::add_handler(w, [fn, w, name = std::string(name)](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg %s 0x%X src=%p %08X %08X %08X", name.c_str(), m.type, m.source, m.a, m.b, m.c);
        if (sui::is_click(m)) {
            trace::line("ui", "click %s", name.c_str());
            g_btn_native[w] = H->time_seconds();
            fn();
            return true;
        }
        return false;
    });
    g_btn_fn[w] = fn;
    return w;
}

// a click under the game's input lock on one of our visible buttons (BUG-0273); main thread
static void locked_click_tick(bool clicked)
{
    double now = H->time_seconds();
    if (g_locked_btn && now - g_locked_at >= kLockedClickWait) {
        Win w = g_locked_btn;
        g_locked_btn = nullptr;
        auto f = g_btn_fn.find(w);
        auto n = g_btn_native.find(w);
        if (n != g_btn_native.end() && n->second >= g_locked_at - 0.05)
            g_locked_native++; // the game delivered it after all: no second press
        else if (f != g_btn_fn.end() && D.root && sui::visible(D.root) && sui::visible(w)) {
            g_locked_clicks++;
            std::string nm;
            for (auto& [n, win] : g_named)
                if (win == w)
                    nm = n;
            H->log(M, SK_INFO, "ui: a click on the co-op window while the game's input is locked (a caption): pressed %s",
                   nm.c_str());
            static std::function<void()> s_fn;
            s_fn = f->second;
            H->run_outside_frame([](void*) { if (s_fn) s_fn(); }, nullptr);
        }
    }
    const bool tribal_fallback = g_tribal_clicks_fix && A.mode && A.mode() == modes::kTribe && tribe::village_orders();
    if (!clicked || !g_locked_clicks_fix || !mouse::g_fix || !g_want_open || !D.root || !sui::visible(D.root) ||
        g_locked_btn || (!clicks_locked() && !tribal_fallback))
        return;
    int cx = 0, cy = 0;
    mouse::last_click(cx, cy);
    for (auto& [w, fn] : g_btn_fn) {
        if (!sui::visible(w))
            continue;
        Rect r = sui::screen_rect(w);
        if (cx >= r.x1 && cx < r.x2 && cy >= r.y1 && cy < r.y2) {
            g_locked_btn = w;
            g_locked_at = now;
            return;
        }
    }
}

static Win add_edit(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t, int max_len,
                    bool draw_background = true)
{
    Win w = sui::text_edit(D.root, r, t, D.font_body, max_len, draw_background);
    if (w) {
        page.push_back(w);
        g_named[name] = w;
        g_edit_max[w] = max_len;
    }
    return w;
}

// BUG-0482: a click on one of our text lines under the game's input lock (a caption: a part found, the
// death, a nest claimed) is dropped like every click (BUG-0273), so the line never takes the focus and
// what the player types next goes nowhere (play_creature lost a chat line typed while the "new part"
// caption was up). The mod then takes the typing itself: the characters the game window takes from its
// queue (mouse::take_chars) go into that line, Backspace erases, Enter does what Enter in the line does
// (the chat: send). It stops at the next click anywhere else, when the window closes, or as soon as the
// game changes the line's text itself (it has the focus after all: never typed twice).
// `coop.fix core.ui_locked_edit off`: the typing is lost (0.7.3.6).
static void do_send_chat();
static bool g_locked_edit_fix = true;
COOP_FIX("core.ui_locked_edit", "BUG-0482", g_locked_edit_fix, "ui: the co-op window's chat line takes the typing while a caption holds the game's input lock");
static Win g_locked_edit;          // the line clicked under the lock, typed by the mod
static std::string g_locked_text;  // its text as the mod left it (the game changing it: it has the focus)
static unsigned g_locked_typed;    // tests: characters the mod typed
static std::u16string g_typed_queue;

static void locked_edit_apply(void*)
{
    Win w = g_locked_edit;
    std::u16string q;
    q.swap(g_typed_queue);
    if (!w || !root_up() || !sui::visible(w))
        return;
    std::string t = sui::edit_text(w);
    if (t != g_locked_text) { // the game typed into it itself
        g_locked_edit = nullptr;
        return;
    }
    int max_len = g_edit_max.count(w) ? g_edit_max[w] : 120;
    bool enter = false;
    for (char16_t c : q) {
        if (c == 0x0D) {
            enter = true;
            break;
        }
        if (c == 0x08) {
            while (!t.empty() && (t.back() & 0xC0) == 0x80) // a UTF-8 continuation byte
                t.pop_back();
            if (!t.empty())
                t.pop_back();
        } else if (c >= 0x20 && (int)t.size() < max_len) {
            char b[4];
            int n = c < 0x80 ? (b[0] = (char)c, 1)
                    : c < 0x800 ? (b[0] = (char)(0xC0 | (c >> 6)), b[1] = (char)(0x80 | (c & 0x3F)), 2)
                                : (b[0] = (char)(0xE0 | (c >> 12)), b[1] = (char)(0x80 | ((c >> 6) & 0x3F)),
                                   b[2] = (char)(0x80 | (c & 0x3F)), 3);
            t.append(b, n);
            g_locked_typed++;
        }
    }
    sui::set_edit_text(w, t);
    g_locked_text = sui::edit_text(w);
    if (enter && w == D.chat_edit) {
        do_send_chat();
        g_locked_text = sui::edit_text(w);
    }
}

// every frame (main thread): a click under the lock on a line starts the mod's typing, any other click ends it
// BUG-0551: a chat line typed while the host was paused saving the world for a returning guest was lost
// (play_creature, out/lab/20261104-135838): the mod took the typing (the "game saved" box is a modal window)
// and two things ended it: the mod's own click on the box's OK button (world.cpp box_task) counted as "a click
// anywhere else", and a frame held by the save took the click and the keys after it at once, and the keys of
// the frame of a click were dropped. The mod's own click no longer ends the typing, and the keys that came
// after the click in its frame go into the line. Off: both lost (0.7.7).
static bool g_locked_keep_fix = true;
COOP_FIX("core.ui_locked_edit_keep", "BUG-0551", g_locked_keep_fix, "ui: the chat line typed under the game saved box keeps its typing over the mod's own click on the box and a held frame");

static void locked_edit_tick(bool clicked)
{
    std::u16string after;
    std::u16string chars = mouse::take_chars(&after);
    if (clicked && g_locked_keep_fix && g_locked_edit && mouse::last_click_own())
        clicked = false; // the mod confirmed the game's box: the player goes on typing
    if (clicked) {
        g_locked_edit = nullptr;
        if (g_locked_edit_fix && mouse::g_fix && g_want_open && D.root && sui::visible(D.root) && clicks_locked()) {
            int cx = 0, cy = 0;
            mouse::last_click(cx, cy);
            for (auto& [w, n] : g_edit_max) {
                if (!sui::visible(w))
                    continue;
                Rect r = sui::screen_rect(w);
                if (cx >= r.x1 && cx < r.x2 && cy >= r.y1 && cy < r.y2) {
                    g_locked_edit = w;
                    g_locked_text = sui::edit_text(w);
                    H->log(M, SK_INFO, "ui: a click on a co-op text line while the game's input is locked (a caption): "
                                       "the mod takes the typing");
                    break;
                }
            }
        }
        if (!g_locked_keep_fix || !g_locked_edit || after.empty())
            return; // (characters of the frame of the click before it went to whatever had the focus before)
        chars = after; // the keys after the click in the same frame (a frame held by the save)
    }
    if (!g_locked_edit || chars.empty())
        return;
    if (!g_want_open || !root_up()) {
        g_locked_edit = nullptr;
        return;
    }
    bool queued = !g_typed_queue.empty();
    g_typed_queue += chars;
    if (!queued)
        H->run_outside_frame(locked_edit_apply, nullptr);
}

// ttl: seconds the text stays (0: until the session event that ends it, session_event)
static void set_status(const std::string& s, uint32_t c, double ttl = 0)
{
    g_status = s;
    g_status_color = c;
    g_status_until = ttl > 0 ? H->time_seconds() + ttl : 0;
    g_dirty = true;
}

// BUG-0104: "Connecting to ..." stayed for the whole session (the lobby showed it as the world line)
// and hid the update line on the connect page for good; the end of the join or of the session now
// replaces it
void session_event(bool connected, const std::string& why)
{
    if (!g_status_fix || !g_ok)
        return;
    if (connected)
        set_status("", kYellow);
    else
        set_status(std::string(T("Disconnected: ", "Отключено: ")) + why, kRed, 30);
}

static void save_str(const char* section, const char* key, const std::string& v)
{
    // BUG-0245: SporeKit refuses a value that is not ASCII when the file could not be made Unicode
    // (it became "????" before): say so instead of losing the typed text silently at the next start
    if (H->config_set_str && H->config_set_str(M, section, key, v.c_str()) != 0)
        notify::show(std::string("could not save the ") + key + ", try again", "danger");
}
static std::string load_str(const char* section, const char* key, const char* def)
{
    char b[256];
    H->config_str(M, section, key, def, b, sizeof b);
    return b;
}

static void apply_name()
{
    std::string n = sui::edit_text(D.name_edit);
    if (!n.empty() && n != A.name()) {
        A.set_name(n);
        save_str("coop", "name", n);
    }
}

static void do_join(const std::string& address)
{
    apply_name();
    std::string addr = address;
    if (net::g_fix.addr_check) // BUG-0138: a leading space or the whole pasted hint line
        addr = net::Endpoint::clean(addr);
    while (!addr.empty() && addr.back() == ' ')
        addr.pop_back();
    if (addr.empty()) {
        set_status(T("Enter the host address, e.g. 192.168.1.5:47810", "Введите адрес хоста, например 192.168.1.5:47810"),
                   kRed);
        return;
    }
    const auto port = sui::edit_text(D.join_port);
    if (addr.find(':') == std::string::npos && !port.empty())
        addr += ":" + port;
    std::string err;
    if (!A.join(addr, err, sui::edit_text(D.join_password))) {
        set_status(std::string(T("Cannot join: ", "Не удалось подключиться: ")) + err, kRed, g_status_fix ? 30 : 0);
        return;
    }
    save_str("ui", "last_address", addr);
    set_status(T("Connecting...", "Подключение..."), kYellow);
}

static void eye_button(std::vector<Win>& page, const char* name, Rect area, std::function<void()> toggle)
{
    const auto group = sui::fnv("sporecoop");
    Win button = add_button(page, name, area, "", std::move(toggle));
    Win eye = nullptr, crossed = nullptr;
    if (sui::has_image(sui::fnv("lineages_eye"), group) && sui::has_image(sui::fnv("lineages_eye_off"), group)) {
        const float x = area.x1 + (area.w() - 20.f) / 2;
        const float y = area.y1 + (area.h() - 14.f) / 2;
        eye = sui::image_window(D.root, sui::fnv("lineages_eye"), group, x, y);
        crossed = sui::image_window(D.root, sui::fnv("lineages_eye_off"), group, x, y);
        for (Win icon : {eye, crossed}) {
            if (!icon) continue;
            sui::set_flag(icon, sui::kFlagIgnoreMouse, true);
            page.push_back(icon);
        }
        if (button && eye && crossed)
            sui::set_drawable(button, nullptr); // the field itself is the button background
    }
    D.privacy[name] = {button, eye, crossed};
}

static void show_eye(const char* name, bool hidden)
{
    const auto& eye = D.privacy.at(name);
    if (eye.eye && eye.crossed) {
        show(eye.eye, !hidden);
        show(eye.crossed, hidden);
    } else
        text(eye.button, hidden ? "Show" : "Hide");
}

static void set_field_hidden(Win field, bool hidden)
{
    D.hidden_fields[field] = hidden;
    sui::set_edit_masked(field, hidden);
}

static void privacy_button(std::vector<Win>& page, const char* name, Rect area, Win field, bool hidden)
{
    set_field_hidden(field, hidden);
    eye_button(page, name, area, [field] {
        set_field_hidden(field, !D.hidden_fields[field]);
        g_dirty = true;
    });
}

static std::string connection_address()
{
    auto address = sui::edit_text(D.addr_edit);
    const auto port = sui::edit_text(D.join_port);
    if (!address.empty() && address.find(':') == std::string::npos && !port.empty())
        address += ":" + port;
    return address;
}

static void load_servers()
{
    if (D.servers_loaded) return;
    D.servers_loaded = true;
    for (size_t i = 0; i < kServerHistoryLimit; ++i) {
        const auto key = std::to_string(i);
        auto address = load_str("servers", ("address" + key).c_str(), "");
        if (!address.empty())
            D.saved_servers.push_back({std::move(address), load_str("servers", ("name" + key).c_str(), "")});
    }
}

static void persist_servers(size_t previous_size)
{
    for (size_t i = 0; i < std::max(previous_size, D.saved_servers.size()); ++i) {
        const auto key = std::to_string(i);
        const auto saved = i < D.saved_servers.size() ? D.saved_servers[i] : Dialog::SavedServer{};
        save_str("servers", ("address" + key).c_str(), saved.address);
        save_str("servers", ("name" + key).c_str(), saved.name);
    }
}

void remember_server(const std::string& address, const std::string& name)
{
    if (!g_ok || address.empty()) return;
    load_servers();
    const auto count = D.saved_servers.size();
    std::erase_if(D.saved_servers, [&](const Dialog::SavedServer& server) { return server.address == address; });
    D.saved_servers.insert(D.saved_servers.begin(), {address, name});
    if (D.saved_servers.size() > kServerHistoryLimit) D.saved_servers.pop_back();
    persist_servers(count);
    g_lan_first = 0;
    g_dirty = true;
}

static bool server_list_input(unsigned message, uintptr_t value, int x, int y)
{
    if (!D.root || !g_want_open || A.role() != 0 || !sui::visible(D.root)) {
        g_lan_dragging = false;
        g_lan_wheel_delta = 0;
        return false;
    }
    const auto root = sui::screen_rect(D.root);
    const float local_x = x - root.x1, local_y = y - root.y1;
    const bool over_list = local_x >= 24 && local_x < 360 && local_y >= 171 && local_y < 335;
    if (message == WM_MOUSEWHEEL && over_list) {
        g_lan_wheel_delta += (short)HIWORD(value);
        g_lan_first = std::clamp(g_lan_first - g_lan_wheel_delta / WHEEL_DELTA, 0, g_lan_max);
        g_lan_wheel_delta %= WHEEL_DELTA;
        g_dirty = true;
        return true;
    }
    if (message == WM_LBUTTONDOWN && over_list && local_x >= 338 && g_lan_max > 0)
        g_lan_dragging = true;
    if (!g_lan_dragging) return false;
    if (message == WM_LBUTTONUP || (message == WM_MOUSEMOVE && !(value & MK_LBUTTON))) {
        g_lan_dragging = false;
        return true;
    }
    if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE) {
        const float ratio = std::clamp((local_y - 192.f) / 122.f, 0.f, 1.f);
        g_lan_first = (int)(ratio * g_lan_max + .5f);
        g_dirty = true;
        return true;
    }
    return false;
}

static void do_host()
{
    apply_name();
    int port = atoi(sui::edit_text(D.port_edit).c_str());
    if (port <= 0 || port > 65535)
        port = A.default_port;
    std::string err;
    auto seats = std::to_string(D.seats);
    auto scaling = sui::edit_text(D.scaling_edit);
    if (seats.size() != 1 || seats[0] < '1' || seats[0] > '4' ||
        scaling.empty() || scaling.find_first_not_of("0123456789") != std::string::npos ||
        scaling.size() > 3 || strtoul(scaling.c_str(),nullptr,10) > founders::kMaxDifficultyPercent) {
        set_status(T("Use 1-4 player slots and a difficulty from 0 to 400%.", "Укажите 1–4 места для игроков и сложность от 0 до 400%."),kRed);
        return;
    }
    if (!A.host((uint16_t)port,D.seats,unsigned(strtoul(scaling.c_str(),nullptr,10)),err,
                sui::edit_text(D.host_password))) {
        set_status(std::string(T("Cannot host: ", "Не удалось создать игру: ")) + err, kRed);
        return;
    }
    save_str("ui","new_founders",seats);
    save_str("ui","new_scaling",scaling);
    save_str("ui", "host_port", std::to_string(port));
    set_status("", kYellow);
}

// "Report a problem": SporeKit writes mods\logs\SporeCoop-report-<time>.zip (environment, co-op state,
// the logs, faults, the screenshots of the marks); the folder opens with the file selected so the
// player can attach it. A screenshot of the screen at the click goes in first: SporeKit takes it at
// the next frame, the archive is written once it is there (at most kReportShotWait s later).
static constexpr double kReportShotWait = 4.0;
static double g_report_wait_until;  // > 0: the report waits for its screenshot
static bool g_report_queued;        // finish_report is queued outside the frame
static int g_report_shot;           // what report_shot answered (1 requested, 0 not, -1 off)

static void finish_report(void*)
{
    g_report_queued = false;
    bool shot = g_report_shot > 0 && A.report_shot_state && A.report_shot_state() == 0;
    std::string p = A.report ? A.report() : "";
    if (p.empty()) {
        set_status(T("This SporeKit cannot write reports (update SporeCoop)", "Этот SporeKit не умеет писать отчёты (обновите SporeCoop)"),
                   kRed);
        return;
    }
    set_status(std::string(T("Report saved: ", "Отчёт сохранён: ")) + p.substr(p.find_last_of("\\/") + 1) +
                   T(" - attach it to an issue on GitHub; the other players should send theirs too",
                     " - приложите его к issue на GitHub; другие игроки пусть пришлют свои") +
                   (shot ? T(". A screenshot of the game is included (coop.ini [report] screenshot=0 turns it off)",
                             ". В отчёт входит снимок экрана игры (отключить: coop.ini [report] screenshot=0)")
                         : ""),
               kGreen);
    std::wstring w(p.begin(), p.end()); // the path is ASCII-safe enough for /select; UTF-8 below if not
    int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    if (n > 0) {
        w.resize(n);
        MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, w.data(), n);
        w.resize(n - 1);
    }
    std::wstring args = L"/select,\"" + w + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

static void do_report()
{
    if (g_report_wait_until > 0 || g_report_queued)
        return; // one is being made
    for (Win password : {D.join_password, D.host_password}) {
        if (!password) continue;
        set_field_hidden(password, true);
    }
    g_dirty = true;
    g_report_shot = A.report_shot ? A.report_shot() : 0;
    if (g_report_shot > 0) {
        H->log(M, SK_INFO, "ui: report: waiting for its screenshot");
        g_report_wait_until = H->time_seconds() + kReportShotWait;
        set_status(T("Making the report...", "Создаю отчёт..."), kYellow);
        return;
    }
    finish_report(nullptr);
}

// tick: the report's screenshot is written (or failed, or did not come): the archive, outside the frame
static void report_tick(double now)
{
    if (g_report_wait_until <= 0 || g_report_queued)
        return;
    if (A.report_shot_state && A.report_shot_state() == 1 && now < g_report_wait_until)
        return;
    g_report_wait_until = 0;
    g_report_queued = true;
    H->run_outside_frame(finish_report, nullptr);
}

// BUG-0597: why "Send my world" sends nothing now (0: it sends; 1 no world loaded; 2 nobody joined)
static int share_blocked()
{
    if (!g_share_explain_fix || A.role() != 1 || (A.vote_active && A.vote_active()) || !A.share_state)
        return 0; // (during a vote the button is "Start now")
    return A.share_state();
}

static void do_send_chat()
{
    std::string t = sui::edit_text(D.chat_edit);
    if (t.empty())
        return;
    A.say(t);
    sui::set_edit_text(D.chat_edit, "");
    g_dirty = true;
}

static void build_tooltip(); // BUG-9107
static Win g_tip_bg, g_tip_text;
static std::string g_tip_want, g_tip_shown;
static void build_dialog()
{
    Win root = sui::find(sui::main_window(), kIdDialog);
    if (root) { // left over from before a hot reload: its handlers are dead
        if (Win p = sui::parent(root))
            sui::remove_child(p, root);
    }
    root = sui::load_layout(kDialogLayout);
    if (!root) {
        H->log(M, SK_ERROR, "ui: cannot load the dialog layout %08X", kDialogLayout);
        return;
    }
    D = Dialog{};
    g_named.clear();
    g_btn_fn.clear();
    g_edit_max.clear();
    g_locked_edit = nullptr;
    g_btn_native.clear();
    g_locked_btn = nullptr;
    D.root = root;
    D.layer = sui::parent(root);
    sui::add_ref(root); // ours: the window survives mode changes that clear the layer
    sui::set_ids(root, kIdDialog, 0);
    Win text_win = sui::find(root, 0x0451D381), login = sui::find(root, 0x0519E168);
    D.font_body = sui::font(text_win);
    g_text_font = D.font_body;
    H->log(M, SK_DEBUG, "ui: dialog font %08X", D.font_body);
    D.like_button = login;
    D.font_button = sui::font(login);
    if (g_bright_buttons && login) { // BUG-9179: every copy of the model button takes this look
        void* game_look = sui::drawable(login);
        void* gold = button_look(game_look, "gold");
        g_red_look = gold ? button_look(game_look, "red") : nullptr;
        if (gold) sui::set_drawable(login, gold);
        H->log(M, gold && g_red_look ? SK_INFO : SK_WARN, "ui: bright buttons: gold %p red %p%s (BUG-9179)", gold,
               g_red_look, gold ? "" : ": the game's look (no lineages_button images in the package, or another class)");
    }
    // keep the frame, glow, logo and close button; hide the account dialog's own content
    for (uint32_t id : {0x0451D381u, 0x0519E168u, 0x06555818u, 0x065D20A8u, 0x0519D9B8u, 0x0451A4C0u})
        if (Win w = sui::find(root, id))
            sui::set_visible(w, false);
    if (Win x = sui::find(root, 0xFFFFFFF2u)) {
        g_named["close"] = x;
        sui::add_handler(x, [](Win, const sui::Msg& m) {
            if (sui::is_click(m)) {
                open(false);
                return true;
            }
            return false;
        });
    }
    place_window(root);
    // The account dialog draws the logo and the hex frame in separate widgets.
    for (Win child : sui::children(root)) {
        const Rect r = sui::area(child);
        if (sui::control_id(child) || r.y1 != 10.f || r.y2 - r.y1 != 49.f ||
            r.x2 - r.x1 != 178.f || !sui::drawable(child))
            continue;
        // The native widget and the export both have transparent edge padding.
        constexpr float suffix_offset = 158.f;
        constexpr float logo_x = (kW - suffix_offset - 400.f) / 2;
        if (!sui::has_image(sui::fnv("lineages_wordmark_suffix"), sui::fnv("sporecoop"))) break;
        if (Win suffix = sui::image_window(root, sui::fnv("lineages_wordmark_suffix"), sui::fnv("sporecoop"),
                                           logo_x + suffix_offset, 6.f)) {
            sui::set_flag(suffix, sui::kFlagIgnoreMouse, true);
            const float offset = logo_x - sui::real_area(child).x1;
            sui::set_area(child, {r.x1 + offset, r.y1, r.x2 + offset, r.y2});
        }
        break;
    }

    std::vector<Win> common;
    D.subtitle = add_label(common, {0, 70, kW, 96}, T("CO-OP", "КООПЕРАТИВ"), kGold, sui::kAlignCenter);
    // every page: which SporeCoop and game build run, and a report for bug reports
    D.version = add_label(common, {40, 553, 437, 573}, A.version_line ? A.version_line() : "", kDim);
    if (D.version) { sui::set_font(D.version, D.font_button); g_small_font[D.version] = true; } // a quiet footer line
    D.report_btn = add_button(common, "report", {545, 98, 720, 134}, T("Report Issue", "Сообщить об ошибке"),
                              do_report);
    D.report_caption = shade_danger_button(common, D.report_btn, T("Report Issue", "Сообщить об ошибке"));

    // ---- connect page ----
    auto& P = D.page_connect;
    D.connect_version = add_label(P, {36, 80, 319, 108}, "", kDim);
    const auto panel_id = sui::fnv("lineages_connect_panels");
    const auto ui_group = sui::fnv("sporecoop");
    Win panel = sui::has_image(panel_id, ui_group) ? sui::image_window(root, panel_id, ui_group, 24, 171) : nullptr;
    if (panel) P.push_back(panel);
    add_label(P, {250, 79, 510, 109}, "Username", kGold, sui::kAlignCenter);
    D.name_edit = add_edit(P, "name", {250, 109, 510, 140}, A.name(), 24);
    add_label(P, {36, 142, 225, 171}, "Server List", kGold);
    add_label(P, {393, 142, 718, 171}, "Connection Info", kGold);
    add_button(P, "refreshlan", {243, 142, 352, 171}, "Refresh", [] {
        g_lan_first = 0;
        if (A.lan_scan) { A.lan_scan(false); A.lan_scan(true); }
        g_dirty = true;
    });
    D.lan_hdr = add_label(P, {37, 177, 334, 199}, "", kDim);
    D.lan_track = sui::image_window(root, sui::fnv("lineages_scroll_track"), sui::fnv("sporecoop"), 342, 178);
    D.lan_thumb = sui::image_window(root, sui::fnv("lineages_scroll_thumb"), sui::fnv("sporecoop"), 342, 178);
    for (Win part : {D.lan_track, D.lan_thumb}) {
        if (!part) continue;
        sui::set_flag(part, sui::kFlagIgnoreMouse, true);
        P.push_back(part);
    }
    for (int i = 0; i < kLanRows; ++i) {
        char name[16];
        snprintf(name, sizeof name, "lan%d", i);
        D.lan_rows[i] = add_button(P, name, {35, 177.f + i * 26, 334, 202.f + i * 26}, "", [i] {
            if (D.lan_addr[i].empty()) return;
            const auto& address = D.lan_addr[i];
            auto split = address.rfind(':');
            sui::set_edit_text(D.addr_edit, address.substr(0, split));
            sui::set_edit_text(D.join_port, split == std::string::npos ? "" : address.substr(split + 1));
            sui::set_edit_text(D.join_password, "");
            g_dirty = true;
        });
    }
    // field labels end beside their fields (the frames start at x 489)
    add_label(P, {380, 177, 484, 205}, "Address", kWhite, sui::kAlignRight);
    add_label(P, {380, 209, 484, 237}, "Port", kWhite, sui::kAlignRight);
    add_label(P, {380, 241, 484, 269}, "Password", kWhite, sui::kAlignRight);
    const auto last_address = load_str("ui", "last_address", "");
    const auto last_port = last_address.rfind(':');
    D.addr_edit = add_edit(P, "address", {489, 177, 677, 205}, last_address.substr(0, last_port), 64, !panel);
    D.join_port = add_edit(P, "joinport", {489, 209, 677, 237},
                           last_port == std::string::npos ? "47810" : last_address.substr(last_port + 1), 5, !panel);
    D.join_password = add_edit(P, "joinpassword", {489, 241, 677, 269}, "", 48, !panel);
    load_servers();
    privacy_button(P, "hideaddress", {678, 177, 718, 205}, D.addr_edit, false);
    privacy_button(P, "hidejoinport", {678, 209, 718, 237}, D.join_port, false);
    privacy_button(P, "hidejoinpassword", {678, 241, 718, 269}, D.join_password, true);
    D.selected_info = add_label(P, {402, 273, 712, 295}, "", kDim); // inside its box, off the border
    D.join_btn = add_button(P, "join", {393, 298, 533, 331}, "Join",
                            [] { do_join(sui::edit_text(D.addr_edit)); });
    add_button(P, "saveserver", {539, 298, 624, 331}, "Save", [] {
        auto address = connection_address();
        const auto count = D.saved_servers.size();
        if (address.empty() || count == kServerHistoryLimit ||
            std::any_of(D.saved_servers.begin(), D.saved_servers.end(),
                        [&](const Dialog::SavedServer& server) { return server.address == address; })) return;
        D.saved_servers.push_back({std::move(address), {}});
        persist_servers(count);
        g_dirty = true;
    });
    Win remove = add_button(P, "deleteserver", {630, 298, 718, 331}, "Delete", [] {
        const auto address = connection_address();
        auto it = std::find_if(D.saved_servers.begin(), D.saved_servers.end(),
                               [&](const Dialog::SavedServer& server) { return server.address == address; });
        if (it == D.saved_servers.end()) return;
        const auto count = D.saved_servers.size();
        D.saved_servers.erase(it);
        persist_servers(count);
        g_dirty = true;
    });
    Win delete_caption = shade_danger_button(P, remove, "Delete");
    if (delete_caption) sui::set_caption(remove, "");

    add_label(P, {24, 336, 735, 366}, "Host Server", kGold, sui::kAlignCenter);
    // one row every 29 px, 26 px tall, labels ending beside the fields (x 140); the seat and transfer
    // buttons have the fields' height and span the field and its eye button (140 to 351)
    add_label(P, {30, 373, 135, 399}, "Port (UDP)", kWhite, sui::kAlignRight);
    add_label(P, {30, 402, 135, 428}, "Password", kWhite, sui::kAlignRight);
    add_label(P, {24, 431, 135, 457}, "Player Slots", kWhite, sui::kAlignRight); // owner 2026-11-12: "Slots", not "seats"
    add_label(P, {30, 460, 135, 486}, "Transfer", kWhite, sui::kAlignRight);
    D.port_edit = add_edit(P, "port", {140, 373, 310, 399}, load_str("ui", "host_port", std::to_string(A.default_port).c_str()), 5, !panel);
    D.host_password = add_edit(P, "hostpassword", {140, 402, 310, 428}, "", 48, !panel);
    privacy_button(P, "hidehostport", {311, 373, 351, 399}, D.port_edit, false);
    privacy_button(P, "hidehostpassword", {311, 402, 351, 428}, D.host_password, true);
    D.seats = std::clamp(atoi(load_str("ui", "new_founders", "2").c_str()), 1, 4);
    for (unsigned i = 0; i < 4; ++i) {
        const auto number = std::to_string(i + 1);
        D.seat_btn[i] = add_button(P, ("seat" + number).c_str(), {140.f + i * 54, 431, 189.f + i * 54, 457},
                                   number, [i] { D.seats = i + 1; g_dirty = true; });
    }
    for (unsigned i = 0; i < 2; ++i) {
        D.transfer_btn[i] = add_button(P, i == 0 ? "optimal" : "fast", {140.f + i * 108, 460, 243.f + i * 108, 486},
                                       i == 0 ? "Optimal" : "Fast", [i] {
            if (A.set_paced_transfer) A.set_paced_transfer(i == 0);
            save_str("coop", "paced_world_transfer", i == 0 ? "1" : "0");
            g_dirty = true;
        });
    }
    D.host_hint = add_label(P, {50, 492, 304, 532}, "", kDim); // inside its box, off the border
    eye_button(P, "hidehostaddresses", {311, 491, 351, 531}, [] {
        D.show_host_addresses = !D.show_host_addresses;
        g_dirty = true;
    });

    // "Difficulty [ 100 ] % per extra seat": the field has the panel's field frame (painted in
    // lineages_connect_panels.png at 536..601 x 372..398) like Port and Password
    add_label(P, {393, 372, 531, 398}, "Difficulty", kGold, sui::kAlignRight);
    D.scaling_edit = add_edit(P, "founderscaling", {537, 373, 600, 397}, load_str("ui", "new_scaling", "100"), 3, !panel);
    if (Win unit = add_label(P, {606, 372, 718, 398}, "% per extra slot", kDim)) {
        use_small_font(unit);
        fit(unit, g_cache[unit]);
    }
    constexpr const char* labels[] = {"Objective effort", "Enemy HP / damage", "Creature hunger drain",
                                      "City takeover resistance", "Civ military interval"};
    for (int i = 0; i < 5; ++i) {
        const float y = 400.f + i * 19;
        // a table: names from the left edge, values ending at the right edge
        Win label = add_label(P, {400, y, 614, y + 19}, labels[i], kDim);
        D.difficulty_value[i] = add_label(P, {615, y, 708, y + 19}, "", kWhite, sui::kAlignRight);
        if (label) { use_small_font(label); fit(label, g_cache[label]); }
        use_small_font(D.difficulty_value[i]);
    }
    D.host_btn = add_button(P, "host", {393, 497, 718, 534}, "Start Server", do_host);
    D.status = add_label(P, {40, 535, 503, 558}, "", kYellow);
    D.update_btn = add_button(P, "update", {515, 538, 718, 572}, "", [] {
        if (A.update_do) A.update_do();
        g_dirty = true;
    });
    // The smaller native button font keeps narrow actions legible at 800×600.
    for (Win w : P)
        if (g_btn_fn.contains(w) || w == delete_caption)
            sui::set_font(w, D.font_button);
    if (D.connect_version) sui::set_font(D.connect_version, D.font_button);
    // ---- lobby page ----
    auto& L = D.page_lobby;
    if (Win panel = sui::image_window(root, sui::fnv("lineages_lobby_panels"), sui::fnv("sporecoop"), 34, 156))
        L.push_back(panel);
    // owner 2026-11-12: "Player Slots" with the count and an Apply button (more slots now, fewer from the next stage)
    D.slots_label = add_label(L, {40, 496, 150, 528}, T("Player Slots", "Места"), kWhite);
    D.next_seats_edit = add_edit(L,"nextseats",{152,496,194,528},"",1);
    D.queue_seats = add_button(L,"queueseats",{198,493,302,531},T("Apply","Применить"),[] {
        auto value = sui::edit_text(D.next_seats_edit);
        if (value.size() != 1 || value[0] < '1' || value[0] > '4') {
            set_status(T("Enter 1 to 4 player slots.","Укажите от 1 до 4 мест."),kRed,8);
            return;
        }
        std::string message;
        bool ok = A.queue_seats(value[0] - '0',message);
        set_status(message,ok ? kGreen : kRed,10);
        g_dirty = true;
    });
    D.session = add_label(L, {40, 103, 530, 129}, "", kWhite);
    D.invite_btn = add_button(L, "copyinvite", {475, 540, 720, 576}, T("Copy Host Address", "Копировать адрес"), [] {
        const auto addresses = A.invite ? A.invite() : std::string{};
        if (addresses.empty()) {
            set_status(T("No local address available", "Нет доступного адреса"), kYellow, 5);
            return;
        }
        const std::string invite = "Friends connect to: " + addresses;
        bool copied = false;
        if (OpenClipboard((HWND)H->game_window())) {
            if (HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, invite.size() + 1)) {
                if (void* bytes = GlobalLock(data)) {
                    memcpy(bytes, invite.c_str(), invite.size() + 1);
                    GlobalUnlock(data);
                    copied = EmptyClipboard() && SetClipboardData(CF_TEXT, data);
                }
                if (!copied) GlobalFree(data);
            }
            CloseClipboard();
        }
        set_status(copied ? T("Host address copied", "Адрес скопирован") : T("Could not copy host address", "Не удалось скопировать адрес"),
                   copied ? kGreen : kYellow, 5);
    });
    add_label(L, {40, 131, 380, 156}, T("Players", "Игроки"), kGold);
    add_label(L, {416, 131, 720, 156}, T("World & Session", "Мир и сессия"), kGold);
    D.rules = add_label(L, {422, 165, 710, 188}, "", kDim);
    D.transfer_mode = add_label(L, {416, 190, 710, 213}, "", kDim);
    D.preview_status = add_label(L, {40, 534, 438, 552}, "", kDim);
    for (int i = 0; i < 4; i++) {
        float y = 163.f + i * 30;
        // name and stage from the left, the ping ending at the column's right edge ("103 ms" never wraps)
        D.row_name[i] = add_label(L, {45, y, 175, y + 26}, "", kWhite);
        D.row_stage[i] = add_label(L, {178, y, 296, y + 26}, "", kWhite);
        D.row_ping[i] = add_label(L, {298, y, 362, y + 26}, "", kDim, sui::kAlignRight);
        D.row_ready[i] = add_label(L, {364, y, 382, y + 26}, "", kDim, sui::kAlignCenter);
        use_small_font(D.row_ping[i]);
    }
    // the session's state line on its own row between the panels, centred (it read as part of the
    // "Session Chat" heading below it; the chat box needs no heading)
    D.world = add_label(L, {40, 298, 720, 330}, "", kYellow, sui::kAlignCenter);
    for (int i = 0; i < 4; i++) { // the smaller font: four lines fit the box without overlapping
        D.chat_lines[i] = add_label(L, {44, 340.f + i * 16.5f, 716, 356.5f + i * 16.5f}, "", kWhite);
        if (D.chat_lines[i]) { sui::set_font(D.chat_lines[i], D.font_button); g_small_font[D.chat_lines[i]] = true; }
    }
    std::vector<Win> arrows; // shown by refresh_lobby only when there is more to see
    D.chat_up = add_arrow_button(arrows, "chatup", {722, 337, 756, 367}, kArrowUp, [] { g_chat_scroll++; g_dirty = true; });
    D.chat_down = add_arrow_button(arrows, "chatdown", {722, 376, 756, 406}, kArrowDown, [] { g_chat_scroll--; g_dirty = true; });
    // the own creation of this stage: "My creature: Kirgle  < >"
    D.creation = add_label(L, {40, 416, 560, 444}, "", kGold); // a row of its own under the chat box
    D.creation_prev = add_arrow_button(L, "cprev", {570, 415, 640, 445}, kArrowLeft, [] {
        // in Civ the row is the role, except for a late joiner without a nation (civ::late_prompt): their creature
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle && !(A.late_prompt && A.late_prompt()))
            A.civ_role_cycle(-1);
        else if (A.creation_cycle)
            A.creation_cycle(-1);
        g_dirty = true;
    });
    D.jump_btn = add_button(L, "jump", {400, 415, 560, 445}, "", [] {
        if (A.jump_do)
            A.jump_do();
        g_dirty = true;
    });
    D.give_btn = add_button(L, "give", {570, 415, 720, 445}, "", [] {
        if (A.give_do)
            A.give_do();
        g_dirty = true;
    });
    // 3 or 4 players: ">" picks the next other player as the recipient (BUG-0075)
    D.give_next = add_arrow_button(L, "givenext", {520, 415, 560, 445}, kArrowRight, [] {
        if (A.give_cycle)
            A.give_cycle();
        g_dirty = true;
    });
    D.creation_next = add_arrow_button(L, "cnext", {650, 415, 720, 445}, kArrowRight, [] {
        // in Civ the row is the role, except for a late joiner without a nation (civ::late_prompt): their creature
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle && !(A.late_prompt && A.late_prompt()))
            A.civ_role_cycle(1);
        else if (A.creation_cycle)
            A.creation_cycle(1);
        g_dirty = true;
    });
    D.space_btn = add_button(L, "space", {308, 493, 494, 531}, T("Space >", "Космос >"), [] {
        g_space_page = true;
        g_dirty = true;
    });
    D.chat_edit = add_edit(L, "chatedit", {40, 453, 560, 483}, "", 120);
    D.send_btn = add_button(L, "send", {570, 451, 720, 485}, T("Send", "Отправить"), do_send_chat);
    D.ready_btn = add_button(L, "ready", {445, 249, 706, 285}, "", [] {
        A.set_ready(!A.ready());
        g_dirty = true;
    });
    // owner 2026-11-12: no "Send World" button any more (the world goes to every player by itself: on a join, a load and
    // a stage change; `coop.share` stays for support). The button is the host's "Start Now" while a ready vote runs.
    D.share_btn = add_button(L, "share", {445, 190, 706, 222}, T("Start Now", "Начать сейчас"), [] {
        if (A.vote_active && A.vote_active()) {
            A.share_world(); // during a vote: finish it now
            return;
        }
        int why = share_blocked();
        if (why) { // BUG-0597: say why nothing is sent (the button is greyed out, a locked click still comes here)
            set_status(why == 1 ? T("Start or load a game first: friends who join get it by themselves",
                                    "Сначала начните игру: вошедшие друзья получат её сами")
                                : T("Nobody has joined yet: friends who join get your world by themselves",
                                    "Пока никто не вошёл: друзья получат ваш мир сами"),
                       kYellow, 8);
            return;
        }
        A.share_world();
        set_status(T("Sending the world to everyone...", "Мир отправляется всем..."), kYellow, g_status_fix ? 5 : 0);
    });
    // BUG-9105: the player's own choice; placed by refresh_lobby (under Send World for the host)
    D.onward_btn = add_button(L, "foreigncreations", {445, 249, 706, 285}, "", [] {
        if (!A.foreign_creations || !A.set_foreign_creations) return;
        const bool on = !A.foreign_creations();
        A.set_foreign_creations(on);
        set_status(on ? T("You'll also get creations your partners received from others",
                          "Вы получите и создания, которые партнёры получили от других")
                      : T("You only get your partners' own creations",
                          "Вы получаете только собственные создания партнёров"),
                   on ? kGreen : kYellow, 8);
        g_dirty = true;
    });
    // BUG-9135 roadmap phase 4: a partner in Tribe without a creature of their own makes one (in the vote's Ready place)
    D.create_btn = add_button(L, "createcreature", {445, 249, 706, 285}, T("Create New Creature", "Создать существо"), [] {
        if (!A.create_creature) return;
        A.create_creature();
        set_status(T("Opening the creature editor...", "Открывается редактор существ..."), kYellow, 5);
        open(false);
    });
    // BUG-9111: the host's Player Cells setting, beside Send World (placed by refresh_lobby)
    D.cells_btn = add_button(L, "playercells", {445, 224, 706, 256}, "", [] {
        if (!A.lineage_cells || !A.set_lineage_cells) return;
        const bool on = !A.lineage_cells();
        A.set_lineage_cells(on);
        set_status(on ? T("Your group's cells now appear in the tide pool", "Клетки вашей группы появятся в водоёме")
                      : T("Only the game's own cells appear in the tide pool", "В водоёме только клетки игры"),
                   on ? kGreen : kYellow, 8);
        g_dirty = true;
    });
    D.leave_btn = add_button(L, "leave", {500, 493, 720, 531}, "", [] {
        A.leave();
        set_status(T("Left the session", "Вы вышли из игры"), kYellow, g_status_fix ? 5 : 0);
    });
    D.leave_caption = shade_danger_button(L, D.leave_btn, "");
    // ---- Space page ----
    auto& S = D.page_space;
    D.space_title = add_label(S, {40, 104, 530, 130}, T("Your galaxy together", "Галактика вместе"), kGold);
    for (int i = 0; i < kSpaceRows; i++) {
        float y = 136.f + i * 29;
        char nm[16];
        D.space_text[i] = add_label(S, {40, y + 2, 545, y + 27}, "", kWhite);
        snprintf(nm, sizeof nm, "sbtn%d", i);
        D.space_button[i] = add_button(S, nm, {555, y, 720, y + 27}, "", [i] {
            if (!D.space_action[i])
                return;
            // game calls and UI changes of a feature: outside the frame
            H->run_outside_frame(
                [](void* p) {
                    auto* f = (std::function<void()>*)p;
                    (*f)();
                    delete f;
                },
                new std::function<void()>(D.space_action[i]));
            g_dirty = true;
        });
    }
    D.space_back = add_button(S, "sback", {40, 488, 240, 528}, T("< Lobby", "< Лобби"), [] {
        g_space_page = false;
        g_dirty = true;
    });
    // more rows than fit: pages
    D.space_prev = add_arrow_button(S, "sprev", {500, 488, 570, 528}, kArrowLeft, [] {
        g_space_first = std::max(0, g_space_first - kSpaceRows);
        g_dirty = true;
    });
    D.space_pageno = add_label(S, {575, 494, 645, 522}, "", kDim, sui::kAlignCenter);
    D.space_next = add_arrow_button(S, "snext", {650, 488, 720, 528}, kArrowRight, [] {
        g_space_first += kSpaceRows;
        g_dirty = true;
    });
    // Enter in the chat field sends the message
    if (D.chat_edit)
        sui::add_handler(D.chat_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_send_chat();
                return true;
            }
            return false;
        });
    if (D.addr_edit)
        sui::add_handler(D.addr_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_join(sui::edit_text(D.addr_edit));
                return true;
            }
            return false;
        });
    for (Win w : common)
        (void)w;
    g_tip_bg = g_tip_text = nullptr; // BUG-9107: made at the first hover, after every page widget
    g_tip_shown.clear();
    sui::set_visible(root, false);
    g_cache[root] = Cache{"", 0, 0};
    H->log(M, SK_INFO, "ui: co-op dialog ready");
}

static void refresh_connect()
{
    const bool paced = A.paced_transfer && A.paced_transfer();
    for (unsigned i = 0; i < 2; ++i)
        if (D.transfer_btn[i]) sui::set_shade_color(D.transfer_btn[i], (i == 0) == paced ? 0xFFFFFFFF : 0xFF949CA8);
    for (unsigned i = 0; i < 4; ++i)
        if (D.seat_btn[i]) sui::set_shade_color(D.seat_btn[i], i + 1 == D.seats ? 0xFFFFFFFF : 0xFF949CA8);
    const std::pair<const char*, Win> privacy[] = {{"hideaddress", D.addr_edit}, {"hidejoinport", D.join_port},
        {"hidejoinpassword", D.join_password}, {"hidehostport", D.port_edit}, {"hidehostpassword", D.host_password}};
    for (const auto& [button, field] : privacy) {
        const bool hidden = D.hidden_fields[field];
        show_eye(button, hidden);
    }
    show_eye("hidehostaddresses", !D.show_host_addresses);
    founders::CampaignRules rules{(uint8_t)D.seats,
        (uint16_t)std::clamp(atoi(sui::edit_text(D.scaling_edit).c_str()), 0, int(founders::kMaxDifficultyPercent))};
    rules.model = 1; // the preview shows a new campaign (BUG-9100)
    auto setting = [](const char* key, uint16_t value) {
        return (uint16_t)std::clamp(H->config_int(M, "campaign", key, value), 0, 65535);
    };
    rules.extra_health_percent = setting("enemy_health_per_extra_founder", rules.extra_health_percent);
    rules.extra_damage_percent = setting("enemy_damage_per_extra_founder", rules.extra_damage_percent);
    rules.extra_hunger_basis_points = setting("hunger_basis_points_per_extra_founder", rules.extra_hunger_basis_points);
    rules.extra_takeover_percent = setting("takeover_per_extra_founder", rules.extra_takeover_percent);
    auto bonus = [](uint32_t percent) {
        const auto increase = percent - 100u;
        return (increase ? "+" : "") + std::to_string(increase) + "%";
    };
    text(D.difficulty_value[0], bonus(rules.workload_percent()));
    text(D.difficulty_value[1], bonus(rules.enemy_health_percent()) + " / " + bonus(rules.enemy_damage_percent()));
    char hunger[80];
    const double hunger_bonus = rules.extra_total(rules.extra_hunger_basis_points) / 100.0;
    snprintf(hunger, sizeof hunger, "%s%.2f%%", hunger_bonus ? "+" : "", hunger_bonus);
    text(D.difficulty_value[2], hunger);
    text(D.difficulty_value[3], bonus(rules.takeover_percent()));
    char interval[80];
    const double seconds_less = (founders::CampaignRules::native_military_interval_ms - rules.civ_military_interval_ms()) / 1000.0;
    snprintf(interval, sizeof interval, "%s%.2f s", seconds_less ? "-" : "", seconds_less);
    text(D.difficulty_value[4], interval);
    bool scanning = A.role() == 0;
    auto discovered = A.lan_games ? A.lan_games() : std::vector<LanGame>{};
    std::vector<LanGame> games;
    for (const auto& saved : D.saved_servers) {
        auto found = std::find_if(discovered.begin(), discovered.end(),
                                  [&](const LanGame& game) { return game.address == saved.address; });
        if (found == discovered.end()) {
            games.push_back({saved.name.empty() ? "Saved Server" : saved.name, saved.address, "Offline", 0, 0});
        } else {
            games.push_back(std::move(*found));
            discovered.erase(found);
        }
    }
    games.insert(games.end(), discovered.begin(), discovered.end());
    const auto address = connection_address();
    const auto selected = std::find_if(games.begin(), games.end(), [&](const LanGame& game) { return game.address == address; });
    std::string details = "Server Status: enter an address";
    if (selected != games.end()) {
        if (!selected->max_players) details = "Saved server: offline or undiscovered";
        else if (selected->scaling < 0) details = selected->info;
        else details = std::to_string(selected->scaling) + "% scaling  |  " +
                       (selected->settings & 1 ? "Optimal" : "Fast") +
                       (selected->settings & 2 ? "  |  Password" : "  |  Open");
    } else if (!address.empty()) details = "Server Status: direct connection";
    text(D.selected_info, details);
    g_lan_max = g_lan_pages_fix ? std::max(0, (int)games.size() - kLanRows) : 0;
    g_lan_first = std::clamp(g_lan_first, 0, g_lan_max);
    text(D.lan_hdr, T("Searching your network...", "Поиск игр в вашей сети..."));
    show(D.lan_hdr, games.empty() && scanning);
    show(D.lan_track, scanning);
    show(D.lan_thumb, scanning && g_lan_max > 0);
    if (D.lan_thumb && g_lan_max > 0) {
        const float y = 178.f + 122.f * g_lan_first / g_lan_max;
        sui::set_area(D.lan_thumb, {342, y, 353, y + 28});
    }
    for (int i = 0; i < kLanRows; ++i) {
        int k = g_lan_first + i;
        bool have = k < (int)games.size() && (g_lan_pages_fix || i < 3);
        if (have) {
            const auto& g = games[k];
            auto name = g.name;
            if (name.size() > 22) {
                size_t end = 22;
                while (end > 0 && ((unsigned char)name[end] & 0xC0) == 0x80) --end;
                name.resize(end);
                name += "...";
            }
            if (g.max_players)
                text(D.lan_rows[i], name + "  [LAN]  " + std::to_string(g.players) + "/" + std::to_string(g.max_players));
            else
                text(D.lan_rows[i], name + "  -  Offline");
            if (D.lan_rows[i])
                sui::set_shade_color(D.lan_rows[i], g.address == address ? 0xFFFFFFFF : 0xFFADB6C2);
            D.lan_addr[i] = g.address;
        } else
            D.lan_addr[i].clear();
        show(D.lan_rows[i], have && scanning);
    }
    std::string hint;
    if (A.local_addresses) {
        auto ips = A.local_addresses();
        std::string port = sui::edit_text(D.port_edit);
        for (size_t i = 0; i < ips.size() && i < 3; i++)
            hint += (i ? ",  " : "") + ips[i] + ":" + port;
    }
    text(D.host_hint, !D.show_host_addresses ? "Host addresses hidden" : hint.empty() ? "No host addresses" : hint);
    std::string up_line, up_btn;
    bool up = A.update && A.update(up_line, up_btn);
    // the session status wins; otherwise the update line (a new SporeCoop release)
    text(D.status, g_status.empty() && up ? up_line : g_status);
    color(D.status, g_status.empty() && up ? kGreen : g_status_color);
    // the Update button belongs to the update line, not to a session message (BUG-0104)
    show(D.update_btn, up && !up_btn.empty() && (!g_status_fix || g_status.empty()));
    text(D.update_btn, up_btn);
}

static void refresh_lobby()
{
    text(D.session, A.session_line());
    auto rows = A.players();
    const bool host = A.role() == 1;
    const bool vote = A.vote_active && A.vote_active();
    const auto rules = world::rules_for_current();
    const unsigned seats = host && A.seat_limit ? A.seat_limit() : rules.seats;
    show(D.invite_btn, host);
    // BUG-9111: the host's line also names the world transfer (its own row went to the Player Cells setting)
    const bool paced = A.paced_transfer && A.paced_transfer();
    const std::string transfer = paced ? T("Optimal transfer", "Обычная передача") : T("Fast transfer", "Быстрая передача");
    const std::string difficulty = std::to_string(rules.difficulty_percent) + "%";
    text(D.rules, A.mode && A.mode() != modes::kMenu
         ? (host ? std::string(T("Difficulty ", "Сложность ")) + difficulty + ", " + transfer
                 : std::string(T("Difficulty Scaling: ", "Сложность: ")) + difficulty)
         : host ? transfer : T("Load a campaign to see its rules", "Загрузите кампанию для просмотра правил"));
    show(D.transfer_mode, false);
    text(D.transfer_mode, std::string(T("World Transfer: ", "Передача мира: ")) +
         (A.paced_transfer && A.paced_transfer() ? "Optimal" : "Fast"));
    std::string preview, update_button;
    // the lobby names an update only when there is something to do ("Lineages is up to date" and the
    // version under it were two more lines at the bottom of every lobby)
    const bool has_update = A.update && A.update(preview, update_button);
    text(D.preview_status, has_update && (!g_label_width_fix || (!preview.empty() && update_button != "Check for Updates")) ? preview : "");
    for (int i = 0; i < 4; i++) {
        bool have = i < (int)rows.size();
        const bool reserved = !have && static_cast<unsigned>(i) < seats;
        show(D.row_name[i], have || reserved);
        show(D.row_stage[i], have || reserved);
        show(D.row_ping[i], have);
        show(D.row_ready[i], have && vote);
        if (!have) {
            if (reserved) {
                text(D.row_name[i], T("Open Slot", "Свободное место"));
                color(D.row_name[i], kDim);
                text(D.row_stage[i], T("Open", "Свободно"));
            }
            continue;
        }
        const auto& p = rows[i];
        std::string n = p.name;
        if (p.host && n.size() <= 12)
            n += T(" (host)", " (хост)");
        text(D.row_name[i], n);
        color(D.row_name[i], p.me ? kGold : kWhite);
        text(D.row_stage[i], p.stage == "Galaxy menu" ? T("Galaxy", "Галактика") : p.stage);
        text(D.row_ping[i], p.me ? "" : (p.ping_ms >= 0 ? std::to_string(std::min(p.ping_ms, 9999)) + T(" ms", " мс") : "?"));
        text(D.row_ready[i], p.ready ? "+" : "-");
        color(D.row_ready[i], p.ready ? kGreen : kDim);
    }
    std::string wl = A.world_line ? A.world_line() : "";
    // BUG-9135 roadmap phase 4: a partner in Tribe without a village of their own (tribe.late_prompt)
    // (and in Civ without a nation of their own, civ::late_prompt: a capital instead of a village)
    const int late = A.late_prompt ? A.late_prompt() : 0;
    const bool late_civ = late && A.mode && A.mode() == modes::kCiv;
    if (wl.empty() && g_status.empty() && late == 1)
        wl = late_civ ? T("You have no city: you play the host's species. Choose your own creature below.",
                          "У вас нет города: вы играете видом хоста. Выберите своё существо ниже.")
                      : T("You have no village: you play one of the host's creatures. Choose your own below.",
                          "У вас нет деревни: вы играете существом хоста. Выберите своё ниже.");
    else if (wl.empty() && g_status.empty() && late == 2)
        wl = late_civ ? T("You play your own creature: the host founds your capital with it shortly.",
                          "Вы играете своим существом: хост скоро создаст с ним вашу столицу.")
                      : T("You play your own creature: the host founds your village with it shortly.",
                          "Вы играете своим существом: хост скоро создаст с ним вашу деревню.");
    // BUG-9150: the village wears a stand-in of the host's creature until the player's own creature reaches it
    else if (wl.empty() && g_status.empty() && late == 3)
        wl = T("Your village wears the host's creature for now. Choose your own creature below.",
               "Ваша деревня пока носит существо хоста. Выберите своё ниже.");
    else if (wl.empty() && g_status.empty() && late == 4)
        wl = T("You play your own creature: your village takes it shortly.",
               "Вы играете своим существом: деревня скоро примет его.");
    int share_why = share_blocked();
    if (wl.empty() && g_status.empty() && share_why) // BUG-0597: a host alone can play now
        wl = share_why == 1 && rows.size() <= 1
                 ? T("Start or load a game now: friends can join any time",
                     "Начните или загрузите игру: друзья войдут когда угодно")
             : share_why == 2 ? T("Nobody has joined yet: friends who join get your world by themselves",
                                  "Пока никто не вошёл: друзья получат ваш мир сами")
                              : "";
    text(D.world, wl.empty() ? g_status : wl);
    // the chat history (50 lines kept): long lines wrapped, scrolled with the arrows (BUG-0129)
    std::vector<std::string> chat;
    for (auto& l : A.chat ? A.chat() : std::vector<std::string>{})
        for (auto& w : notify_text::wrap(l, kChatCols))
            chat.push_back(w);
    g_chat_scroll = std::max(0, std::min(g_chat_scroll, (int)chat.size() - 4));
    size_t first = chat.size() > 4 ? chat.size() - 4 - (size_t)g_chat_scroll : 0;
    for (int i = 0; i < 4; i++)
        text(D.chat_lines[i], first + i < chat.size() ? chat[first + i] : "");
    show(D.chat_up, first > 0);
    show(D.chat_down, g_chat_scroll > 0);
    int ck = 0, cown = 0;
    bool hint = false; // the creation row says how to get a choice: it may take the arrows' room
    std::string cname, cl;
    if (late && A.creation && A.creation(ck, cname, cown)) { // the creature selector asks for their own creature
        if (cown <= 0) {
            cl = T("No creatures of your own yet: make one in a creature editor", "Своих существ пока нет: создайте его в редакторе");
            hint = true;
        } else
            cl = std::string(late == 1 || late == 3 ? T("Choose your own creature for this game: ", "Выберите своё существо для этой игры: ")
                                       : T("Your creature for this game: ", "Ваше существо в этой игре: ")) +
                 (cname.empty() ? std::string(T("none yet", "пока нет")) : cname);
    } else if (A.creation && A.creation(ck, cname, cown)) {
        cl = ck == 1 ? T("My cell: ", "Моя клетка: ") : ck == 2 ? T("My ship: ", "Мой корабль: ") : T("My creature: ", "Моё существо: ");
        bool me_host = A.role() == 1;
        if (!cname.empty())
            cl += cname;
        else if (!g_creation_hint_fix)
            cl += T("the world's own", "из мира хоста");
        else // BUG-0598: say whose it is
            cl += ck == 2 ? T("your empire's ship", "корабль вашей империи")
                  : me_host ? T("your world's species", "вид вашего мира") : T("the host's species", "вид хоста");
        if (ck != 2 && cown > 0)
            cl += std::string(T("  (in My Creations: ", "  (своих: ")) + std::to_string(cown) + ")";
        else if (g_creation_hint_fix && ck != 2 && !me_host) { // no arrows: how to get a choice
            cl += T(" (make one in an editor to pick it)", " (создайте своё в редакторе)");
            hint = true;
        }
    }
    std::string gwhat, gto;
    int gn = A.give ? A.give(gwhat, gto) : 0;
    bool space = gwhat == "spice";
    // only where there are units to give (Tribe, Civ, Space): at the galaxy menu, in Cell or Creature the
    // line "Select your units to give them to a partner" meant nothing
    const bool give_ctx = A.give && !gto.empty() && (!g_label_width_fix || !gwhat.empty()) && (cl.empty() || space);
    // owner 2026-11-12 ("3B"): no "Give To" button and no gift row (BUG-9148); `coop.fix ui.give_button on`: as before.
    // The Civ role row keeps its place (it used to show while nothing was selected to give).
    const bool giving = give_ctx && g_give_button;
    if (!giving) space = false;
    if (space) // the ship can't be changed in flight: the row is for the cargo
        cl = gn ? std::string(T("Spice in cargo: ", "Специи в трюме: ")) + std::to_string(gn)
                : T("No spice in cargo to give", "В трюме нет специи");
    else if (giving) {
        const char* w = gwhat == "city" ? T("the city whose menu you opened", "город, меню которого открыто")
                        : gwhat == "vehicle" ? T("selected vehicles: ", "выбрано машин: ")
                                             : T("selected tribe members: ", "выбрано жителей: ");
        cl = gn ? std::string(w) + (gwhat == "city" ? "" : std::to_string(gn))
                : T("Select your units to give them to a partner", "Выделите свои юниты, чтобы отдать напарнику");
    }
    bool roles = give_ctx && (gn == 0 || !g_give_button) && A.civ_role && A.mode && A.mode() == 0x1654C04; // civ
    if (roles) {
        int r = A.civ_role();
        const char* rn[4] = {T("none - pick one", "нет - выберите"), T("military (stronger vehicles)", "военная (сильнее машины)"),
                             T("economic (trade earns more)", "торговая (выгоднее торговля)"),
                             T("religious (faster conversion)", "религиозная (быстрее обращение)")};
        cl = std::string(T("My role: ", "Моя роль: ")) + rn[r & 3];
    }
    if (g_creation_hint_fix && D.creation) { // the row is wider while it has no arrows and no buttons
        auto& c = g_cache[D.creation];
        float x2 = hint && !giving && !roles ? 720.f : 560.f;
        if (c.box.x2 != x2 && c.left) {
            c.box.x2 = x2;
            fit(D.creation, c);
        }
    }
    text(D.creation, cl);
    show(D.give_btn, giving && gn > 0);
    show(D.give_next, giving && gn > 0 && !space && A.give_choices && A.give_choices() > 1);
    text(D.give_btn, space ? std::string(T("Give 10 To ", "Дать 10: ")) + gto : std::string(T("Give To ", "Отдать ")) + gto);
    std::string jto;
    bool jump = A.jump && A.jump(jto);
    show(D.jump_btn, jump);
    text(D.jump_btn, std::string(T("Jump To ", "К звезде: ")) + jto);
    bool can = (!cl.empty() && !giving && ck != 2 && cown > 0) || roles;
    show(D.creation_prev, can);
    show(D.creation_next, can);
    show(D.ready_btn, vote);
    text(D.ready_btn, A.ready() ? T("Not Ready", "Не готов") : T("I'm Ready", "Я готов"));
    show(D.create_btn, late && !vote && !host && A.create_creature_offered && A.create_creature_offered());
    // BUG-9111, owner 2026-11-12: the host's rows from the top: the vote's Start Now (only while a vote runs), Player
    // Cells, Foreign Creations (hidden during a vote), the vote's Ready
    const bool start_now = host && vote;
    const bool cells = host && A.lineage_cells;
    show(D.share_btn, start_now);
    sui::set_area(D.share_btn, Rect{445, 190, 706, 222});
    float row = 190;
    auto next_row = [&row] { Rect r{445, row, 706, row + 32}; row += 34; return r; };
    if (start_now) next_row();
    if (D.cells_btn) {
        show(D.cells_btn, cells);
        if (cells) sui::set_area(D.cells_btn, next_row());
        text(D.cells_btn, A.lineage_cells && A.lineage_cells() ? T("Player Cells: On", "Клетки игроков: Вкл")
                                                               : T("Player Cells: Off", "Клетки игроков: Выкл"));
    }
    // BUG-9105: hosts in the next row, guests at the top of theirs; the vote's Ready button takes the row
    if (D.onward_btn) {
        const bool onward = A.foreign_creations && !(host && vote);
        if (host && onward) sui::set_area(D.onward_btn, next_row());
        else if (!host) sui::set_area(D.onward_btn, Rect{445, 212, 706, 248});
        show(D.onward_btn, onward);
        text(D.onward_btn, A.foreign_creations && A.foreign_creations() ? T("Foreign Creations: On", "Чужие создания: Вкл")
                                                                        : T("Foreign Creations: Off", "Чужие создания: Выкл"));
    }
    sui::set_area(D.ready_btn, host ? next_row() : Rect{445, 249, 706, 285});
    enable(D.share_btn, true);
    const auto leave_title = host ? T("Stop Hosting", "Закрыть игру") : T("Leave", "Выйти");
    text(D.leave_btn, leave_title);
    text(D.leave_caption, leave_title);
}

static void refresh_space()
{
    const bool villages = A.mode && A.mode() == modes::kTribe;
    const bool nations = A.mode && A.mode() == modes::kCiv && civ::offline_control_available();
    std::vector<spaceext::Row> rows;
    if (villages) {
        auto action = [](const char* command) {
            open(false);
            notify::show(tribe::debug({command}), "info");
        };
        rows.push_back({"Select your villagers before choosing an action.", "", nullptr});
        if (A.role() == 2) {
            rows.push_back({"Choose a destination on the ground.", "Move", [action] { action("march"); }});
            rows.push_back({"Gather fruit near your village.", "Gather", [action] { action("gather"); }});
        }
        rows.push_back({"Help your partner establish a new village.", "Rebuild", [action] { action("rebuild"); }});
        if (A.role() == 2) {
            rows.push_back({"Attack an enemy villager with your selection.", "Attack", [action] { action("attack"); }});
            rows.push_back({"Select all living members of your village.", "Select", [action] { action("select_village"); }});
            rows.push_back({"Attack an enemy hut or tool building.", "Raid", [action] { action("raid"); }});
            rows.push_back({tribe::village_birth_line(), "Add baby", [action] { action("baby"); }});
            rows.push_back({"Collect tools from a rack in your village.", "Equip", [action] { action("equip"); }});
        }
        if (A.role() == 2)
            rows.push_back({"Heal an injured friendly villager with healing tools.", "Heal", [action] { action("heal"); }});
        else rows.push_back({"Select a living helper at the new village site.", "", nullptr});
        if (A.role() == 2)
            rows.push_back({"Eat food stored at your village.", "Eat", [action] { action("eat"); }});
        else rows.push_back({"Stay at least 60 metres from existing huts.", "", nullptr});
        if (A.role() == 2)
            rows.push_back({"Give food to another tribe to make peace.", "Gift food", [action] { action("gift_food"); }});
        else rows.push_back({"Your partner keeps their original species.", "", nullptr});
        rows.push_back({tribe::village_food_line(), "", nullptr});
        if (A.role() == 2) for (int kind = 1; kind <= 9; ++kind)
            rows.push_back({tribe::village_tool_line(kind), "Build", [kind] {
                open(false);
                notify::show(tribe::debug({"build", std::to_string(kind)}), "info");
            }});
        if (A.role() == 2) {
            rows.push_back({"Perform for an NPC tribe with your chief and musicians.", "Perform", [action] { action("perform"); }});
            rows.push_back({tribe::performance_line(), "", nullptr});
            const char* instruments[] = {"Horns [1]", "Maracas [2]", "Didgeridoos [3]"};
            std::string request = tribe::performance_line();
            if (request.empty()) request = "Answer the audience with selected musicians.";
            for (int tool = 4; tool <= 6; ++tool)
                rows.push_back({request, instruments[tool - 4], [tool] {
                    notify::show(tribe::debug({"instrument", std::to_string(tool)}), "info");
                }});
        }
    } else if (nations) {
        const bool enabled = civ::offline_control_enabled();
        rows.push_back({"Command the partner's cities and vehicles while they are offline.",
                        enabled ? "Enabled" : "Disabled", [enabled] {
                            notify::show(civ::debug({"offline_control", enabled ? "off" : "on"}), "info");
                            g_dirty = true;
                        }});
    } else rows = spaceext::rows_all();
    text(D.space_title, nations ? "Civilization settings" : villages ? "Your two villages" : T("Your galaxy together", "Галактика вместе"));
    int n = (int)rows.size(), pages = std::max(1, (n + kSpaceRows - 1) / kSpaceRows);
    if (g_space_first >= n)
        g_space_first = (pages - 1) * kSpaceRows;
    for (int i = 0; i < kSpaceRows; i++) {
        int k = g_space_first + i;
        bool have = k < n;
        show(D.space_text[i], have);
        D.space_action[i] = have ? rows[k].on_click : nullptr;
        show(D.space_button[i], have && !rows[k].button.empty() && rows[k].on_click);
        if (!have)
            continue;
        text(D.space_text[i], rows[k].text);
        color(D.space_text[i], rows[k].color ? rows[k].color : kWhite);
        text(D.space_button[i], rows[k].button);
    }
    if (rows.empty())
        text(D.space_text[0], T("Nothing here yet", "Пока ничего"));
    show(D.space_text[0], true);
    show(D.space_prev, pages > 1);
    show(D.space_next, pages > 1);
    show(D.space_pageno, pages > 1);
    text(D.space_pageno, std::to_string(g_space_first / kSpaceRows + 1) + "/" + std::to_string(pages));
}

static void refresh()
{
    text(D.report_btn, T("Report Issue", "Сообщить об ошибке"));
    text(D.report_caption, T("Report Issue", "Сообщить об ошибке"));
    g_pending = false;
    if (!g_ok)
        return;
    ensure_menu_button();
    if (g_want_open && !D.root)
        build_dialog();
    if (!D.root)
        return;
    if (g_want_open && g_raise) {
        // on top of every UI layer (in game the HUD layers cover the menu layer and take
        // the mouse): a direct child of the main window, brought to the front
        g_raise = false;
        Win main = sui::main_window();
        if (Win p = sui::parent(D.root))
            if (p != main)
                sui::remove_child(p, D.root);
        if (sui::parent(D.root) != main)
            sui::add_child(main, D.root);
        sui::bring_to_front(main, D.root);
        sui::set_flag(D.root, sui::kFlagAlwaysInFront, true);
    }
    if (g_want_open && g_recenter_fix)
        place_window(D.root);
    show(D.root, g_want_open && H->time_seconds() >= g_aside_until);
    if (!g_want_open) {
        if (A.lan_scan)
            A.lan_scan(false);
        return;
    }
    bool lobby = A.role() != 0;
    bool space = lobby && A.mode && A.mode() == modes::kSpace;
    bool villages = lobby && g_village_page && A.mode && A.mode() == modes::kTribe && tribe::village_orders(); // BUG-9153
    bool nations = lobby && civ::offline_control_available();
    if (!space && !villages && !nations)
        g_space_page = false;
    for (Win w : D.page_connect)
        show(w, !lobby);
    for (Win w : D.page_lobby)
        show(w, lobby && !g_space_page);
    for (Win w : D.page_space)
        show(w, lobby && g_space_page);
    bool seat_controls = lobby && A.role() == 1 && !g_space_page && A.mode &&
        (space || A.mode() == modes::kCell || A.mode() == modes::kCreature ||
         A.mode() == modes::kTribe || A.mode() == modes::kCiv);
    show(D.next_seats_edit,seat_controls);
    show(D.queue_seats,seat_controls);
    // more slots apply at once, fewer from the next stage (world::set_seats, owner decisions 2026-11-12); Space: next load
    show(D.slots_label,seat_controls);
    // the box shows the slots in use; a number typed in stays until Apply changes the count
    if (seat_controls) {
        const unsigned now_slots = A.seat_limit ? A.seat_limit() : 0;
        if (now_slots >= 1 && now_slots <= 4 && now_slots != g_slots_shown) {
            g_slots_shown = now_slots;
            sui::set_edit_text(D.next_seats_edit, std::to_string(now_slots));
        }
    } else
        g_slots_shown = 0;
    show(D.space_btn, (space || villages || nations) && !g_space_page);
    text(D.space_btn, nations ? "Nations >" : villages ? "Village >" : T("Space >", "Космос >"));
    if (!lobby || g_space_page) {
        show(D.chat_up, false);
        show(D.chat_down, false);
    }
    text(D.subtitle, lobby && g_space_page ? (nations ? "NATIONS" : villages ? "VILLAGES" : T("SPACE", "КОСМОС"))
                     : lobby ? T("LOBBY", "ЛОББИ") : "");
    if (A.version_line) {
        auto version = A.version_line();
        if (!g_space_page) version = version.substr(0, version.find("  -  "));
        text(D.version, version);
        text(D.connect_version, version);
    }
    show(D.version, lobby);
    const Rect report_area = lobby ? Rect{545, 98, 720, 134} : Rect{545, 107, 720, 142};
    for (Win control : {D.report_btn, D.report_caption})
        if (control && sui::area(control).y1 != report_area.y1)
            sui::set_area(control, report_area);
    if (A.lan_scan)
        A.lan_scan(!lobby);
    if (lobby && g_space_page)
        refresh_space();
    else if (lobby)
        refresh_lobby();
    else
        refresh_connect();
}

static Win g_graphics_fps;
static bool g_graphics_typing;
// BUG-9102: a full field ("144") ignored every digit, and a real Backspace can reach Spore's menu
// navigation, so the player could not change the cap. The first digit after a click into the
// field now replaces the value; Delete clears it.
static bool g_graphics_replace;
static bool g_graphics_replace_fix = true;
COOP_FIX("ui.fps_replace", "BUG-9102", g_graphics_replace_fix,
         "ui: the first digit typed after clicking the frame rate cap replaces it; Delete clears it");

static bool graphics_input(unsigned message, uintptr_t value, int x, int y)
{
    if (!g_graphics_fps) return false;
    if (message == WM_KEYDOWN && (value == VK_ESCAPE || value == VK_RETURN)) {
        g_graphics_typing = false;
        return false;
    }
    const bool backspace = value == VK_BACK && (message == WM_KEYDOWN || message == WM_KEYUP);
    const bool del = g_graphics_replace_fix && value == VK_DELETE && (message == WM_KEYDOWN || message == WM_KEYUP);
    if (message != WM_LBUTTONDOWN && !(g_graphics_typing && (message == WM_CHAR || backspace || del)))
        return false;
    if (sui::find(sui::main_window(), 0xC00F0111) != g_graphics_fps) {
        g_graphics_typing = false;
        return false;
    }
    if (message == WM_LBUTTONDOWN) {
        const auto area = sui::screen_rect(g_graphics_fps);
        g_graphics_typing = x >= area.x1 && x < area.x2 && y >= area.y1 && y < area.y2;
        g_graphics_replace = g_graphics_replace_fix && g_graphics_typing;
        return false;
    }
    if (value == VK_ESCAPE || value == VK_RETURN)
        return false;
    // Spore handles Backspace as menu navigation on keydown, before WM_CHAR.
    if (message == WM_KEYUP || (message == WM_CHAR && value == VK_BACK))
        return true;
    auto text = sui::edit_text(g_graphics_fps);
    if (del)
        text.clear();
    else if (backspace && !text.empty())
        text.pop_back();
    else if (message == WM_CHAR && value >= '0' && value <= '9') {
        if (g_graphics_replace) text.clear();
        if (text.size() < 3) text += (char)value;
    }
    if (message == WM_CHAR || backspace || del) g_graphics_replace = false;
    sui::set_edit_text(g_graphics_fps, text);
    return true;
}

// BUG-9120: a tab click in Spore's Settings goes through the Settings window's buttons and shows or
// hides the window whose id is each button's command id (the tab buttons' commands are their pages).
// Our label / button windows had command 0, which found the dialog's own frame (id 0) and hid it: the
// whole Settings dialog vanished on Audio, Game & Capture, Online, Credits (bisected: every button-type
// control of ours does it, the text field does not). Each control's command is now its own id, so
// that lookup finds the control itself (hidden on the other tabs, as refresh shows it again on Graphic).
// Old (`coop.fix ui.settings_tabs off`): command 0.
static bool g_settings_tabs_fix = true;
COOP_FIX("ui.settings_tabs", "BUG-9120", g_settings_tabs_fix,
         "ui: the Frame Rate Cap controls in Spore's Settings no longer make the dialog vanish on another tab");

// ---- decision 3I (the owner's layout 2026-11-13, images/uiscale_mockup_owner.png) ----
// Spore's Settings dialog grows taller; the Graphic page gets an "Interface" box under the quality table with the
// Interface Size slider (a native WinSlider, lying on its side), the Frame Rate field moves into the Screen Size box
// and a Borderless Mode check box sits under Full Screen Mode (the two exclusive). The left panel keeps only the tabs.
// Old (`coop.fix ui.settings_layout off`, before the dialog first opens): the controls in the left panel.
static bool g_settings_layout = true;
COOP_FIX("ui.settings_layout", "BUG-9191", g_settings_layout,
         "ui: Interface Size slider, Frame Rate and Borderless Mode on Spore's Graphic Settings page (the dialog grows)");
static bool g_settings_visible;

constexpr uint32_t kIfaceBox = 0xC00F0115, kIfaceTitle = 0xC00F0116, kIfaceLabel = 0xC00F0117,
                   kIfaceValue = 0xC00F0118;
constexpr uint32_t kFullScreenCheck = 0x05B591C0;

static void move_win(Win w, float dy, float grow = 0)
{
    const auto a = sui::area(w);
    sui::set_area(w, {a.x1, a.y1 + dy, a.x2, a.y2 + dy + grow});
}

// the dialog `d` pixels taller and up by half of it (centered; 800x600 still holds it). Its root holds the frame
// (pieces relative to it: the lower ones move, the tall edges grow), two backgrounds, Accept, Cancel and the settings
// window (tabs, title, the pages' container).
static void grow_settings_dialog(Win settings, float d)
{
    Win root = sui::parent(settings);
    if (!root) return;
    const float up = std::floor(d / 2);
    for (Win c : sui::children(root)) {
        if (c == settings) continue;
        const auto a = sui::area(c);
        const auto pieces = sui::children(c);
        if (pieces.size() >= 10) { // the frame
            for (Win f : pieces) {
                const auto b = sui::area(f);
                if (b.y1 >= 400) move_win(f, d);
                else if (b.y2 >= 400) move_win(f, 0, d);
            }
            move_win(c, -up, d);
        } else if (a.h() >= 400)
            move_win(c, -up, d); // the backgrounds
        else
            move_win(c, d - up); // Accept, Cancel
    }
    for (Win c : sui::children(settings)) {
        if (sui::control_id(c) == 0x046967C0) { // the pages
            move_win(c, -up, d);
            for (Win page : sui::children(c)) move_win(page, 0, d);
        } else
            move_win(c, -up);
    }
}

static int ui_percent(Win w)
{
    if (sui::as_slider(w)) return (sui::slider_value(w) + 2) / 5 * 5;
    const std::string c = sui::caption(w);
    return atoi(c.c_str() + std::min<size_t>(10, c.size()));
}

static bool borderless_on(Win w)
{
    return sui::caption(w).empty() ? sui::button_selected(w) : sui::caption(w) == "Borderless: On";
}

// the owner's Graphic page; false when a native window it builds on is missing (nothing changed then)
static bool build_graphic_page(Win settings, Win panel, const uint32_t* ids, Win& fps, Win& borderless, Win& ui_scale)
{
    Win row_label = sui::find_caption(panel, "Shadows");          // the quality table's row label: our labels' look
    Win screen_title = sui::find_caption(panel, "Screen Size");
    Win screen_box = screen_title ? sui::parent(screen_title) : nullptr;
    Win full_box = sui::find(panel, 0x061C90B8);
    Win full_check = sui::find(panel, kFullScreenCheck);
    Win full_label = sui::find(panel, 0x0695B438);
    Win combo = sui::find(panel, 0x046170A1);
    Win note = sui::find_caption(panel, "Changing the screen size");
    Win music = sui::find(settings, 0x8BDD39EC); // the Audio page's Music slider: the slider class and its look
    if (!row_label || !screen_box || !full_box || !full_check || !full_label || !combo || !music) {
        H->log(M, SK_WARN, "ui: Graphic page layout: a native window is missing, the old controls instead");
        return false;
    }
    // even spacing (the owner 2026-11-13: "a little inconsistent with spacing"): 8 px between the boxes, rows 8 px
    // under a box's title band (30 px) and 10 px above its bottom; the two check boxes centred in theirs
    constexpr float kGrow = 72, kShift = 65, kBoxH = 98;
    grow_settings_dialog(settings, kGrow);
    for (Win c : sui::children(panel)) // everything under the quality table makes room for the Interface box
        if (sui::area(c).y1 >= 240) move_win(c, kShift);
    for (Win b : {screen_box, full_box}) { // a second row in each
        const auto a = sui::area(b);
        sui::set_area(b, {a.x1, a.y1, a.x2, a.y1 + kBoxH});
    }
    {
        const auto a = sui::area(combo);
        sui::set_area(combo, {a.x1, 68, a.x2, 68 + a.h()});
    }
    if (note) {
        sui::set_caption(note, "Changing the screen size needs a restart.");
        const auto a = sui::area(note);
        sui::set_area(note, {a.x1, kBoxH + 4, a.x2, kBoxH + 24});
    }
    // the Interface box (the Screen Size box's look: its title band), its title, the label, the value, the slider
    Win box = sui::box(panel, {2, 246, 454, 308}, sui::drawable(screen_box));
    // (the native labels are text windows, not buttons: ours take their fonts and colours, sampled 2026-11-13)
    constexpr uint32_t kLabelColor = 0xFFC0C7CB, kTitleColor = 0xFFDBDEE0;
    const uint32_t label_font = sui::font(row_label), title_font = sui::font(screen_title);
    Win title = sui::label(panel, {20, 253, 250, 272}, "Interface", title_font, kTitleColor, sui::kAlignLeft);
    Win size_label = sui::label(panel, {20, 274, 112, 294}, "Interface Size", label_font, kLabelColor, sui::kAlignLeft);
    Win size_value = sui::label(panel, {114, 274, 160, 294}, "100%", label_font, 0xFFFFFFFF, sui::kAlignLeft);
    void* knob = sui::image(sui::fnv("lineages_slider_knob_h"), 0x2F7D0004, sui::fnv("sporecoop"));
    void* track = sui::image(sui::fnv("lineages_slider_track_h"), 0x2F7D0004, sui::fnv("sporecoop"));
    void* look = knob && track ? sui::slider_drawable_with(sui::slider_drawable_of(music), nullptr, knob, track) : nullptr;
    ui_scale = sui::slider(panel, {178, 269, 438, 299}, 50, 200, 100, true, look); // (16 px from the box's right)
    // Frame Rate in the Screen Size box, above the resolution
    const auto sb = sui::area(screen_box);
    Win fps_label = sui::label(panel, {sb.x1 + 18, sb.y1 + 38, sb.x1 + 100, sb.y1 + 58}, "Frame Rate", label_font,
                               kLabelColor, sui::kAlignLeft);
    // the field in the Graphic Cache Size field's look (the owner: "make sure the frame rate field looks nice and
    // native"): its glossy black box and arrows drawn from that field's pixels (tools/settings_ui_assets.py; the
    // native drawables draw nothing in a window of ours), its font, no background of our own
    Win cache = sui::find(panel, 0x04728B30);
    const sui::Rect field_area{sb.x1 + 106, sb.y1 + 34, sb.x1 + 162, sb.y1 + 61};
    const uint32_t ui_group = sui::fnv("sporecoop");
    Win fps_frame = sui::image_window(panel, sui::fnv("lineages_field_box"), ui_group, field_area.x1, field_area.y1);
    fps = sui::text_edit(panel, field_area, "", cache ? sui::font(cache) : sui::font(full_label), 3, !fps_frame);
    // its up / down arrows (the owner: "if you keep the up and down arrow, then we can do 15, 30, 45, 60, 90, 120,
    // 144, 160, 170, 240, 360"): the cache field's spinner look; the upper half steps up, the lower down; below 15
    // the field is blank (Spore's own frame rate); typing any number still works
    Win fps_spin = sui::image_window(panel, sui::fnv("lineages_field_arrows"), ui_group, field_area.x2 + 1,
                                     field_area.y1);
    if (fps_spin && fps) {
        sui::set_flag(fps_spin, sui::kFlagIgnoreMouse, false);
        sui::add_handler(fps_spin, [fps](Win w, const sui::Msg& m) {
            if (m.type != sui::kMsgMouseDown) return false;
            static const int kSteps[] = {15, 30, 45, 60, 90, 120, 144, 160, 170, 240, 360};
            float y = 0;
            memcpy(&y, &m.b, 4); // the mouse, relative to the arrows
            const bool up = y < sui::area(w).h() / 2;
            const int now = atoi(sui::edit_text(fps).c_str());
            int next = now;
            if (up) {
                next = kSteps[std::size(kSteps) - 1];
                for (int s : kSteps)
                    if (s > now) { next = s; break; }
            } else {
                next = 0;
                for (int s : kSteps)
                    if (s < now) next = s;
            }
            sui::set_edit_text(fps, next ? std::to_string(next) : "");
            return true;
        });
    }
    // Borderless Mode under Full Screen Mode (the native check box copied)
    for (Win w : {full_check, full_label}) { // Full Screen Mode up into the first of the two centred rows
        const auto a = sui::area(w);
        sui::set_area(w, {a.x1, 19, a.x2, 45});
    }
    borderless = sui::button_like(full_box, full_check, {29, 53, 55, 79}, "");
    Win border_label = sui::label(full_box, {62, 53, 177, 79}, "Borderless Mode", sui::font(full_label), kLabelColor,
                                  sui::kAlignLeft);
    Win windows[] = {fps_label, fps,        borderless, border_label, ui_scale, box,
                     title,     size_label, size_value, fps_frame,    fps_spin};
    for (int i = 0; i < 11; ++i)
        if (windows[i]) sui::set_ids(windows[i], ids[i], ids[i]); // never command 0 (BUG-9120)
    for (Win l : {box, title, size_label, size_value, fps_label, border_label, fps_frame})
        if (l) sui::set_flag(l, sui::kFlagIgnoreMouse, true);
    H->log(M, SK_INFO, "ui: Graphic page laid out (slider %s, its look %s)", ui_scale ? "made" : "MISSING",
           look ? "ours" : "MISSING");
    return fps && borderless;
}

// ---- decision 3J (the owner 2026-11-14): the Screen Size list, one entry per resolution at its highest refresh rate,
// without the "@ NNHz" text ("So let's default to the highest hz available in the resolution to reduce the size. We'll
// also hide the Hz text in the resolution list. The player can cap their FPS to whatever they need"). The list
// (combo 0x046170A1) is Spore's own popup: one button per display mode, 20 px apart, the selected one beside the combo.
// Spore saves the chosen mode as its place in the mode list (Preferences.prop "OptionScreenSize 21"), so the list
// itself stays: a shorter mode list from Direct3D would move every player's saved place. It picks the button under the
// pointer (checked 2026-11-14: a moved "1280 x 1024" saved 1280 x 1024 @ 360Hz), so the list is drawn short: the other
// refresh rates hidden, the kept buttons moved together (by size), shown (also those Spore scrolled out of its list)
// and renamed. The current resolution keeps its current entry. While the list is open the Screen Size box comes to
// the front (our Frame Rate field and the Interface box drew over it). Old (`coop.fix ui.resolution_list off`):
// Spore's list as it is.
static bool g_resolution_list = true;
COOP_FIX("ui.resolution_list", "BUG-9191", g_resolution_list,
         "ui: the Screen Size list shows each resolution once (its highest refresh rate) without the Hz");

struct ResMove {
    Win w;
};
static std::vector<ResMove> g_res_moves; // the kept buttons while the list is open

static void tidy_resolution_list(Win panel, Win combo, Win screen_box, const std::vector<Win>& ours)
{
    struct Item {
        Win w;
        int width, height, hz;
    };
    static Win s_combo = nullptr;
    static std::vector<Item> s_items;
    static bool s_open = false;
    if (!g_resolution_list || !combo || !screen_box) {
        g_res_moves.clear();
        return;
    }
    if (combo != s_combo) { // a new dialog: its buttons and their modes, read while the captions are Spore's
        s_combo = combo;
        s_items.clear();
        s_open = false;
        g_res_moves.clear();
        for (Win c : sui::children(combo)) {
            int w = 0, h = 0, hz = 0;
            if (sui::as_button(c) && sscanf(sui::caption(c).c_str(), " %d x %d @ %d", &w, &h, &hz) == 3)
                s_items.push_back({c, w, h, hz});
        }
    }
    if (s_items.empty()) return;
    bool open = false;
    for (const Item& it : s_items) open = open || sui::visible(it.w);
    if (open && !s_open) {
        // just opened (Spore laid the buttons out): the selected one sits beside the combo (place 0)
        const Item* sel = nullptr;
        for (const Item& it : s_items)
            if (sui::visible(it.w) && std::fabs(sui::area(it.w).y1) < 1) sel = &it;
        std::vector<const Item*> keep;
        for (const Item& it : s_items) {
            bool k;
            if (sel && it.width == sel->width && it.height == sel->height)
                k = &it == sel; // the current resolution: its current entry
            else {
                k = true;
                for (const Item& o : s_items)
                    if (o.width == it.width && o.height == it.height && (o.hz > it.hz || (o.hz == it.hz && &o < &it)))
                        k = false;
            }
            if (k) keep.push_back(&it);
            else sui::set_visible(it.w, false);
        }
        std::sort(keep.begin(), keep.end(), [](const Item* l, const Item* r) {
            return l->width != r->width ? l->width < r->width : l->height < r->height;
        });
        int cur = 0;
        for (size_t i = 0; i < keep.size(); i++)
            if (keep[i] == sel) cur = (int)i;
        constexpr float kStep = 20;
        const float top = -cur * kStep;
        g_res_moves.clear();
        sui::Rect row{0, 0, sui::area(combo).w(), kStep}; // a laid-out button's columns (Spore's own)
        if (sel) row = sui::area(sel->w);
        for (size_t i = 0; i < keep.size(); i++) {
            Win w = keep[i]->w;
            const auto a = row; // (one Spore scrolled out was never laid out: [10 10 85 30])
            const float y = top + (float)i * kStep;
            sui::set_area(w, {a.x1, y, a.x2, y + kStep});
            sui::set_visible(w, true);
            g_res_moves.push_back({w});
            const std::string c = sui::caption(w);
            const size_t at = c.find(" @");
            if (at != std::string::npos) sui::set_caption(w, c.substr(0, at));
        }
        for (Win c : sui::children(combo)) // the list's background: the visible plain window as tall as the list
            if (!sui::as_button(c) && sui::visible(c) && sui::area(c).h() > kStep * 2) {
                const auto a = sui::area(c);
                sui::set_area(c, {a.x1, top, a.x2, top + (float)keep.size() * kStep});
            }
        sui::bring_to_front(panel, screen_box);
    } else if (open) { // Spore may show the hidden ones again (and hide a kept one it scrolled out)
        for (const Item& it : s_items) {
            bool kept = false;
            for (const ResMove& m : g_res_moves) kept = kept || m.w == it.w;
            if (sui::visible(it.w) != kept) sui::set_visible(it.w, kept);
        }
    } else if (s_open) { // closed: our Frame Rate field over the Screen Size box again
        g_res_moves.clear();
        for (Win w : ours)
            if (w) sui::bring_to_front(sui::parent(w), w);
    }
    { // the combo's caption without the Hz
        const std::string c = sui::caption(combo);
        const size_t at = c.find(" @");
        if (at != std::string::npos) sui::set_caption(combo, c.substr(0, at));
    }
    s_open = open;
}

static void refresh_graphics_options()
{
    constexpr uint32_t kSettings = 0x043DE590;
    constexpr uint32_t kGraphicPanel = 0x0437A968;
    constexpr uint32_t kAccept = 0x043B7A18;
    constexpr uint32_t kDefaults = 0x05CACA50;
    // the Frame Rate label, its field, Borderless, its hint (the new layout: its label), UI Scale, then the new layout's
    // Interface box, title, label and value
    constexpr uint32_t kControls[] = {0xC00F0110, 0xC00F0111, 0xC00F0112, 0xC00F0113, 0xC00F0114,
                                      kIfaceBox, kIfaceTitle, kIfaceLabel, kIfaceValue, 0xC00F0119, 0xC00F011A};
    static Win last_open = nullptr;
    Win settings = sui::find(sui::main_window(), kSettings);
    Win panel = settings ? sui::find(settings, kGraphicPanel) : nullptr;
    auto visible = [](Win w) {
        if (!w) return false;
        for (; w; w = sui::parent(w))
            if (!sui::visible(w)) return false;
        return true;
    };
    g_settings_visible = visible(settings);
    if (!visible(settings)) {
        last_open = nullptr;
        g_graphics_fps = nullptr;
        g_graphics_typing = false;
        return;
    }
    Win fps = sui::find(settings, kControls[1]);
    Win borderless = sui::find(settings, kControls[2]);
    Win ui_scale = sui::find(settings, kControls[4]); // BUG-9191: the mod's HUD size
    const auto file = slotfs::widen(H->mods_dir()) + L"\\" + lineages_graphics::file_name;
    if (!fps && panel && visible(panel)) {
        Win defaults = sui::find(panel, kDefaults);
        Win accept = sui::find(sui::parent(settings), kAccept);
        if (!defaults || !accept) return;
        const uint32_t font = sui::font(sui::find(panel, 0x0695B438));
        if (!font) return;
        const bool laid_out = g_settings_layout && build_graphic_page(settings, panel, kControls, fps, borderless, ui_scale);
        if (!laid_out) {
            Win label = sui::label(settings, {77, 395, 211, 418}, "Frame Rate Cap", font, kWhite, sui::kAlignLeft);
            fps = sui::text_edit(settings, {209, 392, 261, 420}, "", font, 3);
            borderless = sui::button_like(settings, defaults, {76, 431, 263, 467}, "");
            Win hint = sui::label(settings, {77, 472, 268, 500}, "Borderless uses windowed mode.", font, kDim,
                                  sui::kAlignLeft);
            ui_scale = sui::button_like(settings, defaults, {76, 505, 263, 541}, "");
            Win windows[] = {label, fps, borderless, hint, ui_scale};
            for (int i = 0; i < 5; ++i)
                if (windows[i]) sui::set_ids(windows[i], kControls[i], g_settings_tabs_fix ? kControls[i] : 0);
        }
        if (!fps || !borderless) return;
        if (ui_scale && !laid_out)
            sui::add_handler(ui_scale, [](Win button, const sui::Msg& msg) { // 75 .. 150 %, round
                if (!sui::is_click(msg)) return false;
                static const int kSteps[] = {75, 90, 100, 110, 125, 150};
                const int now = atoi(sui::caption(button).c_str() + std::min<size_t>(10, sui::caption(button).size()));
                int next = kSteps[0];
                for (size_t i = 0; i < std::size(kSteps); ++i)
                    if (kSteps[i] == now) { next = kSteps[(i + 1) % std::size(kSteps)]; break; }
                sui::set_caption(button, "UI Scale: " + std::to_string(next) + "%");
                return true;
            });
        sui::add_handler(fps, [](Win, const sui::Msg& msg) {
            return msg.type == sui::kMsgTextChanged;
        });
        if (!laid_out) sui::add_handler(borderless, [](Win button, const sui::Msg& msg) {
            if (!sui::is_click(msg)) return false;
            sui::set_caption(button, sui::caption(button) == "Borderless: On" ? "Borderless: Off" : "Borderless: On");
            return true;
        });
        sui::add_handler(accept, [fps, borderless, ui_scale, file](Win, const sui::Msg& msg) {
            if (!sui::is_click(msg)) return false;
            if (ui_scale) { // its own key: saved apart from the frame cap and the window (read again within 2 s)
                const int percent = ui_percent(ui_scale);
                if (percent && percent != lineages_graphics::read_ui_scale(file))
                    lineages_graphics::write_ui_scale(file, percent);
            }
            const auto input = sui::edit_text(fps);
            char* end = nullptr;
            const long value = strtol(input.c_str(), &end, 10);
            if (!input.empty() && (*end || value < 1 || value > 360)) {
                MessageBoxW((HWND)H->game_window(), L"Enter a whole number from 1 to 360. Leave it blank for Spore's default.",
                            L"Frame Rate Cap", MB_OK | MB_ICONWARNING);
                return true;
            }
            const lineages_graphics::Settings value_to_save{(int)value, borderless_on(borderless)};
            const auto previous = lineages_graphics::read(file);
            if (previous.fps == value_to_save.fps && previous.borderless == value_to_save.borderless)
                return false;
            if (!lineages_graphics::write(file, value_to_save)) {
                MessageBoxW((HWND)H->game_window(), L"Could not save the graphics settings.", L"Lineages", MB_OK | MB_ICONWARNING);
                return true;
            }
            // BUG-9102: re-applying an unchanged borderless window on every accept left Spore's
            // Settings button dead until a restart. Apply only what changed.
            struct Change { lineages_graphics::Settings value; bool fps, window; };
            const bool all = !g_graphics_replace_fix;
            H->run_outside_frame([](void* data) {
                auto* change = static_cast<Change*>(data);
                const auto& settings = change->value;
                const auto cap = std::string("frame.target ") + (settings.fps ? std::to_string(settings.fps) : "auto");
                const int fps = change->fps ? H->command_exec(cap.c_str(), nullptr) : 0;
                const int window = change->window
                    ? H->command_exec(settings.borderless ? "window.borderless on" : "window.borderless off", nullptr) : 0;
                if (fps || window)
                    MessageBoxW((HWND)H->game_window(),
                        window ? L"Turn off Full Screen Mode to use borderless. Restart Spore if its settings prompt you."
                               : L"Restart Spore to apply the new frame rate cap.",
                        L"Lineages Graphics", MB_OK | MB_ICONINFORMATION);
                delete change;
            }, new Change{value_to_save, all || previous.fps != value_to_save.fps,
                          all || previous.borderless != value_to_save.borderless});
            return false; // let the native confirmation save its own settings
        });
        last_open = nullptr;
    }
    if (!fps || !borderless) return;
    if (last_open != settings) {
        const auto saved = lineages_graphics::read(file);
        sui::set_edit_text(fps, saved.fps ? std::to_string(saved.fps) : "");
        const int percent = lineages_graphics::read_ui_scale(file);
        if (sui::caption(borderless).empty()) // the check box: off while Full Screen Mode is on (the two exclusive)
            sui::set_button_selected(borderless,
                                     saved.borderless && !sui::button_selected(sui::find(settings, kFullScreenCheck)));
        else
            sui::set_caption(borderless, saved.borderless ? "Borderless: On" : "Borderless: Off");
        if (sui::as_slider(ui_scale))
            sui::set_slider_value(ui_scale, percent);
        else if (ui_scale)
            sui::set_caption(ui_scale, "UI Scale: " + std::to_string(percent) + "%");
        last_open = settings;
    }
    for (uint32_t id : kControls)
        if (Win control = sui::find(settings, id)) {
            sui::set_visible(control, visible(panel));
            if (g_settings_tabs_fix && visible(panel) && !(sui::flags(control) & sui::kFlagEnabled))
                sui::set_enabled(control, true); // BUG-9120: the tab switch disabled it with its page
        }
    if (sui::as_slider(ui_scale) && panel) { // 3J: the resolution list
        Win combo = sui::find(panel, 0x046170A1);
        tidy_resolution_list(panel, combo, combo ? sui::parent(combo) : nullptr,
                             {sui::find(settings, 0xC00F0119), fps, sui::find(settings, 0xC00F011A),
                              sui::find(settings, kControls[0])});
    }
    if (sui::as_slider(ui_scale)) { // the value beside the slider, rounded to 5 %
        const std::string shown = std::to_string(ui_percent(ui_scale)) + "%";
        Win value = sui::find(settings, kIfaceValue);
        if (value && sui::caption(value) != shown) sui::set_caption(value, shown);
    }
    if (sui::caption(borderless).empty()) { // Borderless Mode and Full Screen Mode exclude each other (the owner)
        static bool was_border;
        Win full = sui::find(settings, kFullScreenCheck);
        if (sui::button_selected(borderless) && full && sui::button_selected(full)) {
            if (!was_border) sui::set_button_selected(full, false); // Borderless was just ticked
            else sui::set_button_selected(borderless, false);       // Full Screen was just ticked
        }
        was_border = sui::button_selected(borderless);
    }
    g_graphics_fps = visible(panel) ? fps : nullptr;
    if (!g_graphics_fps) g_graphics_typing = false;
    mouse::set_graphics_input(graphics_input);
}

static void native_scale_tick(); // below the Party panel (3H)
static void refresh_task(void*)
{
    refresh();
    refresh_graphics_options();
    native_scale_tick();
}
void init(const SkHost* host, SkMod* mod, Api api)
{
    H = host;
    M = mod;
    A = std::move(api);
    g_ok = sui::init(H, M);
    std::string lang = load_str("ui", "lang", "auto");
    g_ru = lang == "ru" || (lang == "auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
    if (g_ok) {
        sui::register_commands();
        register_party_commands();
    }
}

void open(bool on)
{
    if (g_want_open == on)
        return;
    g_want_open = on;
    if (on) {
        g_raise = true;
        if (D.root)
            sui::set_edit_text(D.name_edit, A.name()); // fresh values when the dialog appears
    }
    g_dirty = true;
}

bool is_open() { return g_want_open; }

// ---- tooltips: a couple of words for the button under the mouse, after a short hover ----
static bool g_tips_fix = true;
COOP_FIX("ui.tooltips", "BUG-9107", g_tips_fix, "co-op window: buttons show a short tooltip on hover");
static const char* tip_for(const std::string& name)
{
    struct Tip { const char* name; const char* en; const char* ru; };
    static const Tip kTips[] = {
        {"report", "Send a bug report", "Отправить отчёт"},
        {"refreshlan", "Refresh the server list", "Обновить список серверов"},
        {"join", "Join this server", "Подключиться"},
        {"saveserver", "Save to Server List", "Сохранить сервер"},
        {"deleteserver", "Remove from Server List", "Удалить сервер"},
        {"host", "Host a new session", "Создать сервер"},
        {"update", "Check or install updates", "Обновления"},
        {"optimal", "Steady world transfer", "Плавная передача"},
        {"fast", "Fastest world transfer", "Быстрая передача"},
        {"seat1", "Player slots for a new campaign", "Места в кампании"},
        {"seat2", "Player slots for a new campaign", "Места в кампании"},
        {"seat3", "Player slots for a new campaign", "Места в кампании"},
        {"seat4", "Player slots for a new campaign", "Места в кампании"},
        {"hideaddress", "Show or hide", "Показать / скрыть"},
        {"hidejoinport", "Show or hide", "Показать / скрыть"},
        {"hidejoinpassword", "Show or hide", "Показать / скрыть"},
        {"hidehostport", "Show or hide", "Показать / скрыть"},
        {"hidehostpassword", "Show or hide", "Показать / скрыть"},
        {"hidehostaddresses", "Show or hide addresses", "Показать / скрыть адреса"},
        {"copyinvite", "Copy your address for friends", "Копировать адрес"},
        {"queueseats", "Apply: more slots now, fewer from the next stage", "Применить: больше мест сразу, меньше со следующего этапа"},
        {"chatup", "Older messages", "Ранние сообщения"},
        {"chatdown", "Newer messages", "Новые сообщения"},
        {"cprev", "Previous creation", "Предыдущее создание"},
        {"cnext", "Next creation", "Следующее создание"},
        {"jump", "Jump to your partner", "К партнёру"},
        {"give", "Give selected units", "Отдать выбранных"},
        {"givenext", "Choose who receives", "Выбрать получателя"},
        {"space", "Space co-op page", "Космос вместе"},
        {"send", "Send chat message", "Отправить сообщение"},
        {"ready", "Vote ready", "Готовность"},
        {"share", "Send your world to everyone", "Отправить мир всем"},
        {"foreigncreations", "Also get creations your partners received", "Получать и чужие создания партнёров"},
        {"playercells", "Your group's cells in the tide pool", "Клетки группы в водоёме"},
        {"leave", "Leave the session", "Выйти из игры"},
        {"sback", "Back to the Lobby", "Назад в лобби"},
        {"sprev", "Previous page", "Предыдущая страница"},
        {"snext", "Next page", "Следующая страница"},
    };
    for (const auto& t : kTips)
        if (name == t.name)
            return g_ru ? t.ru : t.en;
    if (name.rfind("lan", 0) == 0)
        return g_ru ? "Выбрать сервер" : "Pick this server";
    return nullptr;
}

// The tooltip is made of Spore's own UI pieces so it looks like the game's: a native text field
// box (dark navy, thin light edge, rounded) and a label in the font of Spore's tooltips
// (025A6D0F, about 7 px a character at 800x600), placed just above the button like the game's.
constexpr uint32_t kTipFont = 0x025A6D0F;
constexpr double kTipDelay = 0.8; // seconds the mouse rests on a button before its tooltip shows
static bool g_tip_pill = true; // tests: off = the stock button look
COOP_FIX("ui.tooltip_pill", "BUG-9107", g_tip_pill, "tests: tooltips use the native-style pill background");
static Rect g_tip_area;
static bool g_tip_queued;
static void build_tooltip()
{
    // the pill: our copy of Spore's tooltip background (data/ui/lineages_tooltip.png) drawn the
    // way the native tooltip draws its own; a native text field box if the image is missing
    g_tip_bg = nullptr;
    void* img = sui::image(sui::fnv("lineages_tooltip"), 0x2F7D0004, sui::fnv("sporecoop"));
    void* look = img ? sui::std_drawable_tooltip(img) : nullptr;
    if (img)
        sui::release(img); // the drawable holds its own reference
    if (look && (g_tip_bg = sui::button_like(D.root, D.like_button, {0, 0, 10, 20}, "")) && g_tip_pill)
        sui::set_drawable(g_tip_bg, look);
    H->log(M, look && g_tip_bg ? SK_INFO : SK_WARN, "ui: tooltip background: image %p, look %p, window %p", img, look,
           g_tip_bg);
    if (!g_tip_bg)
        g_tip_bg = sui::text_edit(D.root, {0, 0, 10, 20}, "", kTipFont, 1, true);
    g_tip_text = sui::label(D.root, {0, 0, 10, 20}, "", kTipFont, 0xFFFFFFFF, sui::kAlignLeft);
    for (Win w : {g_tip_bg, g_tip_text})
        if (w) {
            sui::set_flag(w, sui::kFlagIgnoreMouse, true);
            sui::set_visible(w, false);
        }
    g_tip_shown.clear();
}
static void apply_tooltip(void*)
{
    g_tip_queued = false;
    if (!D.root)
        return;
    if (!g_tip_text && !g_tip_want.empty())
        build_tooltip();
    if (!g_tip_bg || !g_tip_text)
        return;
    const bool on = !g_tip_want.empty();
    if (on) {
        sui::set_caption(g_tip_text, g_tip_want);
        sui::set_area(g_tip_bg, g_tip_area);
        sui::set_area(g_tip_text, {g_tip_area.x1 + 8, g_tip_area.y1, g_tip_area.x2 + 40, g_tip_area.y2} /* room: no wrap */);
    }
    sui::set_visible(g_tip_bg, on);
    sui::set_visible(g_tip_text, on);
    g_tip_shown = g_tip_want;
}

void draw_tooltip()
{
    static Win s_over;
    static double s_since;
    Win over = nullptr;
    const char* tip = nullptr;
    Rect btn{};
    int mx = -1, my = -1;
    mouse::last_move(mx, my);
    if (g_tips_fix && g_want_open && D.root && sui::visible(D.root))
        for (const auto& [name, w] : g_named) {
            if (!w || !g_btn_fn.contains(w) || !sui::visible(w))
                continue;
            const Rect r = sui::screen_rect(w);
            if (mx >= r.x1 && mx < r.x2 && my >= r.y1 && my < r.y2 && (tip = tip_for(name))) {
                over = w;
                btn = r;
                break;
            }
        }
    const double now = H->time_seconds();
    if (over != s_over) {
        s_over = over;
        s_since = now;
    }
    // a short rest first, like Spore's own tooltips: no flicker while the mouse passes over
    std::string want = over && tip && now - s_since >= kTipDelay ? tip : "";
    if (!want.empty()) {
        // positions are relative to the dialog root
        const Rect root = sui::screen_rect(D.root);
        int sw = 0, sh = 0;
        H->screen_size(&sw, &sh);
        // native: about 6.5 px a character, 8 px of padding each side
        const float w = float(strlen(tip)) * 6.9f + 18, h = 20;
        float x = std::clamp(float(mx) - 18, 4.f, float(sw) - w - 4);
        float y = btn.y1 - h - 3;
        if (y < 4)
            y = btn.y2 + 3;
        g_tip_area = {x - root.x1, y - root.y1, x - root.x1 + w, y - root.y1 + h};
    }
    if (want != g_tip_shown && !g_tip_queued) {
        g_tip_want = want;
        g_tip_queued = true;
        H->run_outside_frame(apply_tooltip, nullptr); // UI changes: outside Present
    }
}

bool step_aside(int x, int y, double secs)
{
    if (!g_want_open || !D.root || !sui::visible(D.root))
        return false;
    Rect r = sui::screen_rect(D.root);
    if (x < r.x1 || x >= r.x2 || y < r.y1 || y >= r.y2)
        return false;
    sui::set_visible(D.root, false);
    g_cache[D.root].vis = 0;
    g_aside_until = H->time_seconds() + secs;
    g_dirty = true;
    return true;
}
int input_lock_now() { return input_lock(); }

void tick()
{
    if (!g_ok)
        return;
    // F9: show/hide the co-op window anywhere in the game
    bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9 && !g_f9_down)
        open(!g_want_open);
    g_f9_down = f9;
    // The galaxy menu's intro cinematic (~10 s, starting ~16 s after the menu came up unless the
    // start-up video was skipped with Escape) holds the game's input lock: the game drops every click
    // on the menu, so a click on our "Co-op" item did nothing (seen in coop_ui.py on GOG 2024, where
    // the menu is reached faster; docs/re/space.md 7.9). The button press is seen here instead: on our
    // item the mod presses Escape for the player (the game's own skip of the intro, the menu's key
    // handler) and opens the window once the menu takes clicks again. (Ending the intro with EndCinematic in the
    // menu, as before a load, left the menu broken: an access violation in UTFWin::Window::SetFlag
    // from App::Canvas::Tick 10 s later, or a game that stopped answering.)
    if (g_menu_drop_fix && g_menu_btn && !in_menu())
        g_menu_btn = g_menu_parent = nullptr;
    // BUG-0145: the primary click from the window's messages (mouse.h), not the physical left button
    HWND gw = (HWND)H->game_window();
    mouse::install(gw);
    mouse::set_list_input(server_list_input);
    uint32_t nclicks = mouse::clicks();
    bool lb = mouse::g_fix ? nclicks != g_lb_seen : (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_lb_seen = nclicks;
    locked_click_tick(mouse::g_fix && lb);
    locked_edit_tick(mouse::g_fix && lb);
    if (mouse::g_fix)
        g_lb_down = false;
    // menu_intro_playing() (mode-checked) before any call on the item
    if (lb && !g_lb_down && gw && g_menu_btn && !g_want_open && !g_skip_since &&
        (!g_menu_drop_fix || world::menu_intro_playing()) && sui::visible(g_menu_btn) &&
        world::menu_intro_playing()) {
        POINT c{};
        int cx = 0, cy = 0;
        Rect r = sui::screen_rect(g_menu_btn);
        mouse::last_click(cx, cy);
        bool at = mouse::g_fix ? (c = POINT{cx, cy}, true) : GetCursorPos(&c) && ScreenToClient(gw, &c);
        if (at && c.x >= r.x1 && c.x < r.x2 && c.y >= r.y1 && c.y < r.y2) {
            H->log(M, SK_INFO, "ui: Co-op clicked during the menu intro: Escape skips it, then the window opens");
            LPARAM lp = 1 | (MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC) << 16);
            PostMessageW(gw, WM_KEYDOWN, VK_ESCAPE, lp);
            PostMessageW(gw, WM_KEYUP, VK_ESCAPE, lp | 0xC0000000);
            g_skip_since = H->time_seconds();
        }
    }
    g_lb_down = lb;
    // the game also drops clicks for ~2-3 s after the intro is over (a click 1 s or 2 s after its end
    // did nothing, 3 s after it worked; GOG 2024 lane 9): the window opens when it takes clicks
    if (g_skip_since) {
        double t = H->time_seconds();
        if (world::menu_intro_playing())
            g_intro_over = 0;
        else if (!g_intro_over)
            g_intro_over = t;
        if (g_intro_over && t - g_intro_over >= 3.0) {
            g_skip_since = g_intro_over = 0;
            if (A.mode && A.mode() == modes::kMenu)
                open(true);
        } else if (t - g_skip_since > 20) {
            g_skip_since = g_intro_over = 0; // (the intro ends on its own after ~10 s; a load may have left the menu)
        }
    }
    double now = H->time_seconds();
    report_tick(now);
    if (g_status_until > 0 && now >= g_status_until) { // a message of a finished action (BUG-0104)
        g_status.clear();
        g_status_until = 0;
        g_dirty = true;
    }
    double period = g_settings_visible ? 0.1 : g_want_open ? 0.25 : 1.0; // the Settings: its slider's value
    if (!g_pending && (g_dirty || now - g_last_refresh >= period)) {
        g_pending = true;
        g_dirty = false;
        g_last_refresh = now;
        H->run_outside_frame(refresh_task, nullptr);
    }
}

// ---- BUG-9156: the downed screen in the Co-op window's look ----
static struct {
    Win box = nullptr, title = nullptr, name = nullptr, prev = nullptr, next = nullptr, give = nullptr;
    Win give_caption = nullptr, edge[4] = {};
    // every part is a child of this one window, made back to front (the box, its edges, the texts, the buttons, the
    // GIVE UP caption): as siblings in the game's main window their order came out by chance and the stage's own UI
    // reordered them (the box over its title and buttons: dimmed, the owner 2026-11-13; the red button over its
    // caption in Cell, BUG-9171). Only this window is kept in front of the game's.
    Win root = nullptr;
    bool failed = false;
} g_dh;
// the Co-op window's panel colour (its frame's content window [4 43 750 581], a filled window without a drawable) and
// the edge of its inner boxes (#7891A0, read from a screenshot 2026-11-12); the owner: "Transparency at like 60%"
constexpr uint32_t kMenuPanelFallback = 0xFF203A4A, kMenuBoxEdge = 0xFF7891A0, kDownedBoxAlpha = 0x66;
static uint32_t menu_panel_fill()
{
    if (D.root)
        for (Win frame : sui::children(D.root)) {
            const Rect a = sui::area(frame);
            if (a.x1 != 0 || a.y1 != 0 || a.w() != 760 || a.h() != 600) continue;
            for (Win inner : sui::children(frame)) {
                const Rect b = sui::area(inner);
                if (b.x1 == 4 && b.y1 == 43 && !sui::drawable(inner)) {
                    const uint32_t f = sui::fill_color(inner);
                    if (f >> 24) return f;
                }
            }
        }
    return kMenuPanelFallback;
}
struct DownedWant {
    bool show = false, box = false;
    std::string name;
    Rect give{}, box_area{};
    bool operator==(const DownedWant& o) const
    {
        return show == o.show && box == o.box && name == o.name && give.x1 == o.give.x1 && give.y1 == o.give.y1 &&
               give.x2 == o.give.x2 && give.y2 == o.give.y2 && box_area.x1 == o.box_area.x1 &&
               box_area.y1 == o.box_area.y1 && box_area.x2 == o.box_area.x2 && box_area.y2 == o.box_area.y2;
    }
};
static DownedWant g_dh_want, g_dh_shown;
static bool g_dh_queued;
static void (*g_dh_click)(int);

static bool dh_build()
{
    if (g_dh.give) return true;
    if (g_dh.failed) return false;
    if (!D.root) { // the Co-op window's layout gives the gold button and the font; it stays hidden unless opened
        build_dialog();
        if (D.root) sui::set_visible(D.root, g_want_open);
    }
    Win main = sui::main_window();
    if (!D.like_button || !main) { g_dh.failed = true; return false; }
    g_dh.root = sui::label(main, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (!g_dh.root) { g_dh.failed = true; return false; }
    sui::set_flag(g_dh.root, sui::kFlagIgnoreMouse, false); // its buttons take the clicks
    Win root = g_dh.root;
    auto button = [&](const std::string& caption, int code) {
        Win w = sui::button_like(root, D.like_button, {0, 0, 10, 10}, caption);
        if (!w) return w;
        sui::set_font(w, D.font_body);
        sui::add_handler(w, [code](Win, const sui::Msg& m) {
            if (!sui::is_click(m)) return false;
            if (g_dh_click) g_dh_click(code);
            return true;
        });
        return w;
    };
    // the box: the Co-op window's own panel colour, see-through (60%), with its inner boxes' light edge; rounded
    // (the owner 2026-11-12): data/ui/lineages_downed_panel.png (tools/make_icons.py) drawn cut in thirds so the
    // corners keep their size; without it (an old package) a square box of filled labels
    void* img = sui::image(sui::fnv("lineages_downed_panel"), 0x2F7D0004, sui::fnv("sporecoop"));
    void* look = img ? sui::std_drawable_tooltip(img) : nullptr;
    if (img) sui::release(img); // the drawable holds its own reference
    if (look && (g_dh.box = sui::button_like(root, D.like_button, {0, 0, 10, 10}, ""))) {
        sui::set_drawable(g_dh.box, look);
        sui::set_flag(g_dh.box, sui::kFlagIgnoreMouse, true);
    }
    // (2026-11-13: square on TITAN's QA games: they read Data\SporeCoop.package of the installed game, an older one
    // without the image; "another image of ours here" tells an old package from a broken image)
    H->log(M, look && g_dh.box ? SK_INFO : SK_WARN,
           "ui: downed box background: image %p, look %p, window %p%s", img, look, g_dh.box,
           img ? "" : sui::has_image(sui::fnv("lineages_tooltip"), sui::fnv("sporecoop"))
                          ? ": the game's SporeCoop.package is an older one (no lineages_downed_panel): a square box"
                          : ": no SporeCoop.package images here: a square box");
    if (!g_dh.box) {
        const uint32_t panel = (menu_panel_fill() & 0x00FFFFFFu) | (kDownedBoxAlpha << 24);
        g_dh.box = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
        if (g_dh.box) sui::set_fill_color(g_dh.box, panel);
        for (Win& e : g_dh.edge) {
            e = sui::label(root, {0, 0, 1, 1}, "", D.font_body, kWhite, sui::kAlignCenter);
            if (e) sui::set_fill_color(e, kMenuBoxEdge);
        }
    }
    g_dh.title = sui::label(root, {0, 0, 10, 10}, "SPECTATING", D.font_body, kWhite, sui::kAlignCenter);
    g_dh.name = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kGold, sui::kAlignCenter);
    g_dh.prev = button(kArrowLeft, -1);
    g_dh.next = button(kArrowRight, 1);
    for (Win a : {g_dh.prev, g_dh.next})
        if (a) sui::center_caption(a); // (a copied button's caption sat right of the middle, the owner 2026-11-13)
    g_dh.give = button("", 0);
    if (g_dh.give) { // the Co-op window's red button: its gold button shaded, the caption a white label on top (as Leave)
        apply_red(g_dh.give);
        sui::set_label_color(g_dh.give, 0);
        g_dh.give_caption = sui::label(root, {0, 0, 10, 10}, "GIVE UP", D.font_body, kWhite, sui::kAlignCenter);
    }
    if (!g_dh.give || !g_dh.box || !g_dh.title || !g_dh.name || !g_dh.prev || !g_dh.next) {
        g_dh.failed = !g_dh.give;
        H->log(M, SK_WARN, "ui: downed screen parts missing (give %p box %p)", g_dh.give, g_dh.box);
    }
    // back to front, once: the order inside the root is ours alone
    for (Win w : {g_dh.box, g_dh.edge[0], g_dh.edge[1], g_dh.edge[2], g_dh.edge[3], g_dh.title, g_dh.name, g_dh.prev,
                  g_dh.next, g_dh.give, g_dh.give_caption})
        if (w) {
            sui::add_ref(w);
            sui::bring_to_front(root, w);
            sui::set_visible(w, false);
        }
    sui::add_ref(root);
    sui::set_visible(root, false);
    return g_dh.give != nullptr;
}

static void dh_apply(void*)
{
    g_dh_queued = false;
    const DownedWant want = g_dh_want;
    if (!dh_build()) return;
    Win main = sui::main_window();
    const bool box = want.show && want.box;
    const Rect& b = want.box_area;
    const Rect& g = want.give;
    // the root covers the box and the button under it; its children are placed inside it (their order is fixed)
    const Rect all = box ? Rect{std::min(b.x1, g.x1), b.y1, std::max(b.x2, g.x2), g.y2} : g;
    if (want.show && main) {
        sui::set_area(g_dh.root, all);
        if (sui::parent(g_dh.root) != main) sui::add_child(main, g_dh.root);
        sui::bring_to_front(main, g_dh.root);
        sui::set_flag(g_dh.root, sui::kFlagAlwaysInFront, true);
    }
    sui::set_visible(g_dh.root, want.show);
    auto place = [&](Win w, bool on, const Rect& r) {
        if (!w) return;
        if (on) sui::set_area(w, {r.x1 - all.x1, r.y1 - all.y1, r.x2 - all.x1, r.y2 - all.y1});
        sui::set_visible(w, on);
    };
    // one padding all round (10, the panel's corner radius): the arrow row on the bottom, the title over it
    const float arrow_w = 50.f, arrow_h = 30.f, pad = 10.f, e = 1.f;
    const float arrow_y = b.y2 - pad - arrow_h;
    if (g_dh.give_caption) sui::set_flag(g_dh.give_caption, sui::kFlagIgnoreMouse, true);
    place(g_dh.box, box, b);
    place(g_dh.edge[0], box, {b.x1, b.y1, b.x2, b.y1 + e});
    place(g_dh.edge[1], box, {b.x1, b.y2 - e, b.x2, b.y2});
    place(g_dh.edge[2], box, {b.x1, b.y1, b.x1 + e, b.y2});
    place(g_dh.edge[3], box, {b.x2 - e, b.y1, b.x2, b.y2});
    place(g_dh.title, box, {b.x1, b.y1 + pad, b.x2, arrow_y});
    place(g_dh.name, box, {b.x1 + arrow_w + pad, arrow_y, b.x2 - arrow_w - pad, arrow_y + arrow_h});
    if (g_dh.name && box) sui::set_caption(g_dh.name, want.name);
    place(g_dh.prev, box, {b.x1 + pad, arrow_y, b.x1 + pad + arrow_w, arrow_y + arrow_h});
    place(g_dh.next, box, {b.x2 - pad - arrow_w, arrow_y, b.x2 - pad, arrow_y + arrow_h});
    place(g_dh.give, want.show, g);
    place(g_dh.give_caption, want.show, g);
    g_dh_shown = want;
}

bool set_downed_hud(bool show, bool box, const std::string& name, const Rect& give, const Rect& box_area,
                    void (*on_click)(int))
{
    if (!g_ok || g_dh.failed) return false;
    g_dh_click = on_click;
    DownedWant want;
    want.show = show;
    want.box = box;
    want.name = name;
    want.give = give;
    want.box_area = box_area;
    if (!show) want = DownedWant{};
    g_dh_want = want;
    if (!(want == g_dh_shown) && !g_dh_queued) {
        g_dh_queued = true;
        H->run_outside_frame(dh_apply, nullptr); // UI changes: outside Present
    }
    // BUG-9171: in Cell the red button came over its "GIVE UP" caption after it was placed (the stage's own UI reorders
    // the main window's children): the caption is put in front again twice a second while the screen is up
    static double s_front_at = 0; // (now the root: its parts keep their own order inside it)
    if (show && want == g_dh_shown && g_dh.root && !g_dh_queued && H->time_seconds() - s_front_at > 0.5) {
        s_front_at = H->time_seconds();
        g_dh_queued = true;
        H->run_outside_frame([](void*) {
            g_dh_queued = false;
            Win main = sui::main_window();
            if (main && g_dh.root && g_dh_shown.show) {
                sui::bring_to_front(main, g_dh.root);
                sui::set_flag(g_dh.root, sui::kFlagAlwaysInFront, true);
            }
        }, nullptr);
    }
    return g_dh.give != nullptr || show; // the first frame: the parts are being made
}

// ---------------- the Party cards (decision 3G, 2026-11-14) ----------------
// One card per partner in the art of the stage that partner plays (images: data/ui/lineages_card_*.png,
// tools/party_card_assets.py): a plate with the name, the place, the health bar and a second bar (Creature the hunger,
// Space the ship's energy) and a picture frame right of it (a click: that creature's Sporepedia card). The cards stack
// at the right edge from just above Spore's own UI there (the avatar or ship card, the Pack list, the Tribe and Military
// tabs: measured, so nothing native is covered), the newest partner on top, with a "Party" tab in the art of this
// player's own stage. The own card is the game's. Replaces the 2026-11-13 panel (header, plates, gold cards).
constexpr int kPartyRows = 8;
// the health bar as the game builds its own (the Creature HUD, layout 5ED10964, read 2026-11-13: the empty pill, a
// clipping window (flag 0x400) as wide as the health holding the full pill, the edge on top)
constexpr uint32_t kFlagClip = 0x400;
static uint32_t g_portrait_mode = 0; // the layout mode of the portrait's drawable (std_drawable_image); coop.partymode
struct CardBar {
    Win icon = nullptr, back = nullptr, clip = nullptr, fill = nullptr, ring = nullptr;
    std::string shown_fill, shown_icon;
};
struct PartyRowWin {
    Win plate = nullptr, frame = nullptr, name = nullptr, place = nullptr, portrait = nullptr;
    CardBar bars[2];
    uint32_t shown_portrait = 0;
    std::string shown_style;
};
static struct {
    Win root = nullptr, tab = nullptr, title = nullptr;
    PartyRowWin rows[kPartyRows];
    std::string tab_style;
    bool failed = false;
    Rect area{};
} g_ph;
static std::vector<PartyRow> g_ph_want, g_ph_shown;
static bool g_ph_want_show, g_ph_shown_show, g_ph_queued;
static float g_ph_want_scale = 1, g_ph_shown_scale = 0;
static int g_ph_sw, g_ph_sh, g_ph_shown_sw, g_ph_shown_sh;
static void (*g_ph_click)(uint8_t);
static uint32_t g_ph_portraits_missing;
static float g_ph_floor = -1; // the top of Spore's own UI at the right edge (screen pixels; -1 not measured)
static uint32_t g_ph_own_stage; // this player's game mode (the tab's art)
static bool g_ph_images_missing; // the cards' images not (yet) in the game: the caller shows the text list
static int g_ph_hover = -1;       // the card the pointer is over (its frame lit and grown), -1 none
static bool g_ph_dirty;           // laid out again at the next set_party_hud (a hover change)
static std::vector<PartyRow> g_ph_cards; // the partners shown, top card first (a frame's click)
static float ns_factor_now(); // the UI scale of Spore's own UI (below, 3H)
// the card's layout (1x pixels; tests: `coop.cardlayout <name>=<value>...` changes them live, looked at by screenshot)
// (the owner 2026-11-14: "some text that looked badly spaced", the name against the plate's top and "Party" low in its
// tab: measured on screenshots, now 6-8 px inside every edge and between the rows, the title's glyphs 6 px from the
// tab's top and bottom)
static struct CardLayout {
    float card_h = 71, frame_w = 71, plate_w = 128, plate_dy = 2, plate_cut = 2; // plate: top dy, bottom card_h - cut
    float text_x = 9, name_y = 7, name_h = 18, place_y = 25, place_h = 14;     // from the plate's top left
    float icon_x = 7, bar_x = 22, bar_w = 97, bar_h = 8, bar1_y = 40, bar2_y = 52, icon_dy = -2;
    float pic_pad = 6, gap = 6, edge = 4, above = 6, tab_w = 92, tab_h = 30, tab_gap = 6, title_dy = 1;
} g_cl;

static const char* card_style(uint32_t mode)
{
    switch (mode) {
    case modes::kCell: return "cell";
    case modes::kCreature: return "creature";
    case modes::kTribe: return "tribe";
    case modes::kCiv: return "civ";
    case modes::kSpace: return "space";
    default: return nullptr;
    }
}

// one drawable per image (shared by the windows that show it; the game's tooltip drawable: corners kept, the rest
// stretched)
static void* card_look(const std::string& stem)
{
    static std::vector<std::pair<std::string, void*>> s_looks;
    for (auto& l : s_looks)
        if (l.first == stem) return l.second;
    void* img = sui::image(sui::fnv(stem.c_str()), 0x2F7D0004, sui::fnv("sporecoop"));
    void* look = img ? sui::std_drawable_tooltip(img) : nullptr;
    if (img) sui::release(img);
    if (look) {
        sui::add_ref(look); // ours for the session (the windows take their own)
        s_looks.push_back({stem, look});
    }
    return look;
}

static Win party_image_window(Win root, const char* stem)
{
    void* look = card_look(stem);
    if (!look) return nullptr;
    Win w = sui::button_like(root, D.like_button, {0, 0, 10, 10}, "");
    if (!w) return nullptr;
    sui::set_drawable(w, look);
    sui::set_label_color(w, 0);
    return w;
}

static void set_look(Win w, std::string& shown, const std::string& stem)
{
    if (!w || shown == stem) return;
    if (void* look = card_look(stem)) {
        sui::set_drawable(w, look);
        // the game draws a window's new drawable only once the window changes (seen 2026-11-14: a partner's new
        // stage showed its art ~10 s late): hidden and shown again, it is drawn anew
        const bool was = sui::visible(w);
        sui::set_visible(w, false);
        sui::set_visible(w, was);
        shown = stem;
    }
}

static bool ph_build()
{
    if (g_ph.root) return true;
    if (g_ph.failed) return false;
    if (!D.root) {
        build_dialog();
        if (D.root) sui::set_visible(D.root, g_want_open);
    }
    Win main = sui::main_window();
    if (!D.like_button || !main) { g_ph.failed = true; return false; }
    static double s_retry_at = 0; // a package mounted later (tests) or still loading: looked for again every 5 s
    if (H->time_seconds() < s_retry_at) return false;
    if (!sui::has_image(sui::fnv("lineages_card_plate_space"), sui::fnv("sporecoop"))) {
        static bool s_said = false;
        if (!s_said) H->log(M, SK_WARN, "ui: the Party cards' images are not in this game's SporeCoop.package (an older one): the text list");
        s_said = true;
        s_retry_at = H->time_seconds() + 5;
        g_ph_images_missing = true;
        return false;
    }
    g_ph_images_missing = false;
    g_ph.root = sui::label(main, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (!g_ph.root) { g_ph.failed = true; return false; }
    Win root = g_ph.root;
    sui::set_flag(root, sui::kFlagIgnoreMouse, false); // the frames take the clicks
    std::vector<Win> order;
    g_ph.tab = party_image_window(root, "lineages_card_tab_space");
    g_ph.tab_style = "lineages_card_tab_space";
    g_ph.title = sui::label(root, {0, 0, 10, 10}, T("Party", "Группа"), D.font_body, kWhite, sui::kAlignCenter);
    order.push_back(g_ph.tab);
    order.push_back(g_ph.title);
    for (int i = 0; i < kPartyRows; ++i) {
        auto& r = g_ph.rows[i];
        r.plate = party_image_window(root, "lineages_card_plate_space");
        r.name = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignLeft);
        r.place = sui::label(root, {0, 0, 10, 10}, "", D.font_button ? D.font_button : D.font_body, kWhite,
                             sui::kAlignLeft);
        order.insert(order.end(), {r.plate, r.name, r.place});
        for (auto& bar : r.bars) {
            bar.icon = party_image_window(root, "lineages_card_icon_health");
            bar.back = party_image_window(root, "lineages_party_bar_back");
            bar.clip = sui::label(root, {0, 0, 1, 1}, "", D.font_body, kWhite, sui::kAlignCenter);
            bar.fill = bar.clip ? party_image_window(bar.clip, "lineages_party_bar_fill") : nullptr;
            bar.ring = party_image_window(root, "lineages_party_bar_ring");
            bar.shown_fill = "lineages_party_bar_fill";
            bar.shown_icon = "lineages_card_icon_health";
            if (bar.clip) sui::set_flag(bar.clip, kFlagClip, true);
            for (Win w : {bar.icon, bar.back, bar.clip, bar.fill, bar.ring})
                if (w) sui::set_flag(w, sui::kFlagIgnoreMouse, true);
            order.insert(order.end(), {bar.icon, bar.back, bar.clip, bar.ring});
        }
        r.frame = party_image_window(root, "lineages_card_frame_space");
        r.shown_style = "space";
        // r.portrait: the game's own image window, made for each picture (ph_apply), over the frame
        for (Win w : {r.plate, r.name, r.place})
            if (w) sui::set_flag(w, sui::kFlagIgnoreMouse, true);
        if (r.frame)
            sui::add_handler(r.frame, [i](Win, const sui::Msg& m) {
                if (!sui::is_click(m)) return false;
                if (g_ph_click && i < (int)g_ph_cards.size()) g_ph_click(g_ph_cards[i].player);
                return true;
            });
        order.push_back(r.frame);
    }
    for (Win w : order) // back to front, once
        if (w) {
            sui::add_ref(w);
            sui::bring_to_front(root, w);
            sui::set_visible(w, false);
        }
    sui::add_ref(root);
    sui::set_visible(root, false);
    H->log(M, SK_INFO, "ui: the Party cards are made (%d rows)", kPartyRows);
    return true;
}

// the top of Spore's own UI at the right edge, below the screen's top quarter: the windows of the HUD layouts that
// reach the right edge (the avatar / ship / cell card, the Pack list, the Tribe and Military tabs); the screen's bottom
// bar's top when none. Drawn places: a scaled piece (ui.native_scale) is taller than its layout
// BUG-9191 (owner play test 2026-11-14): in an editor the stats panel at the top right reaches below the top quarter
// (its ability icons), so the cards stacked above those icons and covered the stats: in an editor only the bottom
// half's right-edge windows count, and the cards sit above the editor's bottom buttons, clear of the stats
static bool g_ph_editor_spot = true;
COOP_FIX("ui.party_editor_spot", "BUG-9191", g_ph_editor_spot,
         "ui: in an editor the Party cards sit low at the right edge, not over the editor's stats");
static float measure_right_floor(int sw, int sh)
{
    Win main = sui::main_window();
    float floor = (float)sh - 64;
    const bool editor = g_ph_editor_spot && (g_ph_own_stage == modes::kEditor || g_ph_own_stage == modes::kEdit);
    const float from = sh * (editor ? 0.5f : 0.25f);
    if (!main) return floor;
    struct It { Win w; int depth; };
    std::vector<It> stack{{main, 0}};
    int seen = 0;
    while (!stack.empty() && seen++ < 6000) {
        const It it = stack.back();
        stack.pop_back();
        for (Win c : sui::children(it.w)) {
            if (!sui::visible(c) || c == g_ph.root || c == D.root) continue;
            const Rect r = sui::screen_rect(c);
            const bool layer = r.w() >= sw * 0.9f && r.h() >= sh * 0.9f;
            if (!layer && r.x2 >= sw - 14 && r.x1 >= sw * 0.6f && r.y1 >= from && r.y1 < floor &&
                r.h() < sh * 0.6f && r.w() > 8 && r.h() > 8 && (sui::drawable(c) || !sui::caption(c).empty()))
                floor = r.y1;
            if (it.depth < 9) stack.push_back({c, it.depth + 1});
        }
    }
    if (ns_factor_now() != 1.f) floor = sh - (sh - floor) * ns_factor_now(); // (anchored at the bottom right)
    return floor;
}

static void ph_apply(void*)
{
    g_ph_queued = false;
    std::vector<PartyRow> want; // the partners (the own card is the game's)
    for (const auto& r : g_ph_want)
        if (!r.me) want.push_back(r);
    const bool show = g_ph_want_show && !want.empty();
    const float s = g_ph_want_scale;
    const int sw = g_ph_sw, sh = g_ph_sh;
    if (!show && !g_ph.root) { // nothing to show: made when first needed
        g_ph_shown = g_ph_want;
        g_ph_shown_show = false;
        return;
    }
    if (!ph_build()) return;
    Win main = sui::main_window();
    auto px = [s](float v) { return (float)std::lround(v * s); };
    g_ph_floor = measure_right_floor(sw, sh);
    const int n = std::min<int>((int)want.size(), kPartyRows);
    const CardLayout& L = g_cl;
    const float right = sw - px(L.edge), bottom = g_ph_floor - px(L.above);
    const float row_h = px(L.card_h), gap = px(L.gap);
    const float top = bottom - n * row_h - (n - 1) * gap - px(L.tab_h + L.tab_gap);
    const Rect all{right - px(L.frame_w + L.plate_w - 4), top, right, bottom};
    if (show && main) {
        sui::set_area(g_ph.root, all);
        if (sui::parent(g_ph.root) != main) sui::add_child(main, g_ph.root);
        sui::bring_to_front(main, g_ph.root);
    }
    sui::set_visible(g_ph.root, show);
    g_ph.area = show ? all : Rect{};
    auto place = [&](Win w, bool on, Rect r) {
        if (!w) return;
        if (on) sui::set_area(w, {r.x1 - all.x1, r.y1 - all.y1, r.x2 - all.x1, r.y2 - all.y1});
        sui::set_visible(w, on);
    };
    // the tab in this player's own stage
    const char* own = card_style(g_ph_own_stage);
    const std::string own_style = own ? own : "space";
    set_look(g_ph.tab, g_ph.tab_style, "lineages_card_tab_" + own_style);
    place(g_ph.tab, show, {right - px(L.tab_w), top, right, top + px(L.tab_h)});
    place(g_ph.title, show, {right - px(L.tab_w), top + px(L.title_dy), right, top + px(L.tab_h + L.title_dy)});
    sui::set_label_color(g_ph.title, 0xFFFFFFFF);
    for (int i = 0; i < kPartyRows; ++i) {
        auto& r = g_ph.rows[i];
        const bool on = show && i < n;
        const PartyRow row = on ? want[i] : PartyRow{};
        const char* st = card_style(row.stage);
        const std::string style = st ? st : own_style;
        const bool hover = on && g_ph_hover == i;
        const std::string want_style = style + (hover ? "_hover" : "");
        if (on && want_style != r.shown_style) {
            std::string a = "", b = "";
            set_look(r.plate, a, "lineages_card_plate_" + style);
            set_look(r.frame, b, "lineages_card_frame_" + want_style);
            r.shown_style = want_style;
        }
        // the newest partner on top: row i counts up from the bottom
        const float cy = bottom - (float)(n - i) * row_h - (float)(n - 1 - i) * gap;
        const float fx = right - px(L.frame_w);
        const float grow = hover ? px(3) : 0; // the hovered card's frame and picture grow a little (as the game's buttons)
        const Rect frame{fx - grow, cy - grow, right + std::min(grow, px(L.edge) - 1), cy + px(L.card_h) + grow};
        const Rect plate{fx - px(L.plate_w - 4), cy + px(L.plate_dy), fx + px(4), cy + px(L.card_h - L.plate_cut)};
        place(r.plate, on, plate);
        // left-aligned: the game's labels centre their text, so each box is as wide as its text plus the label's
        // own margin (text_px), starting one margin left of the text
        auto left = [&](float x, float y1, float y2, const std::string& t, bool small) {
            const float m = kLabelMargin;
            return Rect{x - m, y1, x - m + text_px(t, small) + 2 * m, y2};
        };
        place(r.name, on, left(plate.x1 + px(L.text_x), plate.y1 + px(L.name_y), plate.y1 + px(L.name_y + L.name_h),
                               row.name, false));
        place(r.place, on, left(plate.x1 + px(L.text_x), plate.y1 + px(L.place_y),
                                plate.y1 + px(L.place_y + L.place_h), row.place, D.font_button != 0));
        if (on) {
            const bool light = style == "cell"; // dark text on the Cell's light metal
            sui::set_caption(r.name, row.name);
            sui::set_caption(r.place, row.place);
            sui::set_label_color(r.name, light ? 0xFF182244 : 0xFFFFFFFF);
            sui::set_label_color(r.place, light ? 0xFF2A3E8A : style == "space" ? 0xFF96CDFF : 0xFFE8C870);
        }
        // the bars: health, then the second one (its own colour and icon)
        for (int k = 0; k < 2; ++k) {
            auto& bar = r.bars[k];
            // the second bar where the stage has one (Creature hunger, Space energy)
            const bool second = row.stage == modes::kCreature || row.stage == modes::kSpace;
            const float value = k == 0 ? row.health : second ? row.energy : -1.f;
            const bool visible = on && value >= 0;
            if (visible && k == 1) {
                const bool space = row.stage == modes::kSpace;
                std::string fill_stem = space ? "lineages_card_bar_blue" : "lineages_card_bar_orange";
                std::string icon_stem = space ? "lineages_card_icon_energy" : "lineages_card_icon_hunger";
                if (bar.fill && bar.shown_fill != fill_stem) set_look(bar.fill, bar.shown_fill, fill_stem);
                set_look(bar.icon, bar.shown_icon, icon_stem);
            }
            const float by = plate.y1 + px(k == 0 ? L.bar1_y : L.bar2_y);
            const Rect b{plate.x1 + px(L.bar_x), by, plate.x1 + px(L.bar_x + L.bar_w), by + px(L.bar_h)};
            const float filled = std::round((b.x2 - b.x1) * std::clamp(value, 0.f, 1.f));
            place(bar.icon, visible, {plate.x1 + px(L.icon_x), by + px(L.icon_dy), plate.x1 + px(L.icon_x + 12),
                                      by + px(L.icon_dy + 12)});
            place(bar.back, visible, b);
            place(bar.clip, visible && filled > 0, {b.x1, b.y1, b.x1 + filled, b.y2});
            if (bar.fill) { // the full pill inside the clipping window (its own coordinates)
                sui::set_area(bar.fill, {0, 0, b.x2 - b.x1, b.y2 - b.y1});
                sui::set_visible(bar.fill, visible);
            }
            place(bar.ring, visible, b);
        }
        place(r.frame, on, frame);
        // the picture: an image window of the game's own (IImageDrawable::AddImageWindow: the picture as it is, scaled
        // to the window) made again for a new one, over the frame (a picture not here yet: asked again, set_party_hud)
        if (on && row.portrait != r.shown_portrait && row.portrait &&
            sui::has_image(row.portrait, row.portrait_group)) {
            if (r.portrait) {
                sui::set_visible(r.portrait, false);
                sui::remove_child(g_ph.root, r.portrait);
                r.portrait = nullptr;
            }
            r.portrait = sui::image_window(g_ph.root, row.portrait, row.portrait_group, 0, 0);
            if (r.portrait) {
                sui::set_flag(r.portrait, sui::kFlagIgnoreMouse, true);
                r.shown_portrait = row.portrait;
            }
        } else if (on && row.portrait && !r.shown_portrait && g_ph_portraits_missing++ < 8) {
            H->log(M, SK_INFO, "ui: Party cards: no picture %08X/%08X here for %s", row.portrait, row.portrait_group,
                   row.name.c_str());
        }
        if (hover && r.frame) sui::bring_to_front(g_ph.root, r.frame); // over its neighbours while grown
        if (r.portrait) sui::bring_to_front(g_ph.root, r.portrait);
        place(r.portrait, on && r.shown_portrait,
              {frame.x1 + px(L.pic_pad), frame.y1 + px(L.pic_pad), frame.x2 - px(L.pic_pad), frame.y2 - px(L.pic_pad)});
    }
    g_ph_shown = g_ph_want;
    g_ph_cards = want;
    g_ph_shown_show = g_ph_want_show;
    g_ph_shown_scale = s;
    g_ph_shown_sw = sw;
    g_ph_shown_sh = sh;
}

bool set_party_hud(bool show, const std::vector<PartyRow>& rows, float scale, void (*on_portrait)(uint8_t))
{
    if (!g_ok || g_ph.failed) return false;
    g_ph_click = on_portrait;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    // the hover: the pointer's last place (the game window's own mouse moves) over a card's frame (the frames' mouse
    // enter / leave messages did not come, 2026-11-14)
    if (show && g_ph_shown_show && g_ph.root) {
        int mx = -1, my = -1;
        mouse::last_move(mx, my);
        const Rect root = sui::screen_rect(g_ph.root);
        int h = -1;
        for (int i = 0; i < kPartyRows && i < (int)g_ph_cards.size(); ++i) {
            Win f = g_ph.rows[i].frame;
            if (!f || !sui::visible(f)) continue;
            const Rect a = sui::area(f); // (relative to the root)
            if (mx >= root.x1 + a.x1 && mx < root.x1 + a.x2 && my >= root.y1 + a.y1 && my < root.y1 + a.y2) h = i;
        }
        if (h != g_ph_hover) {
            g_ph_hover = h;
            g_ph_dirty = true;
        }
    }
    bool same = show == g_ph_shown_show && rows == g_ph_shown && scale == g_ph_shown_scale &&
                sw == g_ph_shown_sw && sh == g_ph_shown_sh && !g_ph_dirty;
    if (!same) g_ph_dirty = false;
    static double s_floor_at = 0; // Spore's own right-edge UI comes and goes (the Pack list): measured again each second
    if (same && show && H->time_seconds() >= s_floor_at) {
        s_floor_at = H->time_seconds() + 1;
        same = false;
    }
    for (const auto& r : rows)
        if (r.me) g_ph_own_stage = r.stage;
    static double s_retry_at = 0; // a picture that was not here yet: asked again every 2 s
    if (same && show && H->time_seconds() >= s_retry_at)
        for (size_t i = 0; i < rows.size() && i < (size_t)kPartyRows; ++i)
            if (rows[i].portrait && g_ph.rows[i].shown_portrait != rows[i].portrait) {
                same = false;
                s_retry_at = H->time_seconds() + 2;
                break;
            }
    if (!same && !g_ph_queued) {
        g_ph_want = rows;
        g_ph_want_show = show;
        g_ph_want_scale = std::clamp(scale, 0.5f, 2.f);
        g_ph_sw = sw;
        g_ph_sh = sh;
        g_ph_queued = true;
        H->run_outside_frame(ph_apply, nullptr);
    }
    return !g_ph.failed && !g_ph_images_missing;
}

Rect party_hud_area() { return g_ph_shown_show ? g_ph.area : Rect{}; }

// ---- decision 3H A: Spore's own UI at the Interface Size (BUG-9191) ----
// No native setting exists (docs/re/graphics.md). The window manager builds each window's transform and sends it to the
// window's procs as kMsgTransformed (0x15, d = the matrix), as the game's InflateEffect grows a button; a proc of ours on
// every top piece of the UI (the first windows under the full-screen layers: the HUD pieces, dialogs, menus) scales that
// matrix. The game scales about the window's middle; ours moves the pivot to the piece's own screen side (a HUD piece
// at the bottom left grows from the bottom left, a dialog in the middle from its middle), so nothing leaves the
// screen. Hit tests use the inverse the window manager computes from it. The Party panel scales itself (set_party_hud).
// The mouse: ns_remap below. Nothing changes at 100 % (the default). Old (`coop.fix ui.native_scale off`): Spore's UI at
// 100 %, only the mod's parts scale.
static bool g_native_scale = true;
COOP_FIX("ui.native_scale", "BUG-9191", g_native_scale,
         "ui: the Interface Size scales Spore's own HUD, menus and dialogs, not only the mod's parts");
static float g_ns_factor = 1.f;
static float ns_factor_now() { return g_ns_factor; }
static std::vector<std::pair<Win, uint32_t>> g_ns_done; // windows with our proc (and their id, against a reused address)

// one piece's size and pivot: its extent (its own rectangle and its children's: Settings' root is a 44x38 box in a
// corner with its frame and pages placed around it; children as large as the screen, as the dim layer behind
// Options, do not count), the factor (no piece past 95 % of the screen: a dialog at 150 % on a 768 high screen) and
// the pivot (the side of the screen the piece sits on, by thirds, else its middle)
struct NsXform {
    float s = 1, px = 0, py = 0;
    sui::Rect extent;
};
static NsXform ns_xform(Win w)
{
    NsXform t;
    const sui::Rect screen = sui::area(sui::main_window());
    sui::Rect r = sui::screen_rect(w);
    for (Win c : sui::children(w)) {
        if (!sui::visible(c)) continue;
        const sui::Rect b = sui::screen_rect(c);
        if (b.w() >= screen.w() * 0.9f || b.h() >= screen.h() * 0.9f) continue;
        r = {std::min(r.x1, b.x1), std::min(r.y1, b.y1), std::max(r.x2, b.x2), std::max(r.y2, b.y2)};
    }
    t.extent = r;
    t.s = g_ns_factor;
    if (t.s > 1 && r.w() > 0 && r.h() > 0)
        t.s = std::max(1.f, std::min({t.s, screen.w() * 0.95f / r.w(), screen.h() * 0.95f / r.h()}));
    const float cx = (r.x1 + r.x2) / 2, cy = (r.y1 + r.y2) / 2, sw = screen.w(), sh = screen.h();
    t.px = cx < sw / 3 ? r.x1 : cx > sw * 2 / 3 ? r.x2 : cx;
    t.py = cy < sh / 3 ? r.y1 : cy > sh * 2 / 3 ? r.y2 : cy;
    return t;
}

static bool ns_handle(Win w, const sui::Msg& m)
{
    if (m.type != sui::kMsgTransformed || !m.d || g_ns_factor == 1.f) return false;
    float* x = (float*)(uintptr_t)m.d;
    if (IsBadWritePtr(x, 64)) return false;
    const NsXform t = ns_xform(w);
    // the game scales about the window's own middle (seen live 2026-11-13: the main menu, Options); the pivot moves
    // to (px, py)
    const sui::Rect own = sui::screen_rect(w);
    const float mx = (own.x1 + own.x2) / 2, my = (own.y1 + own.y2) / 2;
    x[0] *= t.s;
    x[5] *= t.s;
    x[12] += (t.px - mx) * (1 - t.s);
    x[13] += (t.py - my) * (1 - t.s);
    return false;
}

// the top pieces of Spore's UI, back to front (each window's children in their list order): the first windows under
// the full-screen layers, without the Party panel (it scales itself)
static void ns_pieces(Win w, const sui::Rect& screen, std::vector<Win>& out, int depth = 0)
{
    if (depth > 8) return;
    for (Win c : sui::children(w)) {
        if (out.size() >= 2000) return;
        if (!sui::visible(c) || c == g_ph.root) continue;
        const sui::Rect r = sui::screen_rect(c);
        if (r.w() >= screen.w() * 0.9f && r.h() >= screen.h() * 0.9f) ns_pieces(c, screen, out, depth + 1);
        else out.push_back(c);
    }
}

// the window of the piece drawn under the (unscaled) point: the deepest visible one taking the mouse (later children
// are drawn over earlier ones)
static Win ns_target_at(Win w, float x, float y, int depth = 0)
{
    if (depth > 12) return nullptr;
    const auto kids = sui::children(w);
    const sui::Rect screen = sui::area(sui::main_window());
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        Win c = *it;
        if (!sui::visible(c)) continue;
        if (Win deeper = ns_target_at(c, x, y, depth + 1)) return deeper; // (children can lie outside their parent)
        const sui::Rect r = sui::screen_rect(c);
        const bool inside = x >= r.x1 && x < r.x2 && y >= r.y1 && y < r.y2;
        const bool layer = r.w() >= screen.w() * 0.9f || r.h() >= screen.h() * 0.9f; // a container, not a control
        if (inside && !layer && !(sui::flags(c) & sui::kFlagIgnoreMouse)) return c;
    }
    return nullptr;
}

// the window is drawn under the screen point: its rectangle through its drawn transform (Window +0x148)
static bool ns_drawn_under(Win w, float x, float y)
{
    float sx, sy, tx, ty;
    if (!w || !sui::render_xform(w, sx, sy, tx, ty)) return false;
    const sui::Rect r = sui::real_area(w);
    return x >= tx && x < tx + r.w() * sx && y >= ty && y < ty + r.h() * sy;
}

// The mouse over a scaled piece: Spore hit-tests most windows, and drags (sliders, the planner), in their unscaled
// places, but some (buttons that grow on hover, the main menu's items) through their drawn transform, and that is not
// told by a flag (2026-11-14: "Create" opened Share with the pointer as it was; Options' buttons one row low when
// moved). So the window drawn under the pointer is found here (the piece's layout, unscaled), and the game's own hit
// test (GetWindowAtPosition) is asked about the real point: when it finds that window the point stays as it is, else
// it goes to the window's place at 100 %. A press keeps its choice until the release (a drag past the piece's edge
// follows the real pointer out there). Anywhere else (the world) stays as is.
// the point's place at 100 % inside a piece: its drawn transform undone (the game places a scaled piece a little off
// the pivot ns_handle aims at: Options sat ~30 px higher, 2026-11-13, so the drawn transform is the truth here)
struct NsMap {
    float sx = 1, sy = 1, tx = 0, ty = 0, ox = 0, oy = 0;
};
static bool ns_map_of(Win w, NsMap& m)
{
    if (!sui::render_xform(w, m.sx, m.sy, m.tx, m.ty)) return false;
    const sui::Rect own = sui::screen_rect(w); // its unscaled origin
    m.ox = own.x1;
    m.oy = own.y1;
    return true;
}
static bool ns_remap_scale(unsigned message, int& x, int& y);
// every mouse message: to the scaled pieces' unscaled places
static bool ns_remap(unsigned message, int& x, int& y)
{
    return ns_remap_scale(message, x, y);
}

static bool ns_remap_scale(unsigned message, int& x, int& y)
{
    static Win s_capture = nullptr;
    static NsMap s_capture_t;
    static sui::Rect s_capture_extent;
    static bool s_capture_raw = false; // the press went to the game as it was: its drag too
    if (g_ns_factor == 1.f) {
        s_capture = nullptr;
        return false;
    }
    const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
                      message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDBLCLK;
    const bool up = message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP;
    auto apply = [&](const NsMap& t) {
        x = (int)std::lround((x - t.tx) / t.sx + t.ox);
        y = (int)std::lround((y - t.ty) / t.sy + t.oy);
    };
    if (s_capture && !down) { // the drag of a press that began on a piece: moved while over the piece's drawn extent
        // (a part dragged from an editor's palette into the world follows the real pointer out there)
        const sui::Rect e = s_capture_extent;
        const float ux = (x - s_capture_t.tx) / s_capture_t.sx + s_capture_t.ox,
                    uy = (y - s_capture_t.ty) / s_capture_t.sy + s_capture_t.oy;
        const bool over = ux >= e.x1 && ux < e.x2 && uy >= e.y1 && uy < e.y2;
        if (up) s_capture = nullptr;
        if (!over || s_capture_raw) return false;
        apply(s_capture_t);
        return true;
    }
    s_capture = nullptr;
    Win main = sui::main_window();
    if (!main) return false;
    std::vector<Win> pieces;
    ns_pieces(main, sui::area(main), pieces);
    for (auto it = pieces.rbegin(); it != pieces.rend(); ++it) {
        NsMap t;
        if (!ns_map_of(*it, t) || (t.sx == 1.f && t.sy == 1.f)) continue;
        const float ux = (x - t.tx) / t.sx + t.ox, uy = (y - t.ty) / t.sy + t.oy;
        const sui::Rect e = ns_xform(*it).extent;
        // (any window of the piece under the point: its popups reach past its extent, the resolution list below Settings)
        Win target = ns_target_at(*it, ux, uy);
        if (!target) continue;
        // the game's hits with the point as it is and moved: the one drawn under the pointer wins; both drawn there: a
        // control (button, slider, field) before a plain window, then the one inside the other (the creator chooser's
        // icons: their own hit test is right, a decoration over them took the moved point; the main menu's items and
        // the Settings' buttons are found only moved, 2026-11-14)
        Win raw_hit = sui::window_at((float)x, (float)y), moved_hit = sui::window_at(ux, uy);
        auto ok = [&](Win h) { return h && h != *it && ns_drawn_under(h, (float)x, (float)y); };
        auto control = [](Win h) { return sui::as_button(h) || sui::as_slider(h) || sui::as_combo(h); };
        auto inside = [](Win a, Win b) { // a lies in b
            for (Win w = a; w; w = sui::parent(w))
                if (w == b) return true;
            return false;
        };
        bool raw_right = ok(raw_hit) && raw_hit != moved_hit;
        if (raw_right && ok(moved_hit)) {
            if (control(raw_hit) != control(moved_hit)) raw_right = control(raw_hit);
            else raw_right = inside(raw_hit, moved_hit);
        }
        if (raw_right) { // the game finds it as it is
            if (down) {
                H->log(M, SK_INFO, "ui: press %d,%d on piece %08X: the game's hit test finds %08X drawn there, not moved",
                       x, y, sui::control_id(*it), sui::control_id(raw_hit));
                s_capture = *it;
                s_capture_t = t;
                s_capture_extent = e;
                s_capture_raw = true;
            }
            return false;
        }
        if (down) {
            H->log(M, SK_INFO, "ui: press %d,%d on piece %08X (drawn x%.2f +%.0f,%.0f, origin %.0f,%.0f) -> %.0f,%.0f; "
                   "drawn there %p %08X, the game finds %p %08X as it is, %p %08X moved", x, y, sui::control_id(*it), t.sx,
                   t.tx, t.ty, t.ox, t.oy, ux, uy, target, sui::control_id(target), raw_hit, sui::control_id(raw_hit),
                   moved_hit, sui::control_id(moved_hit));
        }
        apply(t);
        if (down) {
            s_capture = *it;
            s_capture_t = t;
            s_capture_extent = e;
            s_capture_raw = false;
        }
        return true;
    }
    return false;
}

static void native_scale_tick()
{
    static double next = 0;
    const double now = H->time_seconds();
    if (now < next) return;
    next = now + 1;
    float want = 1.f;
    if (g_native_scale)
        want = lineages_graphics::read_ui_scale(slotfs::widen(H->mods_dir()) + L"\\" + lineages_graphics::file_name) / 100.f;
    mouse::set_remap(ns_remap); // (the short resolution list needs it at 100 % too)
    if (want == 1.f && g_ns_factor == 1.f) return; // 100 %: nothing to attach
    Win main = sui::main_window();
    if (!main) return;
    std::vector<Win> pieces;
    ns_pieces(main, sui::area(main), pieces);
    for (Win p : pieces) {
        const uint32_t id = sui::control_id(p);
        bool done = false;
        for (auto& d : g_ns_done) done |= d.first == p && d.second == id;
        if (done) continue;
        // every message group: kMsgTransformed (0x15) is in none of ModAPI's named flags (0x80 alone missed it,
        // 2026-11-13); the proc returns at once for the others
        sui::add_handler(p, ns_handle, 0xFFFF);
        g_ns_done.push_back({p, id});
        if (g_ns_done.size() > 4000) g_ns_done.erase(g_ns_done.begin(), g_ns_done.begin() + 1000);
        sui::invalidate_transform(p);
    }
    if (want != g_ns_factor) { // a new size: every piece found now builds its transform again
        H->log(M, SK_INFO, "ui: Spore's UI at %.0f %% (%zu pieces)", want * 100, pieces.size());
        g_ns_factor = want;
        for (Win p : pieces) sui::invalidate_transform(p);
    }
}

// tests (BUG-9191): `coop.partymode <n>`: the portrait drawable's layout mode, the pictures made again
void register_party_commands()
{
    H->command_add(M, "coop.cardlayout", "coop.cardlayout [<name>=<value>...] - tests: the Party cards' layout (1x px)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       struct F { const char* n; float* v; };
                       CardLayout& L = g_cl;
                       const F fields[] = {{"card_h", &L.card_h}, {"frame_w", &L.frame_w}, {"plate_w", &L.plate_w},
                                           {"plate_dy", &L.plate_dy}, {"plate_cut", &L.plate_cut}, {"text_x", &L.text_x},
                                           {"name_y", &L.name_y}, {"name_h", &L.name_h}, {"place_y", &L.place_y},
                                           {"place_h", &L.place_h}, {"icon_x", &L.icon_x}, {"bar_x", &L.bar_x},
                                           {"bar_w", &L.bar_w}, {"bar_h", &L.bar_h}, {"bar1_y", &L.bar1_y},
                                           {"bar2_y", &L.bar2_y}, {"icon_dy", &L.icon_dy}, {"pic_pad", &L.pic_pad},
                                           {"gap", &L.gap}, {"edge", &L.edge}, {"above", &L.above}, {"tab_w", &L.tab_w},
                                           {"tab_h", &L.tab_h}, {"tab_gap", &L.tab_gap}, {"title_dy", &L.title_dy}};
                       for (int i = 1; i < argc; i++) {
                           const char* eq = strchr(argv[i], '=');
                           if (!eq) continue;
                           for (const F& f : fields)
                               if (!strncmp(argv[i], f.n, eq - argv[i]) && strlen(f.n) == (size_t)(eq - argv[i]))
                                   *f.v = (float)atof(eq + 1);
                       }
                       for (const F& f : fields) H->out_printf(out, "%s=%g ", f.n, *f.v);
                       g_ph_shown.clear(); // laid out again
                       g_ph_shown_show = false;
                       return 0;
                   }, nullptr, 0);
    H->command_add(M, "coop.partymode", "coop.partymode <n> - tests: the Party portraits' drawable layout mode",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1) g_portrait_mode = (uint32_t)strtoul(argv[1], nullptr, 0);
                       for (auto& r : g_ph.rows) r.shown_portrait = 0;
                       g_ph_shown.clear();
                       H->out_printf(out, "portrait mode %u", g_portrait_mode);
                       return 0;
                   }, nullptr, SK_CMD_OUTSIDE_FRAME);
    // tests (BUG-9191, the native Pack card): `coop.layoutshow <instance hex> [x y] [group hex]`: the game's layout
    // shown at x, y with every window visible (looked at by screenshot), `coop.layoutshow off` takes it away
    H->command_add(M, "coop.layoutshow", "coop.layoutshow <inst> [x y] [group] | off - tests: a native layout on screen",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       static Win shown = nullptr;
                       Win main = sui::main_window();
                       if (shown && main) {
                           sui::set_visible(shown, false);
                           sui::remove_child(main, shown);
                           shown = nullptr;
                       }
                       if (argc < 2 || !strcmp(argv[1], "off")) return 0;
                       const uint32_t group = argc > 4 ? (uint32_t)strtoul(argv[4], nullptr, 16) : 0x40464200u;
                       Win w = sui::load_layout((uint32_t)strtoul(argv[1], nullptr, 16), group);
                       if (!w || !main) { H->out_printf(out, "load failed"); return 1; }
                       // (depth-limited: a recursive walk of a layout overflowed the stack, game 8 2026-11-13)
                       std::function<void(Win, int)> all = [&](Win x, int depth) {
                           sui::set_visible(x, true);
                           if (depth < 5)
                               for (Win c : sui::children(x)) if (c != x) all(c, depth + 1);
                       };
                       all(w, 0);
                       const Rect a = sui::area(w);
                       const float x = argc > 3 ? (float)atof(argv[2]) : 500, y = argc > 3 ? (float)atof(argv[3]) : 300;
                       sui::set_area(w, {x, y, x + a.w(), y + a.h()});
                       sui::add_child(main, w);
                       sui::bring_to_front(main, w);
                       shown = w;
                       H->out_printf(out, "%s", sui::dump(w, 4, false).c_str());
                       return 0;
                   }, nullptr, SK_CMD_OUTSIDE_FRAME);
}

void shutdown()
{
    if (Win w = g_ph.root) {
        sui::set_visible(w, false);
        if (Win p = sui::parent(w)) sui::remove_child(p, w);
    }
    g_ph = {};
    g_ph_shown.clear();
    g_ph_shown_show = false;
    if (Win w = g_dh.root) { // BUG-9156: the root takes its parts with it
        sui::set_visible(w, false);
        if (Win p = sui::parent(w)) sui::remove_child(p, w);
    }
    g_dh = {};
    mouse::set_graphics_input(nullptr);
    mouse::set_list_input(nullptr);
    if (D.root)
        sui::set_visible(D.root, false);
    if (g_menu_btn && (!g_menu_drop_fix || in_menu()))
        sui::set_visible(g_menu_btn, false);
}

std::string debug_state()
{
    std::string s = "open=" + std::to_string(g_want_open) + " page=" +
                    (A.role() ? (g_space_page ? "space" : "lobby") : "connect") +
                    " menu_button=" + (g_menu_btn ? "yes" : "no") + " in_menu=" + (in_menu() ? "1" : "0") +
                    " input_lock=" + std::to_string(input_lock()) + " modal=" + (game_modal() ? "1" : "0") +
                    " modal_seen=" + std::to_string(g_modal_seen) + " locked_clicks=" + std::to_string(g_locked_clicks) +
                    " locked_native=" + std::to_string(g_locked_native) +
                    " locked_typed=" + std::to_string(g_locked_typed) +
                    " share_enabled=" + (D.share_btn && (sui::flags(D.share_btn) & sui::kFlagEnabled) ? "1" : "0") + "\n";
    char b[256];
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    snprintf(b, sizeof b, "screen %d %d\n", sw, sh);
    s += b;
    if (g_menu_btn && in_menu()) { // never a call on an item of a menu that is gone (BUG-0163)
        Rect r = sui::screen_rect(g_menu_btn);
        snprintf(b, sizeof b, "menu %.0f %.0f %.0f %.0f %d '%s'\n", r.x1, r.y1, r.x2, r.y2,
                 (int)sui::visible(g_menu_btn), sui::caption(g_menu_btn).c_str());
        s += b;
    }
    for (auto& [name, w] : g_named) {
        Rect r = sui::screen_rect(w);
        bool vis = sui::visible(w) && D.root && sui::visible(D.root);
        std::string cap = name == "hostpassword" || name == "joinpassword" || (D.hidden_fields.contains(w) && D.hidden_fields.at(w))
                              ? "<hidden>" : name == "name" || name == "address" || name == "port" ||
                                             name == "joinport" || name == "chatedit"
                              ? sui::edit_text(w) : sui::caption(w);
        snprintf(b, sizeof b, "%s %.0f %.0f %.0f %.0f %d '%s'\n", name.c_str(), r.x1, r.y1, r.x2, r.y2, (int)vis,
                 cap.c_str());
        s += b;
    }
    const size_t players = A.players ? A.players().size() : 0;
    for (size_t i = 0; i < std::min<size_t>(4, players); i++)
        if (D.row_name[i] && sui::visible(D.row_name[i]))
            s += "row " + std::to_string(i) + " '" + sui::caption(D.row_name[i]) + "' '" + sui::caption(D.row_stage[i]) +
                 "' '" + sui::caption(D.row_ping[i]) + "' '" + sui::caption(D.row_ready[i]) + "'\n";
    for (int i = 0; i < 5; i++)
        if (D.chat_lines[i] && !sui::caption(D.chat_lines[i]).empty())
            s += "chat '" + sui::caption(D.chat_lines[i]) + "'\n";
    if (D.session)
        s += "session '" + sui::caption(D.session) + "'\n";
    if (D.world)
        s += "world '" + sui::caption(D.world) + "'\n";
    if (D.creation)
        s += "creation '" + sui::caption(D.creation) + "'\n";
    if (D.lan_hdr)
        s += "lan '" + sui::caption(D.lan_hdr) + "'\n";
    s += "status '" + g_status + "'\n";
    s += "spage first=" + std::to_string(g_space_first) + " rows=" + std::to_string(kSpaceRows) + "\n";
    return s;
}

uint32_t text_font() { return g_text_font; }

bool russian() { return g_ru; }

void set_russian(bool ru)
{
    g_ru = ru;
    g_dirty = true;
}

} // namespace coop_ui

