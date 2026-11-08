// One report for the session (0.7.12 F4, BUG-0767): see sreport.h and net/sreport.h.
#include "sreport.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <memory>
#include <vector>

#include "fixes.h"
#include "net/protocol.h"
#include "net/sreport.h"
#include "trace.h"

namespace sreport {

using namespace net::sreport;

static const SkHost* H;
static SkMod* M;
static Ctx C;

bool g_fix = true;
COOP_FIX("core.session_report", "BUG-0767", g_fix, "core: the host's report asks every guest for its report (one session zip)");

#define SAY(...) H->out_printf(out, __VA_ARGS__)
#define LOG(...) H->log(M, SK_INFO, __VA_ARGS__)

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
static double now_s() { return H->time_seconds(); }

static std::wstring widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(n > 0 ? n - 1 : 0);
    return w;
}

static bool read_file(const std::string& path, std::vector<uint8_t>& out, size_t cap)
{
    HANDLE f = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(f, &sz) && (uint64_t)sz.QuadPart <= cap;
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size());
    }
    CloseHandle(f);
    return ok;
}

static std::string logs_dir()
{
    const char* d = H->size >= offsetof(SkHost, logs_dir) + sizeof(void*) && H->logs_dir ? H->logs_dir() : nullptr;
    return d ? d : std::string(H->mods_dir()) + "\\logs";
}

static std::string stamp()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[32];
    snprintf(b, sizeof b, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

static std::string file_of(const std::string& p) { return p.substr(p.find_last_of("\\/") + 1); }

// ================= the host =================
static std::unique_ptr<Round> g_round;    // the round being asked (nullptr = none)
static std::string g_host_zip;            // the host's own report of this round ("" = without it)
static std::string g_host_name;
static std::string g_line;                // the host's line about the last round (all guests)
static std::string g_ui_line;             // its short form for the lobby's status line (one line of the window)
static std::string g_last_zip;            // the last session zip written (tests)
static std::vector<std::string> g_last_entries;
static bool g_writing;                    // the session zip is being written outside the frame
static uint32_t g_round_seq;
static unsigned g_rounds, g_refused, g_stops_sent;

static void send_stop(uint8_t to, Why w)
{
    if (w == Why::None || !C.sess)
        return;
    Stop s;
    s.round = g_round->id;
    s.why = w;
    net::Writer wr;
    s.write(wr);
    C.sess->send_reliable_to(to, net::MSG_REPORT, wr);
    g_stops_sent++;
}

static std::string summary_line(const Round& r)
{
    std::string s;
    for (auto& g : r.guests)
        s += (s.empty() ? "" : ", ") + r.line_of(g);
    return s;
}

// outside the frame: the session zip from the host's report and every checked guest report, then the result line
static void write_session(void*)
{
    std::unique_ptr<Round> r = std::move(g_round);
    g_writing = false;
    if (!r)
        return;
    std::set<std::string> taken;
    struct E {
        std::string name;
        std::vector<uint8_t>* data;
    };
    std::vector<uint8_t> host_data;
    std::vector<E> entries;
    std::string text = "SporeCoop session report " + stamp() + "\n";
    text += g_host_zip.empty() ? "Asked by the host (\"Ask the players for their reports\"), without the host's own report.\n"
                               : "The host's \"Report a problem\": the host's report and every guest's it asked for.\n";
    text += "Each report is stored here exactly as it was received (never unpacked). Paths and the Windows user name\n"
            "are masked in every report; no player's network address is written into this file.\n\n";
    if (!g_host_zip.empty()) {
        if (read_file(g_host_zip, host_data, 64u << 20)) {
            std::string n = entry_name("Host-", g_host_name, 0, taken);
            entries.push_back({n, &host_data});
            text += n + "  the host's own report (" + std::to_string((host_data.size() + 1023) / 1024) + " KB)\n";
        } else
            text += "the host's own report could not be read: " + file_of(g_host_zip) + "\n";
    }
    for (auto& g : r->guests) {
        if (g.st == State::Sent) {
            std::string n = entry_name("Guest-", g.name, g.id, taken);
            entries.push_back({n, &g.data});
            text += n + "  " + r->line_of(g) + "\n";
        } else
            text += "(no file)  " + r->line_of(g) + "\n";
    }
    std::string line;
    if (entries.empty()) {
        line = "No report came: " + summary_line(*r);
        g_ui_line = "No report came (see the chat)";
        LOG("session report: round %u: nothing to write (%s)", r->id, summary_line(*r).c_str());
    } else {
        std::string dir = logs_dir(), base = "SporeCoop-session-report-" + stamp(), name = base + ".zip";
        for (int k = 2; GetFileAttributesW(widen(dir + "\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES; k++)
            name = base + "-" + std::to_string(k) + ".zip";
        std::string path = dir + "\\" + name, part = path + ".part";
        FILE* f = _wfopen(widen(part).c_str(), L"wb");
        ZipOut z;
        SYSTEMTIME t;
        GetLocalTime(&t);
        z.dtime = (uint16_t)((t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2));
        z.ddate = (uint16_t)(((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay);
        z.out = [f](const uint8_t* p, size_t n) { return f && fwrite(p, 1, n, f) == n; };
        std::vector<std::string> names;
        for (auto& e : entries) {
            z.add(e.name, e.data->data(), e.data->size());
            names.push_back(e.name);
        }
        z.add("SESSION.txt", (const uint8_t*)text.data(), text.size());
        names.push_back("SESSION.txt");
        bool ok = z.finish();
        if (f)
            ok = fclose(f) == 0 && ok;
        if (ok && MoveFileExW(widen(part).c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING)) {
            g_last_zip = path;
            g_last_entries = names;
            line = "Session report saved: " + name + " - " + summary_line(*r);
            g_ui_line = "Session report saved: " + name;
            LOG("session report: round %u: %s (%u entries, %u KB): %s", r->id, path.c_str(), z.entries(), z.bytes() >> 10,
                summary_line(*r).c_str());
        } else {
            DeleteFileW(widen(part).c_str()); // no partial file
            line = "The session report could not be written (" + summary_line(*r) + ")";
            g_ui_line = "The session report could not be written (see the chat)";
            H->log(M, SK_ERROR, "session report: cannot write %s", path.c_str());
        }
    }
    trace::line("report", "round %u done: %s", r->id, summary_line(*r).c_str());
    g_line = line;
    if (C.feed)
        C.feed(line);
    if (C.chat_log)
        C.chat_log("* " + line);
}

std::string ask(const std::string& host_zip)
{
    if (!is_host())
        return "Only the host can ask for the reports";
    if (!g_fix) {
        LOG("session report: asking the players is switched off (core.session_report)");
        return host_zip.empty() ? "Asking the players for their reports is switched off" : "";
    }
    if (g_round || g_writing) {
        int n = 0;
        for (auto& g : g_round ? g_round->guests : std::vector<Guest>{})
            n += final_state(g.st);
        return "Already asking the players: " + std::to_string(n) + " of " +
               std::to_string(g_round ? g_round->guests.size() : 0) + " answered";
    }
    auto r = std::make_unique<Round>();
    r->id = (++g_round_seq << 16) ^ (GetTickCount() & 0xFFFF);
    r->t0 = now_s();
    uint8_t me = C.sess->my_id();
    g_host_name = C.sess->name_of(me);
    for (auto& p : C.sess->players())
        if (p.id != me) {
            Guest g;
            g.id = p.id;
            g.name = p.name;
            g.last = r->t0;
            r->guests.push_back(g);
        }
    if (r->guests.empty()) {
        LOG("session report: nobody to ask");
        return host_zip.empty() ? "Nobody to ask: no player has joined" : "";
    }
    Ask a;
    a.round = r->id;
    a.host = g_host_name;
    net::Writer w;
    a.write(w);
    for (auto& g : r->guests)
        C.sess->send_reliable_to(g.id, net::MSG_REPORT, w);
    g_host_zip = host_zip;
    g_round = std::move(r);
    g_rounds++;
    g_line = "Asking " + std::to_string(g_round->guests.size()) + " player(s) for their reports...";
    g_ui_line = g_line;
    LOG("session report: round %u: asking %u guests (%s the host's report)", g_round->id, (unsigned)g_round->guests.size(),
        host_zip.empty() ? "without" : "with");
    trace::line("report", "round %u asks %u guests", g_round->id, (unsigned)g_round->guests.size());
    return g_line;
}

bool round_running() { return g_round != nullptr || g_writing; }

std::string host_ui_line()
{
    if (g_round && !g_writing) {
        int done = 0, sent = 0;
        for (auto& g : g_round->guests) {
            done += final_state(g.st);
            sent += g.st == State::Sent;
        }
        return "Reports: " + std::to_string(done) + " of " + std::to_string(g_round->guests.size()) + " answered, " +
               std::to_string(sent) + " arrived";
    }
    return g_ui_line;
}

std::string host_line()
{
    if (g_round && !g_writing) {
        int done = 0;
        for (auto& g : g_round->guests)
            done += final_state(g.st);
        return "Reports: " + std::to_string(done) + " of " + std::to_string(g_round->guests.size()) + " - " +
               summary_line(*g_round);
    }
    return g_line;
}

static void host_message(uint8_t from, uint8_t kind, net::Reader& r)
{
    if (!g_round || g_writing)
        return;
    double t = now_s();
    Why w = Why::None;
    Guest* g = g_round->find(from);
    State before = g ? g->st : State::Asked;
    if (kind == kAnswer) {
        Answer a;
        if (!a.read(r))
            return;
        w = g_round->on_answer(from, a, t);
    } else if (kind == kChunk) {
        Piece p;
        if (!p.read(r)) {
            if (g && g->st == State::Receiving)
                w = g_round->refuse(*g, Why::Malformed, "a piece that does not parse");
        } else
            w = g_round->on_piece(from, p, t);
    } else if (kind == kDone) {
        Done d;
        if (!d.read(r))
            return;
        w = g_round->on_done(from, d, t);
    } else
        return;
    if (g && g->st != before) {
        LOG("session report: %s", g_round->line_of(*g).c_str());
        if (g->st == State::Refused) {
            g_refused++;
            H->log(M, SK_WARN, "session report: %s's report refused (%s): %s", g->name.c_str(), why_text(g->why),
                   g->detail.c_str());
        }
    }
    send_stop(from, w);
}

// ================= the guest =================
enum class GState { Idle, Asked, Making, Sending, Done };
static GState g_gs = GState::Idle;
static uint32_t g_ask_round;
static std::string g_ask_host;
static double g_ask_at, g_make_until;
static std::vector<uint8_t> g_out; // the zip being sent
static uint32_t g_out_size;        // announced (tests may send more)
static size_t g_out_off;
static std::string g_guest_line;
static std::string g_test = "none"; // tests: none | big | overrun | badname | junk
static unsigned g_asks, g_sent_reports, g_declined, g_stopped;
static bool g_make_queued;

static void send_answer(bool send, uint32_t size)
{
    Answer a;
    a.round = g_ask_round;
    a.send = send ? 1 : 0;
    a.size = size;
    net::Writer w;
    a.write(w);
    C.sess->send_reliable_to(0, net::MSG_REPORT, w);
}

// the test hooks: what a broken or hostile guest would send instead of its report
static void apply_test_hook(std::vector<uint8_t>& d)
{
    if (g_test == "big") // announced as it is: over the cap
        d.resize(kMaxReport + (1u << 20), 0x5A);
    else if (g_test == "junk") {
        d.assign(64 * 1024, 0);
        uint32_t x = 12345;
        for (auto& b : d)
            b = (uint8_t)((x = x * 1103515245u + 12345u) >> 16);
    } else if (g_test == "badname") { // a zip with a name that climbs out of a folder and a program in it
        std::vector<uint8_t> z;
        ZipOut o;
        o.out = [&z](const uint8_t* p, size_t n) {
            z.insert(z.end(), p, p + n);
            return true;
        };
        const char readme[] = "SporeCoop problem report\n", bat[] = "@echo off\r\necho not a report\r\n";
        o.add("README.txt", (const uint8_t*)readme, sizeof readme - 1);
        o.add("../../evil.bat", (const uint8_t*)bat, sizeof bat - 1);
        o.finish();
        d = z;
    }
}

// outside the frame: the guest's own report (the same zip as its "Report a problem"), then the answer
static void make_and_send(void*)
{
    g_make_queued = false;
    if (g_gs != GState::Making || !is_guest())
        return;
    std::string p = C.make_report ? C.make_report("asked by the host for the session report") : "";
    std::vector<uint8_t> d;
    if (p.empty() || !read_file(p, d, 64u << 20)) {
        LOG("session report: my report could not be made or read ('%s')", p.c_str());
        send_answer(false, 0);
        g_gs = GState::Done;
        g_guest_line = "Your report could not be made (SporeKit too old?)";
        return;
    }
    if (g_test != "none")
        apply_test_hook(d);
    g_out_size = (uint32_t)std::min<size_t>(d.size(), 0xFFFFFFFFu);
    if (g_test == "overrun") // announces its real size, then sends more than that
        d.resize(d.size() + 256 * 1024, 0x77);
    g_out = std::move(d);
    g_out_off = 0;
    send_answer(true, g_out_size);
    if (g_out_size > kMaxReport) { // the host refuses it: nothing is sent (it says so with a kStop)
        g_out.clear();
        g_out_off = 0;
    }
    g_gs = GState::Sending;
    g_guest_line = "Sending your report to the host...";
    LOG("session report: sending my report %s (%u KB%s) to the host", file_of(p).c_str(), g_out_size >> 10,
        g_test != "none" ? (", test " + g_test).c_str() : "");
}

void answer(bool send)
{
    if (g_gs != GState::Asked || !is_guest())
        return;
    trace::line("report", "guest answers %s", send ? "send" : "don't send");
    if (!send) {
        send_answer(false, 0);
        g_gs = GState::Done;
        g_declined++;
        g_guest_line = "You did not send your report";
        LOG("session report: I did not send my report");
        return;
    }
    g_gs = GState::Making;
    g_guest_line = "Making your report...";
    int s = C.shot ? C.shot() : 0;
    g_make_until = s > 0 ? now_s() + 4.0 : 0; // its screenshot first, as "Report a problem" takes one
}

int guest_prompt(std::string& line)
{
    if (g_gs == GState::Asked) {
        line = (g_ask_host.empty() ? std::string("The host") : g_ask_host) + " (host) asks for your report:";
        return 1;
    }
    line = g_gs == GState::Sending && g_out_size && !g_out.empty()
               ? "Sending your report to the host: " + std::to_string((unsigned)(std::min<size_t>(g_out_off, g_out_size) * 100ull / g_out_size)) + "%"
               : g_guest_line;
    return 0;
}

static void guest_message(uint8_t kind, net::Reader& r)
{
    if (kind == kAsk) {
        Ask a;
        if (!a.read(r))
            return;
        g_asks++;
        if (g_gs == GState::Making || g_gs == GState::Sending) {
            LOG("session report: asked again while sending: the new round waits for no answer");
            return;
        }
        g_ask_round = a.round;
        g_ask_host = a.host.substr(0, 40);
        g_ask_at = now_s();
        g_gs = GState::Asked;
        g_guest_line.clear();
        LOG("session report: the host asks for my report (round %u)", a.round);
        trace::line("report", "asked by the host (round %u)", a.round);
        if (C.feed)
            C.feed(g_ask_host + " asks for your problem report: Send or Don't send in the co-op window (F9)");
        if (C.open_window)
            C.open_window();
    } else if (kind == kStop) {
        Stop s;
        if (!s.read(r) || s.round != g_ask_round)
            return;
        g_stopped++;
        std::vector<uint8_t>().swap(g_out);
        g_gs = GState::Done;
        g_guest_line = std::string("The host refused your report (") + why_text(s.why) + ")";
        LOG("session report: the host refused my report: %s", why_text(s.why));
    }
}

static void guest_tick()
{
    if (g_gs == GState::Asked && now_s() - g_ask_at > kAnswerWait) {
        g_gs = GState::Done;
        g_guest_line = "The host's request for your report ran out";
        LOG("session report: I did not answer in time");
    }
    if (g_gs == GState::Making && !g_make_queued &&
        (now_s() >= g_make_until || !C.shot_state || C.shot_state() != 1)) {
        g_make_queued = true;
        H->run_outside_frame(make_and_send, nullptr);
    }
    if (g_gs != GState::Sending)
        return;
    if (g_out_size > kMaxReport) { // too big to send: the host's kStop ends it
        g_guest_line = "Your report is too big to send (over 25 MB)";
        return;
    }
    int rtt = C.sess->rtt_of(0);
    size_t window = rtt >= 0 && rtt < 40 ? 600 : 160;
    int budget = 300;
    while (budget-- > 0 && C.sess->pending_to(0) < window && g_out_off < g_out.size()) {
        Piece p;
        p.round = g_ask_round;
        p.offset = (uint32_t)g_out_off;
        p.n = (uint16_t)std::min(kChunkBytes, g_out.size() - g_out_off);
        p.data = g_out.data() + g_out_off;
        net::Writer w;
        p.write(w);
        C.sess->send_reliable_to(0, net::MSG_REPORT, w);
        g_out_off += p.n;
    }
    if (g_out_off >= g_out.size()) {
        Done d;
        d.round = g_ask_round;
        d.crc = crc32(g_out.data(), g_out.size());
        net::Writer w;
        d.write(w);
        C.sess->send_reliable_to(0, net::MSG_REPORT, w);
        g_sent_reports++;
        g_gs = GState::Done;
        g_guest_line = "Your report went to the host (" + std::to_string((g_out.size() + 1023) / 1024) + " KB)";
        LOG("session report: my report went to the host (%u KB)", (unsigned)(g_out.size() >> 10));
        std::vector<uint8_t>().swap(g_out);
    }
}

// ================= both =================
void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
}

void tick()
{
    if (!C.sess)
        return;
    if (is_guest())
        guest_tick();
    if (g_round && !g_writing) {
        std::vector<State> before;
        for (auto& g : g_round->guests)
            before.push_back(g.st);
        g_round->tick(now_s());
        for (size_t i = 0; i < before.size(); i++)
            if (before[i] != g_round->guests[i].st)
                LOG("session report: %s", g_round->line_of(g_round->guests[i]).c_str());
        if (g_round->finished()) {
            g_writing = true;
            H->run_outside_frame(write_session, nullptr);
        }
    }
}

void on_message(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (is_host() && from != 0)
        host_message(from, kind, r);
    else if (is_guest() && from == 0)
        guest_message(kind, r);
}

void on_player_leave(uint8_t id)
{
    if (g_round && !g_writing)
        g_round->on_leave(id);
}

void on_disconnect()
{
    if (g_gs != GState::Idle && g_gs != GState::Done)
        g_guest_line = "The session ended before your report was sent";
    g_gs = g_gs == GState::Idle ? GState::Idle : GState::Done;
    std::vector<uint8_t>().swap(g_out);
}

std::string status()
{
    static const char* gs[] = {"idle", "asked", "making", "sending", "done"};
    char b[256];
    snprintf(b, sizeof b, "sreport fix=%d round=%s rounds=%u refused=%u stops=%u guest=%s asks=%u sent=%u declined=%u stopped=%u test=%s\n",
             (int)g_fix, g_round ? (g_writing ? "writing" : "asking") : "none", g_rounds, g_refused, g_stops_sent,
             gs[(int)g_gs], g_asks, g_sent_reports, g_declined, g_stopped, g_test.c_str());
    std::string s = b;
    s += "hostline '" + host_line() + "'\n";
    std::string gl;
    int asked = guest_prompt(gl);
    s += "guestline " + std::to_string(asked) + " '" + gl + "'\n";
    s += "last " + (g_last_zip.empty() ? std::string("-") : file_of(g_last_zip)) + "\n";
    s += "entries";
    for (auto& e : g_last_entries)
        s += " " + e;
    return s + "\n";
}

int command(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string sub = argc > 1 ? argv[1] : "status";
    if (sub == "status") {
        SAY("%s", status().c_str());
        return 0;
    }
    if (sub == "ask") { // the host's "Ask the players for their reports"
        SAY("%s\n", ask("").c_str());
        return 0;
    }
    if (sub == "answer" && argc > 2) { // the guest's click (the test clicks the window; this is for hand runs)
        answer(!strcmp(argv[2], "yes") || !strcmp(argv[2], "send"));
        SAY("%s", status().c_str());
        return 0;
    }
    if (sub == "test" && argc > 2) {
        std::string t = argv[2];
        if (t != "none" && t != "big" && t != "overrun" && t != "badname" && t != "junk") {
            SAY("test: none | big | overrun | badname | junk\n");
            return 1;
        }
        g_test = t;
        SAY("sreport test=%s\n", g_test.c_str());
        return 0;
    }
    SAY("coop.sreport [status] | ask | answer yes|no | test none|big|overrun|badname|junk\n");
    return 1;
}

} // namespace sreport
