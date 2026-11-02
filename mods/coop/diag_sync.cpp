// Support diagnostics: the session clock, bug marks, the sync self-check (diag_coop.h,
// docs/REPORTING.md#reporting-support-answers).
#include "diag_coop.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <deque>
#include <cstddef>
#include <cstdio>
#include <map>

#include "fixes.h"
#include "modes.h"
#include "fixes.h"
#include "net/diagmsg.h"

namespace support {

namespace dg = net::diag;

const SkHost* g_H;
SkMod* g_M;
Probe g_P;
Counters g_c;

#define SLOG(...) g_H->log(g_M, SK_INFO, __VA_ARGS__)
#define SWARN(...) g_H->log(g_M, SK_WARN, __VA_ARGS__)

static bool kit_has_clock()
{
    return g_H->size >= offsetof(SkHost, log_session_clock) + sizeof(void*) && g_H->log_session_clock;
}

// ---------------- the session clock ----------------
static double g_host_t0 = -1;     // host: the session began (local time)
static dg::ClockSync g_clock;     // guest: the host's clock
static double g_clock_set = 1e18; // the offset given to SporeKit's log (1e18: none)
static double g_clock_next = 0;
static uint32_t g_clock_seq = 0;
static bool g_clock_told;

bool in_session() { return g_P.sess && g_P.sess->connected(); }

static bool have_clock() { return in_session() && (g_P.sess->role() == net::Role::Host ? g_host_t0 >= 0 : g_clock.valid()); }

static double clock_offset() { return g_P.sess->role() == net::Role::Host ? -g_host_t0 : g_clock.offset(); }

double session_time(bool* ok)
{
    double now = g_H->time_seconds();
    bool k = have_clock();
    if (ok)
        *ok = k;
    return k ? now + clock_offset() : now;
}

static void set_log_clock()
{
    if (!kit_has_clock())
        return;
    if (!have_clock()) {
        if (g_clock_set < 1e17)
            g_H->log_session_clock(0, 0);
        g_clock_set = 1e18;
        return;
    }
    double off = clock_offset();
    if (std::fabs(off - g_clock_set) > 0.002) { // a better sample moved it by more than 2 ms
        g_H->log_session_clock(off, 1);
        g_clock_set = off;
    }
}

static void clock_tick(double now)
{
    if (!in_session() || g_P.sess->role() != net::Role::Client)
        return;
    if (now < g_clock_next)
        return;
    // quickly at first (a good sample needs a quiet moment), then now and then (clock drift)
    g_clock_next = now + (g_clock.samples() < 8 ? 1.0 : 15.0);
    dg::ClockReq q{++g_clock_seq, now};
    net::Writer w;
    q.write(w);
    g_P.sess->send_unreliable_to(0, net::MSG_DIAG, w);
}

static void on_clock_answer(const dg::ClockAns& a)
{
    double now = g_H->time_seconds();
    g_clock.add(a.t_echo, now, a.t_session);
    set_log_clock();
    if (!g_clock_told && g_clock.valid()) {
        g_clock_told = true;
        SLOG("clock: the session clock of the host: session time %.3f, round trip %.1f ms (error under %.0f ms)",
             now + g_clock.offset(), g_clock.rtt() * 1000, g_clock.rtt() * 500 + 1);
    }
}

// ---------------- report screenshots ----------------
// Each MARK (own or a partner's: every player's report shows what that player saw at that moment) and
// "Report a problem" ask SporeKit for a screenshot of the frame for the report archive
// (docs/REPORTING.md#reporting-shots). SporeKit takes it at the next Present, never waiting.
static bool kit_has_shot()
{
    return g_H->size >= offsetof(SkHost, report_shot_pending) + sizeof(void*) && g_H->report_shot &&
           g_H->report_shot_pending;
}

bool shot_setting() { return g_H->config_int(g_M, "report", "screenshot", 1) != 0; }

int report_shot(const char* tag, const std::string& caption, std::string* name)
{
    if (!shot_setting()) {
        SLOG("report shot: off ([report] screenshot=0 in coop.ini)");
        return -1;
    }
    if (!kit_has_shot())
        return 0;
    char out[300] = {0};
    int r = g_H->report_shot(tag, caption.c_str(), out, sizeof out);
    if (name)
        *name = r ? out : "";
    return r ? 1 : 0;
}

int report_shot_state() { return kit_has_shot() ? g_H->report_shot_pending() : -1; }

void report_shots_forget()
{
    if (!kit_has_shot())
        return;
    char out[64];
    g_H->report_shot(nullptr, nullptr, out, sizeof out);
}

// ---------------- bug marks ----------------
static std::string clean(std::string s)
{
    for (auto& c : s)
        if ((unsigned char)c < 0x20)
            c = ' ';
    return s;
}

static void log_mark(const dg::Mark& m, bool own)
{
    g_c.marks++;
    std::string who = clean(m.name);
    char line[400];
    if (m.t_session >= 0)
        snprintf(line, sizeof line, "MARK by '%s' (player %u) at s %.3f: %s", who.c_str(), m.origin, m.t_session,
                 clean(m.text).c_str());
    else
        snprintf(line, sizeof line, "MARK by '%s' (player %u), no session clock: %s", who.c_str(), m.origin,
                 clean(m.text).c_str());
    SWARN("%s", line);
    // the screen of this game at the mark, for the report (a "report shot:" line follows when written).
    // Its caption must not read "MARK by": tools (merge_logs.py, coop_diag.py) count those lines as marks
    char cap[400];
    if (m.t_session >= 0)
        snprintf(cap, sizeof cap, "the mark by '%s' (player %u) at s %.3f: %s", who.c_str(), m.origin, m.t_session,
                 clean(m.text).c_str());
    else
        snprintf(cap, sizeof cap, "the mark by '%s' (player %u), no session clock: %s", who.c_str(), m.origin,
                 clean(m.text).c_str());
    bool shot = report_shot("mark", cap) > 0;
    if (own) {
        if (g_P.chat_log)
            g_P.chat_log("* marked for the report: " + m.text);
        if (g_P.feed)
            g_P.feed(shot ? "Marked for the report with a screenshot (F9, \"Report a problem\" makes the file to send)"
                          : "Marked for the report (F9, \"Report a problem\" makes the file to send)");
    } else if (g_P.chat_log)
        g_P.chat_log("* " + who + " marked a problem for the report: " + m.text);
}

void mark(const std::string& text)
{
    dg::Mark m;
    bool ok = false;
    double t = session_time(&ok);
    m.t_session = ok ? t : -1;
    m.origin = in_session() ? g_P.sess->my_id() : 0;
    m.name = g_P.my_name ? g_P.my_name() : "?";
    std::string tx = text;
    while (!tx.empty() && isspace((unsigned char)tx.back()))
        tx.pop_back();
    m.text = tx.empty() ? "(no text)" : tx.substr(0, utf8::cut_bytes(tx, dg::kMarkTextMax));
    log_mark(m, true);
    if (!in_session())
        return;
    net::Writer w;
    m.write(w);
    if (g_P.sess->role() == net::Role::Host)
        g_P.sess->broadcast_reliable(net::MSG_DIAG, w);
    else
        g_P.sess->send_reliable_to(0, net::MSG_DIAG, w);
}

bool chat_command(const std::string& text)
{
    size_t i = 0;
    while (i < text.size() && isspace((unsigned char)text[i]))
        i++;
    if (text.size() - i < 4)
        return false;
    std::string head = text.substr(i, 4);
    for (auto& c : head)
        c = (char)tolower((unsigned char)c);
    if (head != "/bug" || (text.size() > i + 4 && !isspace((unsigned char)text[i + 4])))
        return false;
    std::string rest = text.substr(std::min(text.size(), i + 5));
    mark(rest);
    return true;
}

// ---------------- the sync self-check ----------------
static double g_sync_interval = 30.0; // [coop] sync_check (s), 0 = off
static std::string g_force;           // tests: coop.diag sync force <shift|drop|ghost|dna>

struct GuestCheck {
    uint32_t seq = 0;
    bool waiting = false, recheck = false, deaf = false;
    double asked = 0, next = 0;
    int unanswered = 0;
    std::vector<dg::Issue> first;
    std::string last_mismatch;  // ids of the last reported mismatch (repeats are rate limited)
    double last_mismatch_at = -1e9;
    int repeats = 0;
    int ok_window = 0;          // OKs since the last OK summary
    double ok_window_since = 0;
    uint32_t ok_told_place = 0; // the stage of the last OK line written
};
// BUG-0379: the host's own shared totals (dna, cell food) of the last seconds. A guest shows a total
// the host sends it (MSG_CELL_FOOD) up to ~0.5 s after the host counted it, and the host builds its
// digest a round trip after the guest built its own: in a 4-player Cell game that ate every few
// seconds two checks 3 s apart each caught the guest one meal behind ("cell food: host 252, guest
// 240", the guest at 252 0.15 s later), a MISMATCH without a desync. A guest value the host had
// within kTotalLag s before it asked is no difference.
static bool g_fix_total_lag = true;
COOP_FIX("diag.total_lag", "BUG-0379", g_fix_total_lag, "sync-check: a guest's total the host had within 3 s before asking is no difference (food sent late)");
constexpr double kTotalLag = 3.0;
struct TotalAt {
    double t;
    uint8_t id;
    float v;
};
static std::deque<TotalAt> g_host_totals;
static double g_host_totals_next;

static std::map<uint8_t, GuestCheck> g_guests; // host
static bool g_rejoin_grace_fix = true;
COOP_FIX("diag.rejoin_grace", "BUG-0380", g_rejoin_grace_fix,
         "sync-check: a player who leaves and joins again is first checked 10 s after the join, like a newcomer");
static uint32_t g_guest_told_place;            // guest: the stage of the last OK line written
static uint32_t g_seq;

// BUG-0378: players and ships are not compared where neither game has a place (world 0)
static bool g_no_place_fix = true;
COOP_FIX("sync.no_place", "BUG-0378", g_no_place_fix,
         "sync-check: no players / ships compared in world 0 (the galaxy view: nobody's ship is shown there)");

// BUG-0378: in Space a player's allowed distance grows with its speed. The guest's digest is read
// when the host's request arrives, the host's when the guest's digest arrives (both at the start of
// the mod's frame, before any puppet is driven: what the frames showed); a puppet is drawn 0.10 s
// behind its newest state (interpolation), which is up to one send interval (1/15 s at 30 fps) plus
// one way old. So a ship's two positions are ~0.2 s + a round trip (+ half the owner's round trip
// when the state came through the host) apart in time: at Space speeds (30-100+ m/s) 10-30 m
// (campaign "p apart" 10-30 m, BUG-0378). Off: the fixed 10 m.
static bool g_speed_slack_fix = true;
COOP_FIX("sync.speed_slack", "BUG-0378", g_speed_slack_fix,
         "sync-check: in Space a player's position limit grows with its speed x the time between the two reads");

static void add_speed_slack(dg::Limits& l, uint32_t mode, uint8_t guest)
{
    if (!g_speed_slack_fix || mode != modes::kSpace || !g_P.player_speeds || !g_P.sess)
        return;
    auto rtt = [](uint8_t id) {
        net::Session::Link lk;
        return g_P.sess->link_of(id, lk) && lk.rtt_ms >= 0 ? lk.rtt_ms / 1000.0 : 0.1;
    };
    double base = 0.2 + rtt(guest);
    for (auto& [id, v] : g_P.player_speeds()) {
        double t = base + (id != 0 && id != guest ? rtt(id) / 2 : 0.0);
        t = std::min(t, 1.0);
        if (std::isfinite(v) && v > 0)
            l.slack[dg::make_key('p', id)] = (float)std::min(v * t, 300.0);
    }
}

// BUG-0379: in a crowded pool the host's choice of a guest's 900 nearest cells (around its view
// centre, a state late) and the compare's (around the guest's own position) differ near rank 900
static bool g_fix_cell_margin = true;
COOP_FIX("diag.cell_margin", "BUG-0379", g_fix_cell_margin, "sync-check: a guest must have only the nearest 80% of the 900 cells it is sent (the host picks them around a late position)");

static dg::Limits limits_for(uint32_t mode, uint32_t world)
{
    dg::Limits l;
    l.no_place = g_no_place_fix && world == 0;
    if (!g_fix_cell_margin)
        l.cell_sure = 1.f;
    if (mode == modes::kTribe || mode == modes::kCiv)
        l.npc_missing = false; // RTS stages: NPCs are sent around the tribe / cities
    return l;
}

static dg::Digest build_digest(uint32_t seq)
{
    dg::Digest d;
    d.seq = seq;
    d.self = in_session() ? g_P.sess->my_id() : 0;
    d.mode = g_P.mode ? g_P.mode() : 0;
    d.world = g_P.place ? g_P.place() : 0;
    uint32_t ed = 0;
    d.vote = g_P.vote && g_P.vote(ed);
    d.vote_editor = d.vote ? ed : 0;
    float dna;
    if (g_P.dna && g_P.dna(dna))
        d.totals.push_back({dg::kTotalDna, dna});
    int food;
    if (g_P.cell_food && g_P.cell_food(food))
        d.totals.push_back({dg::kTotalCellFood, (float)food});
    if (g_P.players_out)
        d.out = g_P.players_out();
    if (g_P.entries)
        for (auto& e : g_P.entries())
            if (uint32_t k = dg::parse_key(e.key))
                d.entries.push_back({k, e.pos.x, e.pos.y, e.pos.z});
    // tests: a difference on purpose (coop.diag sync force ...)
    if (g_force == "shift")
        for (auto& e : d.entries)
            if (e.key == dg::make_key('p', d.self))
                e.x += 100;
    if (g_force == "drop")
        for (size_t i = 0; i < d.entries.size(); i++)
            if (dg::key_kind(d.entries[i].key) != 'p') {
                d.entries.erase(d.entries.begin() + i);
                break;
            }
    if (g_force == "ghost")
        d.entries.push_back({dg::make_key('n', 0xFFFFF0), 0, 0, 0});
    if (g_force == "dna")
        for (auto& t : d.totals)
            if (t.first == dg::kTotalDna)
                t.second += 50;
    return d;
}

static std::string place_text(uint32_t mode, uint32_t world)
{
    char b[64];
    snprintf(b, sizeof b, "%s %08X", modes::mode_name(mode), world);
    return b;
}

// BUG-0381: under a MISMATCH, what this game knows of the NPCs / citizens named in it (at most 3)
static void describe_keys(const std::string& text)
{
    if (!g_P.describe)
        return;
    int n = 0;
    for (size_t i = 0; i + 1 < text.size() && n < 3; i++) {
        char k = text[i];
        if ((k != 'n' && k != 't') || (i && text[i - 1] != '(' && text[i - 1] != ' ') || !isdigit((unsigned char)text[i + 1]))
            continue;
        size_t j = i + 1;
        while (j < text.size() && isdigit((unsigned char)text[j]))
            j++;
        std::string key = text.substr(i, j - i), d = g_P.describe(key);
        SLOG("sync-check:   %s here: %s", key.c_str(), d.empty() ? "not listed" : d.c_str());
        n++;
        i = j;
    }
}

static std::string name_of(uint8_t id) { return g_P.sess ? g_P.sess->name_of(id) : "?"; }

static void send_result(uint8_t to, uint32_t seq, uint8_t state, int objects, const std::string& text)
{
    dg::ResultMsg m{seq, state, (uint16_t)std::min(objects, 65535), text};
    net::Writer w;
    m.write(w);
    g_P.sess->send_reliable_to(to, net::MSG_DIAG, w);
}

// host: a guest's digest arrived
static void on_digest(uint8_t from, const dg::Digest& gd, size_t bytes)
{
    auto it = g_guests.find(from);
    if (it == g_guests.end() || !it->second.waiting || gd.seq != it->second.seq)
        return;
    GuestCheck& g = it->second;
    g.waiting = false;
    g.unanswered = 0;
    g_c.sync_bytes += bytes;
    double now = g_H->time_seconds();
    double t0 = now;
    dg::Digest hd = build_digest(gd.seq);
    dg::Limits lim = limits_for(hd.mode, hd.world);
    add_speed_slack(lim, hd.mode, from);
    std::vector<std::pair<uint8_t, float>> recent;
    if (g_fix_total_lag)
        for (auto& r : g_host_totals)
            if (r.t >= g.asked - kTotalLag)
                recent.push_back({r.id, r.v});
    dg::Compared c = dg::compare(hd, gd, lim, g_fix_total_lag ? &recent : nullptr);
    double ms = (g_H->time_seconds() - t0) * 1000;
    g_c.sync_checks++;
    g_c.sync_ms_sum += ms;
    g_c.sync_ms_n++;
    g_c.sync_ms_max = std::max(g_c.sync_ms_max, ms);
    g.next = now + g_sync_interval;
    std::string who = "player " + std::to_string(from) + " '" + name_of(from) + "'";
    if (c.other_place) {
        g.recheck = false;
        if (g_detail >= 2)
            SLOG("sync-check: %s plays elsewhere (%s), not compared", who.c_str(), dg::describe(c.issues).c_str());
        send_result(from, gd.seq, dg::kResOtherPlace, 0, "");
        return;
    }
    std::vector<dg::Issue> issues = c.issues;
    if (g.recheck)
        issues = dg::persistent(g.first, c.issues); // only what lasted 3 s
    std::string place = place_text(hd.mode, hd.world);
    if (issues.empty()) {
        g_c.sync_ok++;
        if (g.recheck && g_detail >= 2)
            SLOG("sync-check: %s OK on the recheck (the difference passed)", who.c_str());
        g.recheck = false;
        g.ok_window++;
        if (g.ok_told_place != (hd.mode ^ hd.world) && g_detail >= 1) { // the first OK of each stage
            g.ok_told_place = hd.mode ^ hd.world;
            SLOG("sync-check: OK with %s (%s, %d objects compared: %s; check %.2f ms, %u bytes)", who.c_str(), place.c_str(),
                 c.objects, dg::counts(hd).c_str(), ms, (unsigned)bytes);
            g.ok_window = 0;
            g.ok_window_since = now;
        } else if (now - g.ok_window_since >= 600 && g_detail >= 1) { // then a summary every 10 minutes
            SLOG("sync-check: OK x%d with %s in the last %.0f min (%s, %d objects)", g.ok_window, who.c_str(),
                 (now - g.ok_window_since) / 60, place.c_str(), c.objects);
            g.ok_window = 0;
            g.ok_window_since = now;
        }
        send_result(from, gd.seq, dg::kResOk, c.objects, "");
        return;
    }
    std::string text = dg::describe(issues);
    if (!g.recheck) { // look again in 3 s: a difference of one moment is no desync
        g_c.sync_differs++;
        g.recheck = true;
        g.first = c.issues;
        g.next = now + 3.0;
        if (g_detail >= 2)
            SLOG("sync-check: %s differs, rechecking in 3 s: %s", who.c_str(), text.c_str());
        send_result(from, gd.seq, dg::kResDiffers, c.objects, text);
        return;
    }
    g.recheck = false;
    g_c.sync_mismatch++;
    std::string ids;
    for (auto& i : issues)
        ids += i.id + " ";
    if (ids == g.last_mismatch && now - g.last_mismatch_at < 300) {
        g.repeats++; // the same lasting difference: once per 5 minutes
    } else {
        SWARN("sync-check: MISMATCH with %s (%s, 2 checks 3 s apart, %d objects compared%s): %s", who.c_str(),
              place.c_str(), c.objects,
              g.repeats ? (", seen " + std::to_string(g.repeats) + " more times before").c_str() : "", text.c_str());
        g.last_mismatch = ids;
        g.last_mismatch_at = now;
        g.repeats = 0;
        describe_keys(text);
    }
    send_result(from, gd.seq, dg::kResMismatch, c.objects, text);
}

// guest: the host's verdict
static void on_result(const dg::ResultMsg& m)
{
    uint32_t place = (g_P.mode ? g_P.mode() : 0) ^ (g_P.place ? g_P.place() : 0);
    std::string where = place_text(g_P.mode ? g_P.mode() : 0, g_P.place ? g_P.place() : 0);
    g_c.sync_checks++;
    switch (m.state) {
    case dg::kResOk:
        g_c.sync_ok++;
        if (g_guest_told_place != place && g_detail >= 1) {
            g_guest_told_place = place;
            SLOG("sync-check: OK with the host (%s, %u objects compared)", where.c_str(), m.objects);
        }
        break;
    case dg::kResDiffers:
        g_c.sync_differs++;
        if (g_detail >= 2)
            SLOG("sync-check: differs from the host, rechecking in 3 s: %s", m.text.c_str());
        break;
    case dg::kResMismatch:
        g_c.sync_mismatch++;
        SWARN("sync-check: MISMATCH with the host (%s, %u objects compared): %s", where.c_str(), m.objects, m.text.c_str());
        describe_keys(m.text);
        break;
    default:
        if (g_detail >= 2)
            SLOG("sync-check: the host plays elsewhere, not compared");
    }
}

// guest: the host asks for our digest
static void on_digest_req(uint32_t seq)
{
    double t0 = g_H->time_seconds();
    dg::Digest d = build_digest(seq);
    net::Writer w;
    d.write(w);
    double ms = (g_H->time_seconds() - t0) * 1000;
    g_c.sync_guest_ms_sum += ms;
    g_c.sync_guest_ms_n++;
    g_c.sync_guest_ms_max = std::max(g_c.sync_guest_ms_max, ms);
    g_c.sync_bytes += w.size();
    g_P.sess->send_reliable_to(0, net::MSG_DIAG, w);
}

void sync_tick(double now)
{
    clock_tick(now);
    if (!in_session() || g_P.sess->role() != net::Role::Host || g_sync_interval <= 0) {
        if (!in_session())
            g_guests.clear();
        return;
    }
    // BUG-0380: a player who left kept its entry, so the same id joining again was checked on the old
    // schedule (6 s after its world load in civ_city_join: the catch-up's city was still on its way)
    // instead of 10 s after the join like a newcomer. Off: the entry stays (0.7.3.3).
    if (g_rejoin_grace_fix)
        for (auto it = g_guests.begin(); it != g_guests.end();) {
            bool here = false;
            for (auto& p : g_P.sess->players())
                here = here || p.id == it->first;
            it = here ? std::next(it) : g_guests.erase(it);
        }
    if (now >= g_host_totals_next) { // BUG-0379: the host's totals of the last seconds
        g_host_totals_next = now + 0.1;
        float dna;
        int food;
        if (g_P.dna && g_P.dna(dna))
            g_host_totals.push_back({now, dg::kTotalDna, dna});
        if (g_P.cell_food && g_P.cell_food(food))
            g_host_totals.push_back({now, dg::kTotalCellFood, (float)food});
        while (!g_host_totals.empty() && g_host_totals.front().t < now - kTotalLag - 25.0)
            g_host_totals.pop_front(); // a guest answers within 20 s (or is asked again)
    }
    // one guest per frame at most: the cost of a check is spread over frames
    for (auto& p : g_P.sess->players()) {
        if (p.id == 0)
            continue;
        auto [it, fresh] = g_guests.try_emplace(p.id);
        GuestCheck& g = it->second;
        if (fresh) {
            g.next = now + 10.0 + 2.0 * p.id; // after the join settled, guests apart
            g.ok_window_since = now;
        }
        if (g.deaf)
            continue;
        if (g.waiting) {
            if (now - g.asked > 20.0) {
                g.waiting = false;
                g.next = now + g_sync_interval;
                if (++g.unanswered >= 3) {
                    g.deaf = true;
                    SLOG("sync-check: player %u '%s' does not answer (an older SporeCoop?): not checked", p.id, p.name.c_str());
                }
            }
            continue;
        }
        if (now < g.next)
            continue;
        g.seq = ++g_seq;
        g.waiting = true;
        g.asked = now;
        net::Writer w;
        w.u8(dg::kDigestReq);
        w.u32(g.seq);
        g_P.sess->send_reliable_to(p.id, net::MSG_DIAG, w);
        break;
    }
    for (auto it = g_guests.begin(); it != g_guests.end();) { // players that left
        bool here = false;
        for (auto& p : g_P.sess->players())
            here |= p.id == it->first;
        it = here ? std::next(it) : g_guests.erase(it);
    }
}

void sync_on_session_end()
{
    g_guests.clear();
    g_host_totals.clear();
    g_guest_told_place = 0;
    g_host_t0 = -1;
    g_clock.clear();
    g_clock_told = false;
    g_clock_next = 0;
    set_log_clock();
}

void sync_on_message(uint8_t from, net::Reader& r)
{
    if (!g_P.sess)
        return;
    uint8_t kind = r.u8();
    if (!r.ok())
        return;
    bool host = g_P.sess->role() == net::Role::Host;
    switch (kind) {
    case dg::kClockReq: {
        dg::ClockReq q;
        if (host && q.read(r) && g_host_t0 >= 0) {
            dg::ClockAns a{q.seq, q.t_local, session_time()};
            net::Writer w;
            a.write(w);
            g_P.sess->send_unreliable_to(from, net::MSG_DIAG, w);
        }
        break;
    }
    case dg::kClockAns: {
        dg::ClockAns a;
        if (!host && from == 0 && a.read(r))
            on_clock_answer(a);
        break;
    }
    case dg::kMark: {
        dg::Mark m;
        if (!m.read(r))
            break;
        if (host) {
            m.origin = from; // never trust the id from the wire
            m.name = name_of(from);
            net::Writer w;
            m.write(w);
            g_P.sess->broadcast_reliable(net::MSG_DIAG, w, from);
        }
        log_mark(m, false);
        break;
    }
    case dg::kDigestReq: {
        uint32_t seq = r.u32();
        if (!host && from == 0 && r.ok())
            on_digest_req(seq);
        break;
    }
    case dg::kDigest: {
        size_t bytes = r.left() + 1;
        dg::Digest d;
        if (host && d.read(r))
            on_digest(from, d, bytes);
        break;
    }
    case dg::kResult: {
        dg::ResultMsg m;
        if (!host && from == 0 && m.read(r))
            on_result(m);
        break;
    }
    default:
        break; // a newer kind: ignored
    }
}

void sync_session_start()
{
    sync_on_session_end(); // a clean state
    if (g_P.sess && g_P.sess->role() == net::Role::Host) {
        g_host_t0 = g_H->time_seconds();
        set_log_clock();
        SLOG("clock: session clock started (session time 0 = now)");
    }
}

std::string sync_status()
{
    char b[400];
    bool ok = false;
    double t = session_time(&ok);
    snprintf(b, sizeof b,
             "clock: %s session time %.3f (%s, %d samples, best round trip %.1f ms)\n"
             "sync-check: every %.0f s%s, checks %d ok %d differs %d mismatch %d; host build+compare avg %.3f ms max %.3f ms (%d); "
             "guest build avg %.3f ms max %.3f ms (%d); %llu bytes of digests\n",
             ok ? "on," : "off,", t, g_P.sess && g_P.sess->role() == net::Role::Host ? "host" : "guest", (int)g_clock.samples(),
             g_clock.rtt() * 1000, g_sync_interval, g_force.empty() ? "" : (" (forced: " + g_force + ")").c_str(), g_c.sync_checks,
             g_c.sync_ok, g_c.sync_differs, g_c.sync_mismatch, g_c.sync_ms_n ? g_c.sync_ms_sum / g_c.sync_ms_n : 0.0, g_c.sync_ms_max,
             g_c.sync_ms_n, g_c.sync_guest_ms_n ? g_c.sync_guest_ms_sum / g_c.sync_guest_ms_n : 0.0, g_c.sync_guest_ms_max,
             g_c.sync_guest_ms_n, (unsigned long long)g_c.sync_bytes);
    return b;
}

// coop.diag sync now | on | off | <seconds> | force <shift|drop|ghost|dna|off>
std::string sync_command(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[1] == "now") {
        for (auto& [id, g] : g_guests)
            if (!g.waiting) {
                g.next = 0;
                g.recheck = false;
            }
        return "sync-check: asking now (host)\n";
    }
    if (a.size() >= 2 && a[1] == "off")
        g_sync_interval = 0;
    else if (a.size() >= 2 && a[1] == "on")
        g_sync_interval = 30;
    else if (a.size() >= 3 && a[1] == "force")
        g_force = a[2] == "off" ? "" : a[2];
    else if (a.size() >= 2 && atof(a[1].c_str()) > 0)
        g_sync_interval = std::max(1.0, atof(a[1].c_str()));
    return sync_status();
}

void sync_set_interval(double s) { g_sync_interval = s; }

} // namespace support
