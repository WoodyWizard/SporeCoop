#include "core.h"
#include "path_mask.h"

#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <cstring>
#include <mutex>

namespace sk {

int g_log_level = SK_DEBUG;

static std::mutex g_mu;
static FILE* g_file = nullptr;
static std::deque<std::string> g_ring;
static const size_t kRing = 2000;

// The session clock (a co-op session's common time, set by a mod through log_session_clock): every
// line carries "[s <seconds>]" after the local time, so the logs of the players of one session line
// up (tools/merge_logs.py). Microseconds to add to now_seconds(); LLONG_MIN = no session clock.
static std::atomic<long long> g_sclock_us{LLONG_MIN};

void log_session_clock(double offset, int on)
{
    g_sclock_us = on ? (long long)(offset * 1e6) : LLONG_MIN;
}

bool log_session_time(double& t)
{
    long long off = g_sclock_us.load();
    if (off == LLONG_MIN)
        return false;
    t = now_seconds() + (double)off / 1e6;
    return true;
}

// Paths of the player's computer never go into the log or a report in full (players attach them to
// public issues): the game folder becomes <game>, the Windows user folder %USERPROFILE%, a Documents
// folder outside it <Documents>, Wine's home folder of the Linux user <home>. Longest first; compared
// without case (sporekit/path_mask.h). Set once at log_init.
// BUG-1124: the folders are read through the wide API and kept as UTF-8, the text of the log: the ANSI
// GetEnvironmentVariableA gave a Cyrillic user name in the code page (cp1251), which never matched the
// UTF-8 log lines, so C:\Users\<name> stayed in full. The ANSI and short (8.3) forms are masked too, for
// text some code wrote through an A function.
static sk::pathmask::Masks g_masks;

static std::string env_w(const wchar_t* name)
{
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(name, b, MAX_PATH * 2);
    return n && n < MAX_PATH * 2 ? narrow(std::wstring(b, n)) : std::string();
}

static std::string env_a(const char* name)
{
    char b[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableA(name, b, sizeof b);
    return n && n < sizeof b ? std::string(b, n) : std::string();
}

static std::string short_path(const std::wstring& p)
{
    wchar_t b[MAX_PATH * 2];
    DWORD n = p.empty() ? 0 : GetShortPathNameW(p.c_str(), b, MAX_PATH * 2);
    return n && n < MAX_PATH * 2 ? narrow(std::wstring(b, n)) : std::string();
}

static void init_masks()
{
    g_masks.clear();
    std::wstring g = game_dir(); // ...\SporebinEP1: its parent is the game folder
    size_t k = g.find_last_of(L"\\/");
    if (k != std::wstring::npos)
        g_masks.add(narrow(g.substr(0, k)), "<game>");
    std::string prof = env_w(L"USERPROFILE");
    if (!prof.empty()) {
        g_masks.add(prof, "%USERPROFILE%");
        g_masks.add(env_a("USERPROFILE"), "%USERPROFILE%"); // the ANSI code page form (the same when ASCII)
        g_masks.add(short_path(widen(prof)), "%USERPROFILE%");
    }
    // Documents moved to another drive or OneDrive: not inside the profile
    wchar_t docs[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)) && docs[0]) {
        std::string d = narrow(docs);
        if (!g_masks.covered(d))
            g_masks.add(d, "<Documents>");
    }
    // Wine: \??\Z:\home\<user>
    std::string home = env_w(L"WINEHOMEDIR");
    if (!home.empty())
        g_masks.add(home.rfind("\\??\\", 0) == 0 ? home.substr(4) : home, "<home>");
}

std::string mask_paths(const std::string& s) { return g_masks.apply(s); }

// The log of the previous run is kept: a player who restarts the game after a crash still has
// the log of the crash. logs\previous\<name>-<time of its last line>.log, the newest kKeep of
// each name (sporekit, sporekit.2, ...)
static const int kKeep = 10;

static void keep_previous(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) || (!fa.nFileSizeLow && !fa.nFileSizeHigh))
        return;
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = path.substr(0, slash) + L"\\previous", file = path.substr(slash + 1);
    std::wstring stem = file.substr(0, file.rfind(L".log"));
    CreateDirectoryW(dir.c_str(), nullptr);
    FILETIME lt;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lt);
    FileTimeToSystemTime(&lt, &st);
    wchar_t name[96];
    swprintf(name, 96, L"-%04d%02d%02d-%02d%02d%02d.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    MoveFileExW(path.c_str(), (dir + L"\\" + stem + name).c_str(), MOVEFILE_REPLACE_EXISTING);
    // the oldest go (names sort by time)
    std::vector<std::wstring> old;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + stem + L"-*.log").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            old.push_back(fd.cFileName);
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(old.begin(), old.end());
    for (size_t i = 0; i + kKeep < old.size(); i++)
        DeleteFileW((dir + L"\\" + old[i]).c_str());
}

void log_init(const std::wstring& path)
{
    std::lock_guard<std::mutex> lk(g_mu);
    ThreadFlag tf(SK_TF_IN_LOG);
    if (g_file)
        fclose(g_file);
    init_masks();
    keep_previous(path);
    g_file = _wfopen(path.c_str(), L"w");
}

void logv(SkMod* mod, int level, const char* fmt, va_list ap)
{
    if (level < g_log_level)
        return;
    // a fault inside the locked part below re-enters here from the exception logger on the same
    // thread: the mutex is not recursive (an SRWLOCK), taking it again would block for good. The
    // line is dropped instead (BUG-0115).
    if (fixed(g_fix_reentry) && (thread_flags() & SK_TF_IN_LOG))
        return;
    char msg[2048];
    vsnprintf(msg, sizeof msg, fmt, ap);
    static const char* lv[] = {"DBG", "INF", "WRN", "ERR"};
    SYSTEMTIME st;
    GetLocalTime(&st);
    char sclock[32] = "";
    double t;
    if (log_session_time(t))
        snprintf(sclock, sizeof sclock, "[s %.3f] ", t);
    // no private path in full (a fault inside the masking re-enters this function: SK_TF_IN_LOG is
    // not set yet here, the reentry guard above covers only the locked part)
    std::string m = mask_paths(msg);
    char line[2300];
    snprintf(line, sizeof line, "%02d:%02d:%02d.%03d %s[%s] %s: %s", st.wHour, st.wMinute,
             st.wSecond, st.wMilliseconds, sclock, lv[level & 3], mod ? mod->name.c_str() : "?", m.c_str());
    std::lock_guard<std::mutex> lk(g_mu);
    ThreadFlag tf(SK_TF_IN_LOG);
    if (g_file) {
        fputs(line, g_file);
        fputc('\n', g_file);
        fflush(g_file);
    }
    g_ring.emplace_back(line);
    if (g_ring.size() > kRing)
        g_ring.pop_front();
}

void logf(SkMod* mod, int level, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logv(mod, level, fmt, ap);
    va_end(ap);
}

std::vector<std::string> log_tail(size_t n)
{
    std::lock_guard<std::mutex> lk(g_mu);
    ThreadFlag tf(SK_TF_IN_LOG);
    std::vector<std::string> out;
    size_t start = g_ring.size() > n ? g_ring.size() - n : 0;
    for (size_t i = start; i < g_ring.size(); i++)
        out.push_back(g_ring[i]);
    return out;
}

// kit.test logfault: a fault while this thread holds the log's lock (the page fault is repaired by
// a test handler after the exception logger saw it). Before BUG-0115 the logger took the lock again
// and the thread stopped for good.
void log_fault_test()
{
    volatile uint8_t* page = (volatile uint8_t*)test_fault_page();
    std::lock_guard<std::mutex> lk(g_mu);
    ThreadFlag tf(SK_TF_IN_LOG);
    page[0] = 1;
}

// ---- small utils ----
std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring to_windows_path(const std::string& p)
{
    std::wstring w = widen(p);
    if (!w.empty() && w[0] == L'/')
        w = L"Z:" + w;
    for (auto& c : w)
        if (c == L'/')
            c = L'\\';
    return w;
}

std::string json_escape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            } else
                o += (char)c;
        }
    }
    return o;
}

double now_seconds()
{
    static LARGE_INTEGER f = [] {
        LARGE_INTEGER x;
        QueryPerformanceFrequency(&x);
        return x;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

} // namespace sk
