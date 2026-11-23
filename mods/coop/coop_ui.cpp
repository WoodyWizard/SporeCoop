#include "coop_ui.h"
#include "trace.h"
#include "net/utf8.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstring>
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
#include "typed_text.h"
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
// BUG-1062: "Give 10 to <player>" (Space) and "Give to <player>" kept a fixed width (150 / 170 px): a caption wraps
// when it needs more than the width minus ~30 px (docs/08-ui.md#ui-text-width), so "Give 10 to Carol" showed on two
// lines cut by the button's frame. Now the give button (and in Space the jump button left of it) is as wide as its
// caption needs, growing to the left. Off: the fixed widths (0.7.13).
static bool g_give_fit_fix = true;
COOP_FIX("core.ui_give_fit", "BUG-1062", g_give_fit_fix, "ui: the give and jump buttons are as wide as their captions");
static bool g_creation_hint_fix = true;
COOP_FIX("core.ui_creation_hint", "BUG-0598", g_creation_hint_fix, "ui: the creation row says whose creation it is and how to get a choice");
// BUG-9334 (roadmap-alpha-now: BUG-9294 left "a button for /eras"): on a galaxy server the Social Menu ends with
// "Players in other stages" and a Shown / Hidden button, the same setting as chat /eras on|off. Off: no row (chat only).
static bool g_eras_button = true;
COOP_FIX("galaxy.eras_button", "BUG-9334", g_eras_button, "ui: the Social Menu has a Shown / Hidden button for players in other stages (as /eras)");
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
// BUG-9220 (the owner 2026-11-15: "add a grey (2B2F36) and a blue, put them in appropriate locations"): gold for the
// actions, red for leaving / deleting / reporting, blue for the buttons that go to another page (Settings, Space >,
// < Lobby, the page arrows), grey for the small helpers (the color button, Copy Host Address, the chat and choice
// arrows, Refresh, Save Server, Check for Updates, Don't Send). Off: every button gold (red as before).
static void* g_grey_look, *g_blue_look;
static bool g_button_roles = true;
COOP_FIX("ui.button_roles", "BUG-9220", g_button_roles,
         "ui: page buttons blue, small helper buttons grey (the color button too); off: all gold");
static void* role_look(const std::string& name)
{
    if (!g_button_roles)
        return nullptr;
    static const char* const kBlue[] = {"settings", "space", "setback", "sback", "setprev", "setnext", "sprev", "snext"};
    static const char* const kGrey[] = {"colour", "copyinvite", "chatup", "chatdown", "playersup", "playersdown", "cprev", "cnext", "givenext",
                                        "refreshlan", "saveserver", "update", "rnosend"};
    for (const char* n : kBlue)
        if (name == n) return g_blue_look;
    for (const char* n : kGrey)
        if (name == n) return g_grey_look;
    return nullptr;
}
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
static int g_player_scroll;          // 6E B: the lobby's first player row shown (four rows, any player count)
static int g_row_ids[4] = {-1, -1, -1, -1}; // 6J: the lobby's four rows: their players (refresh_lobby)
constexpr size_t kChatCols = 70;     // letter cells of a chat line (680 px)

// BUG-9117: labels sized by measured character widths, aligned beside their fields; even rows; the
// difficulty field framed; the ping column wide enough; shorter update lines; a tidier lobby.
// `coop.fix ui.polish off`: the 0.7.8-lineages47 layout's text boxes (text_width below).
static bool g_label_width_fix = true;
COOP_FIX("ui.polish", "BUG-9117", g_label_width_fix, "ui: the co-op window's labels are sized to their text and aligned beside their fields");
// BUG-0904, BUG-0905, BUG-0908: labels center their text in their box (docs/08-ui.md), so a left-aligned label's box
// is fitted to the text's width; with one average glyph width the rows of a column started up to ~8 px apart, a
// caption overflowed its button and the lobby's Stage column touched the colour button. On: the advance of each glyph
// (each glyph's advance measured in game, docs/08-ui.md#ui-text-width), the short settings caption and a
// wider colour column. Off: the 0.7.12 pre-release layout.
static bool g_layout_fix = true;
COOP_FIX("core.ui_layout", "BUG-0905", g_layout_fix, "ui: labels fitted by the glyphs' widths, short Settings caption, room for the colour button");
// BUG-9219 (owner 2026-11-15, decision 4I A: Settings "looks like it's floating"): the Settings button is a row of
// the World & Session panel, in Ask for Reports' place (the host's Report Issue asks every guest too, BUG-0767); a
// guest's in the row of its Ready / Create button while that is hidden; no Settings while a vote runs (no free row).
// Off: Settings top left, the host's Ask for Reports row (upstream 0.7.12 F3 / F4).
static bool g_settings_in_panel = true;
// BUG-9221 (the owner 2026-11-15: "ensure consistent spacing", "Copy Host Address is nearly touching the menu border",
// Settings "way too close to exiting the background panel", the World & Session panel's left / right spacing; "usually
// stop hosting would be the button on the bottom right"; "Copy Host Address could be simplified to Copy Server IP").
// On: one grid on the panel image's boxes (Players 34..388, World & Session 406..725, both 156..290; chat 337..407):
// the panel's buttons 16 px in from both sides and 7 px clear of its bottom, 28 px tall with 5 px between; the bottom
// rows 32 px tall with 8 px between, the right edge on the boxes' 725, 16 px clear of the window's bottom; Stop Hosting
// / Leave bottom right, Copy Server IP (and a guest's Send / Don't Send) in the row above. Off: the 56-dev layout.
static bool g_lobby_grid = true;
COOP_FIX("ui.settings_in_panel", "BUG-9219", g_settings_in_panel,
         "ui: Settings in the World & Session panel instead of top left; no Ask for Reports button");
COOP_FIX("ui.lobby_grid", "BUG-9221", g_lobby_grid,
         "ui: the lobby on one spacing grid, Stop Hosting / Leave bottom right, Copy Server IP");
// the World & Session panel's rows (row 0 the first under its text line)
static Rect world_row(int i, float old_y = 0)
{
    if (!g_lobby_grid)
        return Rect{445, old_y, 706, old_y + 32};
    const float y = 189.f + i * 33.f;
    return Rect{422, y, 709, y + 28};
}
// BUG-9218: the own row's colour button (F2) covered the first letter of the own name. On: names start 6 px further
// right, the stage column 10 px. Off: the merge's layout (0.7.14.1 + Lineages 55-dev).
static bool g_color_gap_fix = true;
COOP_FIX("ui.color_gap", "BUG-9218", g_color_gap_fix, "ui: the player list's names start clear of the colour button");

// width of a text in the dialog font (+30: the box's margins), measured at 1080p (the caption widths of the give
// row, BUG-1062, and the label boxes with ui.polish off)
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
        else
            w += 11;
    }
    return w + 30;
}
float caption_width(const std::string& t) { return text_width(t) - 30; }

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

static float text_px(const std::string& t, bool small_font);
static std::map<Win, bool> g_small_font; // labels drawn with the button font (narrower)
constexpr float kLanTextInset = 10.f;     // the server list's text starts this far inside its row (BUG-9243)
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

// BUG-9243 (owner 2026-11-16, the Co-op window: "Searching is outside the box, and the update is way too close to the host
// setup. Text shouldn't wrap here, its too small of a space."): the server list's "Searching your network..." starts
// inside its row (kLanTextInset), the status line under the host box sits lower (centred on Check for Updates), and a
// status text longer than its line takes the narrower button font instead of wrapping onto a second line.
// Off (`coop.fix ui.connect_lines off`): 62-dev's places, wrapped.
static bool g_connect_lines_fix = true;
COOP_FIX("ui.connect_lines", "BUG-9243", g_connect_lines_fix,
         "ui: the Co-op window's server-list search line starts inside its row and the status line stays one line, clear of the host box");
static std::map<Win, bool> g_one_line; // labels that take the button font rather than wrap
static void one_line_font(Win w, const std::string& t, float width); // (after the window's fonts, D)
static void text(Win w, const std::string& t0)
{
    if (!w)
        return;
    const std::string t = demo_text(t0);
    auto& c = g_cache[w];
    if (c.text != t) {
        c.text = t;
        if (g_one_line.count(w)) one_line_font(w, t, c.box.x2 - c.box.x1);
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
static void* g_social_drawable;  // 6L A: the Social item's (the same rule as the Co-op item's)
static Win g_social_drawable_for;

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
    // decision 6L A: the Social item under Co-op (the two person icon), the Social Menu on a click
    if (Win old = sui::find(menu, kIdMenuButton + 2))
        sui::remove_child(menu, old);
    const float step = (r.y2 - r.y1) + 6;
    const Rect r2{r.x1, r.y1 + step, r.x2, r.y2 + step};
    if (Win b2 = sui::button_like(nullptr, share, r2, T("Social", "Друзья"))) {
        sui::set_ids(b2, kIdMenuButton + 2, kIdMenuButton + 2);
        if (g_social_drawable_for != menu) {
            g_social_drawable = nullptr;
            g_social_drawable_for = menu;
        }
        if (!g_social_drawable) {
            // the Co-op item's own icon (owner 2026-11-18: Co-op and Social "look very similar ... so they look the
            // same"): the white shape the game tints as its own items' icons
            void* icon = sui::image(sui::fnv("coop_icon"), 0x2F7D0004, sui::fnv("sporecoop"));
            g_social_drawable = sui::std_drawable_with_icon(sui::drawable(share), icon);
            if (icon && sui::g_release_fix) sui::release(icon);
        }
        if (g_social_drawable) sui::set_drawable(b2, g_social_drawable);
        sui::add_child(menu, b2);
        sui::add_handler(b2, [](Win, const sui::Msg& m) {
            if (!sui::is_click(m)) return false;
            toggle_social();
            return true;
        });
        if (sui::g_release_fix) sui::release(b2); // (the menu holds it; found by its id next time)
    }
    const Rect rv = r2; // the version line under the last item
    // the version under the item: a player's screenshot of the menu tells which SporeCoop runs
    if (Win old = sui::find(menu, kIdMenuButton + 1))
        sui::remove_child(menu, old);
    if (A.version_line) {
        std::string v = A.version_line();
        v = v.substr(0, v.find("  -  "));
        if (Win l = sui::label(menu, {rv.x1 + 6, rv.y2 - 4, rv.x2 + 90, rv.y2 + 14}, v, sui::font(share), kDim,
                               sui::kAlignLeft)) {
            sui::set_ids(l, kIdMenuButton + 1, 0);
            if (sui::g_release_fix)
                sui::release(l); // BUG-1025: the menu holds it, the mod keeps no pointer (found by its id next time)
        }
    }
    H->log(M, SK_INFO, "ui: co-op item added to the main menu");
}

// ---------------- dialog ----------------
// Lineages keeps its one connect page (BUG-9117: the name, the server list, joining and hosting side by side);
// upstream 0.7.12's connect steps (F1: a name step, Host or Join, a Host page, a Join page) replace that page and
// are not used. The lobby replaces the connect page while a session runs (role != 0).
static bool g_settings_page;  // the lobby shows the session settings instead
static uint32_t g_test_color;  // tests / layout shots (coop.uistate color <argb>): the colour of rows without one
static int g_set_first;       // the first settings entry shown (pages of kSetRows)
static std::string g_set_line; // the settings page's answer line (a refused change, a setting's help)
static uint32_t g_set_line_color = kDim;
constexpr int kSetRows = 11; // (13 upstream: the Lineages window is 600 px tall, the page buttons sit as the Space page's)

struct Dialog {
    Win root = nullptr, layer = nullptr;
    uint32_t font_body = 0, font_button = 0;
    Win like_button = nullptr; // gold "Login" button of the layout
    Win subtitle = nullptr;
    Win version = nullptr, connect_version = nullptr, report_btn = nullptr, report_caption = nullptr;
    // connect page
    std::vector<Win> page_connect;
    Win transfer_btn[2]{}, seat_btn[2]{}, seats_edit{}, difficulty_value[5]{}; // seat_btn: - and + (6E B)
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
    Win galaxy_row = nullptr, galaxy_name = nullptr, galaxy_line = nullptr; // decision 5Y A (alpha)
    std::map<Win, bool> hidden_fields;
    Win lan_track = nullptr, lan_thumb = nullptr;
    // lobby page
    std::vector<Win> page_lobby;
    Win session = nullptr, world = nullptr, chat_edit = nullptr, send_btn = nullptr;
    Win invite_btn = nullptr, rules = nullptr, transfer_mode = nullptr, preview_status = nullptr;
    Win ready_btn = nullptr, share_btn = nullptr, leave_btn = nullptr, leave_caption = nullptr, onward_btn = nullptr, cells_btn = nullptr;
    Win create_btn = nullptr; // BUG-9135 phase 4: a late joiner's new creature (tribe.late_create_editor)
    Win ask_btn = nullptr, settings_btn = nullptr;
    Win rask = nullptr, rsend = nullptr, rnosend = nullptr; // a guest asked for its report (F4, BUG-0767)
    Win rline = nullptr;                                     // the host: the answers while a round runs
    Win row_name[4] = {}, row_color[4] = {}, row_stage[4] = {}, row_ping[4] = {}, row_ready[4] = {};
    Win color_btn = nullptr; // F2: the own row's colour swatch as a button: the next colour
    Win chat_lines[5] = {};
    Win chat_up = nullptr, chat_down = nullptr, players_up = nullptr, players_down = nullptr;
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
static void one_line_font(Win w, const std::string& t, float width) // BUG-9243
{
    if (!D.font_button || !D.font_body) return;
    const bool small = g_connect_lines_fix && text_px(t, false) > width;
    if (small == (g_small_font.count(w) != 0)) return;
    sui::set_font(w, small ? D.font_button : D.font_body);
    if (small) g_small_font[w] = true;
    else g_small_font.erase(w);
}
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
    if (void* look = role_look(name)) // BUG-9220
        sui::set_drawable(w, look);
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

// decision 5Y A (alpha): the saved galaxy server address ([ui] galaxy_address), for the server list's line
std::string load_ui_str(const char* key) { return load_str("ui", key, ""); }

// the connect page's Username field (Join, Start Server and Enter in the field take it)
static void apply_name()
{
    std::string n = sui::edit_text(D.name_edit);
    while (!n.empty() && n.back() == ' ')
        n.pop_back();
    while (!n.empty() && n.front() == ' ')
        n.erase(0, 1);
    if (!n.empty() && n != A.name() && A.role() == 0) {
        A.set_name(n);
        save_str("coop", "name", n);
    }
}

static std::string g_join_line; // tests: the status line of the last join (a failed name lookup replaces it at once)
static double g_galaxy_close_at; // decision 5Y A (alpha): a galaxy Join waits for its welcome, then closes the window

static void do_join(const std::string& address)
{
    apply_name();
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
    const auto port = sui::edit_text(D.join_port);
    if (addr.find(':') == std::string::npos && !port.empty())
        addr += ":" + port;
    std::string err;
    // owner 2026-11-17 (alpha test): "I pressed join, but the co-op menu did not change ... I can't leave the server":
    // while connected to the galaxy (no planet session), Join reads "Leave Galaxy" and leaves it
    {
        std::string g_name, g_line;
        bool g_connected = false;
        if (A.galaxy_info && A.galaxy_disconnect && A.role() == 0 && A.galaxy_info(g_name, g_line, g_connected) && g_connected) {
            A.galaxy_disconnect();
            set_status("Left the galaxy.", kYellow);
            return;
        }
    }
    // decision 5Y A (alpha): the galaxy server's port: connect to the galaxy (its planets appear in the galaxy menu)
    if (A.galaxy_connect && addr.size() > 6 && addr.compare(addr.size() - 6, 6, ":47900") == 0) {
        if (!A.galaxy_connect(addr, err)) {
            set_status(std::string("Cannot connect to the galaxy server: ") + err, kRed, g_status_fix ? 30 : 0);
            return;
        }
        save_str("ui", "galaxy_address", addr);
        set_status("Connecting to the galaxy server: its planets appear in the galaxy menu.", kYellow);
        g_galaxy_close_at = H->time_seconds() + 20; // (the window closes once connected: the menu shows the planets)
        return;
    }
    if (!A.join(addr, err, sui::edit_text(D.join_password))) {
        set_status(std::string(T("Cannot join: ", "Не удалось подключиться: ")) + err, kRed, g_status_fix ? 30 : 0);
        return;
    }
    save_str("ui", "last_address", addr);
    // (no address in the line: the connect page can hide it, BUG-9117's privacy buttons)
    g_join_line = T("Connecting...", "Подключение...");
    set_status(g_join_line, kYellow);
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

// 6E B: a typed player-slot count, 1 .. founders::kSeatCeiling (0: not one)
static unsigned typed_seats(const std::string& t)
{
    if (t.empty() || t.size() > 3 || t.find_first_not_of("0123456789") != std::string::npos)
        return 0;
    const unsigned long n = strtoul(t.c_str(), nullptr, 10);
    return n >= 1 && n <= founders::kSeatCeiling ? unsigned(n) : 0;
}

static void do_host()
{
    apply_name();
    int port = atoi(sui::edit_text(D.port_edit).c_str());
    if (port <= 0 || port > 65535)
        port = A.default_port;
    std::string err;
    const unsigned typed = typed_seats(sui::edit_text(D.seats_edit));
    auto scaling = sui::edit_text(D.scaling_edit);
    if (!typed || scaling.empty() || scaling.find_first_not_of("0123456789") != std::string::npos ||
        scaling.size() > 3 || strtoul(scaling.c_str(),nullptr,10) > founders::kMaxDifficultyPercent) {
        const std::string n = std::to_string(founders::kSeatCeiling);
        set_status(std::string(T("Use 1-", "Укажите 1–")) + n +
                       T(" player slots and a difficulty from 0 to 400%.", " мест для игроков и сложность от 0 до 400%."),kRed);
        return;
    }
    D.seats = typed;
    auto seats = std::to_string(D.seats);
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
    if (g_bright_buttons && login) { // BUG-9179: every copy of the model button takes this look
        void* game_look = sui::drawable(login);
        void* gold = button_look(game_look, "gold");
        g_red_look = gold ? button_look(game_look, "red") : nullptr;
        g_grey_look = gold ? button_look(game_look, "grey") : nullptr;
        g_blue_look = gold ? button_look(game_look, "blue") : nullptr;
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
    D.lan_hdr = g_connect_lines_fix ? add_label(P, {35 + kLanTextInset, 177, 334, 202}, "", kDim) // (BUG-9243)
                                    : add_label(P, {37, 177, 334, 199}, "", kDim);
    D.lan_track = sui::image_window(root, sui::fnv("lineages_scroll_track"), sui::fnv("sporecoop"), 342, 178);
    D.lan_thumb = sui::image_window(root, sui::fnv("lineages_scroll_thumb"), sui::fnv("sporecoop"), 342, 178);
    for (Win part : {D.lan_track, D.lan_thumb}) {
        if (!part) continue;
        sui::set_flag(part, sui::kFlagIgnoreMouse, true);
        P.push_back(part);
    }
    if (A.galaxy_info) { // decision 5Y A (alpha): the galaxy server, pinned, two rows tall
        D.galaxy_row = add_button(P, "galaxyrow", {35, 177, 334, 228}, "", [] {
            const auto saved = load_str("ui", "galaxy_address", "");
            const auto colon = saved.rfind(':');
            sui::set_edit_text(D.addr_edit, colon == std::string::npos ? saved : saved.substr(0, colon));
            sui::set_edit_text(D.join_port, "47900");
            sui::set_edit_text(D.join_password, "");
            g_dirty = true;
        });
        // (the body font is the window's large one: the name in it, the line in the rows' small button font)
        D.galaxy_name = add_label(P, {35, 178, 334, 204}, "", kWhite, sui::kAlignCenter);
        D.galaxy_line = add_label(P, {35, 204, 334, 226}, "", kWhite, sui::kAlignCenter);
        if (D.galaxy_line && D.font_button)
            sui::set_font(D.galaxy_line, D.font_button);
        for (Win w : {D.galaxy_name, D.galaxy_line})
            if (w)
                sui::set_flag(w, sui::kFlagIgnoreMouse, true);
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
    privacy_button(P, "hideaddress", {678, 177, 718, 205}, D.addr_edit, true); // (owner 2026-11-17: the address and ports hidden by default)
    privacy_button(P, "hidejoinport", {678, 209, 718, 237}, D.join_port, true); // (owner 2026-11-17: the address and ports hidden by default)
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
    privacy_button(P, "hidehostport", {311, 373, 351, 399}, D.port_edit, true); // (owner 2026-11-17: the address and ports hidden by default)
    privacy_button(P, "hidehostpassword", {311, 402, 351, 428}, D.host_password, true);
    // 6E B (owner 2026-11-18, "Setting it to 17 shouldn't cause any issues"): any count up to founders::kSeatCeiling,
    // typed or stepped with - and + (the four buttons 1-4 before)
    D.seats = std::clamp(atoi(load_str("ui", "new_founders", "2").c_str()), 1, int(founders::kSeatCeiling));
    D.seats_edit = add_edit(P, "seats", {194, 431, 243, 457}, std::to_string(D.seats), 2, !panel);
    for (unsigned i = 0; i < 2; ++i)
        D.seat_btn[i] = add_button(P, i ? "seatmore" : "seatless", {140.f + i * 108, 431, 189.f + i * 108, 457},
                                   i ? "+" : "-", [i] {
            const unsigned now = typed_seats(sui::edit_text(D.seats_edit));
            const int next = std::clamp(int(now ? now : D.seats) + (i ? 1 : -1), 1, int(founders::kSeatCeiling));
            D.seats = unsigned(next);
            sui::set_edit_text(D.seats_edit, std::to_string(next));
            g_dirty = true;
        });
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
    D.status = g_connect_lines_fix ? add_label(P, {40, 547, 503, 571}, "", kYellow) // centred on Check for Updates (BUG-9243)
                                   : add_label(P, {40, 535, 503, 558}, "", kYellow);
    if (D.status) g_one_line[D.status] = true;
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
    // F3 (upstream 0.7.12): the session settings page; top left, across from Report Issue
    D.settings_btn = add_button(L, "settings", {40, 62, 215, 96}, T("Settings", "Настройки"), [] {
        g_settings_page = true;
        g_set_line.clear();
        g_dirty = true;
    });
    // owner 2026-11-12: "Player Slots" with the count and an Apply button (more slots now, fewer from the next stage)
    D.slots_label = add_label(L, {40, 496, 150, 528}, T("Player Slots", "Места"), kWhite);
    D.next_seats_edit = add_edit(L,"nextseats",{152,496,194,528},"",2); // (6E B: up to founders::kSeatCeiling)
    D.queue_seats = add_button(L,"queueseats",{198,493,302,531},T("Apply","Применить"),[] {
        const unsigned value = typed_seats(sui::edit_text(D.next_seats_edit));
        if (!value) {
            const std::string n = std::to_string(founders::kSeatCeiling);
            set_status(std::string(T("Enter 1 to ","Укажите от 1 до ")) + n + T(" player slots."," мест."),kRed,8);
            return;
        }
        std::string message;
        bool ok = A.queue_seats(value,message);
        set_status(message,ok ? kGreen : kRed,10);
        g_dirty = true;
    });
    D.session = add_label(L, {40, 103, 530, 129}, "", kWhite);
    D.invite_btn = add_button(L, "copyinvite", {475, 540, 720, 576}, g_lobby_grid ? T("Copy Server IP", "Копировать IP") : T("Copy Host Address", "Копировать адрес"), [] {
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
    add_label(L, {g_lobby_grid ? 412.f : 416.f, 131, 720, 156}, T("World & Session", "Мир и сессия"), kGold);
    D.rules = add_label(L, {422, 165, 710, 188}, "", kDim);
    D.transfer_mode = add_label(L, {416, 190, 710, 213}, "", kDim);
    D.preview_status = add_label(L, {40, 534, 438, 552}, "", kDim);
    for (int i = 0; i < 4; i++) {
        float y = 163.f + i * 30;
        // the colour swatch (F2), name and stage from the left, the ping ending at the column's right edge ("103 ms"
        // never wraps)
        D.row_color[i] = add_label(L, {45, y, 67, y + 26}, "", kDim);
        // BUG-9218: the colour button's pill is drawn a few pixels wider than its area and covered the name's first
        // letter: the name starts 6 px further right and the stage 10 px (a 14-letter "Name (host)" stays clear of
        // it; ui.color_gap)
        D.row_name[i] = add_label(L, {g_color_gap_fix ? 75.f : 69.f, y, g_color_gap_fix ? 196.f : 186.f, y + 26}, "", kWhite);
        D.row_stage[i] = add_label(L, {g_color_gap_fix ? 199.f : 189.f, y, 296, y + 26}, "", kWhite);
        D.row_ping[i] = add_label(L, {298, y, 362, y + 26}, "", kDim, sui::kAlignRight);
        D.row_ready[i] = add_label(L, {364, y, 382, y + 26}, "", kDim, sui::kAlignCenter);
        use_small_font(D.row_ping[i]);
    }
    // F2 (upstream 0.7.12): the own row's swatch is a button: the next colour (moved onto the own row by refresh_lobby)
    D.color_btn = add_button(L, "colour", {43, 165, 69, 187}, "\xE2\x96\xA0", [] {
        std::string line;
        bool ok = A.color_next && A.color_next(line);
        set_status(line, ok ? kGreen : kRed, 6);
    });
    if (D.color_btn) sui::center_caption(D.color_btn);
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
    // 6E B: more players than the four rows: the rows scroll (the arrows at the Players heading)
    D.players_up = add_arrow_button(arrows, "playersup", {300, 129, 334, 159}, kArrowUp, [] { g_player_scroll--; g_dirty = true; });
    D.players_down = add_arrow_button(arrows, "playersdown", {338, 129, 372, 159}, kArrowDown, [] { g_player_scroll++; g_dirty = true; });
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
    // F4 (upstream 0.7.12, BUG-0767): the host asks every guest for its problem report, any time (its Report Issue
    // asks too); a row of the World & Session panel, placed by refresh_lobby (hidden during a ready vote)
    D.ask_btn = add_button(L, "askreports", {445, 258, 706, 290}, T("Ask for Reports", "Запросить отчёты"), [] {
        std::string line = A.ask_reports ? A.ask_reports()
                                         : T("Not available in this version yet", "В этой версии пока недоступно");
        if (!line.empty())
            set_status(line, kYellow, 10);
    });
    // a guest asked by the host for its report: the question in the update line's place, Send / Don't Send where the
    // host has Copy Host Address; the host's answers while a round runs in the update line's place too
    D.rask = add_label(L, {40, 534, 470, 552}, "", kGold);
    g_named["rask"] = D.rask;
    D.rline = add_label(L, {40, 534, 438, 552}, "", kYellow);
    g_named["rline"] = D.rline;
    D.rsend = add_button(L, "rsend", {475, 540, 580, 576}, T("Send", "Отправить"), [] {
        if (A.report_answer)
            A.report_answer(true);
        g_dirty = true;
    });
    D.rnosend = add_button(L, "rnosend", {585, 540, 720, 576}, T("Don't Send", "Не отправлять"), [] {
        if (A.report_answer)
            A.report_answer(false);
        g_dirty = true;
    });
    if (g_lobby_grid) { // BUG-9221: the lobby's grid (see the switch)
        auto put = [](Win w, Rect r) { if (w) sui::set_area(w, r); };
        put(D.chat_edit, {40, 453, 565, 485});
        put(D.send_btn, {575, 453, 725, 485});
        put(D.creation_prev, {575, 415, 645, 445});
        put(D.creation_next, {655, 415, 725, 445});
        put(D.slots_label, {40, 493, 150, 525});
        put(D.next_seats_edit, {152, 493, 194, 525});
        put(D.queue_seats, {200, 493, 302, 525});
        put(D.space_btn, {310, 493, 500, 525});
        put(D.invite_btn, {508, 493, 725, 525});
        put(D.rsend, {508, 493, 612, 525});
        put(D.rnosend, {617, 493, 725, 525});
        put(D.leave_btn, {508, 533, 725, 565});
        put(D.leave_caption, {508, 533, 725, 565});
        put(D.rules, {422, 160, 709, 186});
    }

    // ---- settings page (upstream 0.7.12 F3: settings_core.h kDefs by group; docs/07-modes.md#modes-session-settings) ----
    // laid out as the Space page: the title clear of Report Issue, the page buttons above the version line
    auto& G = D.page_settings;
    D.set_title = add_label(G, {40, 104, 530, 130}, T("Session Settings", "Настройки сессии"), kGold);
    D.set_note = add_label(G, {40, 134, 720, 156}, "", kDim);
    for (int i = 0; i < kSetRows; i++) {
        float y = 160.f + i * 27;
        char nm[16];
        D.set_label[i] = add_label(G, {40, y + 1, 470, y + 25}, "", kWhite);
        snprintf(nm, sizeof nm, "set%d", i);
        D.set_btn[i] = add_button(G, nm, {480, y, 590, y + 25}, "", [i] { do_set_click(i); });
        D.set_tag[i] = add_label(G, {600, y + 1, 720, y + 25}, "", kDim);
        D.set_id[i] = -1;
    }
    D.set_line = add_label(G, {40, 460, 720, 484}, "", kDim);
    D.set_back = add_button(G, "setback", {40, 488, 240, 528}, T("< Lobby", "< Лобби"), [] {
        g_settings_page = false;
        g_dirty = true;
    });
    D.set_prev = add_arrow_button(G, "setprev", {500, 488, 570, 528}, kArrowLeft, [] {
        g_set_first = std::max(0, g_set_first - kSetRows);
        g_dirty = true;
    });
    D.set_pageno = add_label(G, {575, 494, 645, 522}, "", kDim, sui::kAlignCenter);
    D.set_next = add_arrow_button(G, "setnext", {650, 488, 720, 528}, kArrowRight, [] {
        g_set_first += kSetRows;
        g_dirty = true;
    });

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
    // Enter in a field: the chat sends, the address joins, the Username is taken
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
                apply_name();
                g_dirty = true;
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
    if (const unsigned typed = D.seats_edit ? typed_seats(sui::edit_text(D.seats_edit)) : 0)
        D.seats = typed; // (the preview below follows what is typed)
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
    // decision 5Y A (alpha): the galaxy server's row, pinned above the network games (they move two rows down)
    std::string g_name, g_line;
    bool g_connected = false;
    const bool galaxy = D.galaxy_row && A.galaxy_info && A.galaxy_info(g_name, g_line, g_connected);
    const int first_row = galaxy ? 2 : 0;
    if (galaxy) {
        const bool galaxy_selected = address.size() > 6 && address.compare(address.size() - 6, 6, ":47900") == 0;
        text(D.galaxy_name, g_name);
        text(D.galaxy_line, g_line);
        sui::set_shade_color(D.galaxy_row, galaxy_selected ? 0xFFFFFFFF : 0xFFADB6C2);
        if (galaxy_selected)
            details = g_connected ? "Galaxy server: connected" : "Galaxy server: Join connects to it";
    }
    show(D.galaxy_row, galaxy && scanning);
    // owner 2026-11-17: Join leaves the galaxy while connected; the window steps aside once a Join connected
    text(D.join_btn, galaxy && g_connected && A.role() == 0 ? "Leave Galaxy" : "Join");
    if (galaxy && g_connected && g_galaxy_close_at > 0) {
        g_galaxy_close_at = 0;
        notify::show("Connected to " + g_name + ": pick a planet in the galaxy menu.", "info");
        set_status("Connected to " + g_name + ".", kGreen);
        open(false);
    } else if (g_galaxy_close_at > 0 && H->time_seconds() > g_galaxy_close_at)
        g_galaxy_close_at = 0;
    show(D.galaxy_name, galaxy && scanning);
    show(D.galaxy_line, galaxy && scanning);
    text(D.selected_info, details);
    g_lan_max = g_lan_pages_fix ? std::max(0, (int)games.size() - (kLanRows - first_row)) : 0;
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
        int k = g_lan_first + i - first_row;
        bool have = i >= first_row && k < (int)games.size() && (g_lan_pages_fix || i < 3);
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
        for (size_t i = 0; i < ips.size() && i < 3; i++) // (three fit the box's two lines)
            hint += (i ? ",  " : "") + ips[i] + ":" + port;
    }
    if (!g_demo_addr.empty()) // README pictures: a documentation address
        hint = g_demo_addr;
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

bool g_give_hint_fix = true;
COOP_FIX("core.ui_give_hint", "BUG-0909", g_give_hint_fix, "ui: the give row shows only in Tribe, Civilization and Space");

static void refresh_lobby()
{
    static bool s_layout = g_layout_fix; // the switch changed: every left-aligned label is fitted anew
    if (s_layout != g_layout_fix) {
        s_layout = g_layout_fix;
        for (auto& [w, c] : g_cache)
            if (c.left)
                fit(w, c);
    }
    // (upstream's BUG-0908 Stage column shift belongs to its own lobby layout: the Lineages rows have their own columns)
    text(D.session, A.session_line());
    auto rows = A.players();
    const bool host = A.role() == 1;
    const bool vote = A.vote_active && A.vote_active();
    const auto rules = world::rules_for_current();
    const unsigned seats = host && A.seat_limit ? A.seat_limit() : rules.seats;
    show(D.invite_btn, host);
    // F4 (upstream 0.7.12, BUG-0767): a guest asked for its report, the host's round of answers; both take the update
    // line's place (and the guest's Send / Don't Send the place of the host's Copy Host Address)
    std::string rq;
    const int asked = !host && A.report_prompt ? A.report_prompt(rq) : 0;
    const bool round = host && A.report_round && A.report_round();
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
    text(D.preview_status, !asked && !round && has_update &&
                                   (!g_label_width_fix || (!preview.empty() && update_button != "Check for Updates"))
                               ? preview
                               : "");
    int own_row = -1;
    // 6E B: four rows show players g_player_scroll.. (the players, then the open slots)
    const int listed = std::max<int>((int)rows.size(), (int)std::min<unsigned>(seats, founders::kSeatCeiling));
    g_player_scroll = std::clamp(g_player_scroll, 0, std::max(0, listed - 4));
    show(D.players_up, g_player_scroll > 0);
    show(D.players_down, g_player_scroll + 4 < listed);
    for (int i = 0; i < 4; i++) {
        const int k = i + g_player_scroll;
        bool have = k < (int)rows.size();
        g_row_ids[i] = have ? rows[k].id : -1; // (6J: the right-click menu's rows)
        if (have && rows[k].me)
            own_row = i;
        const bool reserved = !have && static_cast<unsigned>(k) < seats;
        show(D.row_name[i], have || reserved);
        show(D.row_color[i], have && !(rows[k].me && D.color_btn && A.color_next)); // (the own row: its button)
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
        const auto& p = rows[k];
        // BUG-9222: the -NoShare- tag is not shown; BUG-9221: the name fits its column by the font's glyph widths
        // (1 unit = 1 px at 768 lines): " (host)" only while the whole name fits with it (the host is the first row
        // anyway), else a name too wide is cut with "..."
        std::string n = g_lobby_grid ? utf8::shown_name(p.name) : p.name;
        if (g_lobby_grid) {
            constexpr float kNameWidth = 116; // the column 75..196, a clear gap before the stage at 199
            const std::string suffix = p.host ? T(" (host)", " (хост)") : "";
            if (caption_width(n + suffix) <= kNameWidth)
                n += suffix;
            else if (caption_width(n) > kNameWidth) {
                while (utf8::count(n) > 1 && caption_width(n + "...") > kNameWidth)
                    n = utf8::head(n, utf8::count(n) - 1);
                n += "...";
            }
        } else if (p.host && n.size() <= 12)
            n += T(" (host)", " (хост)");
        text(D.row_name[i], n);
        color(D.row_name[i], p.me ? kGold : p.buddy ? 0xFF8FD3FFu : kWhite); // (6J: a friend in light blue)
        // the player's colour (F2 of upstream 0.7.12 assigns it): a swatch in that colour, "-" while it has none; the
        // own row's swatch is the colour button
        uint32_t pc = p.color ? p.color : g_test_color ? g_test_color : 0;
        text(D.row_color[i], pc ? "\xE2\x96\xA0" : "-");
        color(D.row_color[i], pc ? (pc | 0xFF000000u) : kDim);
        if (p.me && D.color_btn) {
            float y = 163.f + i * 30;
            if (std::abs(sui::area(D.color_btn).y1 - (y + 2)) > 0.5f)
                sui::set_area(D.color_btn, {43, y + 2, 69, y + 24});
            color(D.color_btn, pc ? (pc | 0xFF000000u) : kDim); // the caption's square in the own colour
        }
        text(D.row_stage[i], p.stage == "Galaxy menu" ? T("Galaxy", "Галактика") : p.stage);
        text(D.row_ping[i], p.me ? "" : (p.ping_ms >= 0 ? std::to_string(std::min(p.ping_ms, 9999)) + T(" ms", " мс") : "?"));
        text(D.row_ready[i], p.ready ? "+" : "-");
        color(D.row_ready[i], p.ready ? kGreen : kDim);
    }
    show(D.color_btn, own_row >= 0 && A.color_next);
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
    g_chat_scroll = std::max(0, std::min(g_chat_scroll, (int)chat.size() - 4));
    size_t first = chat.size() > 4 ? chat.size() - 4 - (size_t)g_chat_scroll : 0;
    static uint32_t s_chat_col[4] = {kWhite, kWhite, kWhite, kWhite};
    static Win s_chat_w[4] = {};
    for (int i = 0; i < 4; i++) {
        text(D.chat_lines[i], first + i < chat.size() ? chat[first + i] : "");
        uint32_t c = first + i < chat.size() ? chat_col[first + i] : kWhite;
        if (D.chat_lines[i] && (c != s_chat_col[i] || D.chat_lines[i] != s_chat_w[i])) {
            sui::set_label_color(D.chat_lines[i], c);
            s_chat_col[i] = c;
            s_chat_w[i] = D.chat_lines[i];
        }
    }
    show(D.chat_up, first > 0);
    show(D.chat_down, g_chat_scroll > 0);
    int ck = 0, cown = 0;
    bool hint = false; // the creation row says how to get a choice: it may take the arrows' room
    std::string cname, cl;
    bool one = A.one_creature && A.one_creature(); // F5 (upstream BUG-0769): the host's creature for everybody
    if (late && A.creation && A.creation(ck, cname, cown)) { // the creature selector asks for their own creature
        if (cown <= 0) {
            cl = T("No creatures of your own yet: make one in a creature editor", "Своих существ пока нет: создайте его в редакторе");
            hint = true;
        } else
            cl = std::string(late == 1 || late == 3 ? T("Choose your own creature for this game: ", "Выберите своё существо для этой игры: ")
                                       : T("Your creature for this game: ", "Ваше существо в этой игре: ")) +
                 (cname.empty() ? std::string(T("none yet", "пока нет")) : cname);
    } else if (A.creation && A.creation(ck, cname, cown)) {
        if (one && ck != 2)
            cown = 0;
        cl = ck == 1 ? T("My cell: ", "Моя клетка: ") : ck == 2 ? T("My ship: ", "Мой корабль: ") : T("My creature: ", "Моё существо: ");
        bool me_host = A.role() == 1;
        if (!cname.empty())
            cl += cname;
        else if (!g_creation_hint_fix)
            cl += T("the world's own", "из мира хоста");
        else // BUG-0598: say whose it is
            cl += ck == 2 ? T("your empire's ship", "корабль вашей империи")
                  : me_host ? T("your world's species", "вид вашего мира") : T("the host's species", "вид хоста");
        if (one && ck != 2)
            cl += T(" (the host evolves it for everybody)", " (хост развивает его для всех)");
        else if (ck != 2 && cown > 0)
            cl += std::string(T("  (in My Creations: ", "  (своих: ")) + std::to_string(cown) + ")";
        else if (g_creation_hint_fix && ck != 2 && !me_host) { // no arrows: how to get a choice
            cl += T(" (make one in an editor to pick it)", " (создайте своё в редакторе)");
            hint = true;
        }
    }
    std::string gwhat, gto;
    int gn = A.give ? A.give(gwhat, gto) : 0;
    bool space = gwhat == "spice";
    // only where there are units to give (Tribe, Civ, Space): at the galaxy menu, in Cell or Creature the line "Select
    // your units to give them to a partner" meant nothing (BUG-9117 here, BUG-0909 upstream: either switch hides it)
    const bool give_ctx = A.give && !gto.empty() && (!gwhat.empty() || (!g_label_width_fix && !g_give_hint_fix)) &&
                          (cl.empty() || space);
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
    // BUG-1050: in Space with 3-4 players ">" comes back; the jump button holds its Tribe / Civ place (520..560), so
    // the row is cut anew there: jump 400..515, Give 520..690, ">" 694..720 (the window's inner edge, as Send). The
    // Lineages row is 415..445 (under the chat box).
    bool pick = giving && gn > 0 && A.give_choices && A.give_choices() > 1;
    bool pick_space = pick && space && A.give_pick_space && A.give_pick_space();
    auto place = [](Win w, const Rect& r) {
        if (w && (std::abs(sui::area(w).x1 - r.x1) > 0.5f || std::abs(sui::area(w).x2 - r.x2) > 0.5f))
            sui::set_area(w, r);
    };
    std::string give_cap = space ? std::string(T("Give 10 To ", "Дать 10: ")) + gto : std::string(T("Give To ", "Отдать ")) + gto;
    std::string jto;
    bool jump = A.jump && A.jump(jto);
    std::string jump_cap = std::string(T("Jump To ", "К звезде: ")) + jto;
    Rect r_next = pick_space ? Rect{694, 415, 720, 445} : Rect{520, 415, 560, 445};
    Rect r_give = pick_space ? Rect{520, 415, 690, 445} : Rect{570, 415, 720, 445};
    Rect r_jump = pick_space ? Rect{400, 415, 515, 445} : Rect{400, 415, 560, 445};
    if (g_give_fit_fix) { // BUG-1062: right to left, each button as wide as its caption needs (+48: the button's text
        // borders are 23 px left and right, IButton +0x50: docs/08-ui.md#ui-give-fit)
        auto need = [](const std::string& c, float base) { return std::max(base, std::round(caption_width(demo_text(c)) + 48)); };
        r_give.x1 = r_give.x2 - need(give_cap, r_give.x2 - r_give.x1);
        if (pick && !space) // Tribe / Civ: ">" left of the give button
            r_next = Rect{r_give.x1 - 50, 415, r_give.x1 - 10, 445};
        float jx2 = std::min(r_jump.x2, (giving && gn > 0 ? r_give.x1 : 720.f) - 5);
        r_jump = Rect{jx2 - need(jump_cap, r_jump.x2 - r_jump.x1), 415, jx2, 445};
    }
    place(D.give_next, r_next);
    place(D.give_btn, r_give);
    place(D.jump_btn, r_jump);
    show(D.give_next, pick && (!space || pick_space));
    text(D.give_btn, give_cap);
    show(D.jump_btn, jump);
    text(D.jump_btn, jump_cap);
    bool can = (!cl.empty() && !giving && ck != 2 && cown > 0) || roles;
    show(D.creation_prev, can);
    show(D.creation_next, can);
    // BUG-0516 (upstream): the host evolves without a vote and offers this guest its own editor: the Ready button reads
    // "Evolve Now" (shown outside a vote too while the offer holds)
    const int offer = A.evolve_offer && !one && !host ? A.evolve_offer() : 0;
    show(D.ready_btn, vote || offer > 0);
    text(D.ready_btn, offer > 0 ? T("Evolve Now", "Эволюция") : A.ready() ? T("Not Ready", "Не готов") : T("I'm Ready", "Я готов"));
    const bool create = late && !vote && offer <= 0 && !host && A.create_creature_offered && A.create_creature_offered();
    show(D.create_btn, create);
    // BUG-9111, owner 2026-11-12: the host's rows from the top: the vote's Start Now (only while a vote runs), Player
    // Cells, Foreign Creations (hidden during a vote), the vote's Ready (outside a vote: Ask for Reports, F4)
    const bool start_now = host && vote;
    const bool cells = host && A.lineage_cells;
    show(D.share_btn, start_now);
    sui::set_area(D.share_btn, world_row(0, 190));
    float row = 190;
    int row_i = 0;
    auto next_row = [&row, &row_i] { Rect r = world_row(row_i++, row); row += 34; return r; };
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
        else if (!host) sui::set_area(D.onward_btn, g_lobby_grid ? world_row(0) : Rect{445, 212, 706, 248});
        show(D.onward_btn, onward);
        text(D.onward_btn, A.foreign_creations && A.foreign_creations() ? T("Foreign Creations: On", "Чужие создания: Вкл")
                                                                        : T("Foreign Creations: Off", "Чужие создания: Выкл"));
    }
    const Rect ready_row = host ? next_row() : g_lobby_grid ? world_row(1) : Rect{445, 249, 706, 285};
    if (g_lobby_grid && D.create_btn && std::abs(sui::area(D.create_btn).y1 - ready_row.y1) > 0.5f)
        sui::set_area(D.create_btn, ready_row);
    sui::set_area(D.ready_btn, ready_row);
    show(D.ask_btn, host && !vote && !g_settings_in_panel);
    if (host && D.ask_btn)
        sui::set_area(D.ask_btn, ready_row);
    enable(D.ask_btn, !round);
    if (g_settings_in_panel && D.settings_btn) { // BUG-9219: Settings in the panel's free row (none during a vote)
        const bool free_row = host ? !vote : !(vote || offer > 0 || create);
        if (free_row && std::abs(sui::area(D.settings_btn).y1 - ready_row.y1) > 0.5f)
            sui::set_area(D.settings_btn, ready_row);
        show(D.settings_btn, free_row);
    }
    enable(D.share_btn, true);
    const auto leave_title = host ? T("Stop Hosting", "Закрыть игру") : T("Leave", "Выйти");
    text(D.leave_btn, leave_title);
    text(D.leave_caption, leave_title);
    // F4 (upstream 0.7.12, BUG-0767): the guest's question and the host's line about the round; what is sent and how a
    // round ended go into the status line
    show(D.rask, asked);
    show(D.rsend, asked);
    show(D.rnosend, asked);
    if (asked)
        text(D.rask, rq);
    show(D.rline, round); // the answers while the round runs ("Reports: 1 of 3 answered"); the status keeps "Report saved"
    std::string rl = host ? (A.report_line ? A.report_line() : "")
                   : asked ? T("Sent: logs, a game summary, screenshots, crash files (paths masked)",
                               "Отправится: логи, сводка, снимки, сбои (пути скрыты)")
                           : rq;
    if (round)
        text(D.rline, rl);
    else if (rl != g_report_seen) {
        g_report_seen = rl;
        if (!rl.empty())
            set_status(rl, asked ? kWhite : rl.find("refused") != std::string::npos ? kRed : kGreen, asked ? 0 : 60);
    }
    // (upstream's lobby address line, "Friends connect to: <ip:port>" / "Host address: ...", is not shown: the Lineages
    // lobby keeps addresses off the screen and has Copy Host Address, BUG-9117)
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

// a setting's value as its button shows it: On / Off, a named value (creature.mode: Own / Host's), a number
static std::string value_label(int id, int v)
{
    std::string val = settings::value_text(id, v);
    return val == "on" ? T("On", "Вкл") : val == "off" ? T("Off", "Выкл") : val == "own" ? T("Own", "Свои")
           : val == "host" ? T("Host's", "Хоста") : val;
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
        g_set_line = std::string(d.label) + ": " + value_label(id, nv);
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
        text(D.set_btn[i], value_label(id, v));
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

static void refresh()
{
    text(D.report_btn, T("Report Issue", "Сообщить об ошибке"));
    text(D.report_caption, T("Report Issue", "Сообщить об ошибке"));
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
    bool villages = lobby && g_village_page && A.mode && A.mode() == modes::kTribe && tribe::village_orders(); // BUG-9153
    bool nations = lobby && civ::offline_control_available();
    if (!space && !villages && !nations)
        g_space_page = false;
    if (!lobby)
        g_settings_page = false;
    bool sett = lobby && g_settings_page && !g_space_page;
    for (Win w : D.page_connect)
        show(w, !lobby);
    show(D.status, !lobby);
    if (lobby)
        show(D.update_btn, false);
    for (Win w : D.page_lobby)
        show(w, lobby && !g_space_page && !sett);
    for (Win w : D.page_settings)
        show(w, sett);
    for (Win w : D.page_space)
        show(w, lobby && g_space_page);
    bool seat_controls = lobby && A.role() == 1 && !g_space_page && !sett && A.mode &&
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
    show(D.space_btn, (space || villages || nations) && !g_space_page && !sett);
    text(D.space_btn, nations ? "Nations >" : villages ? "Village >" : T("Space >", "Космос >"));
    if (!lobby || g_space_page || sett) {
        show(D.chat_up, false);
        show(D.chat_down, false);
        show(D.players_up, false);
        show(D.players_down, false);
    }
    text(D.subtitle, lobby && g_space_page ? (nations ? "NATIONS" : villages ? "VILLAGES" : T("SPACE", "КОСМОС"))
                     : sett ? T("SETTINGS", "НАСТРОЙКИ")
                     : lobby ? T("LOBBY", "ЛОББИ") : "");
    if (A.version_line) {
        auto version = A.version_line();
        if (!g_space_page) version = version.substr(0, version.find("  -  "));
        text(D.version, version);
        text(D.connect_version, version);
    }
    show(D.version, lobby);
    const Rect report_area = lobby ? (g_lobby_grid ? Rect{550, 98, 725, 132} : Rect{545, 98, 720, 134})
                                   : Rect{545, 107, 720, 142};
    for (Win control : {D.report_btn, D.report_caption})
        if (control && (sui::area(control).y1 != report_area.y1 || sui::area(control).x1 != report_area.x1))
            sui::set_area(control, report_area);
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
    if (!g_nudges.empty())
        apply_test_nudges();
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
// 54 (2026-11-15, BUG-9211): off by default: after Accept the new page drew Spore's Options menu over the Settings
// (its buttons as pills, the quality table's buttons and Defaults hidden); the original controls stay well. On again
// once that is understood.
static bool g_settings_layout = false;
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
static bool g_settings_once = true;
COOP_FIX("ui.settings_once", "BUG-9211", g_settings_once,
         "ui: Spore's Settings dialog is rearranged once, not again when it is opened after Accept");
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

// the Borderless control: the new layout's check box (a small toggle), else the old layout's wide button with a caption
// ("Borderless: On"); told by the width, not by the caption (BUG-9211: a button with no caption yet stayed blank)
static bool borderless_box(Win w) { return w && sui::area(w).w() < 60; }
static bool borderless_on(Win w)
{
    return borderless_box(w) ? sui::button_selected(w) : sui::caption(w) == "Borderless: On";
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
    // BUG-9211: the dialog grows through its root (a small window holding Accept, Cancel, the frame and the backgrounds,
    // docs/re/graphics.md#graphics-native-scale). Any other parent (a dialog made again after Accept) would move what
    // else it holds (the Options menu showed over Settings): the old controls instead.
    if (g_settings_once) {
        Win root = sui::parent(settings);
        const sui::Rect ra = root ? sui::area(root) : sui::Rect{};
        if (!root || !sui::find(root, 0x043B7A18) || ra.w() > 200 || ra.h() > 200 || sui::children(root).size() > 12) {
            H->log(M, SK_WARN, "ui: Graphic page layout: the dialog's root is not the one we know (%.0f x %.0f, %zu windows): "
                   "the old controls instead (BUG-9211)", ra.w(), ra.h(), root ? sui::children(root).size() : 0);
            return false;
        }
    }
    // even spacing (the owner 2026-11-13: "a little inconsistent with spacing"): 8 px between the boxes, rows 8 px
    // under a box's title band (30 px) and 10 px above its bottom; the two check boxes centred in theirs
    constexpr float kGrow = 72, kShift = 65, kBoxH = 98;
    // BUG-9211: after Accept the game drops our controls but keeps the dialog it was built on: the page is built again
    // and the moves below (relative) were made twice (the quality table's buttons, Defaults and Full Screen moved off,
    // the resolution list open). The Screen Size box at our height says the dialog has them already.
    const bool again = g_settings_once && std::abs(sui::area(screen_box).h() - kBoxH) < 0.5f;
    if (again) H->log(M, SK_INFO, "ui: Graphic page: the dialog was laid out before, only our controls again (BUG-9211)");
    if (!again) {
        grow_settings_dialog(settings, kGrow);
        for (Win c : sui::children(panel)) // everything under the quality table makes room for the Interface box
            if (sui::area(c).y1 >= 240) move_win(c, kShift);
    }
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
    auto visible = [](Win w) {
        if (!w) return false;
        for (; w; w = sui::parent(w))
            if (!sui::visible(w)) return false;
        return true;
    };
    // BUG-9211: after Accept the game makes a new Settings dialog and keeps the old one hidden (two windows with this
    // id; seen 2026-11-15): the first found was the old one, so the new one's page was laid out over the old one's
    // state. The visible one is the one shown (else the first, as before).
    Win settings = sui::find(sui::main_window(), kSettings);
    if (g_settings_once && settings && !visible(settings)) { // (a walk of the UI tree: no pointer kept between calls)
        std::vector<Win> stack{sui::main_window()};
        for (int n = 0; !stack.empty() && n < 30000; n++) {
            Win w = stack.back();
            stack.pop_back();
            if (sui::control_id(w) == kSettings && visible(w)) {
                settings = w;
                break;
            }
            for (Win c : sui::children(w)) stack.push_back(c);
        }
    }
    Win panel = settings ? sui::find(settings, kGraphicPanel) : nullptr;
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
        if (borderless_box(borderless)) // the check box: off while Full Screen Mode is on (the two exclusive)
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
    if (borderless_box(borderless)) { // Borderless Mode and Full Screen Mode exclude each other (the owner)
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

// tests (`coop.ui first`): the window as at a first start. Lineages has no name step (its connect page has the
// Username field): the connect page with the current name and no status
void first_start()
{
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

static bool g_cover_up; // BUG-9318
void set_cover_up(bool up) { g_cover_up = up; }
bool cover_up() { return g_cover_up; }

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
        {"seatless", "Fewer player slots for a new campaign", "Меньше мест в кампании"},
        {"seatmore", "More player slots for a new campaign", "Больше мест в кампании"},
        {"hideaddress", "Show or hide", "Показать / скрыть"},
        {"hidejoinport", "Show or hide", "Показать / скрыть"},
        {"hidejoinpassword", "Show or hide", "Показать / скрыть"},
        {"hidehostport", "Show or hide", "Показать / скрыть"},
        {"hidehostpassword", "Show or hide", "Показать / скрыть"},
        {"hidehostaddresses", "Show or hide addresses", "Показать / скрыть адреса"},
        {"copyinvite", "Copy your server's IP for friends", "Копировать адрес"},
        {"queueseats", "Apply: more slots now, fewer from the next stage", "Применить: больше мест сразу, меньше со следующего этапа"},
        {"chatup", "Older messages", "Ранние сообщения"},
        {"chatdown", "Newer messages", "Новые сообщения"},
        {"playersup", "Earlier players", "Предыдущие игроки"},
        {"playersdown", "More players", "Ещё игроки"},
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
        // upstream 0.7.12's lobby buttons (F2 colour, F3 settings, F4 reports)
        {"colour", "Next color", "Следующий цвет"},
        {"settings", "Session settings", "Настройки сессии"},
        {"askreports", "Ask every player for a problem report", "Попросить отчёты у игроков"},
        {"rsend", "Send your report to the host", "Отправить отчёт хосту"},
        {"rnosend", "Keep your report", "Не отправлять отчёт"},
        {"setback", "Back to the Lobby", "Назад в лобби"},
        {"setprev", "Previous page", "Предыдущая страница"},
        {"setnext", "Next page", "Следующая страница"},
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

void open_lobby()
{
    g_settings_page = false;
    g_space_page = false;
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

static void pm_hover_tick(); // 6J (the players' menu, below)
static void so_hover_tick(); // 6L A (the Social Menu, below)
void tick()
{
    pm_hover_tick(); // 6J: the menu item under the pointer
    so_hover_tick(); // 6L A: the Social Menu's button under the pointer
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
    static uint32_t s_f8_seen = mouse::key_presses(VK_F8);
    if (mouse::key_pressed(VK_F8, s_f8_seen)) // 6L A: the Social Menu
        toggle_social();
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
        Rect r = sui::screen_rect(g_menu_btn); // (the click as remapped: layout places, BUG-9238)
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

// ---------------- the memory popup (decision 4S, BUG-9239) ----------------
// Spore keeps what it loads until it is closed (a 32-bit game, 2 GB); near the end the player gets a box in the middle of
// the screen: the owner's text, a live bar of how full the memory is (green, yellow from 20 % left, red from 10 %) and
// two buttons, Ignore and Continue (save, restart Spore, reconnect: mem_refresh.cpp). Not scaled with Spore's UI.
static struct {
    Win root = nullptr, box = nullptr, edge[4] = {}, title = nullptr, msg[2] = {}, dim[3] = {}, lit[3] = {},
        line = nullptr, ignore = nullptr, cont = nullptr;
    bool failed = false;
} g_mp;
static MemoryPopup g_mp_want, g_mp_shown;
static bool g_mp_queued;
static void (*g_mp_click)(int);

static bool mp_build()
{
    if (g_mp.cont) return true;
    if (g_mp.failed) return false;
    if (!D.root) {
        build_dialog();
        if (D.root) sui::set_visible(D.root, g_want_open);
    }
    Win main = sui::main_window();
    if (!D.like_button || !main) { g_mp.failed = true; return false; }
    g_mp.root = sui::label(main, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (!g_mp.root) { g_mp.failed = true; return false; }
    sui::set_flag(g_mp.root, sui::kFlagIgnoreMouse, false);
    Win root = g_mp.root;
    auto button = [&](const std::string& caption, int code, void* look) {
        Win w = sui::button_like(root, D.like_button, {0, 0, 10, 10}, caption);
        if (!w) return w;
        sui::set_font(w, D.font_body);
        if (look) sui::set_drawable(w, look);
        sui::add_handler(w, [code](Win, const sui::Msg& m) {
            if (!sui::is_click(m)) return false;
            if (g_mp_click) g_mp_click(code);
            return true;
        });
        return w;
    };
    const uint32_t panel = (menu_panel_fill() & 0x00FFFFFFu) | 0xE6000000u; // (nearly solid: text over the world)
    g_mp.box = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (g_mp.box) sui::set_fill_color(g_mp.box, panel);
    for (Win& e : g_mp.edge) {
        e = sui::label(root, {0, 0, 1, 1}, "", D.font_body, kWhite, sui::kAlignCenter);
        if (e) sui::set_fill_color(e, kMenuBoxEdge);
    }
    g_mp.title = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kGold, sui::kAlignCenter);
    for (Win& m : g_mp.msg) m = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    // the bar: green, yellow, red zones, each dim (not reached) under its bright part (reached)
    static const uint32_t kDim[3] = {0xFF1F4A28u, 0xFF5A4C1Cu, 0xFF5A2222u}, kLit[3] = {0xFF46C35Au, 0xFFF0C83Cu, 0xFFE84848u};
    for (int i = 0; i < 3; i++) {
        g_mp.dim[i] = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
        if (g_mp.dim[i]) sui::set_fill_color(g_mp.dim[i], kDim[i]);
        g_mp.lit[i] = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
        if (g_mp.lit[i]) sui::set_fill_color(g_mp.lit[i], kLit[i]);
    }
    g_mp.line = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    g_mp.ignore = button("Ignore", 0, g_button_roles ? g_grey_look : nullptr);
    g_mp.cont = button("Continue", 1, g_button_roles ? g_blue_look : nullptr);
    if (!g_mp.cont || !g_mp.ignore || !g_mp.box || !g_mp.lit[0] || !g_mp.dim[2]) {
        g_mp.failed = !g_mp.cont;
        H->log(M, SK_WARN, "ui: memory box parts missing (continue %p box %p bar %p)", g_mp.cont, g_mp.box, g_mp.lit[0]);
    }
    for (Win w : {g_mp.box, g_mp.edge[0], g_mp.edge[1], g_mp.edge[2], g_mp.edge[3], g_mp.title, g_mp.msg[0], g_mp.msg[1],
                  g_mp.dim[0], g_mp.dim[1], g_mp.dim[2], g_mp.lit[0], g_mp.lit[1], g_mp.lit[2], g_mp.line, g_mp.ignore,
                  g_mp.cont})
        if (w) {
            sui::add_ref(w);
            sui::bring_to_front(root, w);
            sui::set_visible(w, false);
            if (w != g_mp.ignore && w != g_mp.cont) sui::set_flag(w, sui::kFlagIgnoreMouse, true);
        }
    sui::add_ref(root);
    sui::set_visible(root, false);
    return g_mp.cont != nullptr;
}

static void mp_apply(void*)
{
    g_mp_queued = false;
    const MemoryPopup want = g_mp_want;
    if (!mp_build()) return;
    Win main = sui::main_window();
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    const float w = 600, h = 206, x = std::floor((sw - w) / 2), y = std::floor(sh * 0.38f - h / 2);
    const Rect all{x, y, x + w, y + h};
    if (want.show && main) {
        sui::set_area(g_mp.root, all);
        if (sui::parent(g_mp.root) != main) sui::add_child(main, g_mp.root);
        sui::bring_to_front(main, g_mp.root);
        sui::set_flag(g_mp.root, sui::kFlagAlwaysInFront, true);
    }
    sui::set_visible(g_mp.root, want.show);
    auto place = [&](Win win, bool on, const Rect& r) { // r: inside the box
        if (!win) return;
        if (on) sui::set_area(win, r);
        sui::set_visible(win, on);
    };
    const bool on = want.show;
    place(g_mp.box, on, {0, 0, w, h});
    place(g_mp.edge[0], on, {0, 0, w, 1});
    place(g_mp.edge[1], on, {0, h - 1, w, h});
    place(g_mp.edge[2], on, {0, 0, 1, h});
    place(g_mp.edge[3], on, {w - 1, 0, w, h});
    place(g_mp.title, on, {10, 12, w - 10, 34});
    place(g_mp.msg[0], on, {10, 42, w - 10, 62});
    place(g_mp.msg[1], on, {10, 62, w - 10, 82});
    const float bx1 = 30, bx2 = w - 30, by1 = 98, by2 = 116;
    const float used = std::clamp(want.used, 0.f, 1.f);
    const float edges[4] = {0.f, std::clamp(want.yellow_at, 0.f, 1.f), std::clamp(want.red_at, 0.f, 1.f), 1.f};
    auto at = [&](float f) { return std::round(bx1 + (bx2 - bx1) * f); };
    for (int i = 0; i < 3; i++) {
        const float a = edges[i], b = std::max(a, edges[i + 1]);
        place(g_mp.dim[i], on && b > a, {at(a), by1, at(b), by2});
        const float lit_to = std::min(b, used); // the filled part of this zone
        place(g_mp.lit[i], on && lit_to - a > 0.002f, {at(a), by1, at(lit_to), by2});
    }
    place(g_mp.line, on, {10, 120, w - 10, 140});
    place(g_mp.ignore, on && want.buttons, {w / 2 - 160, 156, w / 2 - 12, 190});
    place(g_mp.cont, on && want.buttons, {w / 2 + 12, 156, w / 2 + 160, 190});
    if (on) {
        if (g_mp.title) sui::set_caption(g_mp.title, want.title);
        for (int i = 0; i < 2; i++)
            if (g_mp.msg[i]) sui::set_caption(g_mp.msg[i], want.msg[i]);
        if (g_mp.line) sui::set_caption(g_mp.line, want.line);
    }
    g_mp_shown = want;
}

bool set_memory_popup(const MemoryPopup& want_in, void (*on_click)(int))
{
    if (!g_ok || g_mp.failed) return false;
    g_mp_click = on_click;
    MemoryPopup want = want_in;
    if (!want.show) want = MemoryPopup{};
    g_mp_want = want;
    if (!(want == g_mp_shown) && !g_mp_queued) {
        g_mp_queued = true;
        H->run_outside_frame(mp_apply, nullptr);
    }
    static double s_front_at = 0; // (the stage's own UI reorders the main window's children: BUG-9171)
    if (want.show && want == g_mp_shown && g_mp.root && !g_mp_queued && H->time_seconds() - s_front_at > 0.5) {
        s_front_at = H->time_seconds();
        g_mp_queued = true;
        H->run_outside_frame([](void*) {
            g_mp_queued = false;
            Win main = sui::main_window();
            if (main && g_mp.root && g_mp_shown.show) {
                sui::bring_to_front(main, g_mp.root);
                sui::set_flag(g_mp.root, sui::kFlagAlwaysInFront, true);
            }
        }, nullptr);
    }
    return true;
}

// ---------------- the players' right-click menu (decision 6J) ----------------
// Built like the memory popup (a label root under the main window, always in front) in the Co-op window's panel look:
// the box, a 1 px edge, the player's name in gold and up to kPmItems buttons stacked under it.
constexpr int kPmItems = 6;
// owner 2026-11-18 ("The buttons look so bland ... Why aren't we using our own button layout and appropriate
// colors?"): each item is our own button art (lineages_button_<colour>_normal / _hover, the Co-op window's buttons),
// drawn as an image so it never takes Spore's disabled state; gold for joining, red for leaving or removing, blue for
// the rest; the hover image while the pointer is on it (pm_hover_tick)
static void* card_look(const std::string& stem);
static const char* const kPmStyle[4] = {"blue", "gold", "red", "grey"};
static int g_pm_hover = -1, g_pm_shown_hover = -2;
constexpr float kPmW = 224, kPmTitle = 30, kPmRow = 31, kPmPad = 8;
static struct {
    Win root = nullptr, box = nullptr, edge[4] = {}, title = nullptr, item[kPmItems] = {};
    bool failed = false;
} g_pm;
static bool g_pm_open, g_pm_queued;
// BUG-9347: a player menu opened over the Social Menu was drawn under the panel (the panel stays in front of its
// siblings whatever their order: TITAN 2026-11-23, `coop.ui where` listed the menu behind it after bring_to_front):
// a menu from a point on the panel opens beside it (left, else right). Off: at the pointer as before
static bool g_menu_beside_social = true;
COOP_FIX("ui.menu_beside_social", "BUG-9347", g_menu_beside_social, "ui: a player's right-click menu from the Social Menu opens beside the panel, not under it");
static Rect g_so_rect_seen; // (the Social panel's area while it shows: so_apply)
static float g_pm_x, g_pm_y;
static std::string g_pm_title;
static std::vector<MenuItem> g_pm_items;
static void (*g_pm_pick)(int);
static Rect g_pm_rect; // (client pixels, while open)
static void pm_set_look(int i, bool hover)
{
    if (i < 0 || i >= kPmItems || !g_pm.item[i] || i >= (int)g_pm_items.size()) return;
    const int style = std::clamp(g_pm_items[i].style, 0, 3);
    if (void* look = card_look(std::string("lineages_button_") + kPmStyle[style] + (hover ? "_hover" : "_normal")))
        sui::set_drawable(g_pm.item[i], look);
    sui::set_label_color(g_pm.item[i], kWhite); // (white on every colour: owner 2026-11-18, "Keep it white")
}

static void pm_pick(int i)
{
    const int code = i < (int)g_pm_items.size() ? g_pm_items[i].code : -1;
    auto pick = g_pm_pick;
    hide_player_menu();
    if (pick && code >= 0) pick(code);
}

static bool pm_build()
{
    if (g_pm.root) return true;
    if (g_pm.failed) return false;
    if (!D.root) {
        build_dialog();
        if (D.root) sui::set_visible(D.root, g_want_open);
    }
    Win main = sui::main_window();
    if (!D.like_button || !main) { g_pm.failed = true; return false; }
    g_pm.root = sui::label(main, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (!g_pm.root) { g_pm.failed = true; return false; }
    sui::set_flag(g_pm.root, sui::kFlagIgnoreMouse, false);
    sui::set_enabled(g_pm.root, true);
    Win root = g_pm.root;
    const uint32_t panel = (menu_panel_fill() & 0x00FFFFFFu) | 0xF0000000u;
    g_pm.box = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (g_pm.box) sui::set_fill_color(g_pm.box, panel);
    for (Win& e : g_pm.edge) {
        e = sui::label(root, {0, 0, 1, 1}, "", D.font_body, kWhite, sui::kAlignCenter);
        if (e) sui::set_fill_color(e, kMenuBoxEdge);
    }
    g_pm.title = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kGold, sui::kAlignCenter);
    // the items: filled rows with white text (Spore drew a button outside the Co-op window in its disabled look, 6J test
    // 2026-11-18); the mouse hook takes the press (player_menu_press), so they work under the game's input lock too
    for (int i = 0; i < kPmItems; i++) {
        g_pm.item[i] = sui::label(root, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    }
    for (Win w : {g_pm.box, g_pm.edge[0], g_pm.edge[1], g_pm.edge[2], g_pm.edge[3], g_pm.title})
        if (w) {
            sui::add_ref(w);
            sui::bring_to_front(root, w);
            sui::set_flag(w, sui::kFlagIgnoreMouse, true);
        }
    for (Win w : g_pm.item)
        if (w) {
            sui::add_ref(w);
            sui::bring_to_front(root, w);
            sui::set_flag(w, sui::kFlagIgnoreMouse, true);
            sui::set_visible(w, false);
        }
    sui::add_ref(root);
    sui::set_visible(root, false);
    return true;
}

static void pm_apply(void*)
{
    g_pm_queued = false;
    if (!pm_build()) return;
    Win main = sui::main_window();
    if (!g_pm_open || !main) {
        sui::set_visible(g_pm.root, false);
        return;
    }
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    const int n = std::min<int>((int)g_pm_items.size(), kPmItems);
    const float h = kPmTitle + n * kPmRow + kPmPad * 2;
    // at the pointer, kept on the screen (a menu near the right or the bottom edge opens to the other side)
    float x = g_pm_x, y = g_pm_y;
    const Rect so = g_so_rect_seen;
    if (g_menu_beside_social && so.x2 > so.x1 && x >= so.x1 && x <= so.x2 && y >= so.y1 && y <= so.y2) // BUG-9347
        x = so.x1 - kPmW - 4 >= 0 ? so.x1 - kPmW - 4 : so.x2 + 4;
    if (x + kPmW > sw) x = std::max(0.f, x - kPmW);
    if (y + h > sh) y = std::max(0.f, y - h);
    x = std::floor(x), y = std::floor(y);
    g_pm_rect = {x, y, x + kPmW, y + h};
    // over the open Co-op window: inside it (the window manager hit-tests the Co-op window before a sibling drawn over
    // it, 6J test 2026-11-18); else under the main window
    Win host = D.root && sui::visible(D.root) ? D.root : main;
    const Rect origin = host == main ? Rect{} : sui::screen_rect(host);
    sui::set_area(g_pm.root, {x - origin.x1, y - origin.y1, x - origin.x1 + kPmW, y - origin.y1 + h});
    if (sui::parent(g_pm.root) != host) {
        if (Win old = sui::parent(g_pm.root)) sui::remove_child(old, g_pm.root);
        sui::add_child(host, g_pm.root);
    }
    sui::bring_to_front(host, g_pm.root);
    sui::set_flag(g_pm.root, sui::kFlagAlwaysInFront, true);
    sui::set_visible(g_pm.root, true);

    auto place = [&](Win win, bool on, const Rect& r) {
        if (!win) return;
        if (on) sui::set_area(win, r);
        sui::set_visible(win, on);
    };
    place(g_pm.box, true, {0, 0, kPmW, h});
    place(g_pm.edge[0], true, {0, 0, kPmW, 1});
    place(g_pm.edge[1], true, {0, h - 1, kPmW, h});
    place(g_pm.edge[2], true, {0, 0, 1, h});
    place(g_pm.edge[3], true, {kPmW - 1, 0, kPmW, h});
    place(g_pm.title, true, {6, kPmPad, kPmW - 6, kPmPad + kPmTitle - 4});
    if (g_pm.title) sui::set_caption(g_pm.title, g_pm_title);
    for (int i = 0; i < kPmItems; i++) {
        const float top = kPmPad + kPmTitle + i * kPmRow;
        place(g_pm.item[i], i < n, {10, top, kPmW - 10, top + kPmRow - 4});
        if (i < n && g_pm.item[i]) {
            sui::set_caption(g_pm.item[i], g_pm_items[i].text);
            pm_set_look(i, i == g_pm_hover);
        }
    }
    g_pm_shown_hover = g_pm_hover;
}

void show_player_menu(float x, float y, const std::string& title, const std::vector<MenuItem>& items, void (*on_pick)(int))
{
    if (!g_ok || g_pm.failed) return;
    g_pm_x = x, g_pm_y = y;
    g_pm_title = title;
    g_pm_items = items;
    g_pm_pick = on_pick;
    g_pm_open = true;
    g_pm_rect = {x, y, x + kPmW, y + kPmTitle + kPmRow * std::min<int>((int)items.size(), kPmItems) + kPmPad * 2};
    if (!g_pm_queued) {
        g_pm_queued = true;
        H->run_outside_frame(pm_apply, nullptr);
    }
}

void hide_player_menu()
{
    if (!g_pm_open) return;
    g_pm_open = false;
    g_pm_pick = nullptr;
    if (!g_pm_queued) {
        g_pm_queued = true;
        H->run_outside_frame(pm_apply, nullptr);
    }
}

bool player_menu_open() { return g_pm_open; }
static int pm_item_at(float x, float y)
{
    if (!g_pm_open || !point_in_player_menu(x, y)) return -1;
    const int n = std::min<int>((int)g_pm_items.size(), kPmItems);
    for (int i = 0; i < n; i++) {
        const float top = g_pm_rect.y1 + kPmPad + kPmTitle + i * kPmRow;
        if (x >= g_pm_rect.x1 + 10 && x < g_pm_rect.x2 - 10 && y >= top && y < top + kPmRow - 4) return i;
    }
    return -1;
}
static void pm_hover_tick()
{
    // (2026-11-18 review: a menu or invite prompt inside the Co-op window stayed live but hidden when the window
    // closed: laid out again when its place no longer matches the window's state)
    if (g_pm_open && g_pm.root && !g_pm_queued) {
        Win main = sui::main_window();
        const bool in_dialog = D.root && sui::parent(g_pm.root) == D.root;
        if (main && in_dialog != (D.root && g_want_open && sui::visible(D.root))) {
            g_pm_queued = true;
            H->run_outside_frame(pm_apply, nullptr);
            return;
        }
    }
    int x = 0, y = 0;
    const int h = g_pm_open && mouse::last_move(x, y) ? pm_item_at((float)x, (float)y) : -1;
    if (h == g_pm_hover || g_pm_queued) return;
    g_pm_hover = h;
    H->run_outside_frame([](void*) {
        if (!g_pm_open || g_pm_shown_hover == g_pm_hover) return;
        pm_set_look(g_pm_shown_hover, false);
        pm_set_look(g_pm_hover, true);
        g_pm_shown_hover = g_pm_hover;
    }, nullptr);
}
bool player_menu_press(float x, float y)
{
    if (!g_pm_open || !point_in_player_menu(x, y)) return false;
    const int i = pm_item_at(x, y);
    if (i >= 0) pm_pick(i);
    return true; // (the panel itself: nothing behind it gets the press)
}
bool point_in_player_menu(float x, float y)
{
    return g_pm_open && x >= g_pm_rect.x1 && x < g_pm_rect.x2 && y >= g_pm_rect.y1 && y < g_pm_rect.y2;
}

std::string player_at_debug()
{
    std::string s = "root " + std::to_string(D.root && sui::visible(D.root)) + " space " + std::to_string(g_space_page) +
                    " settings " + std::to_string(g_settings_page) + ";";
    {
        const Rect r = g_pm.root ? sui::drawn_rect(g_pm.root) : Rect{};
        char b[200];
        Win hit = sui::window_at(250, 283);
        s += std::string(" hit(250,283) ") + (hit ? sui::type_name(hit) : "none") + " '" + (hit ? sui::caption(hit) : "") +
             "' is_item1 " + std::to_string(hit == g_pm.item[1]) + " parent_root " +
             std::to_string(hit && sui::parent(hit) == g_pm.root) + ";";
        snprintf(b, sizeof b, " menu root %p failed %d vis %d parent-is-main %d (%.0f %.0f %.0f %.0f) item0 vis %d;", g_pm.root,
                 (int)g_pm.failed, g_pm.root ? (int)sui::visible(g_pm.root) : -1,
                 g_pm.root ? (int)(sui::parent(g_pm.root) == sui::main_window()) : -1, r.x1, r.y1, r.x2, r.y2,
                 g_pm.item[0] ? (int)sui::visible(g_pm.item[0]) : -1);
        s += b;
    }
    for (int i = 0; i < 4; i++) {
        const Rect r = D.row_name[i] ? sui::drawn_rect(D.row_name[i]) : Rect{};
        char b[120];
        snprintf(b, sizeof b, " row%d id %d vis %d (%.0f %.0f %.0f %.0f)", i, g_row_ids[i],
                 D.row_name[i] ? (int)sui::visible(D.row_name[i]) : -1, r.x1, r.y1, r.x2, r.y2);
        s += b;
    }
    return s;
}

void chat_prefill(const std::string& text)
{
    g_settings_page = g_space_page = false; // (the chat line is on the lobby page)
    g_dirty = true;
    open(true);
    if (D.chat_edit) sui::set_edit_text(D.chat_edit, text);
}

// ---------------- the Party cards (decision 3G, 2026-11-14) ----------------
// One card per partner in the art of the stage that partner plays (images: data/ui/lineages_card_*.png,
// tools/party_card_assets.py): a plate with the name, the place, the health bar and a second bar (Creature the hunger,
// Space the ship's energy) and a picture frame right of it (a click: that creature's Sporepedia card). The cards stack
// at the right edge from just above Spore's own UI there (the avatar or ship card, the Pack list, the Tribe and Military
// tabs: measured, so nothing native is covered), the newest partner on top, with a "Party" tab in the art of this
// player's own stage. The own card is the game's. Replaces the 2026-11-13 panel (header, plates, gold cards).
constexpr int kPartyRows = 8; // the HUD shows this many partner cards (screen room; 6E B: more players are not listed there, the lobby lists all)
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
static Rect g_ph_tab_rect;               // the Party tab on the screen (empty: not shown)
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

// owner 2026-11-18: "reduce the total size of the Party Panel ... The scaling is very large compared to the native
// "pack" area. It's fine to have it larger, but at the moment, its WAY larger." The cards (and the Social button with
// them) at this share of their drawn size (80%: 72% read cramped, owner 2026-11-18), the text in the small UI font; [ui] party_scale (percent, 50..100)
static float party_compact()
{
    static float s = -1;
    if (s < 0) s = std::clamp(H->config_int(M, "ui", "party_scale", 80), 50, 100) / 100.f;
    return s;
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
    const uint32_t card_font = party_compact() < 0.9f && D.font_button ? D.font_button : D.font_body;
    g_ph.title = sui::label(root, {0, 0, 10, 10}, T("Party", "Группа"), card_font, kWhite, sui::kAlignCenter);
    order.push_back(g_ph.tab);
    order.push_back(g_ph.title);
    for (int i = 0; i < kPartyRows; ++i) {
        auto& r = g_ph.rows[i];
        r.plate = party_image_window(root, "lineages_card_plate_space");
        r.name = sui::label(root, {0, 0, 10, 10}, "", card_font, kWhite, sui::kAlignLeft);
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
static bool ws_on();
static float measure_right_floor(int sw_real, int sh_real)
{
    // decision 3X A: with the whole UI scaled, Spore lays it out for a screen 1/f the size: measure there, then scale
    const float wf = ws_on() ? ns_factor_now() : 1.f;
    const int sw = (int)std::lround(sw_real / wf), sh = (int)std::lround(sh_real / wf);
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
    if (wf != 1.f) return floor * wf; // (laid out smaller, drawn scaled about the top left)
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
    g_ph_tab_rect = show ? Rect{right - px(L.tab_w), top, right, top + px(L.tab_h)} : Rect{}; // (6L A: the Social button)
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
                               row.name, party_compact() < 0.9f && D.font_button != 0)); // (the card's font, 6O)
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
    scale *= party_compact(); // owner 2026-11-18: closer to the native Pack list's size
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

// ---- decision 3X A (owner 2026-11-14: "3XA"): the whole UI as one picture ----
// Scaling each piece about its own corner (above) pulled pieces that belong together apart (the owner's screenshot: the
// goals list, minimap and creature card overlapping). Now Spore lays its UI out for a screen 1/f the size and every
// layer (the main window's children) is drawn scaled by f about the screen's top left, so all pieces grow together and
// keep their edges: the full-screen holders (the layers and the HUD roots in them) get the smaller size and the windows
// in them lay themselves out again (SimpleLayout anchors, Revalidate). The main window keeps the screen (the UI is
// clipped to it: shrunk, everything past it was cut off). Pieces the game places in code (the Creature HUD's Pack
// button at the right edge) keep their distance to their side of the screen. Not scaled: the mod's own windows (they
// scale themselves) and the layer of hints placed over the world (they belong to a spot of the 3D view). The mouse:
// ns_remap (each piece's drawn transform). Off (`coop.fix ui.whole_scale off`): each piece about its corner.
static bool g_whole_scale = true;
COOP_FIX("ui.whole_scale", "BUG-9191", g_whole_scale,
         "ui: the Interface Size scales Spore's UI as one picture laid out for a smaller screen, not piece by piece");
static bool ws_on() { return g_whole_scale && g_native_scale; }
constexpr uint32_t kWorldHintLayer = 0xCBDF6E1Du; // hints over creatures ("to learn to charm"), placed by projection
// BUG-9238 (owner 2026-11-16: at 125 % "text, health bars, and other overlay things were off center"): the health bars
// over creatures (0x93310E4A holders with the bar 0x026E0DF0 and its number 0x04D039A8) are placed by the world's
// projection in their own layer; scaled, they were drawn at 1.25 x their place (off to the right and down). Off
// (`coop.fix ui.world_bars off`): that layer is scaled with the HUD.
constexpr uint32_t kWorldBarLayer = 0xE9F70DF9u;
static bool g_world_bars_fix = true;
COOP_FIX("ui.world_bars", "BUG-9238", g_world_bars_fix,
         "ui: the health bars over creatures stay at their place at an Interface Size other than 100 % (not scaled)");
static bool ws_world_layer(Win layer)
{
    const uint32_t id = sui::control_id(layer);
    return id == kWorldHintLayer || (g_world_bars_fix && id == kWorldBarLayer);
}
static std::vector<std::pair<Win, uint32_t>> g_ws_done; // layers with our proc
struct WsMoved {
    Win w;
    uint32_t id;
    sui::Rect was;
};
static std::vector<WsMoved> g_ws_moved; // pieces placed in code that we moved (their own place, to go back)
static bool ws_skip(Win layer)
{
    return layer == g_ph.root || layer == D.root || layer == g_dh.root || layer == g_mp.root || ws_world_layer(layer);
}
static bool ws_handle(Win w, const sui::Msg& m)
{
    if (m.type != sui::kMsgTransformed || !m.d || g_ns_factor == 1.f || !ws_on() || ws_skip(w)) return false;
    float* x = (float*)(uintptr_t)m.d;
    if (IsBadWritePtr(x, 64)) return false;
    const sui::Rect own = sui::screen_rect(w);
    const float mx = (own.x1 + own.x2) / 2, my = (own.y1 + own.y2) / 2, f = g_ns_factor;
    x[0] *= f; // (the game scales about the window's middle: the pivot goes to the screen's top left)
    x[5] *= f;
    x[12] += (0 - mx) * (1 - f);
    x[13] += (0 - my) * (1 - f);
    return false;
}
// BUG-9241 (QA 2026-11-16, Civ at 125 %: "Buy Sea Vehicle" drawn about 1.25 x the pointer's place, 160 px right and
// 120 px down): Spore places its tooltip window (0x03754E6C, in the layer 0x05B598F6 with other HUD pieces) at the
// pointer's place on the real screen, but that layer is drawn scaled by f. Its own transform moves it back: its middle
// drawn where the game put it (kept on the screen), still at the Interface Size. Off (`coop.fix ui.tooltip_place off`):
// drawn at f x its place.
constexpr uint32_t kTooltipWin = 0x03754E6Cu;
static bool g_tooltip_place_fix = true;
COOP_FIX("ui.tooltip_place", "BUG-9241", g_tooltip_place_fix,
         "ui: Spore's tooltips stay at the pointer at an Interface Size other than 100 % (not drawn at 1.25 x its place)");
static std::vector<std::pair<Win, uint32_t>> g_ws_tips; // tooltip windows with our proc
static unsigned g_ws_tip_moves;
static bool ws_tip_handle(Win w, const sui::Msg& m)
{
    if (m.type != sui::kMsgTransformed || !m.d || g_ns_factor == 1.f || !ws_on() || !g_tooltip_place_fix ||
        sui::control_id(w) != kTooltipWin)
        return false;
    float* x = (float*)(uintptr_t)m.d;
    if (IsBadWritePtr(x, 64)) return false;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    const sui::Rect r = sui::screen_rect(w); // where the game put it (real screen coordinates), drawn at f x that
    const float f = g_ns_factor, hw = r.w() * f / 2, hh = r.h() * f / 2;
    float cx = (r.x1 + r.x2) / 2, cy = (r.y1 + r.y2) / 2;
    const float tx = std::max(hw, std::min((float)sw - hw, cx)), ty = std::max(hh, std::min((float)sh - hh, cy));
    x[12] += tx / f - cx; // (its parent's scale applies to this move too)
    x[13] += ty / f - cy;
    g_ws_tip_moves++;
    return false;
}
static void ws_tip_hook(Win w)
{
    if (sui::control_id(w) != kTooltipWin) return;
    for (auto& d : g_ws_tips)
        if (d.first == w && d.second == kTooltipWin) return;
    sui::add_handler(w, ws_tip_handle, 0xFFFF);
    g_ws_tips.push_back({w, kTooltipWin});
    if (g_ws_tips.size() > 64) g_ws_tips.erase(g_ws_tips.begin(), g_ws_tips.begin() + 16);
    sui::invalidate_transform(w);
}
static void whole_scale_tick(float want);
static double g_ws_editor_since = -1; // an editor was requested (its UI is built at 100 %), -1: none
void editor_opening()
{
    if (!ws_on() || g_ns_factor == 1.f) {
        g_ws_editor_since = H->time_seconds();
        return;
    }
    g_ws_editor_since = H->time_seconds();
    H->run_outside_frame([](void*) { whole_scale_tick(1.f); }, nullptr); // before the editor's own windows are built
}
static void whole_scale_tick(float want)
{
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    Win main = sui::main_window();
    if (!main || sw <= 0 || sh <= 0) return;
    // BUG-9211: Spore's menus and dialogs are made for a screen of at least 1024 x 768; laid out for a smaller one they
    // do not fit (Settings at 150 % on 1280 x 960). The size goes no higher than that (140 % on 1920 x 1080).
    const float cap = std::max(1.f, std::min(sw / 1024.f, sh / 768.f));
    const float W = (float)sw, Hh = (float)sh, was = g_ns_factor, f = std::min(want, cap);
    if (want > cap && f != was) H->log(M, SK_INFO, "ui: %.0f %% asked, %.0f %% on this screen (laid out for 1024 x 768 at least)",
                                       want * 100, cap * 100);
    if (f == 1.f && was == 1.f && g_ws_moved.empty()) return;
    auto full = [&](const sui::Rect& r, float k) {
        return std::abs(r.x1) <= 2 && std::abs(r.y1) <= 2 /* (the editors: x 1) */ && std::abs(r.w() - W / k) < 2 && std::abs(r.h() - Hh / k) < 2;
    };
    const sui::Rect virt{0, 0, W / f, Hh / f};
    if (f != was && !g_ws_moved.empty()) { // a new size: the pieces placed in code go back first; only windows still
        std::set<Win> live;                   // in the tree are touched (a stored pointer can be freed by now)
        std::vector<Win> stack{main};
        for (int n = 0; !stack.empty() && n < 30000; n++) {
            Win w = stack.back();
            stack.pop_back();
            live.insert(w);
            for (Win c : sui::children(w)) stack.push_back(c);
        }
        for (auto& m : g_ws_moved)
            if (live.count(m.w) && sui::control_id(m.w) == m.id) sui::set_area(m.w, m.was);
        g_ws_moved.clear();
    }
    std::vector<Win> relaid;
    for (Win layer : sui::children(main)) {
        if (ws_world_layer(layer)) { // BUG-9238: a world layer keeps the whole screen (one made smaller before is put back)
            const sui::Rect r = sui::area(layer);
            if (std::abs(r.x1) <= 2 && std::abs(r.y1) <= 2 && (std::abs(r.w() - W) >= 2 || std::abs(r.h() - Hh) >= 2) &&
                (full(r, was) || full(r, f))) {
                sui::set_area(layer, {0, 0, W, Hh});
                sui::invalidate_transform(layer);
            }
            continue;
        }
        if (ws_skip(layer)) continue;
        const uint32_t id = sui::control_id(layer);
        bool done = false;
        for (auto& d : g_ws_done) done |= d.first == layer && d.second == id;
        if (!done) {
            sui::add_handler(layer, ws_handle, 0xFFFF);
            g_ws_done.push_back({layer, id});
            if (g_ws_done.size() > 400) g_ws_done.erase(g_ws_done.begin(), g_ws_done.begin() + 100);
            if (f != 1.f) sui::invalidate_transform(layer);
        }
        // the holders (full screen at 100 % or at the last size) get the new size; the first windows in them are pieces
        std::vector<Win> stack{layer};
        for (int n = 0; !stack.empty() && n < 8000; n++) {
            Win w = stack.back();
            stack.pop_back();
            const sui::Rect r = sui::area(w);
            if (full(r, 1.f) || full(r, was) || full(r, f)) {
                if (!full(r, f)) {
                    sui::set_area(w, {r.x1, r.y1, r.x1 + virt.x2, r.y1 + virt.y2}); // (its own offset kept)
                    relaid.push_back(w);
                }
                for (Win c : sui::children(w)) stack.push_back(c);
                continue;
            }
            ws_tip_hook(w); // BUG-9241
            if (g_tooltip_place_fix && sui::control_id(w) == kTooltipWin) continue; // (placed at the pointer, above)
            // a piece: placed in code for the real screen and reaching past the smaller one: kept at its side
            if (f <= 1.f || !sui::visible(w)) continue;
            const bool on_screen = r.x1 > -W * 0.05f && r.y1 > -Hh * 0.05f && r.x2 < W * 1.05f && r.y2 < Hh * 1.05f;
            if (!on_screen || (r.x2 <= virt.x2 + 1 && r.y2 <= virt.y2 + 1)) continue;
            // one offset per side, so pieces placed together stay in order (the Tribe roster column under its "Tribe"
            // button got reordered when each piece went by its own third, 2026-11-15): the far half keeps its
            // distance to the far edge, the near half stays, a piece in the middle stays in the middle
            auto place = [](float a1, float a2, float real, float small) {
                const float c = (a1 + a2) / 2;
                if (std::abs(c / real - 0.5f) < 0.1f) return c / real * small - (a2 - a1) / 2;
                return c > real / 2 ? a1 + (small - real) : a1;
            };
            const float nx = place(r.x1, r.x2, W, virt.x2);
            const float ny = place(r.y1, r.y2, Hh, virt.y2);
            g_ws_moved.push_back({w, sui::control_id(w), r});
            sui::set_area(w, {nx, ny, nx + r.w(), ny + r.h()});
        }
    }
    // the windows right in a holder with a new size lay themselves out again (their anchors are on it); deeper ones
    // keep theirs (their parents kept their size): revalidating them all undid the Settings page's own layout
    // (decision 3I) in a dialog kept between openings (BUG-9211: the quality table's buttons, Defaults and Full Screen
    // gone, the resolution list open, after the size was changed in it)
    for (Win h : relaid)
        for (Win c : sui::children(h)) sui::revalidate(c);
    if (f != was) {
        H->log(M, SK_INFO,
               "ui: Spore's UI at %.0f %% as one picture (laid out for %.0f x %.0f, %zu holders, %zu placed pieces moved) "
               "(3X A)", f * 100, virt.x2, virt.y2, relaid.size(), g_ws_moved.size());
        g_ns_factor = f;
        for (Win layer : sui::children(main))
            if (!ws_skip(layer)) sui::invalidate_transform(layer);
    }
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
    if (ws_on()) {
        // the editors stay at 100 %: they place their palette (its grid and page control) and bars in code from the
        // screen's size; laid out for a smaller screen the page control went off the screen, and with their anchored
        // holders resized too the parts grid kept its old places (2026-11-15)
        const uint32_t m = game::mode_id();
        const bool editor = m == modes::kEditor || m == modes::kEdit;
        if (g_ws_editor_since >= 0 && !editor && now - g_ws_editor_since > 5) g_ws_editor_since = -1; // left (or never came)
        whole_scale_tick(editor || g_ws_editor_since >= 0 ? 1.f : want);
        return;
    }
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
    if (g_ok)
        if (Win pm = pause_menu()) // the next mod instance makes its own (this one's handler dies with the DLL)
            if (Win mine = sui::find(pm, kIdPauseReport))
                sui::set_visible(mine, false);
    if (g_menu_btn && (!g_menu_drop_fix || in_menu()))
        sui::set_visible(g_menu_btn, false);
}

std::string debug_state()
{
    std::string s = "open=" + std::to_string(g_want_open) + " page=" +
                    (A.role() ? (g_space_page ? "space" : g_settings_page ? "settings" : "lobby") : "connect") +
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
    if (D.root) { // the window's frame on the screen (tests: ui_golden crops it)
        Rect r = sui::screen_rect(D.root);
        snprintf(b, sizeof b, "window %.0f %.0f %.0f %.0f %d\n", r.x1, r.y1, r.x2, r.y2, (int)sui::visible(D.root));
        s += b;
        // the texts that vary between runs (ui_golden masks them): addresses, ports, pings, the version, chat, LAN
        std::vector<std::pair<std::string, Win>> areas = {
            {"session", D.session}, {"version", D.version}, {"lanhdr", D.lan_hdr},
            {"statusline", D.status}, {"world", D.world}, {"creationline", D.creation}, {"setline", D.set_line},
            {"connectversion", D.connect_version}, {"hosthint", D.host_hint}};
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

// 6J (after the Party cards' state)
int player_at(float x, float y)
{
    auto in = [&](Win w) {
        if (!w || !sui::visible(w)) return false;
        const Rect r = sui::drawn_rect(w);
        return x >= r.x1 && x < r.x2 && y >= r.y1 && y < r.y2;
    };
    if (D.root && sui::visible(D.root) && !g_space_page && !g_settings_page)
        for (int i = 0; i < 4; i++) { // the whole row: from its colour swatch to its ping, the name's height
            if (g_row_ids[i] < 0 || !D.row_name[i] || !sui::visible(D.row_name[i])) continue;
            const Rect n = sui::drawn_rect(D.row_name[i]);
            float x1 = n.x1, x2 = n.x2;
            for (Win w : {D.row_color[i], D.row_stage[i], D.row_ping[i], D.row_ready[i]})
                if (w) {
                    const Rect r = sui::drawn_rect(w);
                    if (r.x2 > r.x1) x1 = std::min(x1, r.x1), x2 = std::max(x2, r.x2);
                }
            if (x >= x1 - 4 && x < x2 && y >= n.y1 - 2 && y < n.y2 + 2)
                return g_row_ids[i];
        }
    for (int i = 0; i < kPartyRows && i < (int)g_ph_cards.size(); ++i)
        if (in(g_ph.rows[i].frame) || in(g_ph.rows[i].portrait) || in(g_ph.rows[i].name))
            return g_ph_cards[i].player;
    return -1;
}


// ---------------- the Social Menu (decision 6L A, owner 2026-11-18) ----------------
// "I like A, can we also add a button next to the Party panel for this menu? We'll name it the Social Menu and it'll
// have the two person silhouette icon." The button: the Party tab's art of this player's stage with the icon
// (data/ui/coop_icon.png, the Co-op item's icon), left of the Party tab, or at the right edge above
// Spore's own UI when there are no partner cards. The panel at the top right: a gold title, sections (the party,
// friends, the galaxy's players) with a row per player (a dot green online / grey offline, the name in gold for this
// player and light blue for a friend, where it plays under it) and buttons in our own art on the row. Presses are the
// mouse hook's (social_press), so it works under the game's input lock too.
constexpr int kSoRows = 12, kSoChips = 2;
// owner 2026-11-18 ("The panel is looking very wonky ... that background used for the co-op menu would be useful. Make
// this look more native."): the panel is the Co-op window's own native frame (the game's account dialog layout: its
// frame, glow and close button; the content hidden) with a centred gold title as the Co-op window's page titles, and
// each player in the lobby's row box (lineages_field_box) with the lobby's colour swatch
constexpr float kSoEras = 36; // BUG-9334: the eras row under the lists
constexpr float kSoW = 318, kSoHead = 66, kSoSect = 28, kSoRow = 42, kSoPad = 24, kSoBottom = 22, kSoChipW = 68, kSoChipH = 24;
constexpr uint32_t kIdSocialFrame = 0xC00F0200;
constexpr uint32_t kSoOnline = 0xFF5ADC6Eu, kSoOffline = 0xFF6E7882u,
                   kSoFriend = 0xFF8FD3FFu;
struct SoChip {
    Win img = nullptr, cap = nullptr;
    int action = 0, style = 0;
    bool hover = false;
    Rect at{}; // on the screen while shown
};
struct SoRowWin {
    Win bg = nullptr, dot = nullptr, name = nullptr, where = nullptr;
    SoChip chip[kSoChips];
    Rect at{};
    int row = -1; // its entry in g_so_rows (-1: unused)
};
static struct {
    Win button = nullptr, btn_tab = nullptr, btn_icon = nullptr;
    Win root = nullptr, box = nullptr, edge[4] = {}, title = nullptr, hint = nullptr, sect[3] = {}, empty = nullptr;
    SoRowWin rows[kSoRows];
    Win eras_label = nullptr; // BUG-9334: "Players in other stages" and its Shown / Hidden button
    SoChip eras;
    std::string btn_style;
    bool failed = false;
} g_so;
static SocialState g_so_want;
static std::vector<SocialRow> g_so_rows; // what the rows show (their order)
static bool g_so_open, g_so_queued, g_so_dirty = true;
static Rect g_so_rect, g_so_btn_rect;
static bool g_so_native; // the panel is the native frame (else the plain fallback)
static Win g_so_close;   // the frame's close button
struct SoPiece {
    Win w;
    Win src; // the Co-op window's piece: its place there (read when laid out) is laid out again at the panel's size
    Win in;  // the window it is in (sui::parent of a layout's window is not always it)
};
static std::vector<SoPiece> g_so_frame; // the native frame's pieces on the panel
// the Co-op window's native frame worn by a panel of ours (the Social Menu, 8M A's planet panel): its pieces on `root`
// (their drawables shared); false: another layout than the one known (the caller draws a plain panel)
static bool frame_build(Win root, std::vector<SoPiece>& frame, Win* close)
{
    frame.clear();
    if (close) *close = nullptr;
    if (!D.root) return false;
    auto piece = [&](Win src, Win in, uint32_t fill) {
        void* drw = sui::drawable(src);
        Win w = sui::button_like(root, D.like_button, {0, 0, 10, 10}, "");
        if (!w) return (Win) nullptr;
        sui::set_drawable(w, drw);
        sui::set_label_color(w, 0);
        if (fill) sui::set_fill_color(w, fill);
        sui::add_ref(w);
        sui::set_flag(w, sui::kFlagIgnoreMouse, true);
        frame.push_back({w, src, in});
        return w;
    };
    // the window manager lists a window's children newest first (drawn on top): the oldest is made first here
    auto oldest_first = [](Win w) {
        std::vector<Win> v = sui::children(w);
        std::reverse(v.begin(), v.end());
        return v;
    };
    // (found by their shape: a new window's areas are its layout's until it is laid out, the frame's own size 0)
    for (Win c : oldest_first(D.root)) {
        const Rect a = sui::area(c);
        if (a.x1 < 0 && a.y1 < 0 && a.x2 > 400 && sui::drawable(c)) // the glow around the frame (not a corner's)
            piece(c, D.root, 0);
        else if (!sui::drawable(c) && !sui::control_id(c)) { // the frame: a dozen image pieces and the panel
            const std::vector<Win> kids = oldest_first(c);
            const size_t drawn = std::count_if(kids.begin(), kids.end(), [](Win k) { return sui::drawable(k) != nullptr; });
            const bool plain = std::none_of(kids.begin(), kids.end(), [](Win k) { return sui::as_button(k) != nullptr; });
            if (drawn < 10 || drawn + 1 != kids.size() || !plain) continue; // (our pages hold buttons and labels)
            for (Win p : kids) piece(p, c, sui::drawable(p) ? 0 : sui::fill_color(p)); // (the one without: the panel colour)
        }
    }
    if (frame.size() >= 10) { // the close button over the frame (pressed through the mouse hook)
        if (close)
            if (Win x = sui::find(D.root, 0xFFFFFFF2u))
                if (sui::drawable(x)) *close = piece(x, D.root, 0);
        return true;
    }
    for (auto& p : frame) sui::remove_child(root, p.w); // (another layout than the one known: the plain panel)
    frame.clear();
    return false;
}
// the frame's pieces at the panel's size w x h: an edge in the left (top) half of the Co-op window keeps its place, one
// in the right (bottom) half keeps its distance to the right (bottom) edge: corners keep their size, the sides and the
// panel stretch. False: the Co-op window is not laid out yet (again next tick)
static bool frame_layout(const std::vector<SoPiece>& frame, float w, float h)
{
    bool ok = true;
    for (const SoPiece& p : frame) {
        const Rect at = sui::real_area(p.src), in = sui::real_area(p.in); // (laid out: area is the layout's input)
        if (in.w() < 100 || in.h() < 100) {
            sui::set_visible(p.w, false);
            ok = false;
            continue;
        }
        auto mx = [&](float v) { return v <= in.w() / 2 ? v : v - in.w() + w; };
        auto my = [&](float v) { return v <= in.h() / 2 ? v : v - in.h() + h; };
        sui::set_area(p.w, {mx(at.x1), my(at.y1), mx(at.x2), my(at.y2)});
        sui::set_visible(p.w, true);
    }
    return ok;
}
static int g_so_hover_row = -1, g_so_hover_chip = -1;
static void (*g_so_action)(int, const SocialRow&, float, float);

static Win so_label(Win root, uint32_t font, uint32_t color, int align)
{
    Win w = sui::label(root, {0, 0, 10, 10}, "", font, color, align);
    if (w) {
        sui::add_ref(w);
        sui::set_flag(w, sui::kFlagIgnoreMouse, true);
        sui::set_visible(w, false);
    }
    return w;
}

static bool so_build()
{
    if (g_so.root) return true;
    if (g_so.failed) return false;
    if (!D.root) {
        build_dialog();
        if (D.root) sui::set_visible(D.root, g_want_open);
    }
    Win main = sui::main_window();
    if (!main) return false;
    if (!sui::has_image(sui::fnv("coop_icon"), sui::fnv("sporecoop"))) {
        static bool s_said = false;
        if (!s_said) H->log(M, SK_WARN, "ui: the Social Menu's icon is not in this game's SporeCoop.package (an older one)");
        s_said = true;
    }
    const uint32_t small = D.font_button ? D.font_button : D.font_body;
    // the button
    g_so.button = sui::label(main, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (g_so.button) {
        sui::add_ref(g_so.button);
        sui::set_flag(g_so.button, sui::kFlagIgnoreMouse, true);
        sui::set_visible(g_so.button, false);
        g_so.btn_tab = party_image_window(g_so.button, "lineages_card_tab_space");
        g_so.btn_style = "lineages_card_tab_space";
        // the icon as a whole image scaled into its box, as the Party cards' portraits (Spore's image window; the
        // tooltip drawable cuts an image in thirds and a small icon came out in pieces)
        g_so.btn_icon = sui::image_window(g_so.button, sui::fnv("coop_icon"), sui::fnv("sporecoop"), 0, 0);
        for (Win w : {g_so.btn_tab, g_so.btn_icon})
            if (w) {
                sui::add_ref(w);
                sui::set_flag(w, sui::kFlagIgnoreMouse, true);
            }
        if (g_so.btn_icon) sui::bring_to_front(g_so.button, g_so.btn_icon);
    }
    // the panel: a plain root (a stale one from before a hot reload goes first) wearing the Co-op window's native frame:
    // the frame's own pieces (their drawables, shared) laid out again at the panel's size. A second instance of the
    // account dialog layout crashed the game on open (2026-11-18, crash in cWindowManager::PopModal: the layout is a
    // modal window of the window manager), so only its art is reused
    if (Win old = sui::find(main, kIdSocialFrame))
        if (Win p = sui::parent(old)) sui::remove_child(p, old);
    g_so.root = sui::label(main, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (!g_so.root) { g_so.failed = true; return false; }
    sui::add_ref(g_so.root);
    sui::set_ids(g_so.root, kIdSocialFrame, 0);
    Win root = g_so.root;
    g_so_native = frame_build(root, g_so_frame, &g_so_close);
    H->log(M, SK_DEBUG, "ui: the Social Menu wears %zu pieces of the Co-op window's frame", g_so_frame.size());
    if (!g_so_native) { // (no layout: the plain panel)
        sui::set_flag(root, sui::kFlagIgnoreMouse, true);
        g_so.box = so_label(root, D.font_body, kWhite, sui::kAlignCenter);
        if (g_so.box) sui::set_fill_color(g_so.box, (menu_panel_fill() & 0x00FFFFFFu) | 0xF0000000u);
        for (Win& e : g_so.edge) {
            e = so_label(root, D.font_body, kWhite, sui::kAlignCenter);
            if (e) sui::set_fill_color(e, kMenuBoxEdge);
        }
    }
    g_so.title = so_label(root, D.font_body, kGold, sui::kAlignCenter);
    g_so.hint = so_label(root, small, kDim, sui::kAlignRight);
    for (Win& w : g_so.sect) w = so_label(root, small, kGold, sui::kAlignLeft);
    g_so.empty = so_label(root, small, kDim, sui::kAlignLeft);
    for (auto& r : g_so.rows) {
        r.bg = party_image_window(root, "lineages_field_box"); // (the lobby's row box)
        if (r.bg) {
            sui::add_ref(r.bg);
            sui::set_flag(r.bg, sui::kFlagIgnoreMouse, true);
            sui::set_visible(r.bg, false);
        }
        r.dot = so_label(root, D.font_body, kWhite, sui::kAlignCenter);
        r.name = so_label(root, D.font_body, kWhite, sui::kAlignLeft);
        r.where = so_label(root, small, kDim, sui::kAlignLeft);
        for (auto& c : r.chip) {
            c.img = party_image_window(root, "lineages_button_blue_normal");
            if (c.img) {
                sui::add_ref(c.img);
                sui::set_flag(c.img, sui::kFlagIgnoreMouse, true);
                sui::set_visible(c.img, false);
            }
            c.cap = so_label(root, small, kWhite, sui::kAlignCenter);
        }
    }
    g_so.eras_label = so_label(root, small, kWhite, sui::kAlignLeft); // BUG-9334
    g_so.eras.img = party_image_window(root, "lineages_button_gold_normal");
    if (g_so.eras.img) {
        sui::add_ref(g_so.eras.img);
        sui::set_flag(g_so.eras.img, sui::kFlagIgnoreMouse, true);
        sui::set_visible(g_so.eras.img, false);
    }
    g_so.eras.cap = so_label(root, small, kWhite, sui::kAlignCenter);
    sui::set_visible(root, false);
    return true;
}

static const char* const kSoStyle[4] = {"blue", "gold", "red", "grey"};
static void so_chip_look(SoChip& c)
{
    if (!c.img) return;
    if (void* look = card_look(std::string("lineages_button_") + kSoStyle[std::clamp(c.style, 0, 3)] +
                               (c.hover ? "_hover" : "_normal"))) {
        if (sui::drawable(c.img) == look) return;
        sui::set_drawable(c.img, look);
        // BUG-9334: the game draws a window's new drawable only once the window changes (set_look): the eras button
        // stayed gold after a press to Hidden (2026-11-22). Hidden and shown again, it is drawn anew
        const bool was = sui::visible(c.img);
        sui::set_visible(c.img, false);
        sui::set_visible(c.img, was);
    }
}

static void so_apply(void*)
{
    g_so_queued = false;
    if (!g_so.root && !g_so_want.button && !g_so_open) return; // (made when first needed: its images then)
    if (!so_build()) return;
    g_so_dirty = false;
    Win main = sui::main_window();
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    const SocialState& st = g_so_want;
    // the button: left of the Party tab, else at the right edge above Spore's own UI
    if (g_so.button) {
        const bool on = st.button && main;
        if (on) {
            const float bh = g_ph_tab_rect.y2 > g_ph_tab_rect.y1 ? g_ph_tab_rect.y2 - g_ph_tab_rect.y1
                                                                : std::round(30 * party_compact()),
                        bw = std::round(bh * 1.55f); // (as tall as the Party tab, its compact size: 6O)
            Rect r;
            if (g_ph_tab_rect.x2 > g_ph_tab_rect.x1)
                r = {g_ph_tab_rect.x1 - 6 - bw, g_ph_tab_rect.y1, g_ph_tab_rect.x1 - 6, g_ph_tab_rect.y2};
            else {
                const float floor = measure_right_floor(sw, sh);
                r = {sw - 4 - bw, floor - 6 - bh, (float)sw - 4, floor - 6};
            }
            g_so_btn_rect = r;
            sui::set_area(g_so.button, r);
            if (sui::parent(g_so.button) != main) sui::add_child(main, g_so.button);
            sui::bring_to_front(main, g_so.button);
            const char* own = card_style(st.stage);
            set_look(g_so.btn_tab, g_so.btn_style, std::string("lineages_card_tab_") + (own ? own : "space"));
            if (g_so.btn_tab) sui::set_area(g_so.btn_tab, {0, 0, r.w(), r.h()});
            const float ic = std::round(r.h() * 0.82f);
            if (g_so.btn_icon) sui::set_area(g_so.btn_icon, {(r.w() - ic) / 2, (r.h() - ic) / 2, (r.w() + ic) / 2, (r.h() + ic) / 2});
        } else
            g_so_btn_rect = {};
        sui::set_visible(g_so.button, on);
    }
    // the panel
    if (!g_so_open || !main) {
        sui::set_visible(g_so.root, false);
        g_so_rect = {};
        g_so_rect_seen = {};
        for (auto& r : g_so.rows) r.at = {}, r.row = -1;
        g_so.eras.at = {};
        return;
    }
    g_so_rows = st.rows;
    auto place = [&](Win w, bool on, const Rect& r) {
        if (!w) return;
        if (on) sui::set_area(w, r);
        sui::set_visible(w, on);
    };
    // Spore's labels centre their text whatever alignment they get (see fit): a left or right text gets a box as
    // wide as the text (plus the label's inner margin), at the left or right edge of `r`
    auto text_at = [&](Win w, const Rect& r, const std::string& t, bool small, bool right) {
        if (!w) return;
        sui::set_caption(w, t);
        const float m = kLabelMargin, tw = std::min(r.w(), text_px(t, small)) + 2 * m;
        sui::set_area(w, right ? Rect{r.x2 + m - tw, r.y1, r.x2 + m, r.y2} : Rect{r.x1 - m, r.y1, r.x1 - m + tw, r.y2});
        sui::set_visible(w, true);
    };
    // the height: the head, a header per section with rows, the rows
    int sections = 0;
    for (int sec = 0; sec < 3; sec++) {
        bool any = sec == 0 || sec == 1; // (the party and friends always show their header)
        for (auto& r : st.rows) any = any || r.section == sec;
        if (sec == 2 && !st.galaxy) any = false;
        sections += any;
    }
    const int nrows = std::min<int>((int)st.rows.size(), kSoRows);
    const bool friends_empty = std::none_of(st.rows.begin(), st.rows.end(), [](const SocialRow& r) { return r.section == 1; });
    const bool eras_row = st.eras >= 0 && g_eras_button; // BUG-9334
    const float h = kSoHead + sections * kSoSect + nrows * kSoRow + kSoBottom + (friends_empty ? 24 : 0) + (eras_row ? kSoEras : 0);
    // at the top, left of the Party cards and the Social button when they show (they stay in reach: the button closes
    // it again), else at the right edge
    float x2 = sw - 10.f;
    if (g_ph.area.x2 > g_ph.area.x1) x2 = std::min(x2, g_ph.area.x1 - 10);
    if (g_so_btn_rect.x2 > g_so_btn_rect.x1 && g_so_btn_rect.y1 < 44 + h) x2 = std::min(x2, g_so_btn_rect.x1 - 10);
    const float x = std::max(8.f, x2 - kSoW), y = 44;
    g_so_rect = {x, y, x + kSoW, y + h};
    g_so_rect_seen = g_so_rect; // (BUG-9347)
    sui::set_area(g_so.root, g_so_rect);
    if (sui::parent(g_so.root) != main) sui::add_child(main, g_so.root);
    // the frame's pieces: an edge in the left (top) half of the Co-op window keeps its place, one in the right (bottom)
    // half keeps its distance to the right (bottom) edge: corners keep their size, the sides and the panel stretch
    if (!frame_layout(g_so_frame, kSoW, h)) g_so_dirty = true; // (the Co-op window is not laid out yet: the next tick)
    sui::bring_to_front(main, g_so.root);
    sui::set_flag(g_so.root, sui::kFlagAlwaysInFront, true);
    sui::set_visible(g_so.root, true);
    if (!g_so_native) {
        place(g_so.box, true, {0, 0, kSoW, h});
        place(g_so.edge[0], true, {0, 0, kSoW, 1});
        place(g_so.edge[1], true, {0, h - 1, kSoW, h});
        place(g_so.edge[2], true, {0, 0, 1, h});
        place(g_so.edge[3], true, {kSoW - 1, 0, kSoW, h});
    }
    place(g_so.title, true, {0, 20, kSoW, 46}); // (centred, as the Co-op window's "LOBBY")
    sui::set_caption(g_so.title, "SOCIAL");
    text_at(g_so.hint, {kSoW - 120, 24, kSoW - 44, 44}, "F8", true, true);
    float cy = kSoHead;
    int ri = 0;
    for (int sec = 0; sec < 3; sec++) {
        bool any = sec == 0 || sec == 1;
        for (auto& r : st.rows) any = any || r.section == sec;
        if (sec == 2 && !st.galaxy) any = false;
        if (!any) {
            place(g_so.sect[sec], false, {});
            continue;
        }
        char head[64];
        if (sec == 0) snprintf(head, sizeof head, "Party  %d / 4", std::max(1, st.party_n));
        else if (sec == 1) snprintf(head, sizeof head, "Friends  %d online", st.friends_online);
        else snprintf(head, sizeof head, "Online now  %d", st.online_n);
        text_at(g_so.sect[sec], {kSoPad + 4, cy + 3, kSoW - kSoPad, cy + kSoSect - 3}, head, true, false); // (its small font)
        cy += kSoSect;
        for (size_t k = 0; k < st.rows.size() && ri < kSoRows; k++) {
            const SocialRow& row = st.rows[k];
            if (row.section != sec) continue;
            SoRowWin& w = g_so.rows[ri++];
            w.row = (int)k;
            const float top = cy, bot = cy + kSoRow - 4;
            w.at = {x + kSoPad, y + top, x + kSoW - kSoPad, y + bot};
            place(w.bg, true, {kSoPad, top, kSoW - kSoPad, bot});
            place(w.dot, true, {kSoPad + 4, top + 7, kSoPad + 22, top + 29}); // (the lobby's colour swatch)
            if (w.dot) {
                sui::set_caption(w.dot, "\xE2\x96\xA0");
                sui::set_label_color(w.dot, row.online ? kSoOnline : kSoOffline);
            }
            // the buttons on the row (right to left)
            std::vector<std::pair<int, int>> chips; // action, style
            if (row.me) {
                if (row.section == 0 && st.party_n > 1) chips.push_back({kSocLeave, kMenuRed});
            } else if (row.online) { // (placed right to left: Invite, then Whisper at the edge)
                chips.push_back({kSocWhisper, kMenuBlue});
                if (row.section != 0) chips.push_back({kSocInvite, kMenuGold});
            }
            float cx = kSoW - kSoPad - 8;
            for (int c = 0; c < kSoChips; c++) {
                SoChip& ch = w.chip[c];
                const bool on = c < (int)chips.size();
                if (on) {
                    ch.action = chips[c].first;
                    ch.style = chips[c].second;
                    const Rect r{cx - kSoChipW, top + (kSoRow - 4 - kSoChipH) / 2, cx, top + (kSoRow - 4 + kSoChipH) / 2};
                    // (the buttons on the row box, a little inside its right edge)
                    ch.at = {x + r.x1, y + r.y1, x + r.x2, y + r.y2};
                    place(ch.img, true, r);
                    place(ch.cap, true, {r.x1, r.y1 + 1, r.x2, r.y2});
                    so_chip_look(ch);
                    sui::set_caption(ch.cap, ch.action == kSocLeave ? "Leave" : ch.action == kSocInvite ? "Invite" : "Whisper");
                    if (ch.img) sui::bring_to_front(g_so.root, ch.img);
                    if (ch.cap) sui::bring_to_front(g_so.root, ch.cap);
                    cx -= kSoChipW + 6;
                } else {
                    ch.at = {};
                    place(ch.img, false, {});
                    place(ch.cap, false, {});
                }
            }
            const float text_r = cx - 4;
            // one line each: a text wider than the room left of the buttons ends in "..." (a wider one wrapped
            // under the buttons, 2026-11-18)
            auto fitted = [&](std::string t, bool small) {
                const float room = text_r - (kSoPad + 26);
                if (text_px(t, small) <= room) return t;
                while (!t.empty() && text_px(t + "...", small) > room) {
                    t.pop_back();
                    while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back(); // (a whole UTF-8 letter)
                    if (!t.empty() && (unsigned char)t.back() >= 0xC0) t.pop_back();
                }
                while (!t.empty() && t.back() == ' ') t.pop_back();
                return t + "...";
            };
            const bool lead_mark = row.leader && row.section == 0 && st.party_n > 1; // (alone: no "leader")
            text_at(w.name, {kSoPad + 26, top + 2, text_r, top + 21}, fitted(row.name + (lead_mark ? "  (leader)" : ""), false), false, false);
            sui::set_label_color(w.name, row.me ? kGold : !row.online ? kSoOffline : row.buddy ? kSoFriend : kWhite);
            std::string where = row.where; // ("Samuth  -  Creature": the planet alone when both do not fit)
            if (text_px(where, true) > text_r - (kSoPad + 26))
                if (const size_t cut = where.find(" - "); cut != std::string::npos) where = where.substr(0, cut);
            while (!where.empty() && where.back() == ' ') where.pop_back();
            text_at(w.where, {kSoPad + 26, top + 19, text_r, top + kSoRow - 5}, fitted(where, true), true, false);
            cy += kSoRow;
        }
        if (sec == 1 && friends_empty) { // a line under an empty friends list: how to add one
            text_at(g_so.empty, {kSoPad + 4, cy, kSoW - kSoPad, cy + 20}, "Right-click a player to add a friend", true, false);
            cy += 24;
        }
    }
    for (; ri < kSoRows; ri++) {
        SoRowWin& w = g_so.rows[ri];
        w.row = -1, w.at = {};
        for (Win v : {w.bg, w.dot, w.name, w.where}) place(v, false, {});
        for (auto& c : w.chip) c.at = {}, place(c.img, false, {}), place(c.cap, false, {});
    }
    if (!friends_empty) place(g_so.empty, false, {});
    // BUG-9334: the player's own "other eras" setting (chat /eras on|off) under the lists: a label and a button that
    // says what is set now (gold Shown, grey Hidden); a press switches it
    if (eras_row) {
        const float top = cy + 4, bot = top + kSoChipH;
        const Rect r{kSoW - kSoPad - 8 - kSoChipW - 6, top, kSoW - kSoPad - 8, bot};
        text_at(g_so.eras_label, {kSoPad + 4, top + 2, r.x1 - 6, bot}, "Players in other stages", true, false);
        g_so.eras.action = kSocEras;
        g_so.eras.style = st.eras ? kMenuGold : kMenuGrey;
        g_so.eras.at = {x + r.x1, y + r.y1, x + r.x2, y + r.y2};
        place(g_so.eras.img, true, r);
        place(g_so.eras.cap, true, {r.x1, r.y1 + 1, r.x2, r.y2});
        so_chip_look(g_so.eras);
        sui::set_caption(g_so.eras.cap, st.eras ? "Shown" : "Hidden");
        if (g_so.eras.img) sui::bring_to_front(g_so.root, g_so.eras.img);
        if (g_so.eras.cap) sui::bring_to_front(g_so.root, g_so.eras.cap);
    } else {
        g_so.eras.at = {};
        place(g_so.eras_label, false, {});
        place(g_so.eras.img, false, {});
        place(g_so.eras.cap, false, {});
    }
}

static void so_queue()
{
    if (!g_so_queued) {
        g_so_queued = true;
        H->run_outside_frame(so_apply, nullptr);
    }
}

void set_social(const SocialState& s, void (*on_action)(int, const SocialRow&, float, float))
{
    if (!g_ok) return;
    g_so_action = on_action;
    auto same = [](const SocialState& a, const SocialState& b) {
        if (a.button != b.button || a.galaxy != b.galaxy || a.rows.size() != b.rows.size() || a.party_n != b.party_n ||
            a.friends_online != b.friends_online || a.online_n != b.online_n || a.stage != b.stage || a.eras != b.eras)
            return false;
        for (size_t i = 0; i < a.rows.size(); i++) {
            const auto &x = a.rows[i], &y = b.rows[i];
            if (x.name != y.name || x.where != y.where || x.identity != y.identity || x.section != y.section ||
                x.online != y.online || x.leader != y.leader || x.me != y.me || x.buddy != y.buddy || x.session_id != y.session_id)
                return false;
        }
        return true;
    };
    // (the Party tab moves when partners come or go: the button follows it)
    static Rect s_tab;
    const bool tab_moved = s_tab.x1 != g_ph_tab_rect.x1 || s_tab.y1 != g_ph_tab_rect.y1 || s_tab.x2 != g_ph_tab_rect.x2;
    s_tab = g_ph_tab_rect;
    if (same(s, g_so_want) && !g_so_dirty && !tab_moved) return;
    g_so_want = s;
    so_queue();
}

void toggle_social()
{
    g_so_open = !g_so_open;
    g_so_dirty = true;
    so_queue();
}
bool social_open() { return g_so_open; }

static bool so_in(const Rect& r, float x, float y) { return r.x2 > r.x1 && x >= r.x1 && x < r.x2 && y >= r.y1 && y < r.y2; }

bool ui_covers(float x1, float y1, float x2, float y2)
{
    auto hit = [&](const Rect& r) { return r.x2 > r.x1 && x1 < r.x2 && x2 > r.x1 && y1 < r.y2 && y2 > r.y1; };
    return (g_so_open && hit(g_so_rect)) || (g_pm_open && hit(g_pm_rect));
}

bool social_press(float x, float y, bool right)
{
    if (!right && g_so_open && g_so_close && sui::visible(g_so_close)) { // the frame's X (also under an input lock)
        const Rect c = sui::drawn_rect(g_so_close);
        if (c.x2 > c.x1 && x >= c.x1 && x < c.x2 && y >= c.y1 && y < c.y2) {
            toggle_social();
            return true;
        }
    }
    if (!right && so_in(g_so_btn_rect, x, y) && g_so_want.button) {
        toggle_social();
        return true;
    }
    if (!g_so_open || !so_in(g_so_rect, x, y)) return false;
    if (!right && so_in(g_so.eras.at, x, y)) { // BUG-9334
        if (g_so_action) g_so_action(kSocEras, SocialRow{}, x, y);
        return true;
    }
    for (auto& w : g_so.rows) {
        if (w.row < 0 || w.row >= (int)g_so_rows.size() || !so_in(w.at, x, y)) continue;
        const SocialRow row = g_so_rows[w.row];
        if (right) {
            if (g_so_action) g_so_action(kSocMenu, row, x, y);
            return true;
        }
        for (auto& c : w.chip)
            if (so_in(c.at, x, y)) {
                if (g_so_action) g_so_action(c.action, row, x, y);
                return true;
            }
        return true;
    }
    return true; // (the panel itself)
}

// the button under the pointer gets the hover art (as the menu's items)
static void so_hover_tick()
{
    int px = 0, py = 0;
    if (!g_so_open || !mouse::last_move(px, py)) return;
    int hr = -1, hc = -1;
    for (int i = 0; i < kSoRows && hr < 0; i++)
        for (int c = 0; c < kSoChips; c++)
            if (so_in(g_so.rows[i].chip[c].at, (float)px, (float)py)) {
                hr = i, hc = c;
                break;
            }
    if (hr < 0 && so_in(g_so.eras.at, (float)px, (float)py)) hr = kSoRows, hc = 0; // BUG-9334 (the eras button)
    if (hr == g_so_hover_row && hc == g_so_hover_chip) return;
    g_so_hover_row = hr, g_so_hover_chip = hc;
    H->run_outside_frame([](void*) {
        for (int i = 0; i < kSoRows; i++)
            for (int c = 0; c < kSoChips; c++) {
                SoChip& ch = g_so.rows[i].chip[c];
                const bool want = i == g_so_hover_row && c == g_so_hover_chip;
                if (ch.hover != want && ch.at.x2 > ch.at.x1) {
                    ch.hover = want;
                    so_chip_look(ch);
                }
            }
        const bool want = g_so_hover_row == kSoRows;
        if (g_so.eras.hover != want && g_so.eras.at.x2 > g_so.eras.at.x1) {
            g_so.eras.hover = want;
            so_chip_look(g_so.eras);
        }
    }, nullptr);
}

std::string social_debug()
{
    char b[200];
    const Rect& r = g_so.eras.at;
    const std::string stem = std::string("lineages_button_") + kSoStyle[std::clamp(g_so.eras.style, 0, 3)] +
                             (g_so.eras.hover ? "_hover" : "_normal");
    snprintf(b, sizeof b, "social open=%d eras=%d button=(%.0f %.0f %.0f %.0f) caption='%s' look=%s%s", (int)g_so_open,
             g_so_want.eras, r.x1, r.y1, r.x2, r.y2, g_so.eras.cap && r.x2 > r.x1 ? sui::caption(g_so.eras.cap).c_str() : "",
             stem.c_str(), card_look(stem) ? "" : " (no art)");
    return b;
}

// ---------------- the planet details panel (decision 8M A, BUG-9340, owner 2026-11-22) ----------------
// "Why can't we add a little panel (similar to party player name panel) off to the opposite side of the planet menu
// that shows the details of the planet?" Spore's Choose A Stage screen (BUG-9337) has no room for the players per
// stage our box showed: this panel stands right of its stage buttons, in the Social Menu's look (the Co-op window's
// native frame, the lobby's row box, gold title). A row per stage: Spore's own stage icon (the screen's button's
// drawable), the stage, its players and host or why it is grey; the stage under the pointer lit with its players'
// names; the planet's friends. It only shows, takes no clicks (Spore's screen keeps them).
constexpr int kPpRows = 5, kPpFriends = 3;
constexpr float kPpW = 304, kPpHead = 62, kPpSub = 26, kPpRow = 44, kPpRowNames = 16, kPpGap = 4, kPpPad = 20,
                kPpIcon = 32, kPpFriend = 38, kPpBottom = 26;
constexpr uint32_t kIdPlanetFrame = 0xC00F0300;
struct PpRowWin {
    Win bg = nullptr, icon = nullptr, name = nullptr, line = nullptr, names = nullptr, dot = nullptr;
};
static struct {
    Win root = nullptr, box = nullptr, title = nullptr, sub = nullptr, foot = nullptr, sect = nullptr;
    Win lit[4] = {}; // the hovered row's gold edge
    PpRowWin rows[kPpRows];
    PpRowWin fr[kPpFriends];
    std::vector<SoPiece> frame;
    bool native = false, failed = false;
} g_pp;
static PlanetPanel g_pp_want;
static bool g_pp_queued, g_pp_shown;
static Rect g_pp_rect, g_pp_so_seen; // (the Social Menu's place when laid out: it moves the panel)

static bool pp_build()
{
    if (g_pp.root) return true;
    if (g_pp.failed) return false;
    if (!D.root) {
        build_dialog();
        if (D.root) sui::set_visible(D.root, g_want_open);
    }
    Win main = sui::main_window();
    if (!main) return false;
    if (Win old = sui::find(main, kIdPlanetFrame)) // (a stale one from before a hot reload)
        if (Win p = sui::parent(old)) sui::remove_child(p, old);
    g_pp.root = sui::label(main, {0, 0, 10, 10}, "", D.font_body, kWhite, sui::kAlignCenter);
    if (!g_pp.root) { g_pp.failed = true; return false; }
    sui::add_ref(g_pp.root);
    sui::set_ids(g_pp.root, kIdPlanetFrame, 0);
    sui::set_flag(g_pp.root, sui::kFlagIgnoreMouse, true);
    Win root = g_pp.root;
    g_pp.native = frame_build(root, g_pp.frame, nullptr);
    if (!g_pp.native) {
        g_pp.box = so_label(root, D.font_body, kWhite, sui::kAlignCenter);
        if (g_pp.box) sui::set_fill_color(g_pp.box, (menu_panel_fill() & 0x00FFFFFFu) | 0xF0000000u);
    }
    const uint32_t small = D.font_button ? D.font_button : D.font_body;
    g_pp.title = so_label(root, D.font_body, kGold, sui::kAlignCenter);
    g_pp.sub = so_label(root, small, kWhite, sui::kAlignLeft);
    g_pp.sect = so_label(root, small, kGold, sui::kAlignLeft);
    g_pp.foot = so_label(root, small, kDim, sui::kAlignLeft);
    auto row = [&](PpRowWin& r, bool icon) {
        r.bg = party_image_window(root, "lineages_field_box");
        if (r.bg) {
            sui::add_ref(r.bg);
            sui::set_flag(r.bg, sui::kFlagIgnoreMouse, true);
            sui::set_visible(r.bg, false);
        }
        if (icon && D.like_button) { // (a button showing Spore's stage button's drawable)
            r.icon = sui::button_like(root, D.like_button, {0, 0, 10, 10}, "");
            if (r.icon) {
                sui::add_ref(r.icon);
                sui::set_label_color(r.icon, 0);
                sui::set_flag(r.icon, sui::kFlagIgnoreMouse, true);
                sui::set_drawable(r.icon, nullptr);
                sui::set_visible(r.icon, false);
            }
        }
        r.dot = so_label(root, small, kSoOnline, sui::kAlignCenter);
        r.name = so_label(root, D.font_body, kWhite, sui::kAlignLeft);
        r.line = so_label(root, small, kDim, sui::kAlignLeft);
        r.names = so_label(root, small, kSoFriend, sui::kAlignLeft);
    };
    for (auto& r : g_pp.rows) row(r, true);
    for (auto& r : g_pp.fr) row(r, false);
    for (Win& e : g_pp.lit) {
        e = so_label(root, D.font_body, kWhite, sui::kAlignCenter);
        if (e) sui::set_fill_color(e, kGold);
    }
    sui::set_visible(root, false);
    return true;
}

// the icons show Spore's drawables: dropped whenever the panel hides (the screen's layout may go with the screen)
static void pp_drop_icons()
{
    for (auto& r : g_pp.rows)
        if (r.icon) {
            sui::set_visible(r.icon, false);
            sui::set_drawable(r.icon, nullptr);
        }
}

static std::string pp_fitted(std::string t, bool small, float room) // one line: "..." when it does not fit
{
    if (text_px(t, small) <= room) return t;
    while (!t.empty() && text_px(t + "...", small) > room) {
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back(); // (a whole UTF-8 letter)
        if (!t.empty() && (unsigned char)t.back() >= 0xC0) t.pop_back();
    }
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t + "...";
}

static void pp_apply(void*)
{
    g_pp_queued = false;
    const PlanetPanel& st = g_pp_want;
    Win main = sui::main_window();
    if (!st.open || !main) {
        if (g_pp.root) {
            sui::set_visible(g_pp.root, false);
            pp_drop_icons();
        }
        g_pp_shown = false;
        g_pp_rect = {};
        return;
    }
    if (!pp_build()) return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    auto place = [&](Win w, bool on, const Rect& r) {
        if (!w) return;
        if (on) sui::set_area(w, r);
        sui::set_visible(w, on);
    };
    // Spore's labels centre their text whatever alignment they get: a left text gets a box as wide as the text
    auto text_at = [&](Win w, const Rect& r, const std::string& t, bool small) {
        if (!w) return;
        sui::set_caption(w, t);
        const float m = kLabelMargin, tw = std::min(r.w(), text_px(t, small)) + 2 * m;
        sui::set_area(w, {r.x1 - m, r.y1, r.x1 - m + tw, r.y2});
        sui::set_visible(w, true);
    };
    const int nrows = std::min<int>((int)st.lines.size(), kPpRows);
    const int nfr = std::min<int>((int)st.friends.size(), kPpFriends);
    float h = kPpHead + kPpSub;
    for (int i = 0; i < nrows; i++) h += kPpRow + kPpGap + (st.lines[i].hover && !st.lines[i].names.empty() ? kPpRowNames : 0);
    if (nfr) h += 26 + nfr * (kPpFriend + kPpGap);
    if (!st.foot.empty()) h += 22;
    h += kPpBottom;
    // right of Spore's stage buttons, centred on them; inside the screen
    float x = st.right > 0 ? st.right + 36 : sw - kPpW - 16.f;
    x = std::max(8.f, std::min(x, sw - kPpW - 8.f));
    float y = st.bottom > st.top ? (st.top + st.bottom) / 2 - h / 2 : 60.f;
    y = std::max(8.f, std::min(y, sh - h - 8.f));
    // the Social Menu open at the top right: the panel under it, as low as the screen lets it (never left over Spore's
    // planet and buttons: tried 2026-11-22, the planet's picture drew over the panel)
    if (g_so_open && g_so_rect.x2 > g_so_rect.x1 && x < g_so_rect.x2 + 8 && x + kPpW > g_so_rect.x1 - 8 &&
        y < g_so_rect.y2 + 8 && y + h > g_so_rect.y1 - 8)
        y = std::max(8.f, std::min(g_so_rect.y2 + 10, sh - h - 4.f));
    g_pp_so_seen = g_so_open ? g_so_rect : Rect{};
    g_pp_rect = {x, y, x + kPpW, y + h};
    sui::set_area(g_pp.root, g_pp_rect);
    if (sui::parent(g_pp.root) != main) sui::add_child(main, g_pp.root);
    if (g_pp.native && !frame_layout(g_pp.frame, kPpW, h) && !g_pp_queued) { // (the Co-op window not laid out yet)
        g_pp_queued = true;
        H->run_outside_frame(pp_apply, nullptr);
    }
    place(g_pp.box, !g_pp.native, {0, 0, kPpW, h});
    sui::bring_to_front(main, g_pp.root);
    sui::set_flag(g_pp.root, sui::kFlagAlwaysInFront, true);
    sui::set_visible(g_pp.root, true);
    g_pp_shown = true;
    place(g_pp.title, true, {0, 18, kPpW, 44});
    sui::set_caption(g_pp.title, st.title);
    float cy = kPpHead;
    text_at(g_pp.sub, {kPpPad + 4, cy + 2, kPpW - kPpPad, cy + kPpSub - 4}, pp_fitted(st.sub, true, kPpW - 2 * kPpPad - 8), true);
    cy += kPpSub;
    Rect lit{};
    for (int i = 0; i < kPpRows; i++) {
        PpRowWin& w = g_pp.rows[i];
        if (i >= nrows) {
            for (Win v : {w.bg, w.icon, w.name, w.line, w.names, w.dot}) place(v, false, {});
            if (w.icon) sui::set_drawable(w.icon, nullptr);
            continue;
        }
        const PlanetLine& l = st.lines[i];
        const bool names = l.hover && !l.names.empty();
        const float top = cy, bot = cy + kPpRow + (names ? kPpRowNames : 0);
        place(w.bg, true, {kPpPad, top, kPpW - kPpPad, bot});
        if (l.hover) lit = {kPpPad, top, kPpW - kPpPad, bot};
        void* drw = l.icon ? sui::drawable(l.icon) : nullptr;
        if (w.icon) {
            if (sui::drawable(w.icon) != drw) sui::set_drawable(w.icon, drw);
            place(w.icon, drw != nullptr, {kPpPad + 5, top + (kPpRow - kPpIcon) / 2, kPpPad + 5 + kPpIcon, top + (kPpRow + kPpIcon) / 2});
            if (drw) sui::set_enabled(w.icon, l.tone != 2); // (a grey stage: its button's disabled look)
        }
        const float tx = kPpPad + 5 + kPpIcon + 8;
        text_at(w.name, {tx, top + 2, kPpW - kPpPad, top + 22}, pp_fitted(l.name, false, kPpW - kPpPad - 6 - tx), false);
        sui::set_label_color(w.name, l.tone == 2 ? kSoOffline : l.hover ? kGold : kWhite);
        const float lx = l.dot ? tx + 14 : tx;
        place(w.dot, l.dot, {tx - 4, top + 21, tx + 12, top + 39});
        if (w.dot && l.dot) sui::set_caption(w.dot, "\xE2\x96\xA0");
        text_at(w.line, {lx, top + 21, kPpW - kPpPad, top + kPpRow - 3}, pp_fitted(l.line, true, kPpW - kPpPad - 6 - lx), true);
        sui::set_label_color(w.line, l.tone == 1 ? kGold : l.tone == 2 ? kSoOffline : kWhite);
        if (names)
            text_at(w.names, {tx + 14, top + kPpRow - 4, kPpW - kPpPad, bot - 2},
                    pp_fitted(l.names, true, kPpW - kPpPad - 6 - tx - 14), true);
        else
            place(w.names, false, {});
        for (Win v : {w.bg, w.icon, w.name, w.line, w.names, w.dot}) // (in order: the box under its contents)
            if (v && sui::visible(v)) sui::bring_to_front(g_pp.root, v);
        cy = bot + kPpGap;
    }
    place(g_pp.lit[0], lit.w() > 0, {lit.x1, lit.y1, lit.x2, lit.y1 + 2});
    place(g_pp.lit[1], lit.w() > 0, {lit.x1, lit.y2 - 2, lit.x2, lit.y2});
    place(g_pp.lit[2], lit.w() > 0, {lit.x1, lit.y1, lit.x1 + 2, lit.y2});
    place(g_pp.lit[3], lit.w() > 0, {lit.x2 - 2, lit.y1, lit.x2, lit.y2});
    for (Win e : g_pp.lit)
        if (e && sui::visible(e)) sui::bring_to_front(g_pp.root, e);
    if (nfr) {
        char head[48];
        snprintf(head, sizeof head, "Friends here  %d", (int)st.friends.size());
        text_at(g_pp.sect, {kPpPad + 4, cy + 3, kPpW - kPpPad, cy + 23}, head, true);
        cy += 26;
    } else
        place(g_pp.sect, false, {});
    for (int i = 0; i < kPpFriends; i++) {
        PpRowWin& w = g_pp.fr[i];
        if (i >= nfr) {
            for (Win v : {w.bg, w.name, w.line, w.names, w.dot}) place(v, false, {});
            continue;
        }
        const float top = cy, bot = cy + kPpFriend;
        place(w.bg, true, {kPpPad, top, kPpW - kPpPad, bot});
        place(w.dot, true, {kPpPad + 2, top + 9, kPpPad + 18, top + 27});
        if (w.dot) sui::set_caption(w.dot, "\xE2\x96\xA0");
        const float tx = kPpPad + 22;
        text_at(w.name, {tx, top + 1, kPpW - kPpPad, top + 19}, pp_fitted(st.friends[i].first, false, kPpW - kPpPad - 6 - tx), false);
        sui::set_label_color(w.name, kSoFriend);
        text_at(w.line, {tx, top + 18, kPpW - kPpPad, bot - 2}, pp_fitted(st.friends[i].second, true, kPpW - kPpPad - 6 - tx), true);
        sui::set_label_color(w.line, kSoOffline);
        place(w.names, false, {});
        for (Win v : {w.bg, w.dot, w.name, w.line})
            if (v) sui::bring_to_front(g_pp.root, v);
        cy = bot + kPpGap;
    }
    if (!st.foot.empty())
        text_at(g_pp.foot, {kPpPad + 4, cy + 2, kPpW - kPpPad, cy + 20}, pp_fitted(st.foot, true, kPpW - 2 * kPpPad - 8), true);
    else
        place(g_pp.foot, false, {});
}

void set_planet_panel(const PlanetPanel& p)
{
    if (!g_ok) return;
    auto same = [](const PlanetPanel& a, const PlanetPanel& b) {
        if (a.open != b.open || a.title != b.title || a.sub != b.sub || a.foot != b.foot || a.friends != b.friends ||
            a.lines.size() != b.lines.size() || std::fabs(a.right - b.right) > 2 || std::fabs(a.top - b.top) > 2 ||
            std::fabs(a.bottom - b.bottom) > 2)
            return false;
        for (size_t i = 0; i < a.lines.size(); i++) {
            const auto &x = a.lines[i], &y = b.lines[i];
            if (x.name != y.name || x.line != y.line || x.names != y.names || x.tone != y.tone || x.dot != y.dot ||
                x.hover != y.hover || x.icon != y.icon)
                return false;
        }
        return true;
    };
    const Rect so = g_so_open ? g_so_rect : Rect{};
    const bool so_moved = p.open && (so.x1 != g_pp_so_seen.x1 || so.y2 != g_pp_so_seen.y2 || so.x2 != g_pp_so_seen.x2);
    if (same(p, g_pp_want) && p.open == g_pp_shown && !so_moved) return;
    g_pp_want = p;
    if (!g_pp_queued) {
        g_pp_queued = true;
        H->run_outside_frame(pp_apply, nullptr);
    }
}

void planet_panel_hide()
{
    g_pp_want = PlanetPanel{};
    if (g_pp.root) sui::set_visible(g_pp.root, false);
    pp_drop_icons();
    g_pp_shown = false;
    g_pp_rect = {};
}

std::string planet_panel_debug()
{
    std::string s = "planet panel (8M A, BUG-9340): ";
    char b[200];
    snprintf(b, sizeof b, "shown=%d frame=%s rect=(%.0f %.0f %.0f %.0f) title='%s'", (int)g_pp_shown,
             g_pp.native ? "native" : "plain", g_pp_rect.x1, g_pp_rect.y1, g_pp_rect.x2, g_pp_rect.y2, g_pp_want.title.c_str());
    s += b;
    if (!g_pp_want.sub.empty()) s += "; " + g_pp_want.sub;
    for (size_t i = 0; i < g_pp_want.lines.size() && i < (size_t)kPpRows; i++) {
        const auto& l = g_pp_want.lines[i];
        void* drw = g_pp.rows[i].icon ? sui::drawable(g_pp.rows[i].icon) : nullptr;
        snprintf(b, sizeof b, "\n  %s%s: %s%s%s (tone %d, icon %s, refs %d)", l.hover ? "* " : "", l.name.c_str(),
                 l.line.c_str(), l.names.empty() ? "" : " | ", l.names.c_str(), l.tone, drw ? "yes" : "no",
                 drw ? sui::refs(drw) : 0);
        s += b;
    }
    for (auto& f : g_pp_want.friends) s += "\n  friend " + f.first + ": " + f.second;
    if (!g_pp_want.foot.empty()) s += "\n  " + g_pp_want.foot;
    return s;
}

} // namespace coop_ui
