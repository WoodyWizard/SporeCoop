// The rejoin token file (BUG-0235), see rejoin.h.
#include "rejoin.h"
#include "fixes.h"
#include "rejoin_kept.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace rejoin {

static HANDLE g_slot = INVALID_HANDLE_VALUE;
static bool g_tried = false;
static uint64_t g_mem = 0; // the token of this process when the file cannot be used
// BUG-1001: the token of the current join and where it went; the end of that session keeps it in memory for the same
// host address and name (a few of them: a refused join under another name does not push the player's own out)
static uint64_t g_cur = 0;
static std::string g_cur_host, g_cur_name;
struct Kept {
    std::string host, name;
    uint64_t token;
};
static std::vector<Kept> g_kept;

// BUG-1135: the token of a clean leave is also kept on disk (rejoin-<instance>-<slot>-kept.txt, rejoin_kept.h: a salted
// hash of host address and name, 30 days with net.rejoin_month (BUG-1150), at most 16), so the same game after a restart is the same player to that host
// and gets its kept Space state back (BUG-1107 keys it by the token). The file belongs to the slot this game holds
// exclusively: another game of this PC (another slot, another instance) never reads it. `coop.fix net.rejoin_disk off`:
// memory only (a restart sends a fresh token).
static bool g_disk_fix = true;
COOP_FIX("net.rejoin_disk", "BUG-1135", g_disk_fix,
         "net: the token of a clean leave is kept on disk for that host and name (hashed), so after a restart of "
         "the game the host gives the player's kept things back");
static std::wstring g_kept_path; // of the slot taken (empty: no slot, no disk)
// BUG-1150: a day was too short for the per-player store on the host's disk (player_store.h finds a player by its
// token): a weekly game's next evening came with a fresh token and the guest was a new player. 30 days now.
// `coop.fix net.rejoin_month off`: a day (0.7.15 before E1).
static bool g_month_fix = true;
COOP_FIX("net.rejoin_month", "BUG-1150", g_month_fix,
         "net: the token of a clean leave is kept on disk for 30 days (not a day), so the host's store of the player's "
         "things finds the player the next week too");

// NET-2 (BUG-1498, rejoin_kept.h SlotLine): the slot names the host and name its token was written for; a join to
// another host or under another name does not send it (a join that failed at once, a crash then another host), so the
// token a clean leave kept for this host wins again. `coop.fix net.rejoin_slot_pair off`: any join takes the slot's
// token (0.7.16.1). tests: `coop.rejoin slot` prints whether the slot holds a token for the current pair
static bool g_slot_pair_fix = true;
COOP_FIX("net.rejoin_slot_pair", "BUG-1498", g_slot_pair_fix,
         "net: the rejoin token a crash or a failed join left behind is sent only to the host and under the name it was "
         "made for, so the token kept for this host comes back after a failed join");

static uint64_t fresh()
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    static uint64_t n = 0;
    uint64_t x = (uint64_t)q.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ GetTickCount64() ^
                 (++n * 0x9E3779B97F4A7C15ull) ^ (uint64_t)(uintptr_t)&n;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x ? x : 1;
}

static void open_slot(int inst)
{
    if (g_tried)
        return;
    g_tried = true;
    wchar_t p[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) // profile-aware (hooked)
        return;
    std::wstring dir = std::wstring(p) + L"\\SporeCoop";
    CreateDirectoryW(dir.c_str(), nullptr);
    for (int slot = 1; slot <= 8; slot++) {
        std::wstring f = dir + L"\\rejoin-" + std::to_wstring(inst) + L"-" + std::to_wstring(slot) + L".txt";
        HANDLE h = CreateFileW(f.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            g_slot = h;
            g_kept_path = dir + L"\\rejoin-" + std::to_wstring(inst) + L"-" + std::to_wstring(slot) + L"-kept.txt";
            return;
        }
    }
}

// the slot's token if a join of `pair` may send it (BUG-1498), 0 none
static uint64_t read_slot(uint64_t pair)
{
    char b[64] = {};
    DWORD n = 0;
    if (SetFilePointer(g_slot, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !ReadFile(g_slot, b, sizeof b - 1, &n, nullptr))
        return 0;
    SlotLine l;
    if (!l.parse(std::string(b, n)) || !l.usable_for(pair, g_slot_pair_fix))
        return 0;
    return l.token;
}

static bool write_slot(uint64_t v, uint64_t pair = 0)
{
    std::string b = SlotLine::text(v, pair); // "" (v 0): the slot is emptied
    DWORD n = 0;
    if (SetFilePointer(g_slot, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return false;
    if (!b.empty() && (!WriteFile(g_slot, b.data(), (DWORD)b.size(), &n, nullptr) || n != (DWORD)b.size()))
        return false;
    SetEndOfFile(g_slot);
    FlushFileBuffers(g_slot);
    return true;
}

static bool disk_load(KeptFile& f)
{
    f = KeptFile();
    f.ttl = g_month_fix ? KeptFile::kTtlMonth : KeptFile::kTtl;
    if (g_kept_path.empty())
        return false;
    HANDLE h = CreateFileW(g_kept_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    char b[4096];
    DWORD n = 0;
    bool ok = ReadFile(h, b, sizeof b - 1, &n, nullptr);
    CloseHandle(h);
    return ok && f.parse(std::string(b, n));
}

static void disk_save(const KeptFile& f)
{
    if (g_kept_path.empty())
        return;
    std::string t = f.text();
    std::wstring tmp = g_kept_path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD n = 0;
    bool ok = WriteFile(h, t.data(), (DWORD)t.size(), &n, nullptr) && n == (DWORD)t.size();
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), g_kept_path.c_str(), MOVEFILE_REPLACE_EXISTING))
        DeleteFileW(tmp.c_str());
}

static int64_t now_unix() { return (int64_t)time(nullptr); }

// the token kept on disk for this host and name (BUG-1135), 0 = none
static uint64_t disk_find(const std::string& host, const std::string& name)
{
    if (!g_disk_fix)
        return 0;
    KeptFile f;
    if (!disk_load(f))
        return 0;
    return f.find(host, name, now_unix());
}

static void disk_put(const std::string& host, const std::string& name, uint64_t token)
{
    if (!g_disk_fix || g_kept_path.empty())
        return;
    KeptFile f;
    disk_load(f);
    f.ensure_salt(fresh());
    f.put(host, name, token, now_unix());
    disk_save(f);
}

static uint64_t kept_for(const std::string& host, const std::string& name)
{
    for (auto& k : g_kept)
        if (k.host == host && k.name == name)
            return k.token;
    return 0;
}

static uint64_t join_token(int instance, uint64_t kept, uint64_t pair)
{
    open_slot(instance);
    if (g_slot == INVALID_HANDLE_VALUE) {
        if (kept)
            g_mem = kept;
        if (!g_mem)
            g_mem = fresh();
        return g_mem;
    }
    if (uint64_t t = read_slot(pair))
        return t; // a crash (or a join before) to this host under this name left it: the host may still hold our entry
    uint64_t t = kept ? kept : fresh();
    if (!write_slot(t, pair)) {
        if (!g_mem)
            g_mem = t;
        return g_mem;
    }
    return t;
}

static bool g_other, g_cur_other; // tests (BUG-1107): this join comes as another game (see rejoin.h)

void test_other(bool on) { g_other = on; }
bool test_other_on() { return g_other; }

uint64_t token_for_join(int instance, const std::string& host, const std::string& name)
{
    g_cur_other = g_other;
    if (g_other) { // a token of its own, never written to the slot and never kept for this host and name
        g_cur = fresh();
        g_cur_host = host;
        g_cur_name = name;
        return g_cur;
    }
    open_slot(instance); // the disk file belongs to the slot
    uint64_t kept = kept_for(host, name);
    if (!kept)
        kept = disk_find(host, name); // BUG-1135: the same game after a restart
    g_cur = join_token(instance, kept, SlotLine::pair_of(host, name));
    g_cur_host = host;
    g_cur_name = name;
    return g_cur;
}

void clear(bool keep)
{
    if (keep && g_cur && !g_cur_other) {
        auto it = g_kept.begin();
        while (it != g_kept.end() && !(it->host == g_cur_host && it->name == g_cur_name))
            ++it;
        if (it != g_kept.end())
            it->token = g_cur;
        else {
            if (g_kept.size() >= 8)
                g_kept.erase(g_kept.begin());
            g_kept.push_back({g_cur_host, g_cur_name, g_cur});
        }
        disk_put(g_cur_host, g_cur_name, g_cur); // BUG-1135: also across a restart of the game
    }
    g_cur = 0;
    g_mem = 0;
    if (g_slot != INVALID_HANDLE_VALUE)
        write_slot(0);
}

void shutdown(bool keep)
{
    clear(keep); // BUG-1135: a game closed in a session keeps its token for that host and name like a leave
    g_kept.clear();
    if (g_slot != INVALID_HANDLE_VALUE)
        CloseHandle(g_slot);
    g_slot = INVALID_HANDLE_VALUE;
    g_kept_path.clear();
    g_tried = false;
}

} // namespace rejoin
