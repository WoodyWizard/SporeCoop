// One report for the session (0.7.12 F4, BUG-0767): see sreport.h and net/sreport.h.
#include "sreport.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <vector>

#include "fixes.h"
#include "net/protocol.h"
#include "net/sreport.h"
#include "net/utf8.h"
#include "trace.h"

namespace sreport {

using namespace net::sreport;

static const SkHost* H;
static SkMod* M;
static Ctx C;

bool g_fix = true;
COOP_FIX("core.session_report", "BUG-0767", g_fix, "core: the host's report asks every guest for its report (one session zip)");
// BUG-0783: a guest still making or sending its report answers a new Ask at once (its report goes to the new round)
bool g_reask_fix = true;
COOP_FIX("core.sreport_reask", "BUG-0783", g_reask_fix, "core: a guest making or sending its report answers the host's next Ask");
// BUG-1013: the guest cut the host's name in the Ask to 40 bytes: a CJK or long mixed name ended in a broken UTF-8
// sequence in the feed and the co-op window. Whole characters now (net_name: 24 code points, clean UTF-8). Off: 40 bytes.
static bool g_host_cut_fix = true;
COOP_FIX("core.sreport_host_cut", "BUG-1013", g_host_cut_fix, "core: the host's name in a report question is cut by whole characters");
static std::string ask_host(const std::string& h) { return g_host_cut_fix ? utf8::net_name(h) : h.substr(0, 40); }
// BUG-1014: `coop.sreport status` said round=none while the session zip was written (start_write moves the round out
// before g_writing is set): "writing" could never be printed. Off: the old expression (tests read it).
static bool g_writing_status_fix = true;
COOP_FIX("core.sreport_writing", "BUG-1014", g_writing_status_fix, "core: coop.sreport status says round=writing while the session zip is written");
static std::string g_test_ask_name; // tests (coop.sreport askname <name>): the host name sent in the next Asks

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
static double g_stall_wait = kStallWait;  // tests (coop.sreport stallwait <s>): the host's wait for the next piece

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

// BUG-0780: the session zip is read and written on a worker thread (pinned, net::g_spawn_thread): the host's own report
// (up to 64 MB) and every guest's (up to 25 MB each) were read and written in one call on the game's main thread, so
// the host's game stood still meanwhile (no frame, no network: seconds on a slow disk). The result line comes back to
// the main thread (tick). Off: the whole write outside the frame on the main thread (0.7.12 F4).
bool g_write_worker_fix = true;
COOP_FIX("core.sreport_write_worker", "BUG-0780", g_write_worker_fix, "core: the session report zip is written on a worker thread");
static int g_write_delay_ms; // tests (coop.sreport writedelay <ms>): a slow disk, this long at the start and per MB written

struct WriteJob {
    std::unique_ptr<Round> r;
    std::string host_zip, host_name, dir;
    int delay_ms = 0;
    // results (read on the main thread)
    std::string line, ui_line, path, log, err;
    std::vector<std::string> names;
    double ms = 0;
};
static std::mutex g_job_mx;
static WriteJob* g_job_done; // a finished job the main thread takes (tick)
static unsigned g_jobs_worker, g_jobs_main;
static double g_last_write_ms;

// the file work: no SporeKit calls (it runs on a worker thread)
static void do_write(WriteJob& j)
{
    LARGE_INTEGER f0, t0, t1;
    QueryPerformanceFrequency(&f0);
    QueryPerformanceCounter(&t0);
    Round& rr = *j.r;
    std::set<std::string> taken;
    struct E {
        std::string name;
        std::vector<uint8_t>* data;
    };
    std::vector<uint8_t> host_data;
    std::vector<E> entries;
    std::string text = "SporeCoop session report " + stamp() + "\n";
    text += j.host_zip.empty() ? "Asked by the host (\"Ask the players for their reports\"), without the host's own report.\n"
                               : "The host's \"Report a problem\": the host's report and every guest's it asked for.\n";
    text += "Each report is stored here exactly as it was received (never unpacked). Paths and the Windows user name\n"
            "are masked in every report; no player's network address is written into this file.\n\n";
    if (!j.host_zip.empty()) {
        if (read_file(j.host_zip, host_data, 64u << 20)) {
            std::string n = entry_name("Host-", j.host_name, 0, taken);
            entries.push_back({n, &host_data});
            text += n + "  the host's own report (" + std::to_string((host_data.size() + 1023) / 1024) + " KB)\n";
        } else
            text += "the host's own report could not be read: " + file_of(j.host_zip) + "\n";
    }
    for (auto& g : rr.guests) {
        if (g.st == State::Sent) {
            std::string n = entry_name("Guest-", g.name, g.id, taken);
            entries.push_back({n, &g.data});
            text += n + "  " + rr.line_of(g) + "\n";
        } else
            text += "(no file)  " + rr.line_of(g) + "\n";
    }
    if (entries.empty()) {
        j.line = "No report came: " + summary_line(rr);
        j.ui_line = "No report came (see the chat)";
        j.log = "session report: round " + std::to_string(rr.id) + ": nothing to write (" + summary_line(rr) + ")";
    } else {
        std::string base = "SporeCoop-session-report-" + stamp(), name = base + ".zip";
        for (int k = 2; GetFileAttributesW(widen(j.dir + "\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES; k++)
            name = base + "-" + std::to_string(k) + ".zip";
        std::string path = j.dir + "\\" + name, part = path + ".part";
        FILE* f = _wfopen(widen(part).c_str(), L"wb");
        ZipOut z;
        SYSTEMTIME t;
        GetLocalTime(&t);
        z.dtime = (uint16_t)((t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2));
        z.ddate = (uint16_t)(((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay);
        size_t written = 0;
        int delay = j.delay_ms;
        z.out = [f, &written, delay](const uint8_t* p, size_t n) {
            size_t before = written >> 20;
            bool first = written == 0;
            written += n;
            if (delay > 0 && (first || (written >> 20) != before)) // (also before the first byte: a small zip waits too)
                Sleep((DWORD)delay); // tests: a slow disk
            return f && fwrite(p, 1, n, f) == n;
        };
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
            j.path = path;
            j.names = names;
            j.line = "Session report saved: " + name + " - " + summary_line(rr);
            j.ui_line = "Session report saved: " + name;
            j.log = "session report: round " + std::to_string(rr.id) + ": " + path + " (" + std::to_string(z.entries()) +
                    " entries, " + std::to_string(z.bytes() >> 10) + " KB): " + summary_line(rr);
        } else {
            DeleteFileW(widen(part).c_str()); // no partial file
            j.line = "The session report could not be written (" + summary_line(rr) + ")";
            j.ui_line = "The session report could not be written (see the chat)";
            j.err = "session report: cannot write " + path;
        }
    }
    QueryPerformanceCounter(&t1);
    j.ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f0.QuadPart;
}

// main thread: the result line of a written round
static void finish_write(WriteJob* j, const char* where)
{
    if (!j->err.empty())
        H->log(M, SK_ERROR, "%s", j->err.c_str());
    else
        LOG("%s", j->log.c_str());
    LOG("session report: round %u written in %.0f ms (%s)", j->r->id, j->ms, where);
    g_last_write_ms = j->ms;
    if (!j->path.empty()) {
        g_last_zip = j->path;
        g_last_entries = j->names;
    }
    g_ui_line = j->ui_line;
    trace::line("report", "round %u done: %s", j->r->id, summary_line(*j->r).c_str());
    g_line = j->line;
    g_writing = false;
    if (C.feed)
        C.feed(j->line);
    if (C.chat_log)
        C.chat_log("* " + j->line);
    delete j;
}

static void write_worker(void* p)
{
    auto* j = static_cast<WriteJob*>(p);
    do_write(*j);
    std::lock_guard<std::mutex> lk(g_job_mx);
    g_job_done = j;
}

// outside the frame on the main thread (core.sreport_write_worker off, or no thread could be started)
static void write_main(void* p)
{
    auto* j = static_cast<WriteJob*>(p);
    do_write(*j);
    g_jobs_main++;
    finish_write(j, "main thread");
}

// the round is over: its session zip from the host's report and every checked guest report
static void start_write()
{
    auto* j = new WriteJob;
    j->r = std::move(g_round);
    j->host_zip = g_host_zip;
    j->host_name = g_host_name;
    j->dir = logs_dir();
    j->delay_ms = g_write_delay_ms;
    g_writing = true;
    if (g_write_worker_fix && net::g_spawn_thread && net::g_spawn_thread(write_worker, j)) {
        g_jobs_worker++;
        return;
    }
    H->run_outside_frame(write_main, j);
}

static void take_written()
{
    WriteJob* j = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_job_mx);
        std::swap(j, g_job_done);
    }
    if (j)
        finish_write(j, "worker thread");
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
    a.host = g_test_ask_name.empty() ? g_host_name : g_test_ask_name;
    net::Writer w;
    a.write(w);
    for (auto& g : r->guests)
        C.sess->send_reliable_to(g.id, net::MSG_REPORT, w);
    g_host_zip = host_zip;
    r->stall_wait = g_stall_wait;
    g_round = std::move(r);
    g_rounds++;
    g_line = "Asking " + std::to_string(g_round->guests.size()) + " player(s) for their reports...";
    LOG("session report: round %u: asking %u guests (%s the host's report)", g_round->id, (unsigned)g_round->guests.size(),
        host_zip.empty() ? "without" : "with");
    trace::line("report", "round %u asks %u guests", g_round->id, (unsigned)g_round->guests.size());
    return g_line;
}

bool round_running() { return g_round != nullptr || g_writing; }

std::string host_ui_line()
{
    if (g_round && !g_writing) {
        int done = 0;
        for (auto& g : g_round->guests)
            done += final_state(g.st);
        return "Reports: " + std::to_string(done) + " of " + std::to_string(g_round->guests.size()) + " answered";
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
static std::string g_test = "none"; // tests: none | big | overrun | badname | junk | stall
static uint32_t g_stall_round;      // tests (stall): the round whose upload stops halfway (once)
static unsigned g_asks, g_sent_reports, g_declined, g_stopped, g_reasked;
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
            if (!g_reask_fix || a.round == g_ask_round) {
                LOG("session report: asked again while sending: the new round waits for no answer");
                return;
            }
            // BUG-0783: the host asks again only after its last round ended (written or given up), so the report
            // being made or sent goes to the new round: the player already chose Send in this session. Making: the
            // answer goes when it is made. Sending: the new round is answered now and the upload starts again from
            // its start (the pieces still queued carry the old round: the host drops them).
            uint32_t old = g_ask_round;
            g_ask_round = a.round;
            g_ask_host = ask_host(a.host);
            g_ask_at = now_s();
            g_reasked++;
            if (g_gs == GState::Sending) {
                g_out_off = 0;
                send_answer(true, g_out_size);
                g_guest_line = "Sending your report to the host...";
            }
            LOG("session report: asked again (round %u, was %u) while %s: my report goes to the new round", a.round, old,
                g_gs == GState::Sending ? "sending" : "making");
            trace::line("report", "asked again while busy (round %u)", a.round);
            return;
        }
        g_ask_round = a.round;
        g_ask_host = ask_host(a.host);
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
    if (g_test == "stall" && !g_stall_round)
        g_stall_round = g_ask_round;
    if (g_test == "stall" && g_stall_round == g_ask_round && g_out_off >= g_out.size() / 2)
        return; // tests: this round's upload stops halfway (the host times it out), a later round's goes through
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
    take_written(); // BUG-0780
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
        if (g_round->finished())
            start_write();
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
    g_out_off = 0;
}

// BUG-0770: every session end (end_session in coop.cpp, every path; it may run inside the network update: state only,
// no sends). The guest's question or upload ends as on a disconnect; the host's round is dropped unless its zip is
// being written (the write job owns it), its own report of that round forgotten.
void on_session_end()
{
    on_disconnect();
    if (g_round && !g_writing) {
        LOG("session report: round %u dropped: the session ended", g_round->id);
        trace::line("report", "round %u dropped at the session end", g_round->id);
        g_round.reset();
        g_line = g_ui_line = "";
    }
    if (!g_writing)
        g_host_zip.clear();
}

std::string status()
{
    static const char* gs[] = {"idle", "asked", "making", "sending", "done"};
    char b[256];
    snprintf(b, sizeof b, "sreport fix=%d round=%s rounds=%u refused=%u stops=%u guest=%s asks=%u sent=%u declined=%u stopped=%u reasked=%u test=%s\n",
             (int)g_fix,
             g_writing_status_fix ? (g_writing ? "writing" : g_round ? "asking" : "none")
                                  : (g_round ? (g_writing ? "writing" : "asking") : "none"),
             g_rounds, g_refused, g_stops_sent,
             gs[(int)g_gs], g_asks, g_sent_reports, g_declined, g_stopped, g_reasked, g_test.c_str());
    std::string s = b;
    s += "hostline '" + host_line() + "'\n";
    std::string gl;
    int asked = guest_prompt(gl);
    s += "guestline " + std::to_string(asked) + " '" + gl + "'\n";
    s += "last " + (g_last_zip.empty() ? std::string("-") : file_of(g_last_zip)) + "\n";
    snprintf(b, sizeof b, "write worker=%d jobs_worker=%u jobs_main=%u last_ms=%.0f delay_ms=%d\n", (int)g_write_worker_fix,
             g_jobs_worker, g_jobs_main, g_last_write_ms, g_write_delay_ms);
    s += b;
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
        if (t != "none" && t != "big" && t != "overrun" && t != "badname" && t != "junk" && t != "stall") {
            SAY("test: none | big | overrun | badname | junk | stall\n");
            return 1;
        }
        g_test = t;
        g_stall_round = 0;
        SAY("sreport test=%s\n", g_test.c_str());
        return 0;
    }
    if (sub == "writedelay" && argc > 2) { // tests: a slow disk (ms per MB of the session zip)
        g_write_delay_ms = std::max(0, std::min(5000, atoi(argv[2])));
        SAY("sreport writedelay=%d\n", g_write_delay_ms);
        return 0;
    }
    if (sub == "askname" && argc > 2) { // tests (BUG-1013): the host name the next Asks carry ("-": its own)
        g_test_ask_name = strcmp(argv[2], "-") ? std::string(argv[2]) : std::string();
        SAY("sreport askname='%s'\n", g_test_ask_name.c_str());
        return 0;
    }
    if (sub == "stallwait" && argc > 2) { // tests: the host's wait for the next piece of a report (s, 2..60)
        g_stall_wait = std::max(2.0, std::min(kStallWait, atof(argv[2])));
        SAY("sreport stallwait=%.0f\n", g_stall_wait);
        return 0;
    }
    SAY("coop.sreport [status] | ask | answer yes|no | test none|big|overrun|badname|junk|stall | stallwait <s> | writedelay <ms> | askname <name>|-\n");
    return 1;
}

} // namespace sreport
