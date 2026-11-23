// The per-player store on the host's disk (E1, BUG-1149): store.h, player_store.h.
#include "store.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <ctime>
#include <vector>

#include "fixes.h"
#include "modes.h"
#include "world.h"

namespace store {

static const SkHost* H;
static SkMod* M;
static Ctx C;

static std::vector<Feature>& features()
{
    static std::vector<Feature> v;
    return v;
}
bool add(Feature f)
{
    features().push_back(f);
    return true;
}

static pstore::Store g_cur;         // the open world's store
static bool g_open = false;         // g_cur belongs to the world played now
static uint32_t g_serial = 0;
static std::string g_seen_name;     // CurrentGameName when the world was last looked up
static bool g_seen_client = false;
static uint64_t g_salt = 0;
static pstore::Parsed g_parsed = pstore::Parsed::Empty;
static uint32_t g_reads, g_writes, g_write_fails, g_bad_files, g_saves, g_adopted, g_told;
static double g_saved_at = -1;
static std::string g_last_error;

int64_t now_unix() { return (int64_t)time(nullptr); }
uint32_t saves() { return g_saves; }
double since_save() { return g_saved_at < 0 || !H ? -1 : H->time_seconds() - g_saved_at; }

static std::wstring widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// <Spore>\Games-sporecoop-store (beside Games: never inside a slot, which a world share sends whole), "" unknown
static std::wstring root(bool make)
{
    wchar_t p[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) // profile-aware (hooked)
        return L"";
    std::wstring d = std::wstring(p) + L"\\Spore\\Games-sporecoop-store";
    if (make)
        CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static std::wstring file_of(const std::string& world, bool make)
{
    size_t s = world.find('/');
    std::wstring r = root(make);
    if (r.empty() || s == std::string::npos)
        return L"";
    std::wstring dir = r + L"\\" + widen(world.substr(0, s));
    if (make)
        CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + widen(world.substr(s + 1)) + L".txt";
}

// BUG-9309 (NET-12 of the 0.7.15.5 audit): salt.txt was opened with FILE_SHARE_READ only and any failure to open it
// counted as "no file": a moment in which another program held it with write access (a sync or backup tool, an
// antivirus) made a new salt and wrote it over the old one, and every stored player of every world of this host had
// another key from then on (their stored things were never found again). A world's file the same: an empty store,
// written over the file at the next save. Now the files are opened with every share flag, only a file that is not
// there is missing, and one that cannot be read now is not replaced (pstore::salt_step / world_step): no keys / the
// world's store stays closed until it can be read. Off (`coop.fix store.unreadable_kept off`): as 0.7.16.
static bool g_unreadable_fix = true;
COOP_FIX_UNSAFE("store.unreadable_kept", "BUG-9309", g_unreadable_fix,
                "store: a salt or world file another program holds open is read (all share flags), and one that cannot be read now is never replaced (a new salt lost every stored player)");
static DWORD g_read_error;      // the last failed read's error
static uint32_t g_unreadable_n; // reads that found a file unreadable (status `unreadable=`)

static pstore::Got read_file(const std::wstring& f, std::string& out)
{
    using pstore::Got;
    out.clear();
    DWORD share = g_unreadable_fix ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE : FILE_SHARE_READ;
    HANDLE h = CreateFileW(f.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        g_read_error = GetLastError();
        bool gone = g_read_error == ERROR_FILE_NOT_FOUND || g_read_error == ERROR_PATH_NOT_FOUND;
        if (!gone && g_unreadable_fix)
            g_unreadable_n++;
        return gone || !g_unreadable_fix ? Got::Missing : Got::Unreadable;
    }
    LARGE_INTEGER sz{};
    bool sized = GetFileSizeEx(h, &sz) != 0;
    bool ok = sized && sz.QuadPart <= (LONGLONG)pstore::kMaxFileBytes;
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD n = 0;
        ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &n, nullptr) && n == (DWORD)out.size());
        if (!ok && g_unreadable_fix) { // an I/O error now, not a damaged file
            g_read_error = GetLastError();
            sized = false;
        }
    } else if (!sized)
        g_read_error = GetLastError();
    CloseHandle(h);
    if (!sized && g_unreadable_fix) {
        out.clear();
        g_unreadable_n++;
        return Got::Unreadable;
    }
    if (!ok)
        out = "?"; // too big (or, off, unreadable): damaged
    return Got::Text;
}

// a temporary file next to it, then a replace: a crash mid-write leaves the old file (or the .tmp) whole
// replace false: the file is not replaced when it is there now (a new salt: BUG-9309)
static bool write_file(const std::wstring& f, const std::string& text, std::string& err, bool replace = true) // not a lab switch: a parameter (a new salt never replaces a file that came meanwhile)
{
    std::wstring tmp = f + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = "cannot create the file (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    DWORD n = 0;
    bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &n, nullptr) && n == (DWORD)text.size();
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    if (!ok) {
        err = "cannot write the file (error " + std::to_string(GetLastError()) + ")";
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), f.c_str(), (replace ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) {
        err = "cannot replace the file (error " + std::to_string(GetLastError()) + ")";
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// this host profile's salt (salt.txt in the store folder; made once). 0 until known (also while salt.txt is there
// but cannot be read: read again at most every 2 s, BUG-9309)
static double g_salt_retry_at = -1;
static uint32_t g_salt_waits;
static uint64_t salt()
{
    if (g_salt)
        return g_salt;
    double now = H ? H->time_seconds() : 0;
    if (g_salt_retry_at >= 0 && now < g_salt_retry_at)
        return 0;
    std::wstring r = root(true);
    if (r.empty())
        return 0;
    std::wstring f = r + L"\\salt.txt";
    std::string t;
    unsigned long long v = 0;
    pstore::Got got = read_file(f, t);
    bool parsed = got == pstore::Got::Text && sscanf(t.c_str(), "sporecoop-store-salt %16llx", &v) == 1 && v;
    pstore::SaltStep step = pstore::salt_step(got, parsed, g_unreadable_fix);
    if (step == pstore::SaltStep::Use) {
        g_salt = v;
        g_salt_retry_at = -1;
        return g_salt;
    }
    if (step == pstore::SaltStep::NoKeys) {
        g_salt_retry_at = now + 2.0;
        if (g_salt_waits++ % 30 == 0)
            H->log(M, SK_WARN, "store: salt.txt is there but cannot be read now (error %lu): no stored player is looked up "
                               "until it can, the file stays (BUG-9309, %u tries)",
                   (unsigned long)g_read_error, g_salt_waits);
        return 0;
    }
    g_salt_retry_at = -1;
    if (g_unreadable_fix && got == pstore::Got::Text) { // there, no salt in it: kept for a look
        MoveFileExW(f.c_str(), (f + L".bad").c_str(), MOVEFILE_REPLACE_EXISTING);
        H->log(M, SK_WARN, "store: salt.txt holds no salt: a new one (the old file is salt.txt.bad, BUG-9309)");
    }
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    uint64_t x = (uint64_t)q.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ GetTickCount64() ^ (uint64_t)time(nullptr) << 20;
    x = pstore::digest(x, "salt");
    char b[64];
    snprintf(b, sizeof b, "sporecoop-store-salt %016llx\n", (unsigned long long)x);
    std::string err;
    if (!write_file(f, b, err, !g_unreadable_fix)) {
        if (g_unreadable_fix && GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES) {
            H->log(M, SK_WARN, "store: salt.txt came meanwhile (%s): read again (BUG-9309)", err.c_str());
            return 0; // (the next ask reads it)
        }
        H->log(M, SK_WARN, "store: no salt file (%s): the keys hold for this run only", err.c_str());
    }
    g_salt = x;
    return g_salt;
}

static bool is_client() { return C.sess && C.sess->role() == net::Role::Client; }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }

std::string key_of(uint8_t id)
{
    if (!C.sess || !is_host())
        return "";
    if (id == C.sess->my_id())
        return "host";
    std::string k = C.sess->player_key(id);
    uint64_t s = salt();
    return k.empty() || !s ? "" : pstore::stable_key(s, k);
}

pstore::Store* world() { return g_open ? &g_cur : nullptr; }
uint32_t serial() { return g_serial; }
std::string world_name() { return g_open ? g_cur.world : ""; }

static std::string g_unreadable_world; // the world whose file was last found unreadable (said once in the log)

static void open_world(const std::string& w)
{
    std::wstring f = file_of(w, false);
    std::string text;
    pstore::Got got = f.empty() ? pstore::Got::Missing : read_file(f, text);
    pstore::WorldStep step = pstore::world_step(got, g_unreadable_fix);
    if (step == pstore::WorldStep::Wait) { // BUG-9309: no empty store over the file; tick() tries again every second
        if (g_unreadable_world != w)
            H->log(M, SK_WARN, "store: the file of world '%s' cannot be read now (error %lu): its store stays closed and "
                               "is read again, the file is not written (BUG-9309)",
                   w.c_str(), (unsigned long)g_read_error);
        g_unreadable_world = w;
        g_open = false;
        g_cur.reset("");
        return;
    }
    if (g_unreadable_world == w)
        H->log(M, SK_INFO, "store: the file of world '%s' can be read again (BUG-9309)", w.c_str());
    g_unreadable_world.clear();
    bool has = step == pstore::WorldStep::Parse;
    g_parsed = g_cur.parse(has ? text : "", w);
    if (g_parsed == pstore::Parsed::Damaged || g_parsed == pstore::Parsed::Foreign) {
        g_bad_files++;
        MoveFileExW(f.c_str(), (f + L".bad").c_str(), MOVEFILE_REPLACE_EXISTING); // kept for a look; a new one follows
        H->log(M, SK_WARN, "store: the file of world '%s' is %s: an empty store (the file moved to .bad)", w.c_str(),
               pstore::parsed_name(g_parsed));
    }
    if (has)
        g_reads++;
    g_open = true;
    g_serial++;
    H->log(M, SK_INFO, "store: world '%s' opened: %u players, %u records (%s)", w.c_str(), (unsigned)g_cur.players.size(),
           (unsigned)g_cur.records(), has ? pstore::parsed_name(g_parsed) : "no file");
}

// the world of the game played now: "<slot>/<planet>", "" none
static std::string world_now() { return pstore::world_of_game_key(world::current_game_key()); }

void tick()
{
    bool client = is_client();
    std::string name = modes::current_world_name();
    double now = H->time_seconds();
    static double next;
    // the same planet name in another slot (a load of another copy) shows no other name: looked up every second too
    if (name == g_seen_name && client == g_seen_client && now < next)
        return;
    next = now + 1.0;
    g_seen_name = name;
    g_seen_client = client;
    std::string w = client ? "" : world_now();
    if (w.empty()) {
        if (client || name.empty() || name == "SaveGame") { // a guest, or the menu: no world (its file stays)
            g_open = false;
            g_cur.reset("");
        }
        return; // "snapshot.spo" with no planet known yet, the save database not known: as it is
    }
    if (g_open && g_cur.world == w)
        return;
    open_world(w);
}

struct WriteJob {
    std::wstring file;
    std::string text, world;
};
static void write_job(void* p)
{
    WriteJob* j = (WriteJob*)p;
    std::string err;
    if (write_file(j->file, j->text, err)) {
        g_writes++;
        H->log(M, SK_INFO, "store: world '%s' written (%u bytes)", j->world.c_str(), (unsigned)j->text.size());
    } else {
        g_write_fails++;
        g_last_error = err;
        H->log(M, SK_WARN, "store: world '%s' NOT written: %s", j->world.c_str(), err.c_str());
    }
    delete j;
}

static void write_now(bool force)
{
    if (!g_open || g_cur.world.empty())
        return;
    std::wstring f = file_of(g_cur.world, true);
    if (f.empty())
        return;
    // a world nobody ever kept anything for gets no file
    if (!force && !g_cur.records() && GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    g_cur.saved = now_unix();
    H->run_outside_frame(write_job, new WriteJob{f, g_cur.text(), g_cur.world});
}

static void collect()
{
    int64_t now = now_unix();
    for (auto& f : features())
        if (f.save)
            f.save(g_cur, now);
}

void after_save()
{
    if (!H)
        return;
    g_saves++;
    g_saved_at = H->time_seconds();
    if (is_client())
        return; // a guest's save is of its copy of the host's world: the host keeps the store
    std::string w = world_now(); // fresh: the save may have named the game just now (snapshot.spo -> the planet)
    if (w.empty())
        return;
    if (!g_open || g_cur.world != w) {
        g_seen_name = modes::current_world_name();
        open_world(w);
    }
    collect();
    write_now(false);
}

// lifecycle-exempt: no state of the player: only the host's hint (the name fallback), called from player_join
void on_player_join(uint8_t id)
{
    if (!is_host() || !g_open || id == C.sess->my_id())
        return;
    std::string k = key_of(id), name = C.sess->name_of(id);
    if (k.empty() || g_cur.players.count(k))
        return;
    auto cand = g_cur.by_name(name, k);
    // a stored player of the name that is here under another key is somebody else's
    for (auto& p : C.sess->players())
        if (p.id != id)
            std::erase(cand, key_of(p.id));
    if (cand.empty())
        return;
    g_told++;
    std::string t = name + " joined from a game this world does not know (another PC or a new install?). If it is the "
                           "same player, `coop.store adopt " + name + "` in the host's console gives them their saved things";
    H->log(M, SK_INFO, "store: %s", t.c_str());
    if (C.notice)
        C.notice(t);
}

static void say_status(SkOut* out)
{
    H->out_printf(out, "store: world=%s open=%d serial=%u parsed=%s players=%u records=%u reads=%u writes=%u fails=%u bad=%u saves=%u adopted=%u told=%u since_save=%.0f unreadable=%u\n",
                  g_cur.world.empty() ? "-" : g_cur.world.c_str(), (int)g_open, g_serial, pstore::parsed_name(g_parsed),
                  (unsigned)g_cur.players.size(), (unsigned)g_cur.records(), g_reads, g_writes, g_write_fails, g_bad_files,
                  g_saves, g_adopted, g_told, since_save(), g_unreadable_n);
    if (!g_last_error.empty())
        H->out_printf(out, "  last error: %s\n", g_last_error.c_str());
    for (auto& [f, r] : g_cur.wrecs)
        H->out_printf(out, "  world %s v%u %u bytes\n", f.c_str(), r.ver, (unsigned)r.data.size());
    for (auto& [k, p] : g_cur.players) {
        std::string here = "-";
        if (C.sess && is_host())
            for (auto& pl : C.sess->players())
                if (key_of(pl.id) == k)
                    here = std::to_string(pl.id);
        H->out_printf(out, "  player '%s' id=%s seen=%lld:", p.name.c_str(), here.c_str(), (long long)p.seen);
        for (auto& [f, r] : p.recs)
            H->out_printf(out, " %s v%u %uB", f.c_str(), r.ver, (unsigned)r.data.size());
        H->out_printf(out, "\n");
    }
}

int command(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string sub = argc > 1 ? argv[1] : "";
    if (sub.empty() || sub == "status") {
        say_status(out);
        return 0;
    }
    if (sub == "save") { // tests: as if the game saved now (the features' records, the file)
        if (is_client()) {
            H->out_printf(out, "a guest keeps no store\n");
            return 1;
        }
        if (!g_open) {
            std::string w = world_now();
            if (!w.empty())
                open_world(w);
        }
        collect();
        write_now(true);
        H->out_printf(out, "store: %s queued for writing\n", g_open ? g_cur.world.c_str() : "(no world)");
        return g_open ? 0 : 1;
    }
    if (sub == "adopt") { // the host's word: a player that came with a new token gets the stored things of its name
        if (!is_host() || !g_open || argc < 3) {
            H->out_printf(out, "usage (host, a world open): coop.store adopt <player name> [<stored name>]\n");
            return 1;
        }
        int id = -1;
        for (auto& p : C.sess->players())
            if (p.name == argv[2] && p.id != C.sess->my_id())
                id = p.id;
        if (id < 0) {
            H->out_printf(out, "no guest named '%s' in the session\n", argv[2]);
            return 1;
        }
        std::string to = key_of((uint8_t)id);
        auto cand = g_cur.by_name(argc > 3 ? argv[3] : argv[2], to);
        for (auto& p : C.sess->players())
            if (p.id != id)
                std::erase(cand, key_of(p.id));
        if (cand.size() != 1) {
            H->out_printf(out, "%u stored players named '%s' not in the session: nothing moved%s\n", (unsigned)cand.size(),
                          argc > 3 ? argv[3] : argv[2], cand.size() > 1 ? " (more than one: the store keeps them apart)" : "");
            return 1;
        }
        g_cur.adopt(cand[0], to, argv[2], now_unix());
        for (auto& f : features())
            if (f.adopt)
                f.adopt(cand[0], to);
        g_adopted++;
        write_now(true);
        H->out_printf(out, "the stored things of '%s' are %s's now\n", argc > 3 ? argv[3] : argv[2], argv[2]);
        H->log(M, SK_INFO, "store: the host gave the stored things of '%s' to %s (coop.store adopt)", argc > 3 ? argv[3] : argv[2],
               argv[2]);
        return 0;
    }
    if (sub == "saltprobe") { // tests (BUG-9309): another program holds salt.txt open for writing while the salt is read
        if (!is_host()) {
            H->out_printf(out, "saltprobe: host only\n");
            return 1;
        }
        int gid = -1;
        for (auto& p : C.sess->players())
            if (p.id != C.sess->my_id())
                gid = p.id;
        std::wstring r = root(true), f = r + L"\\salt.txt";
        uint64_t before = salt();
        std::string kb = gid >= 0 ? key_of((uint8_t)gid) : "", tb, ta, td;
        auto raw = [&](std::string& t) { // the file's bytes, read with every share flag (as the fix does)
            bool fx = g_unreadable_fix;
            g_unreadable_fix = true;
            read_file(f, t);
            g_unreadable_fix = fx;
        };
        raw(tb);
        // as a sync or backup tool: read and write access, every share flag
        HANDLE hold = CreateFileW(f.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        g_salt = 0; // (as at the first ask of a run)
        g_salt_retry_at = -1;
        uint64_t during = salt();
        std::string kd = gid >= 0 ? key_of((uint8_t)gid) : "";
        if (hold != INVALID_HANDLE_VALUE)
            CloseHandle(hold);
        g_salt_retry_at = -1;
        uint64_t after = salt();
        std::string ka = gid >= 0 ? key_of((uint8_t)gid) : "";
        raw(ta);
        H->out_printf(out, "saltprobe: held=%d guest=%d salt_same=%d file_same=%d during=%s key_before=%s key_during=%s key_after=%s\n",
                      hold != INVALID_HANDLE_VALUE ? 1 : 0, gid, (int)(before && after == before), (int)(!tb.empty() && ta == tb),
                      during == before ? "same" : during ? "other" : "none", kb.empty() ? "-" : kb.c_str(),
                      kd.empty() ? "-" : kd.c_str(), ka.empty() ? "-" : ka.c_str());
        // the tests that follow get the salt of before back (the old side wrote a new one)
        if (!tb.empty() && ta != tb) {
            std::string err;
            write_file(f, tb, err);
        }
        g_salt = before;
        return 0;
    }
    if (sub == "reload") { // tests: read the open world's file again
        std::string w = g_cur.world;
        if (w.empty()) {
            H->out_printf(out, "no world open\n");
            return 1;
        }
        open_world(w);
        say_status(out);
        return 0;
    }
    H->out_printf(out, "usage: coop.store [status | save | adopt <player> [<stored name>] | reload | saltprobe]\n");
    return 1;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    H->command_add(M, "coop.store",
                   "coop.store [status | save | adopt <player> [<stored name>] | reload | saltprobe] - the per-player store on the "
                   "host's disk (E1, BUG-1149)",
                   command, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace store
