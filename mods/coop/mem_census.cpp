// The address space census (BUG-0631, BUG-0621; docs/06-testing.md#testing-mem-census).
//
// `coop.memcensus` walks the game's address space with VirtualQuery and groups the regions by their
// allocation (AllocationBase): what kind (image / mapped / private), how much is reserved and committed,
// and, for private allocations, who made them. "Who" comes from a tracker the test switches on
// (`coop.memcensus track on`): a hook on ntdll's NtAllocateVirtualMemory / NtFreeVirtualMemory that
// remembers every new reservation with the first return addresses on the stack outside the system DLLs
// (ntdll, kernelbase, kernel32, the CRT): the game's own allocator (SporeApp.exe MMapMalloc and its
// segments), the graphics (d3d9 / DXVK), our DLLs (coop.dll, sporekit.dll: their CRT heap grows through
// the same call). Only allocations made while the tracker is on have a caller; older ones are "untracked".
//
// Output (one line per group, for tools/tests/coop_mem_census.py to diff two snapshots):
//   census free_mb=F block_mb=B regions=R allocs=A tracked=T
//   cat <name> n=<allocations> res_kb=<reserved incl. committed> com_kb=<committed>
//   who <module+rva>[<<module+rva>...] n=... res_kb=... com_kb=...      (tracked private allocations)
// Test / lab tool: off by default, never on in a player's game unless typed into the console.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "sporekit.h"

namespace memcensus {

static const SkHost* H;
static SkMod* M;

// ---------------- the tracker ----------------
constexpr int kCallers = 6;
struct Alloc {
    uintptr_t base;   // 0 = empty slot, 1 = deleted
    uint32_t size;
    uint32_t caller[kCallers];
};
constexpr uint32_t kSlots = 1u << 17; // 128 Ki allocations (a 4 GB space of 64 KB grains has 64 Ki)
static Alloc* g_tab;                  // VirtualAlloc'ed before the hook is on (no allocation in the hook)
static CRITICAL_SECTION g_cs;
static bool g_cs_init;
static std::atomic<bool> g_on{false};
static DWORD g_tls = TLS_OUT_OF_INDEXES;
static std::atomic<uint32_t> g_dropped{0};

struct Range {
    uintptr_t lo, hi;
};
static Range g_sys[24]; // system DLLs and the game's allocator: skipped when looking for the caller
static int g_nsys;

typedef LONG(NTAPI* NtAllocFn)(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG);
typedef LONG(NTAPI* NtFreeFn)(HANDLE, PVOID*, PSIZE_T, ULONG);
static NtAllocFn o_alloc;
static NtFreeFn o_free;
static void* g_alloc_target;

static uint32_t slot_of(uintptr_t b) { return (uint32_t)((b >> 16) * 2654435761u) & (kSlots - 1); }

static bool is_sys(uintptr_t a)
{
    for (int i = 0; i < g_nsys; i++)
        if (a >= g_sys[i].lo && a < g_sys[i].hi)
            return true;
    return false;
}

// a return address: in an image's committed code and right after a call instruction
static bool is_ret(uintptr_t a)
{
    if (a < 0x10000 || a >= 0xFFFF0000u)
        return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)(a - 8), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return false;
    if ((uintptr_t)mbi.BaseAddress + mbi.RegionSize < a)
        return false;
    const uint8_t* p = (const uint8_t*)a;
    if (p[-5] == 0xE8)
        return true; // call rel32
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10)
        return true; // call reg / [reg]
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10)
        return true; // call [reg+d8]
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10)
        return true; // call [abs] / [reg+d32]
    if (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10)
        return true; // call [reg*s+d32]
    return false;
}

__attribute__((noinline)) static void callers(uint32_t* out)
{
    uintptr_t* sp = (uintptr_t*)__builtin_frame_address(0);
    uintptr_t self_ret = (uintptr_t)__builtin_return_address(0); // into h_alloc: not a caller
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t top = (uintptr_t)sp + 16384;
    if (VirtualQuery(sp, &mbi, sizeof mbi))
        top = std::min<uintptr_t>(top, (uintptr_t)mbi.BaseAddress + mbi.RegionSize);
    int n = 0;
    for (uintptr_t* p = sp; (uintptr_t)(p + 1) <= top && n < kCallers; p++) {
        uintptr_t v = *p;
        if (v == self_ret || is_sys(v) || !is_ret(v))
            continue;
        if (n && out[n - 1] == v)
            continue;
        out[n++] = (uint32_t)v;
    }
    for (; n < kCallers; n++)
        out[n] = 0;
}

static void remember(uintptr_t base, size_t size, const uint32_t* who)
{
    EnterCriticalSection(&g_cs);
    uint32_t i = slot_of(base), tomb = kSlots;
    for (uint32_t k = 0; k < kSlots; k++, i = (i + 1) & (kSlots - 1)) {
        uintptr_t b = g_tab[i].base;
        if (b == base) {
            tomb = i;
            break;
        }
        if (b == 1 && tomb == kSlots)
            tomb = i;
        if (b == 0) {
            if (tomb == kSlots)
                tomb = i;
            break;
        }
    }
    if (tomb < kSlots) {
        g_tab[tomb].base = base;
        g_tab[tomb].size = (uint32_t)size;
        memcpy(g_tab[tomb].caller, who, sizeof g_tab[tomb].caller);
    } else {
        g_dropped++;
    }
    LeaveCriticalSection(&g_cs);
}

static void forget(uintptr_t base)
{
    EnterCriticalSection(&g_cs);
    uint32_t i = slot_of(base);
    for (uint32_t k = 0; k < kSlots; k++, i = (i + 1) & (kSlots - 1)) {
        if (g_tab[i].base == 0)
            break;
        if (g_tab[i].base == base) {
            g_tab[i].base = 1;
            break;
        }
    }
    LeaveCriticalSection(&g_cs);
}

static bool find(uintptr_t base, Alloc& a)
{
    bool ok = false;
    EnterCriticalSection(&g_cs);
    uint32_t i = slot_of(base);
    for (uint32_t k = 0; k < kSlots; k++, i = (i + 1) & (kSlots - 1)) {
        if (g_tab[i].base == 0)
            break;
        if (g_tab[i].base == base) {
            a = g_tab[i];
            ok = true;
            break;
        }
    }
    LeaveCriticalSection(&g_cs);
    return ok;
}

static bool self_process(HANDLE h) { return h == (HANDLE)-1 || GetProcessId(h) == GetCurrentProcessId(); }

static LONG NTAPI h_alloc(HANDLE proc, PVOID* base, ULONG_PTR zero, PSIZE_T size, ULONG type, ULONG prot)
{
    PVOID want = base ? *base : nullptr;
    LONG r = o_alloc(proc, base, zero, size, type, prot);
    if (r >= 0 && g_on && (type & MEM_RESERVE) && base && *base && size && proc == (HANDLE)-1 &&
        !TlsGetValue(g_tls)) {
        TlsSetValue(g_tls, (void*)1);
        // a reservation at an address asked for inside an existing allocation is not a new one
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(*base, &mbi, sizeof mbi) && mbi.AllocationBase == *base) {
            uint32_t who[kCallers];
            callers(who);
            remember((uintptr_t)*base, *size, who);
        }
        (void)want;
        TlsSetValue(g_tls, nullptr);
    }
    return r;
}

static LONG NTAPI h_free(HANDLE proc, PVOID* base, PSIZE_T size, ULONG type)
{
    PVOID b = base ? *base : nullptr;
    LONG r = o_free(proc, base, size, type);
    if (r >= 0 && g_tab && (type & MEM_RELEASE) && b && proc == (HANDLE)-1)
        forget((uintptr_t)(base && *base ? *base : b));
    return r;
}

static void add_sys(const char* name)
{
    HMODULE m = GetModuleHandleA(name);
    if (!m || g_nsys >= 24)
        return;
    auto* dos = (IMAGE_DOS_HEADER*)m;
    auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)m + dos->e_lfanew);
    g_sys[g_nsys++] = {(uintptr_t)m, (uintptr_t)m + nt->OptionalHeader.SizeOfImage};
}

static std::string track(bool on)
{
    if (!on) {
        g_on = false;
        return "tracker off (the hook stays; nothing new is remembered)\n";
    }
    if (!g_cs_init) {
        InitializeCriticalSection(&g_cs);
        g_cs_init = true;
    }
    if (g_tls == TLS_OUT_OF_INDEXES)
        g_tls = TlsAlloc();
    if (!g_tab)
        g_tab = (Alloc*)VirtualAlloc(nullptr, sizeof(Alloc) * kSlots, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_tab || g_tls == TLS_OUT_OF_INDEXES)
        return "tracker: no memory for its table\n";
    static bool s_ranges;
    if (!s_ranges) {
        s_ranges = true;
        for (const char* n : {"ntdll.dll", "kernelbase.dll", "kernel32.dll", "ucrtbase.dll", "msvcrt.dll",
                              "api-ms-win-crt-heap-l1-1-0.dll", "msvcr90.dll", "msvcr80.dll"})
            add_sys(n);
        // the game's allocator (GeneralAllocator: MallocInternal 0x927E70, MallocAlignedInternal 0x928570,
        // MMapMalloc 0x9271A0, the free and chunk helpers around them; its operator new and malloc): the caller
        // of interest is whoever asked it for memory
        if (uintptr_t mi = H->sym("MallocInternal"))
            if (g_nsys < 24)
                g_sys[g_nsys++] = {mi - 0x3000, mi + 0x1400};
        for (const char* n : {"GeneralAllocator::new_", "malloc", "_aligned_malloc", "malloc_aligned"})
            if (uintptr_t f = H->sym(n))
                if (g_nsys < 24)
                    g_sys[g_nsys++] = {f, f + 0x60};
    }
    if (!g_alloc_target) {
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        void* a = nt ? (void*)GetProcAddress(nt, "NtAllocateVirtualMemory") : nullptr;
        void* f = nt ? (void*)GetProcAddress(nt, "NtFreeVirtualMemory") : nullptr;
        if (!a || !f)
            return "tracker: ntdll exports not found\n";
        if (H->hook_create(M, a, (void*)&h_alloc, (void**)&o_alloc) != 0)
            return "tracker: the NtAllocateVirtualMemory hook failed\n";
        if (H->hook_create(M, f, (void*)&h_free, (void**)&o_free) != 0) {
            H->hook_remove(M, a);
            return "tracker: the NtFreeVirtualMemory hook failed\n";
        }
        g_alloc_target = a;
    }
    g_on = true;
    return "tracker on\n";
}

// ---------------- the walk ----------------
struct Group {
    int n = 0;
    uint64_t res = 0, com = 0;
};

static std::string mod_name(uintptr_t a, std::map<uintptr_t, std::string>& cache, uintptr_t* mbase)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)a, &mbi, sizeof mbi) || mbi.Type != MEM_IMAGE) {
        *mbase = 0;
        return "?";
    }
    uintptr_t b = (uintptr_t)mbi.AllocationBase;
    *mbase = b;
    auto it = cache.find(b);
    if (it != cache.end())
        return it->second;
    char path[MAX_PATH] = {0};
    GetModuleFileNameA((HMODULE)b, path, sizeof path);
    const char* s = strrchr(path, '\\');
    std::string n = s ? s + 1 : path[0] ? path : "?";
    for (auto& c : n)
        c = (char)tolower((unsigned char)c);
    cache[b] = n;
    return n;
}

static std::string where(uint32_t a, std::map<uintptr_t, std::string>& cache, uint32_t grain)
{
    if (!a)
        return "-";
    uintptr_t b;
    std::string n = mod_name(a, cache, &b);
    char t[96];
    uint32_t rva = (uint32_t)(a - b);
    if (grain > 1)
        rva -= rva % grain;
    snprintf(t, sizeof t, "%s+%X", n.c_str(), rva);
    return t;
}

static std::string census(int depth, uint32_t grain, int top)
{
    struct A {
        uintptr_t base;
        DWORD type;
        uint64_t res, com;
    };
    std::vector<A> allocs;
    allocs.reserve(8192);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t p = (uintptr_t)si.lpMinimumApplicationAddress, end = (uintptr_t)si.lpMaximumApplicationAddress;
    uint64_t free = 0, largest = 0;
    int regions = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery((void*)p, &mbi, sizeof mbi) == sizeof mbi && regions < 400000) {
        regions++;
        if (mbi.State == MEM_FREE) {
            free += mbi.RegionSize;
            largest = std::max<uint64_t>(largest, mbi.RegionSize);
        } else {
            uintptr_t ab = (uintptr_t)mbi.AllocationBase;
            if (allocs.empty() || allocs.back().base != ab)
                allocs.push_back({ab, mbi.Type, 0, 0});
            allocs.back().res += mbi.RegionSize;
            if (mbi.State == MEM_COMMIT)
                allocs.back().com += mbi.RegionSize;
        }
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= p)
            break;
        p = next;
    }
    std::map<std::string, Group> cats, whos;
    std::map<uintptr_t, std::string> names;
    int tracked = 0;
    for (auto& a : allocs) {
        std::string cat;
        if (a.type == MEM_IMAGE) {
            uintptr_t b;
            std::string n = mod_name(a.base, names, &b);
            cat = n == "sporeapp.exe" ? "image:sporeapp.exe" : n.find("coop") != std::string::npos ? "image:coop"
                  : n == "sporekit.dll"                    ? "image:sporekit.dll"
                                                           : "image:other";
        } else if (a.type == MEM_MAPPED) {
            cat = a.res >= (16u << 20) ? "mapped:ge16M" : a.res >= (1u << 20) ? "mapped:ge1M" : "mapped:lt1M";
        } else {
            Alloc t;
            if (g_tab && find(a.base, t)) {
                tracked++;
                std::string w;
                std::string first;
                for (int i = 0; i < depth && i < kCallers; i++) {
                    if (!t.caller[i])
                        break;
                    std::string s = where(t.caller[i], names, grain);
                    if (i == 0)
                        first = s.substr(0, s.find('+'));
                    w += (i ? "<" : "") + s;
                }
                if (w.empty())
                    w = first = "?";
                Group& g = whos[w];
                g.n++, g.res += a.res, g.com += a.com;
                cat = "priv:by:" + first;
            } else {
                cat = a.res >= (16u << 20) ? "priv:untracked:ge16M"
                      : a.res >= (1u << 20) ? "priv:untracked:ge1M"
                      : a.res > (64u << 10) ? "priv:untracked:gt64K"
                                            : "priv:untracked:le64K";
            }
        }
        Group& g = cats[cat];
        g.n++, g.res += a.res, g.com += a.com;
    }
    char b[256];
    std::string out;
    snprintf(b, sizeof b, "census free_mb=%d block_mb=%d regions=%d allocs=%d tracked=%d tracker=%s dropped=%u\n",
             (int)(free >> 20), (int)(largest >> 20), regions, (int)allocs.size(), tracked, g_on ? "on" : "off",
             (unsigned)g_dropped);
    out += b;
    for (auto& [k, g] : cats) {
        snprintf(b, sizeof b, "cat %s n=%d res_kb=%llu com_kb=%llu\n", k.c_str(), g.n, (unsigned long long)(g.res >> 10),
                 (unsigned long long)(g.com >> 10));
        out += b;
    }
    std::vector<std::pair<std::string, Group>> w(whos.begin(), whos.end());
    std::sort(w.begin(), w.end(), [](auto& x, auto& y) { return x.second.res > y.second.res; });
    for (int i = 0; i < (int)w.size() && i < top; i++) {
        snprintf(b, sizeof b, "who %s n=%d res_kb=%llu com_kb=%llu\n", w[i].first.c_str(), w[i].second.n,
                 (unsigned long long)(w[i].second.res >> 10), (unsigned long long)(w[i].second.com >> 10));
        out += b;
    }
    return out;
}

static int c_census(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string a1 = argc > 1 ? argv[1] : "";
    if (a1 == "track") {
        std::string a2 = argc > 2 ? argv[2] : "on";
        H->out_printf(out, "%s", track(a2 != "off").c_str());
        return 0;
    }
    // coop.memcensus [depth 1..4] [grain bytes] [top N]
    int depth = argc > 1 ? std::max(1, std::min(kCallers, atoi(argv[1]))) : 3;
    uint32_t grain = argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 0) : 1;
    int top = argc > 3 ? std::max(1, atoi(argv[3])) : 40;
    std::string s = census(depth, grain, top);
    H->out_printf(out, "%s", s.c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    host->command_add(mod, "coop.memcensus",
                      "coop.memcensus [track on|off] | [depth 1-4] [grain] [top] - the game's address space by "
                      "allocation kind and, with the tracker on, by who reserved it (tests, BUG-0631)",
                      c_census, nullptr, SK_CMD_ANY_THREAD);
}

void on_unload()
{
    g_on = false;
}

} // namespace memcensus
