#include "coop_ui.h"
#include "trace.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>

#include "modes.h"
#include "mouse.h"
#include "net/fixes.h"
#include "notify.h"
#include "net/protocol.h"
#include "net/udp.h"
#include "notify_text.h"
#include "space_ext.h"
#include "settings.h"
#include "sui.h"
#include "world.h"
#include "fixes.h"

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
constexpr float kW = 760, kH = 660; // the lobby rows, the host's "Ask the players" row, the footer (version, "Report a problem")
constexpr int kSpaceRows = 12;
static bool g_space_page; // the lobby shows the Space page instead
static int g_lan_first;   // the first game on the network listed (pages of three, BUG-0183)
bool g_lan_pages_fix = true; // `coop.corefix lan_pages 0`: only the first three games, no "More" (BUG-0183)
COOP_FIX("core.lan_pages", "BUG-0183", g_lan_pages_fix, "ui: every network game is listed, with More pages");
static int g_space_first; // the first row shown (pages of kSpaceRows rows)

static const char* T(const char* en, const char* ru) { return g_ru ? ru : en; }

// ---------------- widgets with cached state ----------------
struct Cache {
    std::string text;
    uint32_t color = 0;
    int vis = -1;
    bool left = false; // left aligned label: the box is sized to the text (labels center their text)
    Rect box;
    int enabled = -1;
};
static std::map<Win, Cache> g_cache;
static int g_chat_scroll;            // chat lines scrolled up from the newest (BUG-0129)
constexpr size_t kChatCols = 70;     // letter cells of a chat line (680 px)

// width of a text in the dialog font (average glyph width, measured at 1080p)
static float text_width(const std::string& t)
{
    int n = 0;
    for (unsigned char ch : t)
        n += (ch & 0xC0) != 0x80;
    return n * 9.3f + 30;
}

static void fit(Win w, const Cache& c)
{
    Rect r = c.box;
    r.x2 = std::min(r.x2, r.x1 + text_width(c.text));
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
// The connect steps (0.7.12 F1, docs/08-ui.md#ui-window-steps): the name (first start only), Host or Join, then
// the Host page or the Join page. The lobby replaces them while a session runs (role != 0); a session that ends
// leaves the window on the page of its role (a guest: Join, a host: Host).
enum Step { kStepName, kStepChoose, kStepHost, kStepJoin };
static int g_step = kStepChoose;
static bool g_settings_page;  // the lobby shows the session settings instead
static uint32_t g_test_color;  // tests / layout shots (coop.uistate color <argb>): the colour of rows without one
static int g_set_first;       // the first settings entry shown (pages of kSetRows)
static std::string g_set_line; // the settings page's answer line (a refused change, a setting's help)
static uint32_t g_set_line_color = kDim;
// BUG-0765: the first start asks for the player's name (owner 2026-11-08: typed, no Steam / GOG name); off: the
// window opens on the Host / Join step with "Player"
static bool g_name_first_fix = true;
COOP_FIX("core.ui_name_first", "BUG-0765", g_name_first_fix, "ui: the first start of the co-op window asks for the player's name");
constexpr int kSetRows = 13;

struct Dialog {
    Win root = nullptr, layer = nullptr;
    uint32_t font_body = 0, font_button = 0;
    Win like_button = nullptr; // gold "Login" button of the layout
    Win subtitle = nullptr;
    Win version = nullptr, report_btn = nullptr; // every page: the version line, "Report a problem"
    // connect steps: every widget belongs to one step (status / update / back are shared, shown by refresh)
    std::vector<Win> page_name, page_choose, page_host, page_join;
    Win name_edit = nullptr, name_ok = nullptr, me_line = nullptr, back_btn = nullptr;
    Win addr_edit = nullptr, port_edit = nullptr, join_btn = nullptr, host_btn = nullptr;
    Win lan_hdr = nullptr, host_hint = nullptr, status = nullptr, update_btn = nullptr;
    Win lan_rows[3] = {};
    std::string lan_addr[3];
    Win lan_more = nullptr; // BUG-0183: the next three games on the network
    // lobby page
    std::vector<Win> page_lobby;
    Win session = nullptr, address = nullptr, world = nullptr, chat_edit = nullptr, send_btn = nullptr;
    Win ready_btn = nullptr, share_btn = nullptr, leave_btn = nullptr, ask_btn = nullptr, settings_btn = nullptr;
    Win row_name[4] = {}, row_color[4] = {}, row_stage[4] = {}, row_ping[4] = {}, row_ready[4] = {};
    Win chat_lines[5] = {};
    Win chat_up = nullptr, chat_down = nullptr;
    Win creation = nullptr, creation_prev = nullptr, creation_next = nullptr, give_btn = nullptr, give_next = nullptr, jump_btn = nullptr;
    Win space_btn = nullptr; // lobby -> the Space page (Space stage only)
    // settings page: group headers and the rows of settings_core.h kDefs, a value button and a scope tag each
    std::vector<Win> page_settings;
    Win set_title = nullptr, set_note = nullptr, set_line = nullptr, set_back = nullptr, set_prev = nullptr,
        set_next = nullptr, set_pageno = nullptr;
    Win set_label[kSetRows] = {}, set_btn[kSetRows] = {}, set_tag[kSetRows] = {};
    int set_id[kSetRows] = {}; // the setting of each row now (-1: a group header or nothing)
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
    Cache c{t, argb, 1, align == sui::kAlignLeft, r};
    if (c.left)
        fit(w, c);
    g_cache[w] = c;
    return w;
}

// BUG-0273: while a cinematic or a caption plays, the game holds its input lock
// (cGameInputManager +0x110 above 0) and drops every click on the UI, also on our window: a
// guest's "I'm ready" during the brain-upgrade captions of a vote did nothing, the vote ran out of
// time. The click is taken from the window's messages instead (mouse.h) and given to the button
// under it, unless the game delivered it itself within kLockedClickWait. Off: the clicks are
// dropped while the lock is held (0.7.3.0).
bool g_locked_clicks_fix = true;
COOP_FIX("core.ui_locked_clicks", "BUG-0273", g_locked_clicks_fix, "ui: co-op window buttons take clicks while a caption holds the game's input lock");
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
    if (!clicked || !g_locked_clicks_fix || !mouse::g_fix || !g_want_open || !D.root || !sui::visible(D.root) ||
        g_locked_btn || !clicks_locked())
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

static Win add_edit(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t, int max_len)
{
    Win w = sui::text_edit(D.root, r, t, D.font_body, max_len);
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

// the name step's Continue (and Enter in its field): the name is saved, the next start goes to Host / Join
static void do_name_ok()
{
    std::string n = sui::edit_text(D.name_edit);
    while (!n.empty() && n.back() == ' ')
        n.pop_back();
    while (!n.empty() && n.front() == ' ')
        n.erase(0, 1);
    if (n.empty()) {
        set_status(T("Type the name the other players will see", "Введите имя, которое увидят другие игроки"), kRed, 8);
        return;
    }
    if (A.role() != 0) { // (the name step is not shown in a session; a console join meanwhile)
        set_status(T("Leave the session to change your name", "Чтобы сменить имя, выйдите из игры"), kRed, 8);
        return;
    }
    if (n != A.name())
        A.set_name(n);
    save_str("coop", "name", A.name());
    save_str("ui", "name_chosen", "1");
    g_step = kStepChoose;
    set_status("", kYellow);
}

static void do_join(const std::string& address)
{
    std::string addr = address;
    if (net::g_fix.addr_check) // BUG-0138: a leading space or the whole pasted hint line
        addr = net::Endpoint::clean(addr);
    while (!addr.empty() && addr.back() == ' ')
        addr.pop_back();
    if (addr.empty()) {
        set_status(T("Type the host's address, e.g. 192.168.1.5 or sporecoop.local",
                     "Введите адрес хоста, например 192.168.1.5 или sporecoop.local"),
                   kRed);
        return;
    }
    std::string err;
    if (!A.join(addr, err)) {
        set_status(std::string(T("Cannot join: ", "Не удалось подключиться: ")) + err, kRed, g_status_fix ? 30 : 0);
        return;
    }
    save_str("ui", "last_address", addr);
    // a name or an address without a port: the session uses the default port (net::kDefaultPort)
    bool port = addr.find(':') != std::string::npos;
    set_status(std::string(T("Connecting to ", "Подключение к ")) + addr +
                   (port ? "" : ":" + std::to_string(net::kDefaultPort)) + "...",
               kYellow);
}

static void do_host()
{
    int port = atoi(sui::edit_text(D.port_edit).c_str());
    if (port <= 0 || port > 65535)
        port = A.default_port;
    std::string err;
    if (!A.host((uint16_t)port, err)) {
        set_status(std::string(T("Cannot host: ", "Не удалось создать игру: ")) + err, kRed);
        return;
    }
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
    if (!g_want_open) // the pause menu's item: the window is closed, a panel says where the report is
        notify::show(std::string(T("Report saved: ", "Отчёт сохранён: ")) + p.substr(p.find_last_of("\\/") + 1));
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

static void do_set_click(int row);

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

    std::vector<Win> common;
    D.subtitle = add_label(common, {0, 70, kW, 96}, T("CO-OP", "КООПЕРАТИВ"), kGold, sui::kAlignCenter);
    // every page: which SporeCoop and game build run, and a report for bug reports
    D.version = add_label(common, {40, 608, 500, 630}, A.version_line ? A.version_line() : "", kDim);
    D.report_btn = add_button(common, "report", {510, 602, 720, 634}, T("Report a problem", "Сообщить об ошибке"),
                              do_report);

    // ---- step 1: the name (first start) ----
    auto& N = D.page_name;
    add_label(N, {40, 116, 720, 144}, T("Welcome to SporeCoop!", "Добро пожаловать в SporeCoop!"), kGold);
    add_label(N, {40, 156, 720, 182}, T("Choose the name the other players will see.", "Выберите имя, которое увидят другие игроки."),
              kWhite);
    add_label(N, {40, 184, 720, 210}, T("You can change it later.", "Его можно сменить позже."), kDim);
    add_label(N, {40, 240, 225, 268}, T("Your name", "Ваше имя"), kWhite);
    D.name_edit = add_edit(N, "name", {230, 236, 520, 268}, A.name(), 24);
    D.name_ok = add_button(N, "nameok", {540, 232, 720, 272}, T("Continue", "Далее"), do_name_ok);

    // ---- step 2: Host or Join ----
    auto& C = D.page_choose;
    D.me_line = add_label(C, {40, 112, 520, 140}, "", kWhite);
    add_button(C, "rename", {540, 106, 720, 142}, T("Change name", "Сменить имя"), [] {
        g_step = kStepName;
        sui::set_edit_text(D.name_edit, A.name());
        set_status("", kYellow);
    });
    add_label(C, {40, 176, 720, 204}, T("What would you like to do?", "Что вы хотите сделать?"), kGold);
    add_button(C, "gohost", {40, 214, 360, 264}, T("Host a game", "Создать игру"), [] {
        g_step = kStepHost;
        set_status("", kYellow);
    });
    add_button(C, "gojoin", {400, 214, 720, 264}, T("Join a game", "Подключиться к игре"), [] {
        g_step = kStepJoin;
        g_lan_first = 0;
        set_status("", kYellow);
    });
    add_label(C, {40, 272, 360, 296}, T("Play your world, friends join you", "Ваш мир, друзья входят к вам"), kDim);
    add_label(C, {400, 272, 720, 296}, T("Play in a friend's world", "Игра в мире друга"), kDim);
    add_label(C, {40, 330, 720, 354}, T("Everyone needs SporeCoop and Spore with Galactic Adventures.",
                                        "Всем нужны SporeCoop и Spore с Galactic Adventures."),
              kDim);

    // ---- step 3a: Host ----
    auto& HP = D.page_host;
    add_label(HP, {200, 108, 720, 134}, T("Host a game", "Создать игру"), kGold);
    add_label(HP, {40, 170, 225, 198}, T("Port (UDP)", "Порт (UDP)"), kWhite);
    D.port_edit = add_edit(HP, "port", {230, 166, 330, 198},
                           load_str("ui", "host_port", std::to_string(A.default_port).c_str()), 5);
    add_label(HP, {340, 170, 720, 198},
              (std::string(T("(", "(")) + std::to_string(net::kDefaultPort) + T(" if you are not sure)", ", если не уверены)")),
              kDim);
    D.host_btn = add_button(HP, "host", {540, 214, 720, 254}, T("Host", "Создать"), do_host);
    add_label(HP, {40, 274, 720, 298}, T("Friends on your network find your game in their list.",
                                         "Друзья в вашей сети увидят игру в списке."),
              kDim);
    add_label(HP, {40, 300, 720, 324}, T("Over the internet: forward the UDP port on your router,",
                                         "Через интернет: откройте UDP-порт на роутере"),
              kDim);
    add_label(HP, {40, 326, 720, 350}, T("or play over a VPN (ZeroTier, Tailscale, Radmin VPN).",
                                         "или играйте через VPN (ZeroTier, Tailscale)."),
              kDim);
    D.host_hint = add_label(HP, {40, 366, 720, 392}, "", kWhite);

    // ---- step 3b: Join ----
    auto& J = D.page_join;
    add_label(J, {200, 108, 720, 134}, T("Join a game", "Подключиться к игре"), kGold);
    D.lan_hdr = add_label(J, {40, 148, 590, 174}, "", kDim);
    D.lan_more = add_button(J, "lanmore", {600, 144, 720, 174}, T("More >", "Ещё >"), [] {
        g_lan_first += 3; // refresh_connect wraps it to the first page
        g_dirty = true;
    });
    for (int i = 0; i < 3; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "lan%d", i);
        D.lan_rows[i] = add_button(J, nm, {40, 178.f + i * 38, 720, 212.f + i * 38}, "", [i] {
            if (!D.lan_addr[i].empty()) {
                sui::set_edit_text(D.addr_edit, D.lan_addr[i]);
                do_join(D.lan_addr[i]);
            }
        });
    }
    add_label(J, {40, 306, 720, 332}, T("Or type the host's address", "Или введите адрес хоста"), kGold);
    add_label(J, {40, 344, 225, 372}, T("Address", "Адрес"), kWhite);
    D.addr_edit = add_edit(J, "address", {230, 340, 520, 372}, load_str("ui", "last_address", ""), 64);
    D.join_btn = add_button(J, "join", {540, 336, 720, 376}, T("Join", "Подключиться"),
                            [] { do_join(sui::edit_text(D.addr_edit)); });
    add_label(J, {40, 384, 720, 408}, T("An IP address or a host name: 192.168.1.5, sporecoop.local",
                                        "IP-адрес или имя: 192.168.1.5, sporecoop.local"),
              kDim);
    add_label(J, {40, 410, 720, 434},
              std::string(T("The port is ", "Порт ")) + std::to_string(net::kDefaultPort) +
                  T(" unless you type one (sporecoop.local:47811)", ", если не указан (sporecoop.local:47811)"),
              kDim);

    // shared by the connect steps: Back (Host / Join), the status line and the update button
    std::vector<Win> steps;
    D.back_btn = add_button(steps, "back", {40, 104, 180, 136}, T("< Back", "< Назад"), [] {
        g_step = kStepChoose;
        set_status("", kYellow);
    });
    D.status = add_label(steps, {40, 548, 560, 576}, "", kYellow);
    D.update_btn = add_button(steps, "update", {570, 544, 720, 580}, "", [] {
        if (A.update_do)
            A.update_do();
        g_dirty = true;
    });

    // ---- lobby page ----
    auto& L = D.page_lobby;
    D.settings_btn = add_button(L, "settings", {40, 66, 250, 98}, T("Settings", "Настройки"), [] {
        g_settings_page = true;
        g_set_line.clear();
        g_dirty = true;
    });
    D.session = add_label(L, {40, 104, 720, 130}, "", kWhite);
    add_label(L, {40, 138, 280, 162}, T("Player", "Игрок"), kDim);
    add_label(L, {280, 138, 345, 162}, T("Colour", "Цвет"), kDim);
    add_label(L, {345, 138, 540, 162}, T("Stage", "Этап"), kDim);
    add_label(L, {540, 138, 620, 162}, T("Ping", "Пинг"), kDim);
    add_label(L, {620, 138, 720, 162}, T("Ready", "Готов"), kDim);
    for (int i = 0; i < 4; i++) {
        float y = 166.f + i * 28;
        D.row_name[i] = add_label(L, {40, y, 280, y + 26}, "", kWhite);
        D.row_color[i] = add_label(L, {280, y, 345, y + 26}, "", kDim);
        D.row_stage[i] = add_label(L, {345, y, 540, y + 26}, "", kWhite);
        D.row_ping[i] = add_label(L, {540, y, 620, y + 26}, "", kWhite);
        D.row_ready[i] = add_label(L, {620, y, 720, y + 26}, "", kWhite);
    }
    D.address = add_label(L, {40, 282, 720, 306}, "", kDim);
    D.world = add_label(L, {40, 308, 720, 334}, "", kYellow);
    for (int i = 0; i < 4; i++)
        D.chat_lines[i] = add_label(L, {40, 338.f + i * 23, 720, 360.f + i * 23}, "", kWhite);
    std::vector<Win> arrows; // shown by refresh_lobby only when there is more to see
    D.chat_up = add_button(arrows, "chatup", {722, 338, 756, 368}, "^", [] { g_chat_scroll++; g_dirty = true; });
    D.chat_down = add_button(arrows, "chatdown", {722, 398, 756, 428}, "v", [] { g_chat_scroll--; g_dirty = true; });
    // the own creation of this stage: "My creature: Kirgle  < >"
    D.creation = add_label(L, {40, 434, 560, 460}, "", kGold);
    D.creation_prev = add_button(L, "cprev", {570, 430, 640, 460}, "<", [] {
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle)
            A.civ_role_cycle(-1);
        else if (A.creation_cycle)
            A.creation_cycle(-1);
        g_dirty = true;
    });
    D.jump_btn = add_button(L, "jump", {400, 430, 560, 460}, "", [] {
        if (A.jump_do)
            A.jump_do();
        g_dirty = true;
    });
    D.give_btn = add_button(L, "give", {570, 430, 720, 460}, "", [] {
        if (A.give_do)
            A.give_do();
        g_dirty = true;
    });
    // 3 or 4 players: ">" picks the next other player as the recipient (BUG-0075)
    D.give_next = add_button(L, "givenext", {520, 430, 560, 460}, ">", [] {
        if (A.give_cycle)
            A.give_cycle();
        g_dirty = true;
    });
    D.creation_next = add_button(L, "cnext", {650, 430, 720, 460}, ">", [] {
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle)
            A.civ_role_cycle(1);
        else if (A.creation_cycle)
            A.creation_cycle(1);
        g_dirty = true;
    });
    D.space_btn = add_button(L, "space", {580, 66, 720, 98}, T("Space >", "Космос >"), [] {
        g_space_page = true;
        g_dirty = true;
    });
    D.chat_edit = add_edit(L, "chatedit", {40, 466, 560, 498}, "", 120);
    D.send_btn = add_button(L, "send", {570, 462, 720, 502}, T("Send", "Отправить"), do_send_chat);
    D.ready_btn = add_button(L, "ready", {40, 510, 240, 550}, "", [] {
        A.set_ready(!A.ready());
        g_dirty = true;
    });
    D.share_btn = add_button(L, "share", {260, 510, 480, 550}, T("Send my world", "Отправить мир"), [] {
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
    D.leave_btn = add_button(L, "leave", {500, 510, 720, 550}, "", [] {
        A.leave();
        set_status(T("Left the session", "Вы вышли из игры"), kYellow, g_status_fix ? 5 : 0);
    });
    // the host: ask every guest for its problem report (F4 of 0.7.12 does the asking), any time
    D.ask_btn = add_button(L, "askreports", {40, 558, 440, 592},
                           T("Ask the players for their reports", "Попросить у игроков их отчёты"), [] {
                               std::string line = A.ask_reports ? A.ask_reports()
                                                                : T("Not available in this version yet",
                                                                    "В этой версии пока недоступно");
                               if (!line.empty())
                                   set_status(line, kYellow, 10);
                           });

    // ---- settings page (settings_core.h kDefs by group; docs/07-modes.md#modes-session-settings) ----
    auto& G = D.page_settings;
    D.set_title = add_label(G, {40, 104, 720, 130}, T("Session settings", "Настройки сессии"), kGold);
    D.set_note = add_label(G, {40, 130, 720, 154}, "", kDim);
    for (int i = 0; i < kSetRows; i++) {
        float y = 160.f + i * 27;
        char nm[16];
        D.set_label[i] = add_label(G, {40, y + 1, 470, y + 25}, "", kWhite);
        snprintf(nm, sizeof nm, "set%d", i);
        D.set_btn[i] = add_button(G, nm, {480, y, 590, y + 25}, "", [i] { do_set_click(i); });
        D.set_tag[i] = add_label(G, {600, y + 1, 720, y + 25}, "", kDim);
        D.set_id[i] = -1;
    }
    D.set_line = add_label(G, {40, 514, 720, 538}, "", kDim);
    D.set_back = add_button(G, "setback", {40, 548, 240, 588}, T("< Lobby", "< Лобби"), [] {
        g_settings_page = false;
        g_dirty = true;
    });
    D.set_prev = add_button(G, "setprev", {500, 548, 570, 588}, "<", [] {
        g_set_first = std::max(0, g_set_first - kSetRows);
        g_dirty = true;
    });
    D.set_pageno = add_label(G, {575, 554, 645, 582}, "", kDim, sui::kAlignCenter);
    D.set_next = add_button(G, "setnext", {650, 548, 720, 588}, ">", [] {
        g_set_first += kSetRows;
        g_dirty = true;
    });

    // ---- Space page ----
    auto& S = D.page_space;
    D.space_title = add_label(S, {40, 104, 720, 130}, T("Your galaxy together", "Галактика вместе"), kGold);
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
    D.space_back = add_button(S, "sback", {40, 548, 240, 588}, T("< Lobby", "< Лобби"), [] {
        g_space_page = false;
        g_dirty = true;
    });
    // more rows than fit: pages
    D.space_prev = add_button(S, "sprev", {500, 548, 570, 588}, "<", [] {
        g_space_first = std::max(0, g_space_first - kSpaceRows);
        g_dirty = true;
    });
    D.space_pageno = add_label(S, {575, 554, 645, 582}, "", kDim, sui::kAlignCenter);
    D.space_next = add_button(S, "snext", {650, 548, 720, 588}, ">", [] {
        g_space_first += kSpaceRows;
        g_dirty = true;
    });
    // Enter in a field does what its button does: the chat sends, the address joins, the name continues
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
    if (D.name_edit)
        sui::add_handler(D.name_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_name_ok();
                g_dirty = true;
                return true;
            }
            return false;
        });
    for (Win w : common)
        (void)w;
    sui::set_visible(root, false);
    g_cache[root] = Cache{"", 0, 0};
    H->log(M, SK_INFO, "ui: co-op dialog ready");
}

static void refresh_connect()
{
    bool scanning = A.role() == 0 && g_step == kStepJoin;
    text(D.me_line, std::string(T("Your name: ", "Ваше имя: ")) + A.name());
    auto games = A.lan_games ? A.lan_games() : std::vector<LanGame>{};
    // BUG-0183: only the first three answers were listed; with more hosts on the network (lab lanes,
    // a LAN party) the others could not be joined from the list. "More >" shows the next three.
    bool pages = g_lan_pages_fix && games.size() > 3;
    if (!pages || g_lan_first >= (int)games.size() || g_lan_first < 0)
        g_lan_first = 0;
    if (games.empty())
        text(D.lan_hdr, T("Games on your network: searching...", "Игры в вашей сети: поиск..."));
    else if (pages) {
        char b[160];
        snprintf(b, sizeof b, T("Games on your network: %d-%d of %d (click to join)", "Игры в сети: %d-%d из %d (нажмите, чтобы войти)"),
                 g_lan_first + 1, std::min(g_lan_first + 3, (int)games.size()), (int)games.size());
        text(D.lan_hdr, b);
    } else
        text(D.lan_hdr, T("Games on your network (click to join):", "Игры в вашей сети (нажмите, чтобы войти):"));
    show(D.lan_more, pages && scanning);
    for (int i = 0; i < 3; i++) {
        int k = g_lan_first + i;
        bool have = k < (int)games.size();
        if (have) {
            const auto& g = games[k];
            char b[256];
            snprintf(b, sizeof b, "%s  -  %s  -  %d/%d  -  %s", g.name.c_str(), g.info.c_str(), g.players,
                     g.max_players, g.address.c_str());
            text(D.lan_rows[i], b);
            D.lan_addr[i] = g.address;
        } else
            D.lan_addr[i].clear();
        show(D.lan_rows[i], have && scanning);
    }
    std::string hint;
    if (A.local_addresses) {
        auto ips = A.local_addresses();
        std::string port = sui::edit_text(D.port_edit);
        for (size_t i = 0; i < ips.size() && i < 2; i++) // (two fit the line)
            hint += (i ? ",  " : "") + ips[i] + ":" + port;
    }
    text(D.host_hint, hint.empty() ? "" : std::string(T("Friends connect to: ", "Друзья подключаются к: ")) + hint);
    std::string up_line, up_btn;
    bool up = A.update && A.update(up_line, up_btn);
    // the session status wins; otherwise the update line (a new SporeCoop release)
    text(D.status, g_status.empty() && up ? up_line : g_status);
    color(D.status, g_status.empty() && up ? kGreen : g_status_color);
    // the Update button belongs to the update line, not to a session message (BUG-0104); not on the name step
    show(D.update_btn, up && !up_btn.empty() && (!g_status_fix || g_status.empty()) && g_step != kStepName);
    text(D.update_btn, up_btn);
}

static void refresh_lobby()
{
    text(D.session, A.session_line());
    auto rows = A.players();
    for (int i = 0; i < 4; i++) {
        bool have = i < (int)rows.size();
        show(D.row_name[i], have);
        show(D.row_color[i], have);
        show(D.row_stage[i], have);
        show(D.row_ping[i], have);
        show(D.row_ready[i], have);
        if (!have)
            continue;
        const auto& p = rows[i];
        std::string n = p.name;
        if (p.host)
            n += p.me ? T(" (host, you)", " (хост, вы)") : T(" (host)", " (хост)");
        else if (p.me)
            n += T(" (you)", " (вы)");
        text(D.row_name[i], n);
        color(D.row_name[i], p.me ? kGold : kWhite);
        // the player's colour (F2 of 0.7.12 assigns it): a swatch in that colour, "-" while it has none
        uint32_t pc = p.color ? p.color : g_test_color ? g_test_color : 0;
        text(D.row_color[i], pc ? "\xE2\x96\xA0\xE2\x96\xA0\xE2\x96\xA0" : "-");
        color(D.row_color[i], pc ? (pc | 0xFF000000u) : kDim);
        text(D.row_stage[i], p.stage);
        text(D.row_ping[i], p.me ? "" : (p.ping_ms >= 0 ? std::to_string(p.ping_ms) + T(" ms", " мс") : "?"));
        text(D.row_ready[i], p.ready ? T("ready", "готов") : "-");
        color(D.row_ready[i], p.ready ? kGreen : kDim);
    }
    std::string wl = A.world_line ? A.world_line() : "";
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
    if (A.creation && A.creation(ck, cname, cown)) {
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
    bool giving = A.give && !gto.empty() && (cl.empty() || space);
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
    bool roles = giving && gn == 0 && A.civ_role && A.mode && A.mode() == 0x1654C04; // civ, nothing selected
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
    text(D.give_btn, space ? std::string(T("Give 10 to ", "Дать 10: ")) + gto : std::string(T("Give to ", "Отдать ")) + gto);
    std::string jto;
    bool jump = A.jump && A.jump(jto);
    show(D.jump_btn, jump);
    text(D.jump_btn, std::string(T("Jump to ", "К звезде: ")) + jto);
    bool can = (!cl.empty() && !giving && ck != 2 && cown > 0) || roles;
    show(D.creation_prev, can);
    show(D.creation_next, can);
    bool host = A.role() == 1;
    int offer = A.evolve_offer ? A.evolve_offer() : 0; // BUG-0516: the host evolves now, this guest may follow
    if (offer > 0)
        text(D.ready_btn, T("Evolve now", "Эволюция"));
    else
        text(D.ready_btn, A.ready() ? T("Not ready", "Не готов") : T("I'm ready", "Я готов"));
    show(D.share_btn, host);
    enable(D.share_btn, !share_why || !g_share_explain_fix);
    text(D.share_btn, A.vote_active && A.vote_active() ? T("Start now", "Начать сейчас") : T("Send my world", "Отправить мир"));
    text(D.leave_btn, host ? T("Close session", "Закрыть сессию") : T("Leave", "Выйти"));
    show(D.ask_btn, host);
    // the address to give to friends (the host) or the host's (a guest)
    std::string addr = A.host_address ? A.host_address() : "";
    text(D.address, addr.empty() ? ""
                    : std::string(host ? T("Friends connect to: ", "Друзья подключаются к: ") : T("Host address: ", "Адрес хоста: ")) +
                          addr);
    // the settings button says how many settings differ from the built-in defaults (F3: the lobby shows it)
    int changed = 0;
    for (int i = 0; i < settings::kCount; i++)
        changed += settings::get((settings::Id)i) != settings::kDefs[i].def;
    char sb[80];
    if (changed)
        snprintf(sb, sizeof sb, T("Settings (%d changed)", "Настройки (изменено: %d)"), changed);
    else
        snprintf(sb, sizeof sb, "%s", T("Settings", "Настройки"));
    text(D.settings_btn, sb);
}

// the settings page's entries: a group header (-1 - the index of its first setting) or a setting id. "All stages"
// first, then the groups in the order of their first row; the rows of a group in kDefs order.
static std::vector<int> settings_entries()
{
    std::vector<const char*> groups;
    for (int i = 0; i < settings::kCount; i++) {
        const char* g = settings::kDefs[i].group;
        if (std::none_of(groups.begin(), groups.end(), [&](const char* x) { return !strcmp(x, g); }))
            groups.push_back(g);
    }
    std::stable_partition(groups.begin(), groups.end(), [](const char* g) { return !strcmp(g, "All stages"); });
    std::vector<int> e;
    for (const char* g : groups) {
        bool head = false;
        for (int i = 0; i < settings::kCount; i++)
            if (!strcmp(settings::kDefs[i].group, g)) {
                if (!head)
                    e.push_back(-i - 1);
                head = true;
                e.push_back(i);
            }
    }
    return e;
}

// who may change setting `id` from this window now (settings_core.h can_set: a guest never, a session setting
// only before the start)
static settings::Refusal set_refusal(int id)
{
    return settings::can_set(id, A.role() != 2, settings::started());
}

static void do_set_click(int row)
{
    int id = row >= 0 && row < kSetRows ? D.set_id[row] : -1;
    if (id < 0)
        return;
    const auto& d = settings::kDefs[id];
    int v = settings::get((settings::Id)id);
    int nv = d.max == 1 && d.min == 0 ? !v : (v >= d.max ? d.min : v + 1);
    std::string why;
    auto r = set_refusal(id);
    if (r != settings::Refusal::None || !settings::set((settings::Id)id, nv, false, &why)) {
        g_set_line = r == settings::Refusal::Guest ? T("Only the host changes the settings", "Настройки меняет только хост")
                     : r == settings::Refusal::SessionStarted
                         ? T("Locked: a session setting changes only before the start", "Заблокировано: меняется только до начала")
                         : std::string(T("Not changed: ", "Не изменено: ")) + why;
        g_set_line_color = kRed;
    } else {
        std::string val = settings::value_text(id, nv);
        g_set_line = std::string(d.label) + ": " + (val == "on" ? T("on", "вкл") : val == "off" ? T("off", "выкл") : val);
        g_set_line_color = kGreen;
    }
    trace::line("ui", "setting %s -> %d (%s)", d.key, nv, why.c_str());
    g_dirty = true;
}

static void refresh_settings()
{
    auto e = settings_entries();
    int n = (int)e.size(), pages = std::max(1, (n + kSetRows - 1) / kSetRows);
    if (g_set_first >= n || g_set_first < 0)
        g_set_first = (pages - 1) * kSetRows;
    bool guest = A.role() == 2, started = settings::started();
    text(D.set_note, guest ? T("The host decides these settings: you see them here.",
                               "Эти настройки выбирает хост, здесь они видны вам.")
                     : started ? T("Live settings change now; the others are locked in play.",
                                   "Живые меняются сейчас, остальные заблокированы в игре.")
                               : T("Click to change. Live settings can change in play too.",
                                   "Нажмите, чтобы изменить. Живые можно менять и в игре."));
    for (int i = 0; i < kSetRows; i++) {
        int k = g_set_first + i;
        bool have = k < n;
        int id = have ? e[k] : -1;
        D.set_id[i] = id;
        show(D.set_label[i], have);
        show(D.set_btn[i], have && id >= 0);
        show(D.set_tag[i], have && id >= 0);
        if (!have)
            continue;
        auto& c = g_cache[D.set_label[i]]; // a group header at the left, its settings indented
        float x1 = id < 0 ? 40.f : 60.f;
        if (c.box.x1 != x1) {
            c.box.x1 = x1;
            fit(D.set_label[i], c);
        }
        if (id < 0) {
            text(D.set_label[i], settings::kDefs[-id - 1].group);
            color(D.set_label[i], kGold);
            continue;
        }
        const auto& d = settings::kDefs[id];
        int v = settings::get((settings::Id)id);
        bool differs = v != d.def;
        text(D.set_label[i], std::string(d.label) + (differs ? " *" : ""));
        color(D.set_label[i], differs ? kYellow : kWhite);
        std::string val = settings::value_text(id, v);
        text(D.set_btn[i], val == "on" ? T("On", "Вкл") : val == "off" ? T("Off", "Выкл") : val);
        auto r = set_refusal(id);
        enable(D.set_btn[i], r == settings::Refusal::None);
        bool live = d.scope == settings::Scope::Live;
        bool locked = !live && started; // (a guest too: the host's started flag)
        text(D.set_tag[i], locked                                   ? T("locked", "закрыта")
                           : live                                   ? T("live", "живая")
                                                                    : T("session", "сессия"));
        color(D.set_tag[i], locked ? kRed : live ? kGreen : kDim);
    }
    text(D.set_line, !g_set_line.empty() ? g_set_line
                     : T("* differs from the default. Live: the host can change it in play.",
                         "* не по умолчанию. Живая: хост меняет её и в игре."));
    color(D.set_line, !g_set_line.empty() ? g_set_line_color : kDim);
    show(D.set_prev, pages > 1);
    show(D.set_next, pages > 1);
    show(D.set_pageno, pages > 1);
    text(D.set_pageno, std::to_string(g_set_first / kSetRows + 1) + "/" + std::to_string(pages));
}

static void refresh_space()
{
    auto rows = spaceext::rows_all();
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

// ---------------- "Report a problem" in the game's pause menu (0.7.12 F1, docs/08-ui.md#ui-window-steps) ----
// The pause menu (Esc in a stage: "Options", window 0x064C23F8 inside 0x0615B50D, loaded once per stage and shown /
// hidden) is a column of buttons (Resume Game, Spore Guide, Spy Glass, Ban, Settings, Save, Exit to Main Menu, Quit
// Spore in Creature). One more button is added at its end: the column keeps its top and its bottom (the frame
// does not grow), the buttons get a smaller pitch. A copy of the "Spore Guide" button (its "?" icon). Done from
// refresh (outside the frame), mostly while the menu is still hidden; never a pointer kept: found anew each time.
constexpr uint32_t kIdPauseMenu = 0x064C23F8, kIdPauseReport = 0xC00F0002, kIdPauseGuide = 0x0447060A;
static bool g_pause_report_fix = true;
COOP_FIX("core.ui_pause_report", "BUG-0765", g_pause_report_fix, "ui: \"Report a problem\" in the game's pause menu");
static Win g_pause_mine; // the button this mod instance made (compared only, never used across refreshes)
static unsigned g_pause_clicks; // tests: clicks on it

static bool shown_chain(Win w)
{
    for (; w; w = sui::parent(w))
        if (!(sui::flags(w) & sui::kFlagVisible))
            return false;
    return true;
}

static Win pause_menu() { return in_menu() ? nullptr : sui::find(sui::main_window(), kIdPauseMenu); }

static void pause_menu_tick()
{
    Win pm = pause_menu();
    if (!pm)
        return;
    std::vector<Win> btns;
    Win mine = nullptr;
    for (Win c : sui::children(pm)) {
        if (sui::control_id(c) == kIdPauseReport) {
            mine = c;
            continue;
        }
        if ((sui::flags(c) & sui::kFlagVisible) && sui::as_button(c) && !sui::caption(c).empty())
            btns.push_back(c); // (the close cross has no caption)
    }
    if (btns.size() < 2)
        return;
    // The game's buttons are anchored (their area is the layout input, relative to the menu's bottom / right edge;
    // real_area is where they are): they are placed in their own area space, ours (no anchors) at the real place,
    // the anchors' offset taken from the first button before anything moves.
    std::sort(btns.begin(), btns.end(), [](Win a, Win b) { return sui::real_area(a).y1 < sui::real_area(b).y1; });
    Rect first = sui::area(btns.front()), last = sui::area(btns.back()), fr = sui::real_area(btns.front());
    Rect off{fr.x1 - first.x1, fr.y1 - first.y1, fr.x2 - first.x2, fr.y2 - first.y2};
    float bottom = last.y2;
    if (mine) {
        bottom = std::max(bottom, sui::area(mine).y2 - off.y2); // (ours: area = real)
        if (mine != g_pause_mine || !g_pause_report_fix) { // a button of an earlier mod instance (hot reload): dead handler
            sui::remove_child(pm, mine);
            mine = nullptr;
        }
    }
    int n = (int)btns.size() + (g_pause_report_fix ? 1 : 0);
    float pitch = (bottom - first.y1 + 3) / n, h = pitch - 3;
    auto target = [&](int i) { return Rect{first.x1, first.y1 + i * pitch, first.x2, first.y1 + i * pitch + h}; };
    auto place = [&](Win w, Rect r) {
        Rect a = sui::area(w);
        if (std::abs(a.y1 - r.y1) > 0.5f || std::abs(a.y2 - r.y2) > 0.5f || std::abs(a.x1 - r.x1) > 0.5f ||
            std::abs(a.x2 - r.x2) > 0.5f)
            sui::set_area(w, r);
    };
    for (int i = 0; i < (int)btns.size(); i++)
        place(btns[i], target(i));
    if (!g_pause_report_fix)
        return;
    Rect t = target(n - 1);
    Rect r{t.x1 + off.x1, t.y1 + off.y1, t.x2 + off.x2, t.y2 + off.y2}; // ours: the real place
    if (mine) {
        place(mine, r);
        return;
    }
    Win like = nullptr;
    for (Win b : btns)
        if (sui::control_id(b) == kIdPauseGuide)
            like = b;
    if (!like)
        like = btns.front();
    Win b = sui::button_like(nullptr, like, r, T("Report a problem", "Сообщить об ошибке"));
    if (!b)
        return;
    sui::set_ids(b, kIdPauseReport, kIdPauseReport);
    sui::add_handler(b, [](Win, const sui::Msg& m) {
        if (sui::is_click(m)) {
            g_pause_clicks++;
            trace::line("ui", "click pause_report");
            H->log(M, SK_INFO, "ui: \"Report a problem\" in the pause menu");
            do_report();
            return true;
        }
        return false;
    });
    sui::add_child(pm, b);
    g_pause_mine = b;
    H->log(M, SK_INFO, "ui: \"Report a problem\" added to the pause menu (%d buttons, pitch %.1f)", n, pitch);
}

static void refresh()
{
    g_pending = false;
    if (!g_ok)
        return;
    ensure_menu_button();
    pause_menu_tick();
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
    int role = A.role();
    bool lobby = role != 0;
    bool space = lobby && A.mode && A.mode() == modes::kSpace;
    if (!space)
        g_space_page = false;
    if (!lobby)
        g_settings_page = false;
    // a session leaves the window on the step of its role (the next session of a guest is a join again)
    if (role == 2)
        g_step = kStepJoin;
    else if (role == 1)
        g_step = kStepHost;
    bool sett = lobby && g_settings_page && !g_space_page;
    for (Win w : D.page_name)
        show(w, !lobby && g_step == kStepName);
    for (Win w : D.page_choose)
        show(w, !lobby && g_step == kStepChoose);
    for (Win w : D.page_host)
        show(w, !lobby && g_step == kStepHost);
    for (Win w : D.page_join)
        show(w, !lobby && g_step == kStepJoin);
    show(D.back_btn, !lobby && (g_step == kStepHost || g_step == kStepJoin));
    show(D.status, !lobby);
    if (lobby)
        show(D.update_btn, false);
    for (Win w : D.page_lobby)
        show(w, lobby && !g_space_page && !sett);
    for (Win w : D.page_settings)
        show(w, sett);
    for (Win w : D.page_space)
        show(w, lobby && g_space_page);
    show(D.space_btn, space && !g_space_page && !sett);
    if (!lobby || g_space_page || sett) {
        show(D.chat_up, false);
        show(D.chat_down, false);
    }
    text(D.subtitle, lobby && g_space_page ? T("CO-OP: SPACE", "КООПЕРАТИВ: КОСМОС")
                     : sett                ? T("CO-OP: SETTINGS", "КООПЕРАТИВ: НАСТРОЙКИ")
                     : lobby               ? T("CO-OP LOBBY", "ЛОББИ КООПЕРАТИВА")
                                           : T("CO-OP", "КООПЕРАТИВ"));
    if (A.version_line)
        text(D.version, A.version_line());
    if (A.lan_scan)
        A.lan_scan(!lobby);
    if (lobby && g_space_page)
        refresh_space();
    else if (sett)
        refresh_settings();
    else if (lobby)
        refresh_lobby();
    else
        refresh_connect();
}

static void refresh_task(void*) { refresh(); }

void init(const SkHost* host, SkMod* mod, Api api)
{
    H = host;
    M = mod;
    A = std::move(api);
    g_ok = sui::init(H, M);
    std::string lang = load_str("ui", "lang", "auto");
    g_ru = lang == "ru" || (lang == "auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
    if (g_ok)
        sui::register_commands();
    // BUG-0765: the first start (no name chosen yet: coop.ini still has the default name) begins with the name
    bool chosen = load_str("ui", "name_chosen", "0") == "1";
    g_step = g_name_first_fix && !chosen && (A.name().empty() || A.name() == "Player") ? kStepName : kStepChoose;
}

void first_start()
{
    g_step = kStepName;
    g_settings_page = g_space_page = false;
    if (D.root)
        sui::set_edit_text(D.name_edit, A.name());
    set_status("", kYellow);
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
    // F9: show/hide the co-op window anywhere in the game. BUG-0713: a press the game window took (mouse.h), not
    // the physical key: F9 in another program toggled the window (input.window off: the old polling)
    mouse::install((HWND)H->game_window());
    static uint32_t s_f9_seen = mouse::key_presses(VK_F9);
    bool f9_press = mouse::key_pressed(VK_F9, s_f9_seen);
    bool f9 = mouse::g_window_input ? f9_press : (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9 && (mouse::g_window_input || !g_f9_down))
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
    double period = g_want_open ? 0.25 : 1.0;
    if (!g_pending && (g_dirty || now - g_last_refresh >= period)) {
        g_pending = true;
        g_dirty = false;
        g_last_refresh = now;
        H->run_outside_frame(refresh_task, nullptr);
    }
}

void shutdown()
{
    if (D.root)
        sui::set_visible(D.root, false);
    if (g_ok)
        if (Win pm = pause_menu()) // the next mod instance makes its own (this one's handler dies with the DLL)
            if (Win mine = sui::find(pm, kIdPauseReport))
                sui::set_visible(mine, false);
    if (g_menu_btn && (!g_menu_drop_fix || in_menu()))
        sui::set_visible(g_menu_btn, false);
}

std::string debug_state()
{
    static const char* const steps[] = {"name", "choose", "host", "join"};
    std::string s = "open=" + std::to_string(g_want_open) + " page=" +
                    (A.role() ? (g_space_page ? "space" : g_settings_page ? "settings" : "lobby") : "connect") +
                    " step=" + steps[g_step & 3] +
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
        std::string cap = name == "name" || name == "address" || name == "port" || name == "chatedit"
                              ? sui::edit_text(w) : sui::caption(w);
        snprintf(b, sizeof b, "%s %.0f %.0f %.0f %.0f %d '%s'\n", name.c_str(), r.x1, r.y1, r.x2, r.y2, (int)vis,
                 cap.c_str());
        s += b;
    }
    for (int i = 0; i < 4; i++)
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
    if (D.address)
        s += "hostaddr '" + sui::caption(D.address) + "'\n";
    for (int i = 0; i < kSetRows; i++) // the settings rows on the page now: key, value, editable, tag
        if (D.set_id[i] >= 0 && D.set_btn[i] && sui::visible(D.set_btn[i]))
            s += "setting " + std::to_string(i) + " " + settings::kDefs[D.set_id[i]].key + " " +
                 std::to_string(settings::get((settings::Id)D.set_id[i])) + " " +
                 ((sui::flags(D.set_btn[i]) & sui::kFlagEnabled) ? "1" : "0") + " '" + sui::caption(D.set_tag[i]) + "'\n";
    if (D.set_line)
        s += "setline '" + sui::caption(D.set_line) + "'\n";
    if (Win pm = g_ok ? pause_menu() : nullptr) {
        if (Win mine = sui::find(pm, kIdPauseReport)) {
            Rect r = sui::screen_rect(mine);
            snprintf(b, sizeof b, "pausereport %.0f %.0f %.0f %.0f %d '%s' clicks=%u\n", r.x1, r.y1, r.x2, r.y2,
                     (int)shown_chain(mine), sui::caption(mine).c_str(), g_pause_clicks);
            s += b;
        }
        for (Win c : sui::children(pm)) // the game's own buttons (tests: no overlap with ours)
            if (sui::control_id(c) != kIdPauseReport && sui::as_button(c) && !sui::caption(c).empty() &&
                (sui::flags(c) & sui::kFlagVisible)) {
                Rect r = sui::screen_rect(c);
                snprintf(b, sizeof b, "pausebtn %.0f %.0f %.0f %.0f '%s'\n", r.x1, r.y1, r.x2, r.y2, sui::caption(c).c_str());
                s += b;
            }
    }
    return s;
}

uint32_t text_font() { return g_text_font; }

bool russian() { return g_ru; }

void set_russian(bool ru)
{
    g_ru = ru;
    g_dirty = true;
}

void set_test_color(uint32_t argb)
{
    g_test_color = argb;
    g_dirty = true;
}

} // namespace coop_ui
