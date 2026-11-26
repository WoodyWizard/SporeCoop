#include "coop_ui.h"
#include "diag_coop.h"
#include "trace.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
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
#include "history.h"
#include "history_logic.h"
#include "i18n.h"
#include "rivals.h"
#include "settings.h"
#include "sui.h"
#include "typed_text.h"
#include "world.h"
#include "fixes.h"
#include "../../sporekit/display_logic.h"

namespace coop_ui {

static uint32_t g_text_font;

using sui::Rect;
using sui::Win;

static const SkHost* H;
static SkMod* M;
static Api A;
static bool g_ok;      // Spore UI available
static bool g_ru;      // Russian captions (kept in sync with i18n::g_lang == Lang::Ru)
static uint32_t g_lang_rev = 0, g_lang_applied_rev = 0; // BUG-1575: live UI refresh on language change
// BUG-1572: mask host/peer IP addresses during vote auto-open and when streamer mode (Hide IP) is enabled
static bool g_hide_ip_fix = true;
COOP_FIX("ui.hide_ip", "BUG-1572", g_hide_ip_fix,
         "ui: mask host/peer IP addresses during vote auto-open and when streamer mode (Hide IP) is enabled");
static bool g_hide_ip = false;
static bool g_vote_auto_open = false;
static bool g_want_open, g_pending, g_dirty = true;
static double g_last_refresh;
static std::string g_status;
static uint32_t g_status_color = 0xFFFFD866;
static double g_status_until; // the status goes away then (0 = stays until the next session event)
bool g_status_fix = true;     // BUG-0104 (`coop.fix core.ui_status off`: the status of the last button stays)
COOP_FIX("core.ui_status", "BUG-0104", g_status_fix, "ui: the co-op window shows the session's status, not the last button's");
// BUG-0597: "Send my world" pressed in the main menu (no world loaded) or with nobody joined did nothing while
// the window said "Sending the world to everyone...": players read it as "solo does not start". The button is
// greyed out then and the lobby says why (a host can start or load a game alone, friends join any time and get
// the world by themselves).
// BUG-0598: a guest without own creations of the stage's kind saw "My cell: the world's own" with no arrows and
// no hint, and could not tell that a choice exists (a player asked where to pick the own creation). The row says
// whose it is ("the host's species") and how to get a choice (make one in an editor).
// BUG-1062: "Give 10 to <player>" (Space) and "Give to <player>" kept a fixed width (150 / 170 px): a caption wraps
// when it needs more than the width minus ~30 px (docs/08-ui.md#ui-text-width), so "Give 10 to Carol" showed on two
// lines cut by the button's frame. Now the give button (and in Space the jump button left of it) is as wide as its
// caption needs, growing to the left. Off: the fixed widths (0.7.13).
static bool g_give_fit_fix = true;
COOP_FIX("core.ui_give_fit", "BUG-1062", g_give_fit_fix, "ui: the give and jump buttons are as wide as their captions");
// UIFX-8 (BUG-1518): BUG-1062's fit had no lower bound: a long partner name (24 wide letters) made the give button
// start left of the creation label's text, and a long star name pushed the jump button toward the window's left edge.
// On: the give button starts at kGiveMinX1 at the most, the jump button at kJumpMinX1, and a caption too wide for that
// is cut with "..." (the name part). Off (`coop.fix core.ui_give_clamp off`): 0.7.16.1
static bool g_give_clamp_fix = true;
COOP_FIX("core.ui_give_clamp", "BUG-1518", g_give_clamp_fix,
         "ui: the give and jump buttons keep clear of the creation label with long names (the name is cut with ...)");
constexpr float kGiveMinX1 = 400.f, kJumpMinX1 = 240.f;
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
constexpr int kRivalsRows = 12;
static bool g_space_page;  // the lobby shows the Space page instead
static bool g_rivals_page; // the lobby shows the Rivals / Race page instead
static int g_lan_first;    // the first game on the network listed (pages of three, BUG-0183)
static int g_rec_first;    // BUG-0867: the first recent server listed (pages of three)
static bool g_host_pw_show, g_join_pw_show; // BUG-0867: password mask toggle on Host / Join steps
bool g_lan_pages_fix = true; // `coop.fix core.lan_pages off`: only the first three games, no "More" (BUG-0183)
COOP_FIX("core.lan_pages", "BUG-0183", g_lan_pages_fix, "ui: every network game is listed, with More pages");
// BUG-0874: native Spore-style tooltips on co-op window buttons after 0.8 s mouse rest
static bool g_tooltips_fix = true;
COOP_FIX("ui.tooltips", "BUG-0874", g_tooltips_fix, "ui: show Spore-style hover tooltips on co-op window buttons after 0.8 s rest");
static constexpr double kTooltipDelay = 0.8;
static int g_tip_mx = -99999, g_tip_my = -99999;
static double g_tip_rest_since = 0;
static std::string g_tip_hover_btn, g_tip_shown_btn, g_tip_shown_text;
static bool g_tip_clicked = false;
static int g_space_first;  // the first row shown (pages of kSpaceRows rows)
static int g_rivals_first; // the first row shown on the Rivals / Race page

static const char* T(const char* en, const char* ru) { return i18n::tr(en, ru); }

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

// BUG-0904, BUG-0905, BUG-0908: labels center their text in their box (docs/08-ui.md), so a left-aligned label's box
// is fitted to the text's width; with one average glyph width the rows of a column started up to ~8 px apart, a
// caption overflowed its button and the lobby's Stage column touched the colour button. On: the advance of each glyph
// (each glyph's advance measured in game, docs/08-ui.md#ui-text-width), the short settings caption and a
// wider colour column. Off: the 0.7.12 pre-release layout.
static bool g_layout_fix = true;
COOP_FIX("core.ui_layout", "BUG-0905", g_layout_fix, "ui: labels fitted by the glyphs' widths, short Settings caption, room for the colour button");

// width of a text in the dialog font (+30: the box's margins), measured at 1080p
static float text_width(const std::string& t)
{
    if (!g_layout_fix) { // one average glyph width
        int n = 0;
        for (unsigned char ch : t)
            n += (ch & 0xC0) != 0x80;
        return n * 9.3f + 30;
    }
    // the advance of each glyph of the dialog font in pixels at 1440x1080 (measured: 12 against 4 copies of each
    // glyph in a label, docs/08-ui.md#ui-text-width); ASCII 32..126, then Cyrillic U+0410..U+044F
    static const unsigned char kAscii[95] = {
        6, 7, 7, 11, 11, 13, 13, 4, 7, 7, 8, 11, 7, 7, 7, 7, 11, 11, 11,
        11, 11, 11, 11, 11, 11, 11, 7, 7, 11, 11, 11, 8, 16, 12, 11, 12, 12, 11,
        11, 13, 13, 5, 10, 12, 11, 14, 13, 13, 11, 13, 12, 10, 12, 13, 12, 17, 11,
        13, 11, 7, 7, 7, 11, 11, 7, 10, 11, 10, 11, 11, 7, 10, 11, 6, 7, 10,
        6, 16, 11, 11, 11, 11, 8, 8, 8, 11, 11, 16, 11, 10, 10, 7, 5, 7, 11};
    static const unsigned char kCyr[64] = {
        12, 11, 11, 10, 14, 11, 18, 11, 13, 14, 12, 13, 15, 13, 13, 13, 11, 12, 12, 12, 15, 12, 13, 12,
        18, 18, 15, 16, 12, 12, 18, 12, 10, 11, 10, 9, 12, 11, 15, 9, 11, 11, 10, 11, 14, 11, 11, 11,
        11, 10, 9, 10, 15, 11, 11, 11, 15, 16, 12, 14, 10, 10, 15, 11};
    float w = 0;
    for (size_t i = 0; i < t.size();) {
        unsigned char ch = (unsigned char)t[i];
        uint32_t cp = ch;
        int n = ch < 0x80 ? 1 : (ch >> 5) == 6 ? 2 : (ch >> 4) == 14 ? 3 : 4;
        if (n > 1) {
            cp = ch & (0x3F >> (n - 1));
            for (int k = 1; k < n && i + k < t.size(); k++)
                cp = (cp << 6) | ((unsigned char)t[i + k] & 0x3F);
        }
        i += n;
        if (cp >= 32 && cp < 127)
            w += kAscii[cp - 32];
        else if (cp >= 0x410 && cp < 0x450)
            w += kCyr[cp - 0x410];
        else if (cp == 0x401 || cp == 0x451) // Ё ё
            w += 11;
        else if (cp >= 0x2E80) // CJK ideographs / punctuation (BUG-1575)
            w += 16;
        else
            w += 11;
    }
    return w + 30;
}
float caption_width(const std::string& t) { return text_width(t) - 30; }

// BUG-1518: prefix + name, the name cut by whole UTF-8 code points and "..." added until the caption needs at most
// `room` px (its button's text borders included: +48)
static std::string cut_caption(const std::string& prefix, std::string name, float room)
{
    if (std::round(caption_width(prefix + name)) + 48 <= room)
        return prefix + name;
    while (!name.empty()) {
        size_t n = name.size() - 1;
        while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80)
            n--; // the start of the last code point
        name.resize(n);
        if (std::round(caption_width(prefix + name + "...")) + 48 <= room)
            break;
    }
    return prefix + name + "...";
}

// README pictures (coop.uistate demo <address> <real port> <shown port>): the window shows a documentation address
// instead of the PC's own and the default port instead of a test lane's; nothing else changes
static std::string g_demo_addr, g_demo_from, g_demo_to;

std::string demo_text(std::string t)
{
    if (g_demo_from.empty() || g_demo_from == g_demo_to)
        return t;
    for (const char* pre : {"port ", ":", "порт "}) {
        std::string a = pre + g_demo_from, b = pre + g_demo_to;
        for (size_t at = t.find(a); at != std::string::npos; at = t.find(a, at + b.size()))
            if (at + a.size() == t.size() || !isdigit((unsigned char)t[at + a.size()]))
                t.replace(at, a.size(), b);
    }
    return t;
}

static void fit(Win w, const Cache& c)
{
    Rect r = c.box;
    r.x2 = std::min(r.x2, r.x1 + text_width(c.text));
    sui::set_area(w, r);
}

static void text(Win w, const std::string& t0)
{
    if (!w)
        return;
    const std::string t = demo_text(t0);
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
// `coop.fix core.menu_drop off` = the old behaviour (tests).
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
        else if (sui::g_release_fix)
            sui::release(icon); // BUG-1025: GetImage's reference is ours; the drawable's slots took their own
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
                               sui::kAlignLeft)) {
            sui::set_ids(l, kIdMenuButton + 1, 0);
            if (sui::g_release_fix)
                sui::release(l); // BUG-1025: the menu holds it, the mod keeps no pointer (found by its id next time)
        }
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
    Win host_pw_lbl = nullptr, host_pw_edit = nullptr, host_pw_eye = nullptr; // BUG-0867
    Win join_pw_lbl = nullptr, join_pw_edit = nullptr, join_pw_eye = nullptr; // BUG-0867
    Win lan_hdr = nullptr, host_hint = nullptr, status = nullptr, update_btn = nullptr;
    Win lan_rows[3] = {};
    std::string lan_addr[3];
    bool lan_locked[3] = {}; // BUG-0867
    Win lan_more = nullptr; // BUG-0183: the next three games on the network
    Win rec_hdr = nullptr, rec_more = nullptr; // BUG-0867: persisted recent servers list
    Win rec_rows[3] = {};
    std::string rec_addr[3];
    Win tip_win = nullptr;       // BUG-0874: native Spore-style hover tooltip overlay
    void* tip_drawable = nullptr;
    // lobby page
    std::vector<Win> page_lobby;
    Win session = nullptr, address = nullptr, world = nullptr, chat_edit = nullptr, send_btn = nullptr;
    Win ready_btn = nullptr, share_btn = nullptr, leave_btn = nullptr, ask_btn = nullptr, settings_btn = nullptr;
    Win rask = nullptr, rsend = nullptr, rnosend = nullptr; // a guest asked for its report (F4, BUG-0767)
    Win rline = nullptr;                                     // the host: the answers while a round runs
    Win row_name[4] = {}, row_color[4] = {}, row_stage[4] = {}, row_ping[4] = {}, row_ready[4] = {};
    Win color_btn = nullptr; // F2: ">" next to the own colour: the next colour
    Win stage_hdr = nullptr;  // the Stage column's header (moved with the column, BUG-0908)
    Win chat_lines[5] = {};
    Win chat_up = nullptr, chat_down = nullptr;
    Win creation = nullptr, creation_prev = nullptr, creation_next = nullptr, give_btn = nullptr, give_next = nullptr, jump_btn = nullptr;
    Win creation_up = nullptr, cup_prev = nullptr, cup_next = nullptr; // BUG-1403: the creature row above the role row
    Win space_btn = nullptr;   // lobby -> the Space page (Space stage only)
    Win rivals_btn = nullptr;  // lobby -> the Rivals / Race page (WP-R1, BUG-0794)
    Win vote_mode_btn = nullptr; // stage transition vote: next stage Together/Rivals toggle (host)
    Win lineage_btn = nullptr; // BUG-1477: a guest's consent box "my old cells as wild species" (Cell, setting on)
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
    // Rivals / Race page: stage modes, relations, influence, race progress, draft, chronicle (rivals.h, BUG-0794)
    std::vector<Win> page_rivals;
    Win rivals_title = nullptr, rivals_back = nullptr, rivals_prev = nullptr, rivals_next = nullptr, rivals_pageno = nullptr;
    Win rivals_text[kRivalsRows] = {}, rivals_button[kRivalsRows] = {};
    std::function<void()> rivals_action[kRivalsRows];
};
static Dialog D;
static std::map<std::string, Win> g_named; // for tests (coop.uistate)
static bool g_row_creation; // BUG-1289: the row shows the creature choice (in Civ too, where the arrows pick a role)

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
// under it, unless the game delivered it itself within kLockedClickWait.
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
// out/lab/20261103-224540). The locked paths (buttons, text lines) take any such box too.
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
    if (game_modal()) {
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
    if (!clicked || !mouse::g_fix || !g_want_open || !D.root || !sui::visible(D.root) ||
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
static void do_send_chat();
static void do_host();
static void do_join(const std::string& address);
COOP_FIX("core.ui_locked_edit_utf16", "BUG-1027", typed_text::g_utf16_fix, "ui: a line typed under the input lock counts characters (not UTF-8 bytes) against its limit, drops DEL, keeps emoji whole");
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
    t = typed_text::apply(t, q, max_len, enter, g_locked_typed); // BUG-1027: UTF-16 units against the limit
    sui::set_edit_text(w, t);
    g_locked_text = sui::edit_text(w);
    if (enter && w == D.chat_edit) {
        do_send_chat();
        g_locked_text = sui::edit_text(w);
    } else if (enter && w == D.host_pw_edit) {
        do_host();
    } else if (enter && w == D.join_pw_edit) {
        do_join(sui::edit_text(D.addr_edit));
    }
}

// every frame (main thread): a click under the lock on a line starts the mod's typing, any other click ends it
// BUG-0551: a chat line typed while the host was paused saving the world for a returning guest was lost
// (play_creature, out/lab/20261104-135838): the mod took the typing (the "game saved" box is a modal window)
// and two things ended it: the mod's own click on the box's OK button (world.cpp box_task) counted as "a click
// anywhere else", and a frame held by the save took the click and the keys after it at once, and the keys of
// the frame of a click were dropped. The mod's own click no longer ends the typing, and the keys that came
// after the click in its frame go into the line.

static void locked_edit_tick(bool clicked)
{
    std::u16string after;
    std::u16string chars = mouse::take_chars(&after);
    if (clicked && g_locked_edit && mouse::last_click_own())
        clicked = false; // the mod confirmed the game's box: the player goes on typing
    if (clicked) {
        g_locked_edit = nullptr;
        if (mouse::g_fix && g_want_open && D.root && sui::visible(D.root) && clicks_locked()) {
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
        if (!g_locked_edit || after.empty())
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

static std::string g_join_line; // tests: the status line of the last join (a failed name lookup replaces it at once)

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
    if (net::g_fix.session_password && A.set_password && D.join_pw_edit)
        A.set_password(sui::edit_text(D.join_pw_edit));
    std::string err;
    if (!A.join(addr, err)) {
        set_status(std::string(T("Cannot join: ", "Не удалось подключиться: ")) + err, kRed, g_status_fix ? 30 : 0);
        return;
    }
    save_str("ui", "last_address", addr);
    // a name or an address without a port: the session uses the default port (net::kDefaultPort)
    bool port = addr.find(':') != std::string::npos;
    g_join_line = std::string(T("Connecting to ", "Подключение к ")) + addr +
                  (port ? "" : ":" + std::to_string(net::kDefaultPort)) + "...";
    set_status(g_join_line, kYellow);
}

static void do_host()
{
    int port = atoi(sui::edit_text(D.port_edit).c_str());
    if (port <= 0 || port > 65535)
        port = A.default_port;
    if (net::g_fix.session_password && A.set_password && D.host_pw_edit)
        A.set_password(sui::edit_text(D.host_pw_edit));
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

// BUG-1528 (the tearing report, out/research/dc-report-tearing-20261116.md): "Report a problem" opened Explorer with the
// archive selected; in exclusive full screen Explorer took the foreground and the game minimized itself (its own focus
// pause): the report's own log then showed a 20-30 s "freeze", and the player had to Alt+Tab back. On: while the game
// window fills its monitor (exclusive or borderless full screen) the folder is not opened; the status names it. Off
// (`coop.fix core.report_no_explorer off`): Explorer opens in every mode (0.7.16.1). tests: `coop.uistate fullscreen
// 1|0|auto` forces the answer (the lab plays windowed)
static bool g_report_fs_fix = true;
COOP_FIX("core.report_no_explorer", "BUG-1528", g_report_fs_fix,
         "ui: Report a problem does not open Explorer while the game fills the screen (it minimized the game)");

// BUG-1541: hide the co-op lobby window for one clean rendered frame when taking a Report a problem or /bug
// screenshot so the dialog does not obscure the center of the captured game scene.
static bool g_shot_hide_fix = true;
COOP_FIX("ui.report_shot_hide", "BUG-1541", g_shot_hide_fix,
         "ui: temporarily hide the co-op lobby window for one frame when taking a Report a problem or /bug screenshot");
static bool g_shot_hide;
static bool g_shot_hidden_applied;
static std::string g_shot_tag;
static std::string g_shot_caption;
static int g_shot_wait_frames;
static int g_shot_restore_frames;
static double g_shot_hide_until;

bool defer_report_shot(const char* tag, const std::string& caption)
{
    if (!g_shot_hide_fix || !g_ok || !g_want_open || !D.root || !sui::visible(D.root))
        return false;
    if (!tag || !*tag)
        return false;
    if (!g_shot_tag.empty())
        return true;
    g_shot_tag = tag;
    g_shot_caption = caption;
    g_shot_hide = true;
    g_shot_hidden_applied = false;
    g_shot_wait_frames = 1;
    g_shot_restore_frames = 0;
    g_shot_hide_until = H->time_seconds() + 1.5;
    g_dirty = true;
    return true;
}

bool report_shot_deferred() { return !g_shot_tag.empty(); }

static void shot_hide_tick(double now)
{
    if (!g_shot_hide)
        return;
    if (!g_shot_tag.empty()) {
        if (!g_shot_hidden_applied) {
            g_dirty = true;
            if (now >= g_shot_hide_until) {
                // Fallback if refresh could not run (never leave a report shot stuck)
                std::string tag = std::move(g_shot_tag);
                std::string cap = std::move(g_shot_caption);
                g_shot_tag.clear();
                g_shot_caption.clear();
                g_shot_hide = false;
                support::report_shot_now(tag.c_str(), cap);
                g_dirty = true;
            }
            return;
        }
        if (g_shot_wait_frames > 0) {
            g_shot_wait_frames--;
            return;
        }
        // A full frame has now rendered with D.root hidden, and we are inside Present before report_shot_on_present().
        std::string tag = std::move(g_shot_tag);
        std::string cap = std::move(g_shot_caption);
        g_shot_tag.clear();
        g_shot_caption.clear();
        int r = support::report_shot_now(tag.c_str(), cap);
        if (r > 0) {
            g_shot_restore_frames = 1;
        } else {
            g_shot_hide = false;
            g_shot_hidden_applied = false;
            g_dirty = true;
        }
        return;
    }
    if (g_shot_restore_frames > 0) {
        g_shot_restore_frames--;
        if (g_shot_restore_frames == 0) {
            g_shot_hide = false;
            g_shot_hidden_applied = false;
            g_dirty = true;
        }
        return;
    }
    if (now >= g_shot_hide_until) {
        g_shot_hide = false;
        g_shot_hidden_applied = false;
        g_dirty = true;
    }
}
static int g_test_fullscreen = -1; // tests: 1 / 0 forces the answer, -1 the game window decides
static bool game_fills_screen()
{
    if (g_test_fullscreen >= 0)
        return g_test_fullscreen == 1;
    HWND w = H->game_window ? (HWND)H->game_window() : nullptr;
    if (!w || IsIconic(w))
        return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    RECT r;
    if (!GetWindowRect(w, &r) || !GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    bool caption = (GetWindowLongW(w, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
    return !caption && r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right &&
           r.bottom >= mi.rcMonitor.bottom;
}
void set_test_fullscreen(int v) { g_test_fullscreen = v; }

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
    bool round = A.report_round && A.report_round(); // F4: the host's report asked every guest for its report too
    set_status(std::string(T("Report saved: ", "Отчёт сохранён: ")) + p.substr(p.find_last_of("\\/") + 1) +
                   (round ? T(" - asking the players for theirs: the session report follows",
                              " - игроков попросили прислать свои: будет общий отчёт")
                          : T(" - attach it to an issue on GitHub; the other players should send theirs too",
                              " - приложите его к issue на GitHub; другие игроки пусть пришлют свои")) +
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
    if (g_report_fs_fix && game_fills_screen()) { // BUG-1528: Explorer would minimize the game
        H->log(M, SK_INFO, "ui: report: the folder is not opened: the game fills the screen (BUG-1528)");
        set_status(std::string(T("Report saved in SporebinEP1\\mods\\logs: ", "Отчёт сохранён в SporebinEP1\\mods\\logs: ")) +
                       p.substr(p.find_last_of("\\/") + 1) +
                       T(" (the folder is not opened while the game is in full screen)",
                         " (папка не открывается, пока игра во весь экран)"),
                   kGreen);
        return;
    }
    H->log(M, SK_INFO, "ui: report: the folder opens in Explorer");
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
    if (A.role() != 1 || (A.vote_active && A.vote_active()) || !A.share_state)
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
static std::string g_report_seen; // the last session report line put into the status (F4)

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
    D.host_pw_lbl = add_label(HP, {40, 216, 225, 244}, T("Password", "Пароль"), kWhite);
    D.host_pw_edit = add_edit(HP, "hostpw", {230, 212, 520, 244}, "", 64);
    if (D.host_pw_edit)
        sui::set_edit_password(D.host_pw_edit, !g_host_pw_show);
    D.host_pw_eye = add_button(HP, "hostpweye", {540, 210, 720, 246}, T("Show", "Показать"), [] {
        g_host_pw_show = !g_host_pw_show;
        if (D.host_pw_edit)
            sui::set_edit_password(D.host_pw_edit, !g_host_pw_show);
        g_dirty = true;
    });
    D.host_btn = add_button(HP, "host", {540, 258, 720, 298}, T("Host", "Создать"), do_host);
    add_label(HP, {40, 314, 720, 338}, T("Friends on your network find your game in their list.",
                                         "Друзья в вашей сети увидят игру в списке."),
              kDim);
    add_label(HP, {40, 340, 720, 364}, T("Over the internet: forward the UDP port on your router,",
                                         "Через интернет: откройте UDP-порт на роутере"),
              kDim);
    add_label(HP, {40, 366, 720, 390}, T("or play over a VPN (ZeroTier, Tailscale, Radmin VPN).",
                                         "или играйте через VPN (ZeroTier, Tailscale)."),
              kDim);
    D.host_hint = add_label(HP, {40, 406, 720, 432}, "", kWhite);

    // ---- step 3b: Join ----
    auto& J = D.page_join;
    add_label(J, {200, 108, 720, 134}, T("Join a game", "Подключиться к игре"), kGold);
    D.lan_hdr = add_label(J, {40, 144, 590, 168}, "", kDim);
    D.lan_more = add_button(J, "lanmore", {600, 140, 720, 170}, T("More >", "Ещё >"), [] {
        g_lan_first += 3; // refresh_connect wraps it to the first page
        g_dirty = true;
    });
    for (int i = 0; i < 3; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "lan%d", i);
        D.lan_rows[i] = add_button(J, nm, {40, 174.f + i * 36, 720, 206.f + i * 36}, "", [i] {
            if (!D.lan_addr[i].empty()) {
                sui::set_edit_text(D.addr_edit, D.lan_addr[i]);
                if (D.lan_locked[i] && net::g_fix.session_password && D.join_pw_edit &&
                    sui::edit_text(D.join_pw_edit).empty()) {
                    set_status(T("This server requires a password - enter it below and click Join",
                                 "Для входа нужен пароль - введите его ниже и нажмите Подключиться"),
                               kYellow, 15);
                    return;
                }
                do_join(D.lan_addr[i]);
            }
        });
    }
    add_label(J, {40, 286, 720, 310}, T("Or type the host's address", "Или введите адрес хоста"), kGold);
    add_label(J, {40, 320, 225, 348}, T("Address", "Адрес"), kWhite);
    D.addr_edit = add_edit(J, "address", {230, 316, 520, 348}, load_str("ui", "last_address", ""), 64);
    D.join_btn = add_button(J, "join", {540, 314, 720, 350}, T("Join", "Подключиться"),
                            [] { do_join(sui::edit_text(D.addr_edit)); });
    D.join_pw_lbl = add_label(J, {40, 360, 225, 388}, T("Password", "Пароль"), kWhite);
    D.join_pw_edit = add_edit(J, "joinpw", {230, 356, 520, 388}, "", 64);
    if (D.join_pw_edit)
        sui::set_edit_password(D.join_pw_edit, !g_join_pw_show);
    D.join_pw_eye = add_button(J, "joinpweye", {540, 354, 720, 390}, T("Show", "Показать"), [] {
        g_join_pw_show = !g_join_pw_show;
        if (D.join_pw_edit)
            sui::set_edit_password(D.join_pw_edit, !g_join_pw_show);
        g_dirty = true;
    });
    add_label(J, {40, 396, 720, 418},
              std::string(T("IP or host name (default port ", "IP или имя хоста (порт по умолчанию ")) +
                  std::to_string(net::kDefaultPort) + T(", e.g. 192.168.1.5)", ", например 192.168.1.5)"),
              kDim);
    D.rec_hdr = add_label(J, {40, 424, 590, 448}, "", kDim);
    g_named["rechdr"] = D.rec_hdr;
    D.rec_more = add_button(J, "recmore", {600, 420, 720, 450}, T("More >", "Ещё >"), [] {
        g_rec_first += 3;
        g_dirty = true;
    });
    for (int i = 0; i < 3; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "rec%d", i);
        D.rec_rows[i] = add_button(J, nm, {40, 454.f + i * 30, 720, 482.f + i * 30}, "", [i] {
            if (!D.rec_addr[i].empty()) {
                sui::set_edit_text(D.addr_edit, D.rec_addr[i]);
                set_status(std::string(T("Selected ", "Выбран ")) + D.rec_addr[i], kYellow, 8);
                g_dirty = true;
            }
        });
    }

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
    D.stage_hdr = add_label(L, {345, 138, 540, 162}, T("Stage", "Этап"), kDim);
    add_label(L, {540, 138, 620, 162}, T("Ping", "Пинг"), kDim);
    add_label(L, {620, 138, 720, 162}, T("Ready", "Готов"), kDim);
    for (int i = 0; i < 4; i++) {
        float y = 166.f + i * 28;
        D.row_name[i] = add_label(L, {40, y, 280, y + 26}, "", kWhite);
        D.row_color[i] = add_label(L, {280, y, 318, y + 26}, "", kDim); // (the own row: ">" at 318..346)
        D.row_stage[i] = add_label(L, {345, y, 540, y + 26}, "", kWhite);
        D.row_ping[i] = add_label(L, {540, y, 620, y + 26}, "", kWhite);
        D.row_ready[i] = add_label(L, {620, y, 720, y + 26}, "", kWhite);
    }
    D.color_btn = add_button(L, "colour", {318, 167, 346, 191}, ">", [] { // F2: moved onto the own row by refresh_lobby
        std::string line;
        bool ok = A.color_next && A.color_next(line);
        set_status(line, ok ? kGreen : kRed, 6);
    });
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
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle && !g_row_creation)
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
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle && !g_row_creation)
            A.civ_role_cycle(1);
        else if (A.creation_cycle)
            A.creation_cycle(1);
        g_dirty = true;
    });
    // BUG-1403: Civ, a guest playing the host's species: "My creature: ... < >" one line up (the chat's 4th line),
    // "My role: ... < >" under it in the row above; these arrows always pick a creature
    D.creation_up = add_label(L, {40, 404, 560, 430}, "", kGold);
    g_named["creationup"] = D.creation_up;
    D.cup_prev = add_button(L, "cupprev", {570, 404, 640, 430}, "<", [] {
        if (A.creation_cycle)
            A.creation_cycle(-1);
        g_dirty = true;
    });
    D.cup_next = add_button(L, "cupnext", {650, 404, 720, 430}, ">", [] {
        if (A.creation_cycle)
            A.creation_cycle(1);
        g_dirty = true;
    });
    // BUG-1477: a guest's consent to its old cells as wild species (only in Cell with the host's setting on)
    D.lineage_btn = add_button(L, "lineage", {260, 558, 720, 590}, "", [] {
        if (A.lineage_toggle)
            A.lineage_toggle();
        g_dirty = true;
    });
    D.space_btn = add_button(L, "space", {580, 66, 720, 98}, T("Space >", "Космос >"), [] {
        g_space_page = true;
        g_rivals_page = false;
        g_dirty = true;
    });
    // WP-R1 (BUG-0794): dedicated Rivals / Race page button in the lobby top bar (right of centered CO-OP LOBBY title)
    D.rivals_btn = add_button(L, "rivals", {580, 66, 720, 98}, T("Rivals >", "Соперники >"), [] {
        g_rivals_page = true;
        g_space_page = false;
        g_settings_page = false;
        g_dirty = true;
    });
    // WP-R1 (BUG-0794, BUG-1551): stage transition vote mode preview/toggle button (host toggles before Ready; sits to the right of ask_btn)
    D.vote_mode_btn = add_button(L, "votemode", {450, 558, 720, 592}, "", [] {
        if (A.vote_toggle_next_stage)
            A.vote_toggle_next_stage();
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
        std::string planet, said;
        if (why == 1 && A.resume && A.resume(planet) && A.resume_do) { // BUG-1434: Continue <planet>
            bool ok = A.resume_do(said);
            set_status(ok ? std::string(T("Loading ", "Загружается ")) + planet + "..." : said, ok ? kYellow : kRed, 8);
            g_dirty = true;
            return;
        }
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
    // a guest asked by the host for its report (F4, BUG-0767): the question in the row of the host's button, what is
    // sent in the status line
    D.rask = add_label(L, {40, 562, 400, 590}, "", kGold);
    g_named["rask"] = D.rask;
    D.rline = add_label(L, {450, 562, 720, 590}, "", kYellow);
    g_named["rline"] = D.rline;
    D.rsend = add_button(L, "rsend", {410, 558, 560, 592}, T("Send", "Отправить"), [] {
        if (A.report_answer)
            A.report_answer(true);
        g_dirty = true;
    });
    D.rnosend = add_button(L, "rnosend", {570, 558, 720, 592}, T("Don't send", "Не отправлять"), [] {
        if (A.report_answer)
            A.report_answer(false);
        g_dirty = true;
    });

    // ---- settings page (settings_core.h kDefs by group; docs/07-modes.md#modes-session-settings) ----
    auto& G = D.page_settings;
    D.set_title = add_label(G, {40, 104, 720, 130}, T("Session settings", "Настройки сессии"), kGold);
    D.set_note = add_label(G, {40, 130, 720, 154}, "", kDim);
    for (int i = 0; i < kSetRows; i++) {
        float y = 160.f + i * 27;
        char nm[16];
        D.set_label[i] = add_label(G, {40, y + 1, 464, y + 25}, "", kWhite);
        snprintf(nm, sizeof nm, "set%d", i);
        D.set_btn[i] = add_button(G, nm, {466, y, 634, y + 25}, "", [i] { do_set_click(i); });
        D.set_tag[i] = add_label(G, {640, y + 1, 740, y + 25}, "", kDim);
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

    // ---- Rivals / Race page (WP-R1, BUG-0794) ----
    auto& R = D.page_rivals;
    D.rivals_title = add_label(R, {40, 104, 720, 130}, T("Rivals & Race", "Соперники и гонка"), kGold);
    for (int i = 0; i < kRivalsRows; i++) {
        float y = 136.f + i * 29;
        char nm[16];
        D.rivals_text[i] = add_label(R, {40, y + 2, 545, y + 27}, "", kWhite);
        snprintf(nm, sizeof nm, "rbtn%d", i);
        D.rivals_button[i] = add_button(R, nm, {555, y, 720, y + 27}, "", [i] {
            if (!D.rivals_action[i])
                return;
            H->run_outside_frame(
                [](void* p) {
                    auto* f = (std::function<void()>*)p;
                    (*f)();
                    delete f;
                },
                new std::function<void()>(D.rivals_action[i]));
            g_dirty = true;
        });
    }
    D.rivals_back = add_button(R, "rback", {40, 548, 240, 588}, T("< Lobby", "< Лобби"), [] {
        g_rivals_page = false;
        g_dirty = true;
    });
    D.rivals_prev = add_button(R, "rprev", {500, 548, 570, 588}, "<", [] {
        g_rivals_first = std::max(0, g_rivals_first - kRivalsRows);
        g_dirty = true;
    });
    D.rivals_pageno = add_label(R, {575, 554, 645, 582}, "", kDim, sui::kAlignCenter);
    D.rivals_next = add_button(R, "rnext", {650, 548, 720, 588}, ">", [] {
        g_rivals_first += kRivalsRows;
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
    if (D.host_pw_edit)
        sui::add_handler(D.host_pw_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_host();
                return true;
            }
            return false;
        });
    if (D.join_pw_edit)
        sui::add_handler(D.join_pw_edit, [](Win, const sui::Msg& m) {
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
    // BUG-0874: native Spore-style hover tooltip overlay (StdDrawable 9-slice + centered caption, mouse-transparent)
    D.tip_win = sui::label(D.root, {0, 0, 220, 32}, "", D.font_body, 0xFFF2F8FF, sui::kAlignCenter);
    if (D.tip_win) {
        sui::set_button_borders(D.tip_win, 12.f, 5.f, 12.f, 5.f);
        void* img = sui::image(sui::fnv("tooltip_bg"), 0x2F7D0004, sui::fnv("sporecoop"));
        if (img) {
            D.tip_drawable = sui::std_drawable_9slice(img);
            if (D.tip_drawable)
                sui::set_drawable(D.tip_win, D.tip_drawable);
            if (sui::g_release_fix)
                sui::release(img);
        }
        sui::set_flag(D.tip_win, sui::kFlagIgnoreMouse, true);
        sui::set_flag(D.tip_win, sui::kFlagAlwaysInFront, true);
        sui::set_visible(D.tip_win, false);
    }
    for (Win w : common)
        (void)w;
    sui::set_visible(root, false);
    g_cache[root] = Cache{"", 0, 0};
    H->log(M, SK_INFO, "ui: co-op dialog ready");
}

static void refresh_connect()
{
    bool scanning = A.role() == 0 && g_step == kStepJoin;
    bool hosting_step = A.role() == 0 && g_step == kStepHost;
    bool pw_on = net::g_fix.session_password;
    show(D.host_pw_lbl, hosting_step && pw_on);
    show(D.host_pw_edit, hosting_step && pw_on);
    show(D.host_pw_eye, hosting_step && pw_on);
    text(D.host_pw_eye, g_host_pw_show ? T("Hide", "Скрыть") : T("Show", "Показать"));
    show(D.join_pw_lbl, scanning && pw_on);
    show(D.join_pw_edit, scanning && pw_on);
    show(D.join_pw_eye, scanning && pw_on);
    text(D.join_pw_eye, g_join_pw_show ? T("Hide", "Скрыть") : T("Show", "Показать"));
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
            std::string shown = (g_hide_ip_fix && g_hide_ip) ? i18n::mask_ip_address(g.address) : g.address;
            char b[280];
            snprintf(b, sizeof b, "%s%s  -  %s  -  %d/%d  -  %s",
                     g.locked ? T("[Locked] ", "[Пароль] ") : "",
                     g.name.c_str(), g.info.c_str(), g.players, g.max_players, shown.c_str());
            text(D.lan_rows[i], b);
            D.lan_addr[i] = g.address;
            D.lan_locked[i] = g.locked;
        } else {
            D.lan_addr[i].clear();
            D.lan_locked[i] = false;
        }
        show(D.lan_rows[i], have && scanning);
    }
    // BUG-0867: recently joined server addresses (up to 8 persisted in coop.ini [recent], pages of 3)
    auto recs = A.recent_servers ? A.recent_servers() : std::vector<std::string>{};
    bool rec_pages = recs.size() > 3;
    if (!rec_pages || g_rec_first >= (int)recs.size() || g_rec_first < 0)
        g_rec_first = 0;
    show(D.rec_hdr, scanning && !recs.empty());
    show(D.rec_more, scanning && rec_pages);
    if (!recs.empty()) {
        if (rec_pages) {
            char b[160];
            snprintf(b, sizeof b, T("Recent servers: %d-%d of %d (click to fill)", "Недавние серверы: %d-%d из %d (нажмите)"),
                     g_rec_first + 1, std::min(g_rec_first + 3, (int)recs.size()), (int)recs.size());
            text(D.rec_hdr, b);
        } else {
            text(D.rec_hdr, T("Recent servers (click to fill address):", "Недавние серверы (нажмите, чтобы подставить):"));
        }
    }
    for (int i = 0; i < 3; i++) {
        int k = g_rec_first + i;
        bool have = k < (int)recs.size();
        if (have) {
            text(D.rec_rows[i], (g_hide_ip_fix && g_hide_ip) ? i18n::mask_ip_address(recs[k]) : recs[k]);
            D.rec_addr[i] = recs[k];
        } else {
            D.rec_addr[i].clear();
        }
        show(D.rec_rows[i], have && scanning);
    }
    std::string hint;
    if (A.local_addresses) {
        auto ips = A.local_addresses();
        std::string port = sui::edit_text(D.port_edit);
        for (size_t i = 0; i < ips.size() && i < 2; i++) // (two fit the line)
            hint += (i ? ",  " : "") + ips[i] + ":" + port;
    }
    if (!g_demo_addr.empty())
        hint = g_demo_addr;
    if (g_hide_ip_fix && g_hide_ip && !hint.empty())
        hint = i18n::mask_ip_address(hint);
    text(D.host_hint, hint.empty() ? "" : std::string(T("Friends connect to: ", "Друзья подключаются к: ")) + hint);
    std::string up_line, up_btn;
    bool up = A.update && A.update(up_line, up_btn);
    // the session status wins; otherwise the update line (a new SporeCoop release)
    text(D.status, g_status.empty() && up ? up_line : g_status);
    color(D.status, g_status.empty() && up ? kGreen : g_status_color);
    // the Update button belongs to the update line, not to a session message (BUG-0104)
    show(D.update_btn, up && !up_btn.empty() && (!g_status_fix || g_status.empty()));
    text(D.update_btn, up_btn);
}

bool g_give_hint_fix = true;
COOP_FIX("core.ui_give_hint", "BUG-0909", g_give_hint_fix, "ui: the give row shows only in Tribe, Civilization and Space");
// BUG-1403: in Civ the creature row of a guest that plays the host's species and has own creatures (BUG-1289) took the
// role row's place, so it could pick no role while keeping the host's species. On (the owner's decision: two rows):
// the creature row goes one line up (the chat shows 3 lines meanwhile), the role row stays where it was under it;
// each row's arrows change only its own thing. Off: one row, the creature's (rel/0.7.16 before the fix)
static bool g_civ_rows_fix = true;
COOP_FIX("core.ui_civ_two_rows", "BUG-1403", g_civ_rows_fix, "ui: in Civ a guest playing the host's species sees its creature row and its role row at once");

static void refresh_lobby()
{
    static bool s_layout = g_layout_fix; // the switch changed: every left-aligned label is fitted anew
    if (s_layout != g_layout_fix) {
        s_layout = g_layout_fix;
        for (auto& [w, c] : g_cache)
            if (c.left)
                fit(w, c);
    }
    { // BUG-0908: the Stage column starts after the colour button (318..346) with room, not at its edge
        float x1 = g_layout_fix ? 362.f : 345.f;
        for (int i = -1; i < 4; i++) {
            Win w = i < 0 ? D.stage_hdr : D.row_stage[i];
            if (!w)
                continue;
            auto& c = g_cache[w];
            if (c.box.x1 != x1) {
                c.box.x1 = x1;
                fit(w, c);
            }
        }
    }
    // BUG-1579: keep the host lobby header on a single line so it never wraps over the column headers at y=138.
    {
        std::string sline = A.session_line();
        if (caption_width(sline) + 30.f > 680.f) {
            if (sline.rfind("Hosting on UDP port ", 0) == 0)
                sline.replace(0, 20, T("Host UDP ", "Сервер UDP "));
            size_t pl = sline.find(" players");
            if (pl != std::string::npos && caption_width(sline) + 30.f > 680.f)
                sline.replace(pl, 8, T(" pl.", " игр."));
            static const char kRuAutoFull[] = " (\xD0\xB0\xD0\xB2\xD1\x82\xD0\xBE\xD1\x81\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5)";
            size_t as = sline.find(kRuAutoFull);
            if (as != std::string::npos && caption_width(sline) + 30.f > 680.f)
                sline.replace(as, sizeof(kRuAutoFull) - 1, " (\xD0\xB0\xD0\xB2\xD1\x82\xD0\xBE\xD1\x81\xD0\xBE\xD1\x85\xD1\x80.)");
            if (caption_width(sline) + 30.f > 680.f)
                sline = cut_caption("", sline, 680.f + 18.f);
        }
        text(D.session, sline);
    }
    auto rows = A.players();
    int own_row = -1;
    for (int i = 0; i < 4; i++) {
        bool have = i < (int)rows.size();
        if (have && rows[i].me)
            own_row = i;
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
        if (p.me && D.color_btn) { // F2: the own colour's ">" on the own row
            float y = 166.f + i * 28;
            static float s_y = -1;
            if (s_y != y) {
                sui::set_area(D.color_btn, {318, y + 1, 346, y + 25});
                s_y = y;
            }
        }
        text(D.row_stage[i], p.stage);
        text(D.row_ping[i], p.me ? "" : (p.ping_ms >= 0 ? std::to_string(p.ping_ms) + T(" ms", " мс") : "?"));
        text(D.row_ready[i], p.ready ? T("ready", "готов") : "-");
        color(D.row_ready[i], p.ready ? kGreen : kDim);
    }
    show(D.color_btn, own_row >= 0 && A.color_next);
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
    int ck = 0, cown = 0;
    bool hint = false; // the creation row says how to get a choice: it may take the arrows' room
    std::string cname, cl;
    bool one = A.one_creature && A.one_creature(); // F5 (BUG-0769): the host's creature for everybody
    if (A.creation && A.creation(ck, cname, cown)) {
        if (one && ck != 2)
            cown = 0;
        cl = ck == 1 ? T("My cell: ", "Моя клетка: ") : ck == 2 ? T("My ship: ", "Мой корабль: ") : T("My creature: ", "Моё существо: ");
        bool me_host = A.role() == 1;
        if (!cname.empty())
            cl += cname;
        else // BUG-0598: say whose it is
            cl += ck == 2 ? T("your empire's ship", "корабль вашей империи")
                  : me_host ? T("your world's species", "вид вашего мира") : T("the host's species", "вид хоста");
        if (one && ck != 2)
            cl += T(" (the host evolves it for everybody)", " (хост развивает его для всех)");
        else if (ck == 1 && cown > 0 && A.creation_premades && A.creation_premades()) // BUG-1476
            cl += std::string(T("  (game cells: ", "  (клетки игры: ")) + std::to_string(cown) + ")";
        else if (ck != 2 && cown > 0)
            cl += std::string(T("  (in My Creations: ", "  (своих: ")) + std::to_string(cown) + ")";
        else if (ck != 2 && !me_host) { // no arrows: how to get a choice
            cl += T(" (make one in an editor to pick it)", " (создайте своё в редакторе)");
            hint = true;
        }
    }
    std::string gwhat, gto;
    // BUG-1579: hide the Together-mode F9 Give row (tribe members / Civ cities & vehicles) in both Tribe and Civ Rivals.
    bool rivals_no_give = A.mode && (A.mode() == modes::kTribe || A.mode() == modes::kCiv) &&
                          rivals::stage_is_rivals(rivals::stage_from_mode(A.mode()));
    int gn = (!rivals_no_give && A.give) ? A.give(gwhat, gto) : 0;
    bool space = gwhat == "spice";
    // BUG-1289: in Tribe and Civ the creature row of a guest without its own creature gives way to selected units
    if (!cl.empty() && gn > 0 && A.mode && (A.mode() == 0x1654C02 || A.mode() == 0x1654C04)) {
        cl.clear();
        hint = false;
    }
    // BUG-1403: in Civ with nothing selected the creature row moves one line up and the role row comes back under it
    std::string up_row;
    if (g_civ_rows_fix && !cl.empty() && gn == 0 && ck == 0 && cown > 0 && A.civ_role && A.mode &&
        A.mode() == 0x1654C04)
        up_row.swap(cl);
    // BUG-0909: give() names what is given only in Tribe, Civilization and Space; elsewhere (the galaxy menu, Cell,
    // Creature) the row showed the Tribe / Civ hint "Select your units to give them to a partner"
    bool giving = A.give && !gto.empty() && (cl.empty() || space) && (!gwhat.empty() || !g_give_hint_fix);
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
    if (!up_row.empty() && !roles) { // no role row here (no partner to give to): the creature row keeps its place
        cl.swap(up_row);
        up_row.clear();
    }
    const bool two_rows = !up_row.empty();
    text(D.creation_up, up_row);
    show(D.creation_up, two_rows);
    show(D.cup_prev, two_rows);
    show(D.cup_next, two_rows);
    if (D.creation) { // the row is wider while it has no arrows and no buttons
        auto& c = g_cache[D.creation];
        float x2 = hint && !giving && !roles ? 720.f : 560.f;
        if (c.box.x2 != x2 && c.left) {
            c.box.x2 = x2;
            fit(D.creation, c);
        }
    }
    text(D.creation, cl);
    show(D.give_btn, giving && gn > 0);
    // BUG-1050: in Space with 3-4 players ">" comes back; the jump button holds its Tribe / Civ place (520..560), so
    // the row is cut anew there: jump 400..515, Give 520..690, ">" 694..720 (the window's inner edge, as Send)
    bool pick = giving && gn > 0 && A.give_choices && A.give_choices() > 1;
    bool pick_space = pick && space && A.give_pick_space && A.give_pick_space();
    auto place = [](Win w, const Rect& r) {
        if (w && (std::abs(sui::area(w).x1 - r.x1) > 0.5f || std::abs(sui::area(w).x2 - r.x2) > 0.5f))
            sui::set_area(w, r);
    };
    std::string give_cap = space ? std::string(T("Give 10 to ", "Дать 10: ")) + gto : std::string(T("Give to ", "Отдать ")) + gto;
    std::string jto;
    bool jump = A.jump && A.jump(jto);
    std::string jump_cap = std::string(T("Jump to ", "К звезде: ")) + jto;
    Rect r_next = pick_space ? Rect{694, 430, 720, 460} : Rect{520, 430, 560, 460};
    Rect r_give = pick_space ? Rect{520, 430, 690, 460} : Rect{570, 430, 720, 460};
    Rect r_jump = pick_space ? Rect{400, 430, 515, 460} : Rect{400, 430, 560, 460};
    if (g_give_fit_fix) { // BUG-1062: right to left, each button as wide as its caption needs (+48: the button's text
        // borders are 23 px left and right, IButton +0x50: docs/08-ui.md#ui-give-fit)
        auto need = [](const std::string& c, float base) { return std::max(base, std::round(caption_width(demo_text(c)) + 48)); };
        if (g_give_clamp_fix && r_give.x2 - need(give_cap, r_give.x2 - r_give.x1) < kGiveMinX1) // BUG-1518
            give_cap = cut_caption(space ? T("Give 10 to ", "Дать 10: ") : T("Give to ", "Отдать "), gto,
                                   r_give.x2 - kGiveMinX1);
        r_give.x1 = r_give.x2 - need(give_cap, r_give.x2 - r_give.x1);
        if (pick && !space) // Tribe / Civ: ">" left of the give button
            r_next = Rect{r_give.x1 - 50, 430, r_give.x1 - 10, 460};
        float jx2 = std::min(r_jump.x2, (giving && gn > 0 ? r_give.x1 : 720.f) - 5);
        if (g_give_clamp_fix && jx2 - need(jump_cap, r_jump.x2 - r_jump.x1) < kJumpMinX1) // BUG-1518
            jump_cap = cut_caption(T("Jump to ", "К звезде: "), jto, jx2 - kJumpMinX1);
        r_jump = Rect{jx2 - need(jump_cap, r_jump.x2 - r_jump.x1), 430, jx2, 460};
    }
    place(D.give_next, r_next);
    place(D.give_btn, r_give);
    place(D.jump_btn, r_jump);
    show(D.give_next, pick && (!space || pick_space));
    text(D.give_btn, give_cap);
    show(D.jump_btn, jump);
    text(D.jump_btn, jump_cap);
    bool can = (!cl.empty() && !giving && ck != 2 && cown > 0) || roles;
    g_row_creation = !cl.empty() && !giving && !roles && ck != 2 && cown > 0; // BUG-1289: the arrows pick a creature
    show(D.creation_prev, can);
    show(D.creation_next, can);
    // the chat history (50 lines kept): long lines wrapped, scrolled with the arrows (BUG-0129)
    std::vector<std::string> chat;
    std::vector<uint32_t> chat_col; // F2: the writer's colour of each wrapped line
    {
        std::vector<std::string> lines = A.chat ? A.chat() : std::vector<std::string>{};
        std::vector<uint32_t> cols = A.chat_colors ? A.chat_colors() : std::vector<uint32_t>{};
        for (size_t i = 0; i < lines.size(); i++)
            for (auto& w : notify_text::wrap(lines[i], kChatCols)) {
                chat.push_back(w);
                chat_col.push_back(i < cols.size() && cols[i] ? cols[i] : kWhite);
            }
    }
    const int nchat = two_rows ? 3 : 4; // BUG-1403: the creature row above the role row takes the 4th line
    g_chat_scroll = std::max(0, std::min(g_chat_scroll, (int)chat.size() - nchat));
    size_t first = chat.size() > (size_t)nchat ? chat.size() - nchat - (size_t)g_chat_scroll : 0;
    static uint32_t s_chat_col[4] = {kWhite, kWhite, kWhite, kWhite};
    static Win s_chat_w[4] = {};
    for (int i = 0; i < 4; i++) {
        text(D.chat_lines[i], i < nchat && first + i < chat.size() ? chat[first + i] : "");
        uint32_t c = first + i < chat.size() ? chat_col[first + i] : kWhite;
        if (D.chat_lines[i] && (c != s_chat_col[i] || D.chat_lines[i] != s_chat_w[i])) {
            sui::set_label_color(D.chat_lines[i], c);
            s_chat_col[i] = c;
            s_chat_w[i] = D.chat_lines[i];
        }
    }
    show(D.chat_up, first > 0);
    show(D.chat_down, g_chat_scroll > 0);
    bool host = A.role() == 1;
    int offer = A.evolve_offer && !one ? A.evolve_offer() : 0; // BUG-0516: the host evolves now, this guest may follow
    if (offer > 0)
        text(D.ready_btn, T("Evolve now", "Эволюция"));
    else
        text(D.ready_btn, A.ready() ? T("Not ready", "Не готов") : T("I'm ready", "Я готов"));
    show(D.share_btn, host);
    std::string resume; // BUG-1434: a host in the galaxy menu continues the co-op world it played last
    bool can_resume = host && share_why == 1 && A.resume && A.resume(resume);
    if (can_resume && resume.size() > 14)
        resume = resume.substr(0, 13) + ".";
    enable(D.share_btn, !share_why || can_resume);
    text(D.share_btn, A.vote_active && A.vote_active() ? T("Start now", "Начать сейчас")
                      : can_resume                    ? std::string(T("Continue ", "Продолжить ")) + resume
                                                      : T("Send my world", "Отправить мир"));
    text(D.leave_btn, host ? T("Close session", "Закрыть сессию") : T("Leave", "Выйти"));
    std::string vm_label;
    bool show_vm = host && rivals::enabled() && A.vote_next_stage_mode && A.vote_next_stage_mode(vm_label);
    bool round = host && A.report_round && A.report_round();
    // BUG-1551: keep ask_btn, rline, and vote_mode_btn disjoint in every state.
    // Normally ask_btn occupies 40..440 and vote_mode_btn occupies 450..720; while a report round runs
    // simultaneously (ask_btn is already disabled and rline occupies 450..720), vote_mode_btn moves to 40..440.
    show(D.ask_btn, host && !(show_vm && round));
    // F4 (BUG-0767): the guest's question and the host's line about the round, in the status line
    std::string rq;
    int asked = !host && A.report_prompt ? A.report_prompt(rq) : 0;
    show(D.rask, asked);
    show(D.rsend, asked);
    show(D.rnosend, asked);
    if (asked)
        text(D.rask, rq);
    enable(D.ask_btn, !round);
    show(D.rline, round); // the answers while the round runs ("Reports: 1 of 3 answered"); the status keeps "Report saved"
    show(D.vote_mode_btn, show_vm);
    if (show_vm) {
        sui::set_area(D.vote_mode_btn, round ? Rect{40, 558, 440, 592} : Rect{450, 558, 720, 592});
        text(D.vote_mode_btn, vm_label);
    }
    std::string rl = host ? (A.report_line ? A.report_line() : "")
                   : asked ? T("Sent: zip with logs, summary, screenshots, crash files (paths masked)",
                               "Отправится .zip: логи, сводка, снимки, сбои (пути скрыты)")
                           : rq;
    if (round)
        text(D.rline, rl);
    else if (rl != g_report_seen) {
        g_report_seen = rl;
        if (!rl.empty())
            set_status(rl, asked ? kWhite : rl.find("refused") != std::string::npos ? kRed : kGreen, asked ? 0 : 60);
    }
    // the address to give to friends (the host) or the host's (a guest)
    std::string addr = A.host_address ? A.host_address() : "";
    if (!g_demo_addr.empty() && !addr.empty())
        addr = g_demo_addr;
    if (g_hide_ip_fix && !addr.empty() && (g_hide_ip || g_vote_auto_open))
        addr = i18n::mask_ip_address(addr);
    text(D.address, addr.empty() ? ""
                    : std::string(host ? T("Friends connect to: ", "Друзья подключаются к: ") : T("Host address: ", "Адрес хоста: ")) +
                          addr);
    // the settings button says how many settings differ from the built-in defaults (F3: the lobby shows it)
    int changed = 0;
    for (int i = 0; i < settings::kCount; i++)
        changed += settings::get((settings::Id)i) != settings::kDefs[i].def;
    char sb[80];
    if (changed && g_layout_fix) // BUG-0904: the long caption wrapped out of the button; the page marks them with *
        snprintf(sb, sizeof sb, T("Settings (%d)", "Настройки (%d)"), changed);
    else if (changed)
        snprintf(sb, sizeof sb, T("Settings (%d changed)", "Настройки (изменено: %d)"), changed);
    else
        snprintf(sb, sizeof sb, "%s", T("Settings", "Настройки"));
    text(D.settings_btn, sb);
}

// BUG-0865 / BUG-0866: local Display settings (frame rate cap, borderless window, third-person FOV)
// shown as a local group at the end of the Settings page (editable by host and guest alike; no wire)
static bool g_display_settings_fix = true;
COOP_FIX("ui.display_settings", "BUG-0865", g_display_settings_fix,
         "ui: Display local settings (frame rate cap, borderless window, third-person FOV) in Settings");

constexpr int kLocalHdr = -10000;
constexpr int kLocalFps = 10000;
constexpr int kLocalBorderless = 10001;
constexpr int kLocalFov = 10002;
constexpr int kLocalHideIp = 10003; // BUG-1572: streamer mode IP masking
constexpr int kLocalLang = 10004;   // BUG-1575: 5-language selector (Auto / EN / RU / HU / ES / ZH)

static bool is_local_setting(int id)
{
    return id == kLocalFps || id == kLocalBorderless || id == kLocalFov || id == kLocalHideIp || id == kLocalLang;
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
    if (g_display_settings_fix) {
        e.push_back(kLocalHdr);
        e.push_back(kLocalFps);
        e.push_back(kLocalBorderless);
        e.push_back(kLocalFov);
        e.push_back(kLocalHideIp);
        e.push_back(kLocalLang);
    }
    return e;
}

// a setting's value as its button shows it: On / Off, a named value (creature.mode: Own / Host's), a number
static std::string value_label(int id, int v)
{
    return i18n::setting_value(settings::value_text(id, v));
}

// who may change setting `id` from this window now (settings_core.h can_set: a guest never, a session setting
// only before the start)
static settings::Refusal set_refusal(int id)
{
    auto rf = settings::can_set(id, A.role() != 2, settings::started());
    if (rf == settings::Refusal::None && !settings::is_live((settings::Id)id) && settings::started())
        rf = settings::Refusal::SessionStarted;
    return rf;
}

static void do_set_click(int row)
{
    int id = row >= 0 && row < kSetRows ? D.set_id[row] : -1;
    if (id < 0)
        return;
    if (id == kLocalFps) {
        int cur = A.frame_target ? A.frame_target() : 0;
        int nv = skdisp::next_fps_preset(cur);
        if (A.set_frame_target)
            nv = A.set_frame_target(nv);
        g_set_line = std::string(T("Frame rate cap: ", "Ограничение FPS: ")) +
                     (nv == 0 ? T("Auto", "Авто") : std::to_string(nv) + " fps");
        g_set_line_color = kGreen;
        trace::line("ui", "display.frame_target -> %d", nv);
        g_dirty = true;
        return;
    }
    if (id == kLocalBorderless) {
        bool cur = A.borderless ? A.borderless() : false;
        bool nv = A.set_borderless ? A.set_borderless(!cur) : !cur;
        g_set_line = std::string(T("Borderless window: ", "Окно без рамки: ")) +
                     (nv ? T("On", "Вкл") : T("Off", "Выкл"));
        g_set_line_color = kGreen;
        trace::line("ui", "display.borderless -> %d", nv ? 1 : 0);
        g_dirty = true;
        return;
    }
    if (id == kLocalFov) {
        int nv = A.cycle_fov ? A.cycle_fov() : 0;
        g_set_line = std::string(T("Third-person FOV: ", "Обзор от 3-го лица: ")) +
                     (nv == 0 ? T("Default", "Обычный") : "+" + std::to_string(nv * 3) + "\xC2\xB0");
        g_set_line_color = kGreen;
        trace::line("ui", "display.fov -> %d", nv);
        g_dirty = true;
        return;
    }
    if (id == kLocalHideIp) {
        set_hide_ip(!g_hide_ip);
        g_set_line = std::string(T("Hide IP (streamer mode): ", "Скрывать IP (режим стримера): ")) +
                     (g_hide_ip ? T("On", "Вкл") : T("Off", "Выкл"));
        g_set_line_color = kGreen;
        trace::line("ui", "display.hide_ip -> %d", g_hide_ip ? 1 : 0);
        g_dirty = true;
        return;
    }
    if (id == kLocalLang) {
        // Cycle: Auto -> En -> Ru -> Hu -> Es -> Zh -> Auto
        static const char* const kOrder[6] = {"auto", "en", "ru", "hu", "es", "zh"};
        int cur = i18n::is_auto() ? 0 : ((int)i18n::lang() + 1);
        int nxt = (cur + 1) % 6;
        set_lang(kOrder[nxt]);
        save_str("ui", "lang", kOrder[nxt]);
        g_set_line = std::string(T("Language: ", "Язык интерфейса: ")) +
                     (i18n::is_auto() ? std::string(T("Auto", "Авто")) + " (" + i18n::lang_display(i18n::lang()) + ")"
                                      : std::string(i18n::lang_display(i18n::lang())));
        g_set_line_color = kGreen;
        trace::line("ui", "display.lang -> %s", kOrder[nxt]);
        g_dirty = true;
        return;
    }
    const auto& d = settings::kDefs[id];
    int v = settings::get((settings::Id)id);
    int nv = d.max == 1 && d.min == 0 ? !v : (v >= d.max ? d.min : v + 1);
    std::string why;
    auto r = set_refusal(id);
    if (r != settings::Refusal::None || !settings::set((settings::Id)id, nv, false, &why)) {
        g_set_line = r == settings::Refusal::Guest ? T("Only the host changes the settings", "Настройки меняет только хост")
                     : r == settings::Refusal::SessionStarted
                         ? T("Locked: a session setting changes only before the start", "Закрыто: меняется только в меню галактики до старта")
                         : std::string(T("Not changed: ", "Не изменено: ")) + why;
        g_set_line_color = kRed;
    } else {
        g_set_line = std::string(i18n::setting_label(d.key, d.label)) + ": " + value_label(id, nv);
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
                                   "Настройки «на лету» меняются сразу, остальные только в меню галактики.")
                               : T("Click to change. Live settings can change in play too.",
                                   "Нажмите, чтобы изменить. Настройки «на лету» меняются и в игре."));
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
        if (id == kLocalHdr) {
            text(D.set_label[i], T("Display (this PC)", "Экран и интерфейс (этот ПК)"));
            color(D.set_label[i], kGold);
            continue;
        }
        if (id < 0) {
            text(D.set_label[i], i18n::setting_group(settings::kDefs[-id - 1].group));
            color(D.set_label[i], kGold);
            continue;
        }
        if (is_local_setting(id)) {
            if (id == kLocalFps) {
                int fps = A.frame_target ? A.frame_target() : 0;
                text(D.set_label[i], std::string(T("Frame rate cap", "Ограничение FPS")) + (fps ? " *" : ""));
                color(D.set_label[i], fps ? kYellow : kWhite);
                text(D.set_btn[i], fps == 0 ? T("Auto", "Авто") : std::to_string(fps) + " fps");
            } else if (id == kLocalBorderless) {
                bool bl = A.borderless ? A.borderless() : false;
                text(D.set_label[i], std::string(T("Borderless window", "Окно без рамки")) + (bl ? " *" : ""));
                color(D.set_label[i], bl ? kYellow : kWhite);
                text(D.set_btn[i], bl ? T("On", "Вкл") : T("Off", "Выкл"));
            } else if (id == kLocalFov) {
                int st = A.fov_step ? A.fov_step() : 0;
                text(D.set_label[i], std::string(T("Third-person FOV", "Обзор от 3-го лица")) + (st ? " *" : ""));
                color(D.set_label[i], st ? kYellow : kWhite);
                text(D.set_btn[i], st == 0 ? T("Default", "Обычный") : "+" + std::to_string(st * 3) + "\xC2\xB0");
            } else if (id == kLocalHideIp) {
                text(D.set_label[i], std::string(T("Hide IP (streamer mode)", "Скрывать IP (режим стримера)")) + (g_hide_ip ? " *" : ""));
                color(D.set_label[i], g_hide_ip ? kYellow : kWhite);
                text(D.set_btn[i], g_hide_ip ? T("On", "Вкл") : T("Off", "Выкл"));
            } else {
                bool custom = !i18n::is_auto();
                text(D.set_label[i], std::string(T("Language", "Язык интерфейса")) + (custom ? " *" : ""));
                color(D.set_label[i], custom ? kYellow : kWhite);
                text(D.set_btn[i], i18n::is_auto() ? std::string(T("Auto", "Авто")) : std::string(i18n::lang_display(i18n::lang())));
            }
            enable(D.set_btn[i], true);
            text(D.set_tag[i], T("local", "локально"));
            color(D.set_tag[i], kGreen);
            continue;
        }
        const auto& d = settings::kDefs[id];
        int v = settings::get((settings::Id)id);
        bool differs = v != d.def;
        text(D.set_label[i], std::string(i18n::setting_label(d.key, d.label)) + (differs ? " *" : ""));
        color(D.set_label[i], differs ? kYellow : kWhite);
        text(D.set_btn[i], value_label(id, v));
        auto r = set_refusal(id);
        enable(D.set_btn[i], r == settings::Refusal::None);
        bool live = settings::is_live((settings::Id)id);
        bool locked = !live && started; // (a guest too: the host's started flag)
        text(D.set_tag[i], locked                                   ? T("locked", "закрыто")
                           : live                                   ? T("live", "на лету")
                                                                    : T("session", "до старта"));
        color(D.set_tag[i], locked ? kRed : live ? kGreen : kDim);
    }
    text(D.set_line, !g_set_line.empty() ? g_set_line
                     : T("* differs from the default. Live: the host can change it in play.",
                         "* отличается от стандарта. На лету: хост меняет и в игре."));
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

static void refresh_rivals()
{
    auto rows = rivals::ui_rows();
    // BUG-1565: compact guest starting-path / Space philosophy picker on the Rivals page (no lobby/Together impact)
    if (A.role() == 2 && history::can_pick_ui()) {
        const uint32_t m = A.mode ? A.mode() : 0;
        const int cur_st = m == modes::kSpace ? history::kSpace
                           : m == modes::kCiv ? history::kCiv
                           : m == modes::kTribe ? history::kTribe
                           : m == modes::kCreature ? history::kCreature
                                                   : -1;
        if (cur_st > history::kCell) {
            rows.push_back({T("Starting Path (Mid-Game)", "Начальный путь (с середины игры)"), "", nullptr, kGold});
            if (cur_st == history::kSpace) {
                const uint32_t sp = history::guest_card(history::kSpace);
                std::string label = std::string(T("Space Philosophy: ", "Философия в Космосе: ")) +
                                    history::card_label(history::kSpace, sp, g_ru);
                rows.push_back({label, T("Next >", "Далее >"), [] { history::cycle_philosophy(); }, kWhite});
            }
            static const char* const kStageEn[4] = {"Cell", "Creature", "Tribe", "Civ"};
            static const char* const kStageRu[4] = {"Клетка", "Существо", "Племя", "Цивилизация"};
            for (int st = 0; st < cur_st && st < 4; st++) {
                const uint32_t c = history::guest_card(st);
                std::string label = std::string(g_ru ? kStageRu[st] : kStageEn[st]) + ": " +
                                    history::card_label(st, c, g_ru);
                rows.push_back({label, T("Change", "Сменить"), [st] { history::cycle_stage_card(st); }, kDim});
            }
        }
    }
    int n = (int)rows.size(), pages = std::max(1, (n + kRivalsRows - 1) / kRivalsRows);
    if (g_rivals_first < 0)
        g_rivals_first = 0;
    if (g_rivals_first >= n)
        g_rivals_first = (pages - 1) * kRivalsRows;
    bool in_stage = rivals::valid_stage(rivals::current_stage());
    text(D.rivals_title, in_stage ? T("Race, Relations & Chronicle", "Гонка, отношения и хроника")
                                  : T("Rivals Stage Modes", "Режимы этапов: Соперники"));
    for (int i = 0; i < kRivalsRows; i++) {
        int k = g_rivals_first + i;
        bool have = k < n;
        show(D.rivals_text[i], have);
        D.rivals_action[i] = have ? rows[k].on_click : nullptr;
        show(D.rivals_button[i], have && !rows[k].button.empty() && rows[k].on_click);
        if (!have)
            continue;
        text(D.rivals_text[i], rows[k].text);
        color(D.rivals_text[i], rows[k].color ? rows[k].color : kWhite);
        text(D.rivals_button[i], rows[k].button);
        // BUG-1575: dynamically widen Rivals row button if localized caption (e.g. Russian/Hungarian/Spanish) exceeds 165 px
        if (!rows[k].button.empty() && rows[k].on_click && D.rivals_button[i]) {
            float want_w = std::min(230.f, std::max(165.f, std::round(caption_width(rows[k].button) + 42.f)));
            float bx1 = 720.f - want_w;
            Rect br = sui::area(D.rivals_button[i]);
            if (std::abs(br.x1 - bx1) > 0.5f) {
                br.x1 = bx1;
                sui::set_area(D.rivals_button[i], br);
            }
            if (D.rivals_text[i]) {
                auto& tc = g_cache[D.rivals_text[i]];
                float tx2 = bx1 - 8.f;
                if (std::abs(tc.box.x2 - tx2) > 0.5f) {
                    tc.box.x2 = tx2;
                    fit(D.rivals_text[i], tc);
                }
            }
        }
    }
    if (rows.empty())
        text(D.rivals_text[0], T("Nothing here yet", "Пока ничего"));
    show(D.rivals_text[0], true);
    show(D.rivals_prev, pages > 1);
    show(D.rivals_next, pages > 1);
    enable(D.rivals_prev, g_rivals_first > 0);
    enable(D.rivals_next, g_rivals_first + kRivalsRows < n);
    show(D.rivals_pageno, pages > 1);
    text(D.rivals_pageno, std::to_string(g_rivals_first / kRivalsRows + 1) + "/" + std::to_string(pages));
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

// tests (ui_golden's twin, docs/06-testing.md#testing-ui-golden): a named control moved by (dx, dy) or hidden,
// applied at the end of every refresh, so the page logic cannot put it back
struct Nudge {
    float dx = 0, dy = 0;
    float dw = 0; // BUG-1065's twin: the width changed by this much (a caption that no longer fits)
    bool hide = false;
    bool have_base = false;
    Rect base{};
};
static std::map<std::string, Nudge> g_nudges;

static void apply_test_nudges()
{
    for (auto& [name, n] : g_nudges) {
        auto it = g_named.find(name);
        if (it == g_named.end() || !it->second)
            continue;
        Win w = it->second;
        if (!n.have_base) {
            n.base = sui::area(w);
            n.have_base = true;
        }
        if (n.hide)
            sui::set_visible(w, false);
        else
            sui::set_area(w, Rect{n.base.x1 + n.dx, n.base.y1 + n.dy, n.base.x2 + n.dx + n.dw, n.base.y2 + n.dy});
    }
}

// ---------------- Spore-style hover tooltips (BUG-0874) ----------------
static std::string tooltip_for(const std::string& name)
{
    if (name == "nameok")
        return T("Save your display name and continue", "Сохранить имя и продолжить");
    if (name == "rename")
        return T("Change the name other players see", "Изменить имя, которое видят игроки");
    if (name == "gohost")
        return T("Host a co-op session in your world", "Создать совместную игру в своём мире");
    if (name == "gojoin")
        return T("Join a friend's co-op session", "Подключиться к совместной игре друга");
    if (name == "back")
        return T("Return to Host or Join", "Вернуться к выбору: создать или войти");
    if (name == "host")
        return T("Start hosting on this UDP port", "Открыть сервер на этом UDP-порту");
    if (name == "hostpweye")
        return T("Show or hide the session password", "Показать или скрыть пароль сессии");
    if (name == "join")
        return T("Connect to the host address", "Подключиться по указанному адресу");
    if (name == "joinpweye")
        return T("Show or hide the server password", "Показать или скрыть пароль сервера");
    if (name == "lanmore")
        return T("Show more games found on your network", "Показать другие игры в локальной сети");
    if (name == "recmore")
        return T("Show more recently joined servers", "Показать другие недавние серверы");
    if (name == "lan0" || name == "lan1" || name == "lan2")
        return T("Click to join this game on your network", "Нажмите, чтобы войти в эту сетевую игру");
    if (name == "rec0" || name == "rec1" || name == "rec2")
        return T("Click to fill the host address field", "Нажмите, чтобы подставить адрес хоста");
    if (name == "update")
        return T("Download and install the new SporeCoop version", "Скачать и установить новую версию SporeCoop");
    if (name == "report")
        return T("Save a zip with logs and diagnostics for bug reports", "Сохранить архив с логами для отчёта об ошибке");
    if (name == "settings")
        return T("View or change session rules and display options", "Правила сессии и настройки экрана");
    if (name == "ready")
        return T("Mark yourself ready for stage transitions and votes", "Отметить готовность к переходам и голосованиям");
    if (name == "share")
        return T("Send your current world save to all joined players", "Отправить сохранение мира всем игрокам");
    if (name == "leave")
        return T("Close this session or disconnect from the host", "Закрыть сессию или отключиться от хоста");
    if (name == "askreports")
        return T("Ask all joined players to send their problem reports", "Попросить всех игроков прислать отчёты");
    if (name == "send")
        return T("Send your chat message to everyone in the session", "Отправить сообщение в чат всем игрокам");
    if (name == "space")
        return T("Open the Space stage co-op status and actions", "Открыть окно совместного этапа Космос");
    if (name == "rivals")
        return T("Open Rivals mode settings, race progress, relations, and influence",
                 "Открыть настройки режима Соперники, прогресс гонки, отношения и влияние");
    if (name == "rbtn0" && g_rivals_page && g_rivals_first == 0)
        return rivals::config().preset() == rivals::Preset::AllRivals
                   ? T("Switch all stages to Together and restore Together default Settings",
                       "Переключить все этапы на Вместе и вернуть настройки по умолчанию")
                   : T("Switch all stages to Rivals and apply Rivals default Settings",
                       "Переключить все этапы на Соперников и применить настройки для соперников");
    if (name == "votemode")
        return T("Toggle Together or Rivals mode for the next stage",
                 "Переключить режим Вместе или Соперники для следующего этапа");
    if (name == "colour")
        return T("Pick your next player colour", "Выбрать следующий цвет игрока");
    if (name == "cprev" || name == "cnext")
        return T("Choose a different creation or role", "Выбрать другое творение или роль");
    if (name == "cupprev" || name == "cupnext")
        return T("Choose a different creature from My Creations", "Выбрать другое существо из Моих творений");
    if (name == "give")
        return T("Transfer the selected units or spice to this partner", "Передать выбранные юниты или специю напарнику");
    if (name == "givenext")
        return T("Pick another partner to receive the gift", "Выбрать другого напарника для передачи");
    if (name == "jump")
        return T("Fly to your partner's star system", "Перелететь в звёздную систему напарника");
    if (name == "rsend")
        return T("Pack and send your logs (.zip) to the host's report", "Собрать и отправить архив (.zip) с логами в отчёт хоста");
    if (name == "rnosend")
        return T("Decline sending your report this time", "Не отправлять ваш отчёт в этот раз");
    if (name == "setback" || name == "sback" || name == "rback")
        return T("Return to the co-op lobby", "Вернуться в лобби кооператива");
    if (name == "close")
        return T("Close the co-op window (press F9 to reopen)", "Закрыть окно кооператива (F9 - открыть снова)");
    // BUG-1575: localized hover tooltips on every Settings page row button (set0..set11)
    if (name.rfind("set", 0) == 0 && name.size() > 3 && std::isdigit((unsigned char)name[3])) {
        int row = atoi(name.c_str() + 3);
        int id = row >= 0 && row < kSetRows ? D.set_id[row] : -1;
        if (id == kLocalFps)
            return T("Limit FPS to reduce stutter or GPU load (Auto = game default)", "Ограничить FPS (Авто = стандарт игры)");
        if (id == kLocalBorderless)
            return T("Run the game in a borderless fullscreen window", "Запускать игру в окне без рамки");
        if (id == kLocalFov)
            return T("Wider camera in 3rd-person stages (Creature, Adventures)", "Увеличить угол обзора в 3-м лице (Существо, Приключения)");
        if (id == kLocalHideIp)
            return T("Mask IP addresses in lobby, join list, and during vote popups", "Скрывать IP в лобби, списке серверов и при голосованиях");
        if (id == kLocalLang)
            return T("Change the mod interface language (saved in coop.ini)", "Переключить язык интерфейса мода (сохраняется в coop.ini)");
        if (id >= 0 && id < settings::kCount) {
            const auto& d = settings::kDefs[id];
            return i18n::setting_help(d.key, d.help);
        }
    }
    return {};
}

static void tooltip_clear()
{
    g_tip_hover_btn.clear();
    if (!g_tip_shown_btn.empty() || !g_tip_shown_text.empty()) {
        g_tip_shown_btn.clear();
        g_tip_shown_text.clear();
        g_dirty = true;
    }
}

static void tooltip_tick(double now, bool clicked)
{
    if (!g_tooltips_fix || !g_want_open || !D.root || !sui::visible(D.root)) {
        tooltip_clear();
        g_tip_clicked = false;
        return;
    }
    if (clicked) {
        g_tip_clicked = true;
        if (!g_tip_shown_btn.empty()) {
            g_tip_shown_btn.clear();
            g_tip_shown_text.clear();
            g_dirty = true;
        }
    }
    int mx = 0, my = 0;
    bool have_move = mouse::last_move(mx, my);
    std::string hit;
    if (have_move) {
        for (const auto& [name, w] : g_named) {
            if (!w || !sui::visible(w) || tooltip_for(name).empty())
                continue;
            Rect r = sui::screen_rect(w);
            if (mx >= r.x1 && mx < r.x2 && my >= r.y1 && my < r.y2) {
                hit = name;
                break;
            }
        }
    }
    bool moved = (mx != g_tip_mx || my != g_tip_my);
    if (moved) {
        g_tip_mx = mx;
        g_tip_my = my;
        g_tip_clicked = false;
        g_tip_rest_since = now;
        g_tip_hover_btn = hit;
        if (!g_tip_shown_btn.empty()) {
            g_tip_shown_btn.clear();
            g_tip_shown_text.clear();
            g_dirty = true;
        }
        return;
    }
    if (hit != g_tip_hover_btn) {
        g_tip_hover_btn = hit;
        g_tip_rest_since = now;
        if (!g_tip_shown_btn.empty()) {
            g_tip_shown_btn.clear();
            g_tip_shown_text.clear();
            g_dirty = true;
        }
        return;
    }
    if (!g_tip_hover_btn.empty() && !g_tip_clicked && g_tip_shown_btn.empty() &&
        (now - g_tip_rest_since >= kTooltipDelay)) {
        g_tip_shown_btn = g_tip_hover_btn;
        g_tip_shown_text = tooltip_for(g_tip_shown_btn);
        g_dirty = true;
    }
}

static void refresh_tooltip()
{
    if (!D.tip_win)
        return;
    if (!g_tooltips_fix || !g_want_open || !D.root || !sui::visible(D.root) || g_tip_shown_btn.empty() ||
        g_tip_shown_text.empty()) {
        sui::set_visible(D.tip_win, false);
        return;
    }
    auto it = g_named.find(g_tip_shown_btn);
    if (it == g_named.end() || !it->second || !sui::visible(it->second)) {
        g_tip_shown_btn.clear();
        g_tip_shown_text.clear();
        sui::set_visible(D.tip_win, false);
        return;
    }
    Rect br = sui::area(it->second);
    float tw = std::min(kW - 24.f, std::max(140.f, std::round(caption_width(g_tip_shown_text) + 38.f)));
    float th = 32.f;
    float tx = std::max(12.f, std::min(kW - 12.f - tw, (br.x1 + br.x2 - tw) * 0.5f));
    float ty = br.y1 - th - 6.f;
    if (ty < 40.f)
        ty = br.y2 + 6.f;
    sui::set_area(D.tip_win, {tx, ty, tx + tw, ty + th});
    sui::set_caption(D.tip_win, g_tip_shown_text);
    sui::bring_to_front(D.root, D.tip_win);
    sui::set_visible(D.tip_win, true);
}

static void refresh_static_labels()
{
    if (!D.root)
        return;
    // Update static button/header labels on live language switch (BUG-1575) and widen Report button (BUG-1579:
    // +72 px accounts for Spore's 46 px pill borders + Cyrillic glyph width so "Сообщить об ошибке" never clips).
    std::string rcap = T("Report a problem", "Сообщить об ошибке");
    text(D.report_btn, rcap);
    if (D.report_btn) {
        float want_w = i18n::lang() == i18n::Lang::En
                           ? 210.f
                           : std::min(320.f, std::max(210.f, std::round(caption_width(rcap) + 72.f)));
        Rect rr = sui::area(D.report_btn);
        float rx1 = 720.f - want_w;
        if (std::abs(rr.x1 - rx1) > 0.5f) {
            rr.x1 = rx1;
            sui::set_area(D.report_btn, rr);
        }
        if (D.version) {
            auto& vc = g_cache[D.version];
            float vx2 = rx1 - 10.f;
            if (std::abs(vc.box.x2 - vx2) > 0.5f) {
                vc.box.x2 = vx2;
                fit(D.version, vc);
            }
        }
    }
    text(D.settings_btn, T("Settings", "Настройки"));
    text(D.space_btn, T("Space >", "Космос >"));
    text(D.rivals_btn, T("Rivals >", "Соперники >"));
    text(D.send_btn, T("Send", "Отправить"));
    text(D.ask_btn, T("Ask the players for their reports", "Попросить у игроков их отчёты"));
    text(D.rsend, T("Send", "Отправить"));
    text(D.rnosend, T("Don't send", "Не отправлять"));
    text(D.set_title, T("Session settings", "Настройки сессии"));
    text(D.set_back, T("< Lobby", "< Лобби"));
    text(D.space_title, T("Your galaxy together", "Галактика вместе"));
    text(D.space_back, T("< Lobby", "< Лобби"));
    text(D.rivals_title, T("Rivals & Race", "Соперники и гонка"));
    text(D.rivals_back, T("< Lobby", "< Лобби"));
    text(D.stage_hdr, T("Stage", "Этап"));
    text(D.back_btn, T("< Back", "< Назад"));
    text(D.lan_more, T("More >", "Ещё >"));
    text(D.rec_more, T("More >", "Ещё >"));
    text(D.name_ok, T("Continue", "Далее"));
    text(D.host_btn, T("Host", "Создать"));
    text(D.host_pw_lbl, T("Password", "Пароль"));
    text(D.join_btn, T("Join", "Подключиться"));
    text(D.join_pw_lbl, T("Password", "Пароль"));
    if (auto it = g_named.find("rename"); it != g_named.end())
        text(it->second, T("Change name", "Сменить имя"));
    if (auto it = g_named.find("gohost"); it != g_named.end())
        text(it->second, T("Host a game", "Создать игру"));
    if (auto it = g_named.find("gojoin"); it != g_named.end())
        text(it->second, T("Join a game", "Подключиться к игре"));
    if (g_menu_btn)
        text(g_menu_btn, T("Co-op", "Кооператив"));
}

static void refresh()
{
    g_pending = false;
    if (!g_ok)
        return;
    // a session leaves the window on the step of its role (the next session of a guest is a join again), also one
    // joined or hosted from the console while the window was closed
    if (int r = A.role())
        g_step = r == 2 ? kStepJoin : kStepHost;
    if (!g_name_first_fix && g_step == kStepName) // (switched off after the start: the 0.7.11 window, no name step)
        g_step = kStepChoose;
    ensure_menu_button();
    pause_menu_tick();
    if (g_want_open && !D.root) {
        build_dialog();
        g_lang_applied_rev = 0; // force initial label sizing (e.g. Report button width for non-English)
    }
    if (!D.root)
        return;
    if (g_lang_applied_rev != g_lang_rev + 1) {
        g_lang_applied_rev = g_lang_rev + 1;
        refresh_static_labels();
    }
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
    show(D.root, g_want_open && H->time_seconds() >= g_aside_until && !g_shot_hide);
    if (g_shot_hide && D.root && !sui::visible(D.root))
        g_shot_hidden_applied = true;
    if (!g_want_open) {
        if (D.tip_win)
            sui::set_visible(D.tip_win, false);
        if (A.lan_scan)
            A.lan_scan(false);
        return;
    }
    int role = A.role();
    bool lobby = role != 0;
    bool space = lobby && A.mode && A.mode() == modes::kSpace;
    bool rivals_on = rivals::enabled();
    if (!space)
        g_space_page = false;
    if (!lobby || !rivals_on)
        g_rivals_page = false;
    if (!lobby)
        g_settings_page = false;
    bool sett = lobby && g_settings_page && !g_space_page && !g_rivals_page;
    bool rpage = lobby && g_rivals_page && !g_space_page && !sett;
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
        show(w, lobby && !g_space_page && !sett && !rpage);
    for (Win w : D.page_settings)
        show(w, sett);
    for (Win w : D.page_space)
        show(w, lobby && g_space_page);
    for (Win w : D.page_rivals)
        show(w, rpage);
    bool show_sbtn = space && !g_space_page && !sett && !rpage;
    show(D.space_btn, show_sbtn);
    { // WP-R1 (BUG-0794): Rivals / Race button in lobby top bar (shares right side with Space > when in Space)
        bool show_rbtn = lobby && rivals_on && !g_space_page && !sett && !rpage;
        show(D.rivals_btn, show_rbtn);
        if (show_rbtn) {
            if (show_sbtn) {
                sui::set_area(D.rivals_btn, {480, 66, 590, 98});
                sui::set_area(D.space_btn, {596, 66, 720, 98});
            } else {
                sui::set_area(D.rivals_btn, {580, 66, 720, 98});
            }
            bool in_stage = rivals::valid_stage(rivals::current_stage());
            text(D.rivals_btn, in_stage ? T("Race >", "Гонка >") : T("Rivals >", "Соперники >"));
        } else if (show_sbtn) {
            sui::set_area(D.space_btn, {580, 66, 720, 98});
        }
    }
    { // BUG-1477
        const int lb = A.lineage_box ? A.lineage_box() : -1;
        show(D.lineage_btn, lobby && !g_space_page && !sett && !rpage && !space && lb >= 0);
        if (lb >= 0) {
            text(D.lineage_btn, lb ? T("[x] My old cells as wild species", "[x] Мои старые клетки как дикие виды")
                                   : T("[ ] My old cells as wild species", "[ ] Мои старые клетки как дикие виды"));
        }
    }
    if (!lobby || g_space_page || sett || rpage) {
        show(D.chat_up, false);
        show(D.chat_down, false);
    }
    text(D.subtitle, lobby && g_space_page ? T("CO-OP: SPACE", "КООПЕРАТИВ: КОСМОС")
                     : rpage               ? T("CO-OP: RIVALS", "КООПЕРАТИВ: СОПЕРНИКИ")
                     : sett                ? T("CO-OP: SETTINGS", "КООПЕРАТИВ: НАСТРОЙКИ")
                     : lobby               ? T("CO-OP LOBBY", "ЛОББИ КООПЕРАТИВА")
                                           : T("CO-OP", "КООПЕРАТИВ"));
    if (A.version_line)
        text(D.version, A.version_line());
    if (A.lan_scan)
        A.lan_scan(!lobby);
    if (lobby && g_space_page)
        refresh_space();
    else if (rpage)
        refresh_rivals();
    else if (sett)
        refresh_settings();
    else if (lobby)
        refresh_lobby();
    else
        refresh_connect();
    refresh_tooltip();
    if (!g_nudges.empty())
        apply_test_nudges();
}

static void refresh_task(void*) { refresh(); }

void init(const SkHost* host, SkMod* mod, Api api)
{
    H = host;
    M = mod;
    A = std::move(api);
    g_ok = sui::init(H, M);
    std::string lang = load_str("ui", "lang", "auto");
    set_lang(lang);
    g_hide_ip = load_str("ui", "hide_ip", "0") == "1";
    g_vote_auto_open = false;
    if (g_ok) {
        sui::set_state_provider(debug_state);
        sui::register_commands();
    }
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
    if (!on)
        g_vote_auto_open = false;
    else
        g_vote_auto_open = false; // explicit open by user/F9: show address unless streamer Hide IP is enabled
    if (g_want_open == on) {
        g_dirty = true;
        return;
    }
    g_want_open = on;
    if (on) {
        g_raise = true;
        if (D.root)
            sui::set_edit_text(D.name_edit, A.name()); // fresh values when the dialog appears
    } else {
        tooltip_clear();
    }
    g_dirty = true;
}

void open_for_vote()
{
    if (!g_want_open)
        g_vote_auto_open = true; // BUG-1572: mask IP whenever F9 pops up automatically for a vote
    if (g_want_open) {
        g_dirty = true;
        return;
    }
    g_want_open = true;
    g_raise = true;
    if (D.root)
        sui::set_edit_text(D.name_edit, A.name());
    g_dirty = true;
}

bool hide_ip() { return g_hide_ip; }

void set_hide_ip(bool on)
{
    g_hide_ip = on;
    save_str("ui", "hide_ip", on ? "1" : "0");
    g_dirty = true;
}

bool is_open() { return g_want_open; }

void test_reset()
{
    g_settings_page = false;
    g_space_page = false;
    g_rivals_page = false;
    g_rivals_first = 0;
    g_shot_hide = false;
    g_shot_hidden_applied = false;
    g_shot_tag.clear();
    g_shot_caption.clear();
    tooltip_clear();
    g_tip_clicked = false;
    if (D.host_pw_edit)
        sui::set_edit_text(D.host_pw_edit, "");
    if (D.join_pw_edit)
        sui::set_edit_text(D.join_pw_edit, "");
    open(false);
}

void open_lobby()
{
    g_settings_page = false;
    g_space_page = false;
    g_rivals_page = false;
    open(true);
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
bool game_modal_up() { return game_modal() != nullptr; }

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
    tooltip_tick(now, mouse::g_fix && lb);
    shot_hide_tick(now);
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
    g_shot_hide = false;
    g_shot_hidden_applied = false;
    g_shot_tag.clear();
    g_shot_caption.clear();
    tooltip_clear();
    if (D.tip_win)
        sui::set_visible(D.tip_win, false);
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
                    (A.role() ? (g_space_page ? "space" : g_rivals_page ? "rivals" : g_settings_page ? "settings" : "lobby")
                              : "connect") +
                    " step=" + steps[g_step & 3] +
                    " menu_button=" + (g_menu_btn ? "yes" : "no") + " in_menu=" + (in_menu() ? "1" : "0") +
                    " input_lock=" + std::to_string(input_lock()) + " modal=" + (game_modal() ? "1" : "0") +
                    " modal_seen=" + std::to_string(g_modal_seen) + " locked_clicks=" + std::to_string(g_locked_clicks) +
                    " locked_native=" + std::to_string(g_locked_native) +
                    " locked_typed=" + std::to_string(g_locked_typed) +
                    " share_enabled=" + (D.share_btn && (sui::flags(D.share_btn) & sui::kFlagEnabled) ? "1" : "0") +
                    " tooltip_btn=" + (g_tip_shown_btn.empty() ? std::string("none") : g_tip_shown_btn) + "\n";
    char b[256];
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    snprintf(b, sizeof b, "screen %d %d\n", sw, sh);
    s += b;
    if (D.root) { // the window's frame on the screen (tests: ui_golden crops it)
        Rect r = sui::screen_rect(D.root);
        snprintf(b, sizeof b, "window %.0f %.0f %.0f %.0f %d\n", r.x1, r.y1, r.x2, r.y2, (int)sui::visible(D.root));
        s += b;
        // the texts that vary between runs (ui_golden masks them): addresses, ports, pings, the version, chat, LAN
        std::vector<std::pair<std::string, Win>> areas = {
            {"session", D.session}, {"hostaddr", D.address}, {"version", D.version}, {"lanhdr", D.lan_hdr},
            {"statusline", D.status}, {"world", D.world}, {"creationline", D.creation}, {"creationupline", D.creation_up}, {"setline", D.set_line},
            {"meline", D.me_line}, {"hosthint", D.host_hint}};
        for (int i = 0; i < 4; i++)
            areas.push_back({"ping" + std::to_string(i), D.row_ping[i]});
        for (int i = 0; i < 5; i++)
            areas.push_back({"chatline" + std::to_string(i), D.chat_lines[i]});
        for (auto& [nm, w] : areas)
            if (w) {
                Rect a = sui::screen_rect(w);
                snprintf(b, sizeof b, "area %s %.0f %.0f %.0f %.0f %d\n", nm.c_str(), a.x1, a.y1, a.x2, a.y2,
                         (int)sui::visible(w));
                s += b;
            }
    }
    if (g_menu_btn && in_menu()) { // never a call on an item of a menu that is gone (BUG-0163)
        Rect r = sui::screen_rect(g_menu_btn);
        snprintf(b, sizeof b, "menu %.0f %.0f %.0f %.0f %d '%s'\n", r.x1, r.y1, r.x2, r.y2,
                 (int)sui::visible(g_menu_btn), sui::caption(g_menu_btn).c_str());
        s += b;
    }
    for (auto& [name, w] : g_named) {
        Rect r = sui::screen_rect(w);
        bool vis = sui::visible(w) && D.root && sui::visible(D.root);
        std::string cap;
        if (name == "hostpw" || name == "joinpw")
            cap.assign(sui::edit_text(w).size(), '*'); // BUG-0867: never leak raw password in uistate / logs
        else if (name == "name" || name == "address" || name == "port" || name == "chatedit")
            cap = sui::edit_text(w);
        else
            cap = sui::caption(w);
        snprintf(b, sizeof b, "%s %.0f %.0f %.0f %.0f %d '%s'\n", name.c_str(), r.x1, r.y1, r.x2, r.y2, (int)vis,
                 cap.c_str());
        s += b;
    }
    if (D.tip_win) { // BUG-0874: hover tooltip state for UI tests
        Rect tr = sui::screen_rect(D.tip_win);
        bool tvis = sui::visible(D.tip_win) && D.root && sui::visible(D.root);
        snprintf(b, sizeof b, "tooltip %.0f %.0f %.0f %.0f %d '%s'\n", tr.x1, tr.y1, tr.x2, tr.y2, (int)tvis,
                 tvis ? g_tip_shown_text.c_str() : "");
        s += b;
    }
    if (A.recent_servers) {
        auto recs = A.recent_servers();
        for (size_t i = 0; i < recs.size(); i++)
            s += "recent " + std::to_string(i) + " '" + recs[i] + "'\n";
    }
    for (int i = 0; i < 4; i++)
        if (D.row_name[i] && sui::visible(D.row_name[i]))
            s += "row " + std::to_string(i) + " '" + sui::caption(D.row_name[i]) + "' '" + sui::caption(D.row_color[i]) +
                 "' '" + sui::caption(D.row_stage[i]) +
                 "' '" + sui::caption(D.row_ping[i]) + "' '" + sui::caption(D.row_ready[i]) + "'\n";
    { // F2: the colour of each row's swatch (ARGB; tests)
        auto rows = A.players ? A.players() : std::vector<PlayerRow>{};
        for (size_t i = 0; i < rows.size() && i < 4; i++) {
            char cb[96];
            snprintf(cb, sizeof cb, "rowcolor %u id=%u argb=%08X\n", (unsigned)i, (unsigned)rows[i].id, rows[i].color);
            s += cb;
        }
    }
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
    s += "joinline '" + g_join_line + "'\n";
    s += "spage first=" + std::to_string(g_space_first) + " rows=" + std::to_string(kSpaceRows) + "\n";
    s += "rpage first=" + std::to_string(g_rivals_first) + " rows=" + std::to_string(kRivalsRows) + "\n";
    if (g_rivals_page) {
        for (int i = 0; i < kRivalsRows; i++)
            if (D.rivals_text[i] && sui::visible(D.rivals_text[i]))
                s += "rrow " + std::to_string(i) + " '" + sui::caption(D.rivals_text[i]) + "' '" +
                     (D.rivals_button[i] && sui::visible(D.rivals_button[i]) ? sui::caption(D.rivals_button[i]) : "") +
                     "'\n";
    }
    if (D.address)
        s += "hostaddr '" + sui::caption(D.address) + "'\n";
    s += "privacy hide_ip=" + std::string(g_hide_ip ? "1" : "0") + " vote_auto=" + (g_vote_auto_open ? "1" : "0") +
         " lang=" + i18n::lang_code(i18n::lang()) + "\n";
    for (int i = 0; i < kSetRows; i++) // the settings rows on the page now: key, value, editable, tag
        if (D.set_id[i] >= 0 && D.set_btn[i] && sui::visible(D.set_btn[i])) {
            int id = D.set_id[i];
            if (is_local_setting(id)) {
                const char* key = id == kLocalFps ? "display.frame_target"
                                  : id == kLocalBorderless ? "display.borderless"
                                  : id == kLocalFov ? "display.fov"
                                  : id == kLocalHideIp ? "display.hide_ip"
                                                       : "display.lang";
                int val = id == kLocalFps ? (A.frame_target ? A.frame_target() : 0)
                          : id == kLocalBorderless ? (A.borderless && A.borderless() ? 1 : 0)
                          : id == kLocalFov ? (A.fov_step ? A.fov_step() : 0)
                          : id == kLocalHideIp ? (g_hide_ip ? 1 : 0)
                                               : (int)i18n::lang();
                s += "setting " + std::to_string(i) + " " + key + " " + std::to_string(val) + " " +
                     ((sui::flags(D.set_btn[i]) & sui::kFlagEnabled) ? "1" : "0") + " '" + sui::caption(D.set_tag[i]) +
                     "'\n";
            } else {
                s += "setting " + std::to_string(i) + " " + settings::kDefs[id].key + " " +
                     std::to_string(settings::get((settings::Id)id)) + " " +
                     ((sui::flags(D.set_btn[i]) & sui::kFlagEnabled) ? "1" : "0") + " '" + sui::caption(D.set_tag[i]) +
                     "'\n";
            }
        }
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

bool russian() { return i18n::is_ru(); }

void set_russian(bool ru)
{
    i18n::set_lang(ru ? i18n::Lang::Ru : i18n::Lang::En, false);
    g_ru = i18n::is_ru();
    g_lang_rev++;
    g_dirty = true;
}

bool set_lang(const std::string& code)
{
    i18n::Lang l = i18n::Lang::En;
    bool is_auto = false;
    if (!i18n::parse_lang(code, l, is_auto))
        return false;
    if (is_auto)
        l = i18n::lang_from_win_primary((uint16_t)PRIMARYLANGID(GetUserDefaultUILanguage()));
    i18n::set_lang(l, is_auto);
    g_ru = i18n::is_ru();
    g_lang_rev++;
    g_dirty = true;
    return true;
}

std::string lang_code() { return i18n::lang_code(i18n::lang()); }

void set_test_color(uint32_t argb)
{
    g_test_color = argb;
    g_dirty = true;
}

void set_demo(const std::string& addr, const std::string& from, const std::string& to)
{
    g_demo_addr = addr;
    g_demo_from = from;
    g_demo_to = to;
    g_dirty = true;
}

std::string widgets_state()
{
    // every visible widget of the window (tests: layout checks): screen rect, kind (L label, B button), the width
    // the caption needs (caption_width) and the caption
    std::string s;
    if (!D.root || !sui::visible(D.root))
        return s;
    char b[512];
    for (auto& [w, c] : g_cache) {
        if (!sui::visible(w) || sui::parent(w) != D.root)
            continue;
        bool btn = g_btn_fn.count(w) != 0;
        std::string cap = sui::caption(w);
        Rect r = sui::screen_rect(w);
        snprintf(b, sizeof b, "widget %.0f %.0f %.0f %.0f %c %.0f '%s'\n", r.x1, r.y1, r.x2, r.y2, btn ? 'B' : 'L',
                 cap.empty() ? 0.f : caption_width(cap), cap.c_str());
        s += b;
    }
    return s;
}

std::string test_nudge(const std::string& name, float dx, float dy, bool hide, float dw)
{
    if (name == "off") { // every nudged control back to its place
        for (auto& [nm, n] : g_nudges) {
            auto it = g_named.find(nm);
            if (it != g_named.end() && it->second && n.have_base)
                sui::set_area(it->second, n.base);
        }
        g_nudges.clear();
        g_dirty = true;
        return "ui: no control nudged";
    }
    if (!g_named.count(name))
        return "ui: no control '" + name + "' (the window is built at its first open)";
    Nudge& n = g_nudges[name];
    n.dx = dx;
    n.dy = dy;
    n.dw = dw;
    n.hide = hide;
    g_dirty = true;
    char b[160];
    snprintf(b, sizeof b, "ui: %s %s (%.0f, %.0f) width %+.0f", name.c_str(), hide ? "hidden" : "moved by", dx, dy, dw);
    return b;
}

} // namespace coop_ui
