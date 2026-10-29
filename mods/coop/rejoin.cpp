// The rejoin token file (BUG-0235), see rejoin.h.
#include "rejoin.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <string>

namespace rejoin {

static HANDLE g_slot = INVALID_HANDLE_VALUE;
static bool g_tried = false;
static uint64_t g_mem = 0; // the token of this process when the file cannot be used

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

uint64_t token_for_join(int instance)
{
    open_slot(instance);
    if (g_slot == INVALID_HANDLE_VALUE) {
        if (!g_mem)
            g_mem = fresh();
        return g_mem;
    }
    if (uint64_t t = read_slot())
        return t; // a crash (or a join before) left it: the host may still hold our old entry
    uint64_t t = fresh();
    if (!write_slot(t)) {
        if (!g_mem)
            g_mem = fresh();
        return g_mem;
    }
    return t;
}

void clear()
{
    g_mem = 0;
    if (g_slot != INVALID_HANDLE_VALUE)
        write_slot(0);
}

void shutdown()
{
    clear();
    if (g_slot != INVALID_HANDLE_VALUE)
        CloseHandle(g_slot);
    g_slot = INVALID_HANDLE_VALUE;
    g_tried = false;
}

} // namespace rejoin
