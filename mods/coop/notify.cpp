#include "notify.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>
#include <vector>

#include "notify_text.h"
#include "sui.h"

namespace notify {

static const SkHost* H;
static std::function<uint32_t()> g_font;
static std::mutex g_mu;
static std::deque<std::pair<std::string, std::string>> g_queue; // (text, kind) waiting to be built (outside the frame)
static bool g_build_queued, g_anim_queued;
static double g_last_frame; // UI is touched only while frames run (not under a modal game box)
static unsigned g_shown, g_failed;

constexpr float kW = 560, kH = 58, kGap = 8, kMargin = 24;
constexpr double kIn = 0.25, kOut = 0.35;
constexpr size_t kMaxVisible = 5;

struct Toast {
    sui::Win w = nullptr;
    std::vector<sui::Win> labels; // ours (sui::label keeps one reference for the mod: BUG-1025)
    std::string text, kind;
    double t0 = 0, life = 6;
    float y = -1; // current top (slides when panels above go away)
};
static std::vector<Toast> g_toasts; // oldest first (top of the stack)
static sui::Win g_root;             // the main window the panels live in

using notify_text::kind_of;
using notify_text::lines_of;

// The dialog font: about 9.3 px per letter cell at 1080p; a label centres its text, so its box is
// made as wide as the text (coop_ui does the same). 46 cells fit a line; longer texts take two.
constexpr uint32_t kDialogFont = 0x015A5D11; // the co-op window text font (logged by coop_ui)

static float text_w(const std::string& s)
{
    return (notify_text::g_fix ? utf8::columns(s) : (int)s.size()) * 9.3f + 30.f; // cells, not bytes (BUG-0129)
}

static void screen(float& sw, float& sh)
{
    int w = 0, h = 0;
    H->screen_size(&w, &h);
    sw = w > 0 ? (float)w : 1440.f;
    sh = h > 0 ? (float)h : 1080.f;
}

// the co-op status block's bottom (coop.cpp, 0 = none): the panels start below it (BUG-0155)
static std::atomic<float> g_top_min{0.f};
void set_top_min(float y) { g_top_min = y; }

static float top_of(size_t i, float sh) { return std::max(sh * 0.15f, g_top_min.load()) + i * (kH + kGap); }

// BUG-1025 (tests, `coop.notify` status): what is left of the dropped panels, from the game's reference counts read
// while the windows are alive (never after: a window the game frees may be gone). panel_refs: the panel's count
// before it is taken out of the main window (1 = only the main window's: freed with it); label_refs: a label's count
// before (ours + the panel's); kept: labels with a reference left once the panel is freed (leaked)
static unsigned g_dropped, g_labels_freed, g_labels_kept;
static int g_panel_refs = -1, g_label_refs = -1;

// outside the frame (build, animate, shutdown at the unload)
static void remove(Toast& t)
{
    if (t.w && g_root) {
        g_panel_refs = sui::refs(t.w);
        for (sui::Win l : t.labels) {
            int n = g_label_refs = sui::refs(l);
            if (sui::g_release_fix) { // ours back while the panel still holds it: the panel's release frees it
                sui::release(l);
                n--;
            }
            (n <= 1 && g_panel_refs == 1 ? g_labels_freed : g_labels_kept)++; // (1: the panel's)
        }
        sui::remove_child(g_root, t.w);
        g_dropped++;
    }
    t.labels.clear();
    t.w = nullptr;
}

static bool frames_running() { return H->time_seconds() - g_last_frame < 0.25; }

// outside the frame: build the queued panels
static void build(void*)
{
    std::deque<std::pair<std::string, std::string>> q;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_build_queued = false;
        if (!frames_running()) // a modal box (e.g. "game saved") holds the frames: later (tick)
            return;
        q.swap(g_queue);
    }
    sui::Win root = sui::main_window();
    if (root != g_root) { // another main window (should not happen): forget the old panels
        g_toasts.clear();
        g_root = root;
    }
    if (!root)
        return;
    float sw, sh;
    screen(sw, sh);
    for (auto& [text, given] : q) {
        while (g_toasts.size() >= kMaxVisible) {
            remove(g_toasts.front());
            g_toasts.erase(g_toasts.begin());
        }
        std::string kind = given.empty() ? kind_of(text) : given;
        char name[32];
        snprintf(name, sizeof name, "toast_%s", kind.c_str());
        float y = top_of(g_toasts.size(), sh);
        sui::Win w = sui::image_window(root, sui::fnv(name), sui::fnv("sporecoop"), sw + 10, y);
        if (!w) {
            g_failed++;
            continue;
        }
        sui::set_flag(w, sui::kFlagIgnoreMouse, true);
        sui::set_flag(w, sui::kFlagAlwaysInFront, true);
        uint32_t font = g_font && g_font() ? g_font() : kDialogFont;
        auto lines = lines_of(text);
        std::vector<sui::Win> labels;
        for (size_t li = 0; li < lines.size(); li++) {
            float y0 = lines.size() == 1 ? 0.f : (li == 0 ? 5.f : 27.f);
            float h = lines.size() == 1 ? kH : 26.f;
            sui::Rect lr{68, y0, 68 + std::min(text_w(lines[li]), kW - 80), y0 + h};
            if (sui::Win l = sui::label(w, lr, lines[li], font, 0xFFFFFFFF, sui::kAlignLeft)) {
                sui::set_flag(l, sui::kFlagIgnoreMouse, true);
                labels.push_back(l);
            }
        }
        sui::bring_to_front(root, w);
        Toast t;
        t.w = w;
        t.labels = std::move(labels);
        t.text = text;
        t.kind = kind;
        t.t0 = H->time_seconds();
        t.life = notify_text::life_of(kind);
        t.y = y;
        g_toasts.push_back(t);
        g_shown++;
    }
}

// outside the frame: move the panels
static void animate(void*)
{
    g_anim_queued = false;
    if (!g_root || g_toasts.empty() || !frames_running())
        return;
    float sw, sh;
    screen(sw, sh);
    double now = H->time_seconds();
    float x_in = sw - kW - kMargin, x_out = sw + 10;
    for (size_t i = 0; i < g_toasts.size();) {
        Toast& t = g_toasts[i];
        double age = now - t.t0;
        if (age > t.life + kOut) {
            remove(t);
            g_toasts.erase(g_toasts.begin() + i);
            continue;
        }
        float k; // 0 = out of the screen, 1 = in place (ease out / ease in)
        if (age < kIn)
            k = 1.f - (float)std::pow(1.0 - age / kIn, 3);
        else if (age > t.life)
            k = 1.f - (float)std::pow((age - t.life) / kOut, 2);
        else
            k = 1.f;
        float want_y = top_of(i, sh);
        t.y += (want_y - t.y) * 0.25f; // close the gap of a panel that went away
        if (std::fabs(want_y - t.y) < 0.5f)
            t.y = want_y;
        float x = x_out + (x_in - x_out) * k;
        sui::set_area(t.w, {x, t.y, x + kW, t.y + kH});
        i++;
    }
}

void init(const SkHost* host, SkMod*, std::function<uint32_t()> font)
{
    H = host;
    g_font = std::move(font);
}

void show(const std::string& text, const char* kind)
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_queue.emplace_back(text, kind && notify_text::g_fix ? kind : "");
    while (g_queue.size() > kMaxVisible)
        g_queue.pop_front();
    if (!g_build_queued && frames_running()) {
        g_build_queued = true;
        H->run_outside_frame(build, nullptr); // UI windows are never built inside Present
    }
}

void tick(double now)
{
    g_last_frame = now;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_queue.empty() && !g_build_queued) {
            g_build_queued = true;
            H->run_outside_frame(build, nullptr);
        }
    }
    if (!g_toasts.empty() && !g_anim_queued) {
        g_anim_queued = true;
        H->run_outside_frame(animate, nullptr);
    }
}

void shutdown()
{
    for (auto& t : g_toasts)
        remove(t);
    g_toasts.clear();
}

std::string status()
{
    char b[160];
    snprintf(b, sizeof b, "notify: shown=%u failed=%u visible=%u\n", g_shown, g_failed, (unsigned)g_toasts.size());
    std::string s = b;
    snprintf(b, sizeof b, "notify dropped=%u labels_freed=%u labels_kept=%u panel_refs=%d label_refs=%d release=%d\n",
             g_dropped, g_labels_freed, g_labels_kept, g_panel_refs, g_label_refs, (int)sui::g_release_fix);
    s += b;
    for (auto& t : g_toasts) {
        sui::Rect r = sui::area(t.w);
        snprintf(b, sizeof b, "toast %s (%.0f %.0f %.0f %.0f) ", t.kind.c_str(), r.x1, r.y1, r.x2, r.y2);
        s += b + ("'" + t.text + "'"); // whole (a fixed buffer cut long texts)
        for (auto& l : lines_of(t.text))
            s += " | " + l;
        s += "\n";
    }
    return s;
}

} // namespace notify
