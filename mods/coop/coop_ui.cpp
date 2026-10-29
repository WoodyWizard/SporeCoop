#include "coop_ui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
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
constexpr float kW = 760, kH = 600; // 560 + a footer row (version, "Report a problem")
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
COOP_FIX("core.menu_drop", "BUG-0163", g_menu_drop_fix, "ui: the galaxy menu's Co-op item pointer goes with the menu");
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
    Win version = nullptr, report_btn = nullptr; // every page: the version line, "Report a problem"
    // connect page
    std::vector<Win> page_connect;
    Win name_edit = nullptr, addr_edit = nullptr, port_edit = nullptr, join_btn = nullptr, host_btn = nullptr;
    Win lan_hdr = nullptr, host_hint = nullptr, status = nullptr, update_btn = nullptr;
    Win lan_rows[3] = {};
    std::string lan_addr[3];
    Win lan_more = nullptr; // BUG-0183: the next three games on the network
    // lobby page
    std::vector<Win> page_lobby;
    Win session = nullptr, world = nullptr, chat_edit = nullptr, send_btn = nullptr;
    Win ready_btn = nullptr, share_btn = nullptr, leave_btn = nullptr;
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
    Cache c{t, argb, 1, align == sui::kAlignLeft, r};
    if (c.left)
        fit(w, c);
    g_cache[w] = c;
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
    sui::add_handler(w, [fn, name = std::string(name)](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg %s 0x%X src=%p %08X %08X %08X", name.c_str(), m.type, m.source, m.a, m.b, m.c);
        if (sui::is_click(m)) {
            fn();
            return true;
        }
        return false;
    });
    return w;
}

static Win add_edit(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t, int max_len)
{
    Win w = sui::text_edit(D.root, r, t, D.font_body, max_len);
    if (w) {
        page.push_back(w);
        g_named[name] = w;
    }
    return w;
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
    std::string err;
    if (!A.join(addr, err)) {
        set_status(std::string(T("Cannot join: ", "Не удалось подключиться: ")) + err, kRed, g_status_fix ? 30 : 0);
        return;
    }
    save_str("ui", "last_address", addr);
    set_status(std::string(T("Connecting to ", "Подключение к ")) + addr + "...", kYellow);
}

static void do_host()
{
    apply_name();
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

// "Report a problem": SporeKit writes mods\logs\report-<time>.txt (environment, co-op state, the
// last log lines, faults); the folder opens with the file selected so the player can attach it
static void do_report()
{
    std::string p = A.report ? A.report() : "";
    if (p.empty()) {
        set_status(T("This SporeKit cannot write reports (update SporeCoop)", "Этот SporeKit не умеет писать отчёты (обновите SporeCoop)"),
                   kRed);
        return;
    }
    set_status(std::string(T("Report saved: ", "Отчёт сохранён: ")) + p.substr(p.find_last_of("\\/") + 1) +
                   T(" - attach it to an issue on GitHub", " - приложите его к issue на GitHub"),
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

static void do_send_chat()
{
    std::string t = sui::edit_text(D.chat_edit);
    if (t.empty())
        return;
    A.say(t);
    sui::set_edit_text(D.chat_edit, "");
    g_dirty = true;
}

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
    D.version = add_label(common, {40, 548, 500, 570}, A.version_line ? A.version_line() : "", kDim);
    D.report_btn = add_button(common, "report", {510, 542, 720, 574}, T("Report a problem", "Сообщить об ошибке"),
                              do_report);

    // ---- connect page ----
    auto& P = D.page_connect;
    add_label(P, {40, 112, 225, 140}, T("Your name", "Ваше имя"), kWhite);
    D.name_edit = add_edit(P, "name", {230, 108, 520, 140}, A.name(), 24);
    add_label(P, {40, 158, 720, 186}, T("Join a friend's game", "Подключиться к другу"), kGold);
    add_label(P, {40, 198, 225, 226}, T("Host address", "Адрес хоста"), kWhite);
    D.addr_edit = add_edit(P, "address", {230, 194, 520, 226}, load_str("ui", "last_address", ""), 64);
    D.join_btn = add_button(P, "join", {540, 190, 720, 230}, T("Join", "Подключиться"),
                            [] { do_join(sui::edit_text(D.addr_edit)); });
    D.lan_hdr = add_label(P, {40, 242, 590, 268}, "", kDim);
    D.lan_more = add_button(P, "lanmore", {600, 238, 720, 268}, T("More >", "Ещё >"), [] {
        g_lan_first += 3; // refresh_connect wraps it to the first page
        g_dirty = true;
    });
    for (int i = 0; i < 3; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "lan%d", i);
        D.lan_rows[i] = add_button(P, nm, {40, 272.f + i * 38, 720, 306.f + i * 38}, "", [i] {
            if (!D.lan_addr[i].empty()) {
                sui::set_edit_text(D.addr_edit, D.lan_addr[i]);
                do_join(D.lan_addr[i]);
            }
        });
    }
    add_label(P, {40, 390, 720, 418}, T("Host a game", "Создать игру"), kGold);
    add_label(P, {40, 430, 225, 458}, T("Port (UDP)", "Порт (UDP)"), kWhite);
    D.port_edit = add_edit(P, "port", {230, 426, 330, 458},
                           load_str("ui", "host_port", std::to_string(A.default_port).c_str()), 5);
    D.host_btn = add_button(P, "host", {540, 422, 720, 462}, T("Host", "Создать"), do_host);
    D.host_hint = add_label(P, {40, 466, 720, 492}, "", kDim);
    D.status = add_label(P, {40, 498, 560, 526}, "", kYellow);
    D.update_btn = add_button(P, "update", {570, 494, 720, 530}, "", [] {
        if (A.update_do)
            A.update_do();
        g_dirty = true;
    });

    // ---- lobby page ----
    auto& L = D.page_lobby;
    D.session = add_label(L, {40, 104, 720, 130}, "", kWhite);
    add_label(L, {40, 138, 300, 162}, T("Player", "Игрок"), kDim);
    add_label(L, {300, 138, 540, 162}, T("Stage", "Этап"), kDim);
    add_label(L, {540, 138, 620, 162}, T("Ping", "Пинг"), kDim);
    add_label(L, {620, 138, 720, 162}, T("Ready", "Готов"), kDim);
    for (int i = 0; i < 4; i++) {
        float y = 166.f + i * 28;
        D.row_name[i] = add_label(L, {40, y, 300, y + 26}, "", kWhite);
        D.row_stage[i] = add_label(L, {300, y, 540, y + 26}, "", kWhite);
        D.row_ping[i] = add_label(L, {540, y, 620, y + 26}, "", kWhite);
        D.row_ready[i] = add_label(L, {620, y, 720, y + 26}, "", kWhite);
    }
    D.world = add_label(L, {40, 284, 720, 310}, "", kYellow);
    for (int i = 0; i < 4; i++)
        D.chat_lines[i] = add_label(L, {40, 316.f + i * 23, 720, 338.f + i * 23}, "", kWhite);
    std::vector<Win> arrows; // shown by refresh_lobby only when there is more to see
    D.chat_up = add_button(arrows, "chatup", {722, 316, 756, 346}, "^", [] { g_chat_scroll++; g_dirty = true; });
    D.chat_down = add_button(arrows, "chatdown", {722, 376, 756, 406}, "v", [] { g_chat_scroll--; g_dirty = true; });
    // the own creation of this stage: "My creature: Kirgle  < >"
    D.creation = add_label(L, {40, 408, 560, 434}, "", kGold);
    D.creation_prev = add_button(L, "cprev", {570, 404, 640, 434}, "<", [] {
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle)
            A.civ_role_cycle(-1);
        else if (A.creation_cycle)
            A.creation_cycle(-1);
        g_dirty = true;
    });
    D.jump_btn = add_button(L, "jump", {400, 404, 560, 434}, "", [] {
        if (A.jump_do)
            A.jump_do();
        g_dirty = true;
    });
    D.give_btn = add_button(L, "give", {570, 404, 720, 434}, "", [] {
        if (A.give_do)
            A.give_do();
        g_dirty = true;
    });
    // 3 or 4 players: ">" picks the next other player as the recipient (BUG-0075)
    D.give_next = add_button(L, "givenext", {520, 404, 560, 434}, ">", [] {
        if (A.give_cycle)
            A.give_cycle();
        g_dirty = true;
    });
    D.creation_next = add_button(L, "cnext", {650, 404, 720, 434}, ">", [] {
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
    D.chat_edit = add_edit(L, "chatedit", {40, 438, 560, 470}, "", 120);
    D.send_btn = add_button(L, "send", {570, 434, 720, 474}, T("Send", "Отправить"), do_send_chat);
    D.ready_btn = add_button(L, "ready", {40, 488, 240, 528}, "", [] {
        A.set_ready(!A.ready());
        g_dirty = true;
    });
    D.share_btn = add_button(L, "share", {260, 488, 480, 528}, T("Send my world", "Отправить мир"), [] {
        A.share_world();
        set_status(T("Sending the world to everyone...", "Мир отправляется всем..."), kYellow, g_status_fix ? 5 : 0);
    });
    D.leave_btn = add_button(L, "leave", {500, 488, 720, 528}, "", [] {
        A.leave();
        set_status(T("Left the session", "Вы вышли из игры"), kYellow, g_status_fix ? 5 : 0);
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
    D.space_back = add_button(S, "sback", {40, 488, 240, 528}, T("< Lobby", "< Лобби"), [] {
        g_space_page = false;
        g_dirty = true;
    });
    // more rows than fit: pages
    D.space_prev = add_button(S, "sprev", {500, 488, 570, 528}, "<", [] {
        g_space_first = std::max(0, g_space_first - kSpaceRows);
        g_dirty = true;
    });
    D.space_pageno = add_label(S, {575, 494, 645, 522}, "", kDim, sui::kAlignCenter);
    D.space_next = add_button(S, "snext", {650, 488, 720, 528}, ">", [] {
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
    sui::set_visible(root, false);
    g_cache[root] = Cache{"", 0, 0};
    H->log(M, SK_INFO, "ui: co-op dialog ready");
}

static void refresh_connect()
{
    bool scanning = A.role() == 0;
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
        for (size_t i = 0; i < ips.size() && i < 3; i++)
            hint += (i ? ",  " : "") + ips[i] + ":" + port;
    }
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

static void refresh_lobby()
{
    text(D.session, A.session_line());
    auto rows = A.players();
    for (int i = 0; i < 4; i++) {
        bool have = i < (int)rows.size();
        show(D.row_name[i], have);
        show(D.row_stage[i], have);
        show(D.row_ping[i], have);
        show(D.row_ready[i], have);
        if (!have)
            continue;
        const auto& p = rows[i];
        std::string n = p.name;
        if (p.host)
            n += T("  (host)", "  (хост)");
        if (p.me)
            n += T("  - you", "  - вы");
        text(D.row_name[i], n);
        color(D.row_name[i], p.me ? kGold : kWhite);
        text(D.row_stage[i], p.stage);
        text(D.row_ping[i], p.me ? "" : (p.ping_ms >= 0 ? std::to_string(p.ping_ms) + T(" ms", " мс") : "?"));
        text(D.row_ready[i], p.ready ? T("ready", "готов") : "-");
        color(D.row_ready[i], p.ready ? kGreen : kDim);
    }
    std::string wl = A.world_line ? A.world_line() : "";
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
    std::string cname, cl;
    if (A.creation && A.creation(ck, cname, cown)) {
        cl = ck == 1 ? T("My cell: ", "Моя клетка: ") : ck == 2 ? T("My ship: ", "Мой корабль: ") : T("My creature: ", "Моё существо: ");
        cl += cname.empty() ? T("the world's own", "из мира хоста") : cname;
        if (ck != 2 && cown > 0)
            cl += std::string(T("  (in My Creations: ", "  (своих: ")) + std::to_string(cown) + ")";
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
    text(D.ready_btn, A.ready() ? T("Not ready", "Не готов") : T("I'm ready", "Я готов"));
    show(D.share_btn, host);
    text(D.share_btn, A.vote_active && A.vote_active() ? T("Start now", "Начать сейчас") : T("Send my world", "Отправить мир"));
    text(D.leave_btn, host ? T("Stop hosting", "Закрыть игру") : T("Leave", "Выйти"));
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

static void refresh()
{
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
    show(D.root, g_want_open);
    if (!g_want_open) {
        if (A.lan_scan)
            A.lan_scan(false);
        return;
    }
    bool lobby = A.role() != 0;
    bool space = lobby && A.mode && A.mode() == modes::kSpace;
    if (!space)
        g_space_page = false;
    for (Win w : D.page_connect)
        show(w, !lobby);
    for (Win w : D.page_lobby)
        show(w, lobby && !g_space_page);
    for (Win w : D.page_space)
        show(w, lobby && g_space_page);
    show(D.space_btn, space && !g_space_page);
    if (!lobby || g_space_page) {
        show(D.chat_up, false);
        show(D.chat_down, false);
    }
    text(D.subtitle, lobby && g_space_page ? T("CO-OP: SPACE", "КООПЕРАТИВ: КОСМОС")
                     : lobby ? T("CO-OP LOBBY", "ЛОББИ КООПЕРАТИВА") : T("CO-OP", "КООПЕРАТИВ"));
    if (A.version_line)
        text(D.version, A.version_line());
    if (A.lan_scan)
        A.lan_scan(!lobby);
    if (lobby && g_space_page)
        refresh_space();
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
    uint32_t nclicks = mouse::clicks();
    bool lb = mouse::g_fix ? nclicks != g_lb_seen : (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_lb_seen = nclicks;
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
    if (g_menu_btn && (!g_menu_drop_fix || in_menu()))
        sui::set_visible(g_menu_btn, false);
}

std::string debug_state()
{
    std::string s = "open=" + std::to_string(g_want_open) + " page=" +
                    (A.role() ? (g_space_page ? "space" : "lobby") : "connect") +
                    " menu_button=" + (g_menu_btn ? "yes" : "no") + " in_menu=" + (in_menu() ? "1" : "0") + "\n";
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
    return s;
}

uint32_t text_font() { return g_text_font; }

bool russian() { return g_ru; }

} // namespace coop_ui
