// The local IPv4 addresses, looked up off the main thread with a fault guard (adapters.h, BUG-0286).
#include "adapters.h"
#include "fixes.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace adapters {

static const SkHost* H;
static SkMod* M;
#define LOG(...) H->log(M, SK_INFO, __VA_ARGS__)
#define WRN(...) H->log(M, SK_WARN, __VA_ARGS__)
#define SAY(...) H->out_printf(out, __VA_ARGS__)

// BUG-0286: Wine 10.0's GetAdaptersAddresses faulted in nsi.dll (memcpy with a garbage count from
// the \\.\Nsi reply) on the main thread, inside the co-op window's refresh: the game crashed.
// On: the lookup runs on a worker thread, rarely (kMaxAgeMs), with the ERROR_BUFFER_OVERFLOW retry
// and a fault guard. Off: the old synchronous lookup every 10 s on the caller's thread.
bool g_fix = true;
COOP_FIX("core.adapters", "BUG-0286", g_fix, "core: local addresses are looked up on a guarded worker thread, rarely, never on the main thread");

constexpr DWORD kMaxAgeMs = 300000; // a fresh lookup when the list is older than 5 min and asked for
constexpr DWORD kOldAgeMs = 10000;  // the old cache (fix off)

static std::mutex g_mu;
static std::vector<Addr> g_list; // the last good result (both paths)
static DWORD g_at;               // GetTickCount of the last finished lookup (0 = never)
static std::atomic<bool> g_running{false};   // a worker is alive
static std::atomic<bool> g_broken{false};    // a lookup faulted: no more lookups this run
static std::atomic<bool> g_stale{false};     // the next list() starts a lookup (coop.adapters fault)
static std::atomic<bool> g_inject{false};    // tests: the next lookup faults like Wine's nsi did
static std::atomic<bool> g_unloaded{false};
static std::atomic<int> g_threads{0};
static std::atomic<int> g_stress{0};
static std::atomic<uint32_t> g_gen{0};       // finished good lookups (lan_scan rebuilds its targets)         // lookups the worker still has to run (coop.adapters stress)
static std::atomic<uint32_t> g_lookups{0}, g_failed{0}, g_faults{0}, g_main_lookups{0};
static char g_fault_text[200];

// the old path's own cache (fix off): exactly the code before the fix
static std::vector<Addr> g_old_cache;
static DWORD g_old_at;

static void parse(const IP_ADAPTER_ADDRESSES* aa, std::vector<Addr>& out)
{
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sin = (sockaddr_in*)u->Address.lpSockaddr;
            if (!sin || sin->sin_family != AF_INET)
                continue;
            uint32_t ip = sin->sin_addr.s_addr;
            const uint8_t* b = (const uint8_t*)&ip;
            if (b[0] == 127 || (b[0] == 169 && b[1] == 254))
                continue;
            int plen = u->OnLinkPrefixLength;
            uint32_t mask = plen <= 0 ? 0 : htonl(plen >= 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> plen));
            char s[32];
            snprintf(s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            out.push_back({s, (ip & mask) | ~mask});
        }
    }
}

// tests (coop.adapters fault): a read of the address Wine's memcpy faulted on, through ucrt's memcpy
static void maybe_inject()
{
    if (!g_inject.exchange(false))
        return;
    LOG("adapters: test fault injected into the lookup (a read of 0xD54CF6CC, as in BUG-0286)");
    static uint8_t sink[64];
    void* (*volatile cpy)(void*, const void*, size_t) = memcpy;
    cpy(sink, (const void*)(uintptr_t)0xD54CF6CCu, sizeof sink);
}

static const ULONG kFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

// the old lookup (fix off): one call with a 16 KB buffer, cached 10 s, on the caller's thread
static std::vector<Addr> old_lookup()
{
    if (!g_old_cache.empty() && GetTickCount() - g_old_at < kOldAgeMs)
        return g_old_cache;
    g_old_at = GetTickCount();
    g_old_cache.clear();
    g_main_lookups++;
    maybe_inject();
    ULONG len = 16384;
    std::vector<uint8_t> buf(len);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_INET, kFlags, nullptr, aa, &len) != NO_ERROR)
        return g_old_cache;
    parse(aa, g_old_cache);
    return g_old_cache;
}

// One lookup at a time per Wine prefix: two games of one bottle enumerating at the same moment get
// garbage from the shared \\.\Nsi device (Wine 10.0: a game crashed within seconds when two games
// looked up in a loop, docs/re/net.md#net-wine-nsi-race). Named objects are per wineserver, so this mutex is shared by
// every SporeCoop game of the prefix. Held by the worker only.
static HANDLE g_nsi_mutex;
static std::atomic<bool> g_holding{false};
static std::atomic<uint32_t> g_mutex_timeouts{0};

static bool nsi_lock()
{
    if (!g_nsi_mutex)
        g_nsi_mutex = CreateMutexW(nullptr, FALSE, L"Local\\SporeCoop.nsi");
    if (!g_nsi_mutex)
        return true; // no mutex: look up anyway (the guard still holds)
    DWORD w = WaitForSingleObject(g_nsi_mutex, 15000);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        g_mutex_timeouts++;
        return false;
    }
    g_holding = true;
    return true;
}

static void nsi_unlock()
{
    if (g_holding.exchange(false))
        ReleaseMutex(g_nsi_mutex);
}

// one guarded lookup on the worker: the documented retry while the buffer is too small
static DWORD lookup(std::vector<Addr>& out)
{
    if (!nsi_lock())
        return ERROR_TIMEOUT;
    maybe_inject();
    ULONG len = 16384;
    std::vector<uint8_t> buf;
    DWORD r = ERROR_BUFFER_OVERFLOW;
    for (int i = 0; i < 4 && r == ERROR_BUFFER_OVERFLOW; i++) {
        buf.assign(len + 1024, 0);
        len = (ULONG)buf.size();
        r = GetAdaptersAddresses(AF_INET, kFlags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &len);
    }
    nsi_unlock();
    if (r == NO_ERROR)
        parse((const IP_ADAPTER_ADDRESSES*)buf.data(), out);
    return r;
}

// ---- the fault guard ----
// mingw/clang has no __try on i686. A vectored handler sees a fault of the worker thread while it
// is inside a lookup and sends that thread to landing() on the faulting stack (the frames below are
// abandoned: the vector and Wine's buffers leak). In the crash seen, ucrt's memcpy faulted on its
// first read (it copies backwards when the destination is above the source): nothing was written.
static std::atomic<DWORD> g_tid{0};
static std::atomic<bool> g_in_call{false};
static void* g_veh;
static void* g_self;      // the module reference the worker holds (FreeLibraryAndExitThread)
static uintptr_t g_seh_top; // fs:[0] when the worker started
static EXCEPTION_RECORD g_er;

[[noreturn]] static void end_thread()
{
    if (g_veh) {
        RemoveVectoredExceptionHandler(g_veh);
        g_veh = nullptr;
    }
    void* self = g_self;
    g_tid = 0;
    g_running = false;
    g_threads--;
    FreeLibraryAndExitThread((HMODULE)self, 0);
}

[[noreturn]] __attribute__((noinline, used)) static void landing()
{
    asm volatile("movl %0, %%fs:0" : : "r"(g_seh_top) : "memory"); // drop the abandoned SEH frames
    g_in_call = false;
    nsi_unlock(); // (this thread owns it)
    g_broken = true;
    g_faults++;
    g_stress = 0;
    char mod[MAX_PATH] = "?";
    uintptr_t eip = (uintptr_t)g_er.ExceptionAddress, base = 0;
    HMODULE m = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)eip, &m) && m) {
        GetModuleFileNameA(m, mod, sizeof mod);
        base = (uintptr_t)m;
    }
    const char* name = strrchr(mod, '\\') ? strrchr(mod, '\\') + 1 : mod;
    snprintf(g_fault_text, sizeof g_fault_text, "0x%08lX at %s+0x%X (data 0x%08lX)", g_er.ExceptionCode, name,
             (unsigned)(eip - base), g_er.NumberParameters > 1 ? (unsigned long)g_er.ExceptionInformation[1] : 0ul);
    WRN("adapters: GetAdaptersAddresses faulted on the lookup thread (%s): contained, the address list "
        "keeps its last value and no lookup runs again this time (BUG-0286)", g_fault_text);
    end_thread();
}

static LONG CALLBACK guard(EXCEPTION_POINTERS* ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!g_in_call || GetCurrentThreadId() != g_tid)
        return EXCEPTION_CONTINUE_SEARCH;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED && code != EXCEPTION_DATATYPE_MISALIGNMENT)
        return EXCEPTION_CONTINUE_SEARCH;
    g_in_call = false;
    memcpy(&g_er, ep->ExceptionRecord, sizeof g_er);
    CONTEXT* c = ep->ContextRecord;
    c->Esp = ((c->Esp - 256) & ~0xFu) - 4; // below the faulting frame, as after a call
    c->Ebp = 0;
    c->Eip = (DWORD)(uintptr_t)&landing;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI worker(void*)
{
    uintptr_t top;
    asm volatile("movl %%fs:0, %0" : "=r"(top));
    g_seh_top = top;
    g_tid = GetCurrentThreadId();
    g_veh = AddVectoredExceptionHandler(0, guard); // last: SporeKit's handler records the fault first
    do {
        std::vector<Addr> v;
        g_in_call = true;
        DWORD r = lookup(v);
        g_in_call = false;
        g_lookups++;
        if (r != NO_ERROR) {
            g_failed++;
        } else {
            std::lock_guard<std::mutex> l(g_mu);
            g_list = std::move(v);
            g_gen++;
        }
        {
            std::lock_guard<std::mutex> l(g_mu);
            g_at = GetTickCount() | 1;
        }
        if (g_stress > 0)
            g_stress--;
    } while (g_stress > 0 && !g_unloaded);
    end_thread();
}

static bool start_worker()
{
    if (g_unloaded || g_broken || g_running.exchange(true))
        return false;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)(void*)&start_worker, &self)) {
        g_running = false;
        return false;
    }
    g_self = self;
    g_threads++;
    HANDLE h = CreateThread(nullptr, 64 * 1024, worker, nullptr, 0, nullptr);
    if (!h) {
        g_threads--;
        g_running = false;
        FreeLibrary(self);
        return false;
    }
    CloseHandle(h);
    return true;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod; // no lookup at start: every game of a lab bottle would enumerate at the same moment
}

uint32_t generation() { return g_fix ? g_gen.load() : 0; }

std::vector<Addr> list()
{
    if (!g_fix)
        return old_lookup();
    DWORD at;
    {
        std::lock_guard<std::mutex> l(g_mu);
        at = g_at;
    }
    if (g_stale.exchange(false) || !at || GetTickCount() - at >= kMaxAgeMs)
        start_worker();
    std::lock_guard<std::mutex> l(g_mu);
    return g_list;
}

int command(int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "";
    if (!strcmp(a, "fault")) { // the next lookup faults (like Wine's nsi did); the next list() looks up
        g_inject = true;
        g_stale = true;
        g_old_at = GetTickCount() - kOldAgeMs; // the old cache is due as well
        SAY("adapters: the next lookup faults (test)\n");
        return 0;
    }
    if (!strcmp(a, "stress")) { // N lookups in a row (fix on: on the worker; off: here, on the main thread)
        int n = argc > 2 ? atoi(argv[2]) : 100;
        n = n < 1 ? 1 : n > 100000 ? 100000 : n;
        if (g_fix) {
            g_stress += n;
            bool started = start_worker();
            SAY("adapters: %d lookups queued on the worker (%s)\n", n, started ? "started" : g_broken ? "broken: none run" : "running");
        } else {
            for (int i = 0; i < n; i++) {
                g_old_at = GetTickCount() - kOldAgeMs;
                old_lookup();
            }
            SAY("adapters: %d lookups run on the main thread (fix off)\n", n);
        }
        return 0;
    }
    DWORD at;
    std::vector<Addr> v;
    {
        std::lock_guard<std::mutex> l(g_mu);
        at = g_at;
        v = g_fix ? g_list : g_old_cache;
    }
    SAY("adapters: fix %s lookups=%u failed=%u faults=%u main_thread_lookups=%u running=%d broken=%d stress_left=%d "
        "mutex_timeouts=%u age=%ld ms%s%s\n",
        g_fix ? "on" : "off", (unsigned)g_lookups, (unsigned)g_failed, (unsigned)g_faults, (unsigned)g_main_lookups,
        (int)g_running, (int)g_broken, (int)g_stress, (unsigned)g_mutex_timeouts, at ? (long)(GetTickCount() - at) : -1L,
        g_faults ? " last_fault=" : "", g_faults ? g_fault_text : "");
    for (auto& x : v) {
        const uint8_t* b = (const uint8_t*)&x.broadcast;
        SAY("  %s broadcast %u.%u.%u.%u\n", x.ip.c_str(), b[0], b[1], b[2], b[3]);
    }
    return 0;
}

int shutdown()
{
    g_unloaded = true;
    g_stress = 0;
    return g_threads;
}

} // namespace adapters
