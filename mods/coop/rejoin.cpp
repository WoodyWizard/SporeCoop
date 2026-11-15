// The rejoin token file (BUG-0235), see rejoin.h.
#include "rejoin.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
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
            return;
        }
    }
}

static uint64_t read_slot()
{
    char b[32] = {};
    DWORD n = 0;
    if (SetFilePointer(g_slot, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !ReadFile(g_slot, b, sizeof b - 1, &n, nullptr))
        return 0;
    unsigned long long v = 0;
    if (n < 16 || sscanf(b, "%16llx", &v) != 1)
        return 0;
    return v;
}

static bool write_slot(uint64_t v)
{
    char b[24];
    int len = v ? snprintf(b, sizeof b, "%016llx\n", (unsigned long long)v) : 0;
    DWORD n = 0;
    if (SetFilePointer(g_slot, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return false;
    if (len && (!WriteFile(g_slot, b, (DWORD)len, &n, nullptr) || n != (DWORD)len))
        return false;
    SetEndOfFile(g_slot);
    FlushFileBuffers(g_slot);
    return true;
}

static uint64_t kept_for(const std::string& host, const std::string& name)
{
    for (auto& k : g_kept)
        if (k.host == host && k.name == name)
            return k.token;
    return 0;
}

static uint64_t join_token(int instance, uint64_t kept)
{
    open_slot(instance);
    if (g_slot == INVALID_HANDLE_VALUE) {
        if (kept)
            g_mem = kept;
        if (!g_mem)
            g_mem = fresh();
        return g_mem;
    }
    if (uint64_t t = read_slot())
        return t; // a crash (or a join before) left it: the host may still hold our old entry
    uint64_t t = kept ? kept : fresh();
    if (!write_slot(t)) {
        if (!g_mem)
            g_mem = t;
        return g_mem;
    }
    return t;
}

uint64_t token_for_join(int instance, const std::string& host, const std::string& name)
{
    g_cur = join_token(instance, kept_for(host, name));
    g_cur_host = host;
    g_cur_name = name;
    return g_cur;
}

void clear(bool keep)
{
    if (keep && g_cur) {
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
    }
    g_cur = 0;
    g_mem = 0;
    if (g_slot != INVALID_HANDLE_VALUE)
        write_slot(0);
}

void shutdown()
{
    clear();
    g_kept.clear();
    if (g_slot != INVALID_HANDLE_VALUE)
        CloseHandle(g_slot);
    g_slot = INVALID_HANDLE_VALUE;
    g_tried = false;
}

} // namespace rejoin
