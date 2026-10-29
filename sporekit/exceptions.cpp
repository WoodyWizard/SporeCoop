// Exception logger: records access violations and other faults (with nearest symbol)
// before the game's / Wine's handlers swallow them. Invaluable when a hook breaks a frame.
#include "core.h"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace sk {

struct ExcRec {
    DWORD code;
    uintptr_t eip;
    uintptr_t addr; // faulting data address for AV
    DWORD tid;
    uint32_t count;
    std::string where;
    uint32_t regs[8];
    uint32_t stack[12];
    uint32_t rets[10]; // likely return addresses into the exe found deeper on the stack (0 = none)
};

// a dword on the stack that points just after a call instruction in the exe's code
static bool exe_return_address(uint32_t v)
{
    static uintptr_t lo, hi;
    if (!hi) {
        auto base = (const uint8_t*)GetModuleHandleW(nullptr);
        auto nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        lo = (uintptr_t)base + sec->VirtualAddress;
        hi = lo + sec->Misc.VirtualSize;
    }
    if (v < lo + 6 || v >= hi)
        return false;
    const uint8_t* c = (const uint8_t*)v;
    return c[-5] == 0xE8 || (c[-6] == 0xFF && (c[-5] & 0x38) == 0x10) || (c[-2] == 0xFF && (c[-1] & 0xF8) == 0xD0) ||
           (c[-3] == 0xFF && (c[-2] & 0x38) == 0x10) || (c[-7] == 0xFF && (c[-6] & 0x38) == 0x10);
}

static std::mutex g_mu;
// the last fault (copies): a crash report when the game ends right after it (its own handler
// caught the exception and closed the game, so the unhandled-exception filter never ran)
static EXCEPTION_RECORD g_last_er;
static CONTEXT g_last_ctx;
static DWORD g_last_tid; // the thread of g_last_er (BUG-0248), written under g_mu with it
static std::atomic<double> g_last_t{-1};

bool exceptions_last(EXCEPTION_RECORD* er, CONTEXT* ctx, double* t, DWORD* tid)
{
    DWORD so_tid;
    if (exceptions_stack_overflow(er, ctx, &so_tid, t) && *t >= g_last_t.load()) {
        if (tid)
            *tid = so_tid;
        return true; // a stack overflow is never in g_last_* (BUG-0056)
    }
    if (g_last_t.load() < 0)
        return false;
    std::lock_guard<std::mutex> lk(g_mu);
    ThreadFlag tf(SK_TF_IN_EXC);
    *er = g_last_er;
    *ctx = g_last_ctx;
    *t = g_last_t.load();
    if (tid)
        *tid = g_last_tid;
    return true;
}
// a fault inside SporeKit or a mod (a DLL under mods\): told to the mods at the next frame
static std::atomic<bool> g_mod_fault{false};
static char g_mod_fault_text[256];

static bool in_our_code(uintptr_t eip, std::string& name)
{
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)eip, &m) || !m)
        return false;
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(m, p, MAX_PATH);
    std::wstring w = p;
    for (auto& c : w)
        c = (wchar_t)towlower(c);
    name = narrow(w.substr(w.find_last_of(L"\\/") + 1));
    return w.find(L"sporekit.dll") != std::wstring::npos || w.find(L"\\mods\\") != std::wstring::npos;
}
static std::vector<ExcRec> g_recs; // unique by (code, eip)
static std::atomic<uint32_t> g_total{0};

// A stack overflow (BUG-0056): the faulting thread has about one page of stack left. The handler
// only copies into these statics (no lock, no allocation, no log) and the crash report thread
// formats them on its own stack (diag.cpp).
static volatile LONG g_so_taken;
static EXCEPTION_RECORD g_so_er;
static CONTEXT g_so_ctx;
static volatile DWORD g_so_tid;
static volatile double g_so_t = -1;

bool exceptions_stack_overflow(EXCEPTION_RECORD* er, CONTEXT* ctx, DWORD* tid, double* t)
{
    if (g_so_t < 0)
        return false;
    *er = g_so_er;
    *ctx = g_so_ctx;
    *tid = g_so_tid;
    *t = g_so_t;
    return true;
}

// The handler runs at the first chance of every fault, on the faulting thread, maybe inside a lock
// of SporeKit (BUG-0115):
//  * a fault inside the handler itself, or while this thread holds g_mu (SK_TF_IN_EXC), is not
//    handled again: g_mu is not recursive, taking it again would block the thread for good;
//  * a fault inside the log's lock is recorded, its log lines are dropped (logv checks SK_TF_IN_LOG);
//  * under g_mu only copies are made: the name lookup (GetModuleHandleExW takes the loader lock,
//    another thread holding the loader lock may fault and wait for g_mu) and the log come after.
static LONG CALLBACK veh(EXCEPTION_POINTERS* ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // ignore debugger/C++/thread-naming noise
    if (code == 0x406D1388 || code == 0xE06D7363 || code == DBG_PRINTEXCEPTION_C ||
        code == 0x4001000A || code == STATUS_BREAKPOINT || code == STATUS_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (code == EXCEPTION_STACK_OVERFLOW && fixed(g_fix_stackov)) {
        if (InterlockedCompareExchange(&g_so_taken, 1, 0) == 0) {
            memcpy(&g_so_er, ep->ExceptionRecord, sizeof g_so_er);
            memcpy(&g_so_ctx, ep->ContextRecord, sizeof g_so_ctx);
            g_so_tid = GetCurrentThreadId();
            g_so_t = now_seconds();
        }
        g_total++;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    bool fix = fixed(g_fix_reentry);
    if (fix && (thread_flags() & SK_TF_IN_EXC))
        return EXCEPTION_CONTINUE_SEARCH;
    ThreadFlag in_exc(SK_TF_IN_EXC);
    g_total++;
    CONTEXT* c = ep->ContextRecord;
    uintptr_t eip = c->Eip;
    ExcRec r{};
    r.code = code;
    r.eip = eip;
    r.addr = code == EXCEPTION_ACCESS_VIOLATION ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
    r.tid = GetCurrentThreadId();
    r.count = 1;
    uint32_t regs[8] = {c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp};
    memcpy(r.regs, regs, sizeof regs);
    const uint32_t* sp = (const uint32_t*)c->Esp;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(sp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
        for (int i = 0; i < 12; i++)
            r.stack[i] = sp[i];
        // the call chain (roughly): return addresses within 2 KB of the stack, same committed region
        const uint32_t* end = (const uint32_t*)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize);
        int n = 0;
        for (const uint32_t* q = sp; q < end && q < sp + 512 && n < 10; q++)
            if (exe_return_address(*q))
                r.rets[n++] = *q;
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_last_er = *ep->ExceptionRecord;
        g_last_ctx = *c;
        g_last_tid = r.tid;
        g_last_t = now_seconds();
        for (auto& x : g_recs)
            if (x.code == code && x.eip == eip) {
                x.count++;
                return EXCEPTION_CONTINUE_SEARCH;
            }
        if (g_recs.size() >= 64)
            return EXCEPTION_CONTINUE_SEARCH;
        g_recs.push_back(r); // the name is filled in below
    }
    r.where = symbols_reverse(eip);
    if (r.where.empty()) {
        HMODULE m = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)eip, &m) && m) {
            wchar_t n[MAX_PATH];
            GetModuleFileNameW(m, n, MAX_PATH);
            std::wstring s = n;
            char off[32];
            snprintf(off, sizeof off, "+0x%X", (unsigned)(eip - (uintptr_t)m));
            r.where = narrow(s.substr(s.find_last_of(L"\\/") + 1)) + off;
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& x : g_recs)
            if (x.code == code && x.eip == eip && x.where.empty())
                x.where = r.where;
    }
    std::string mod;
    if (!g_mod_fault.load() && in_our_code(eip, mod)) {
        snprintf(g_mod_fault_text, sizeof g_mod_fault_text, "0x%08X %s", (unsigned)eip, r.where.c_str());
        g_mod_fault = true;
    }
    // logging takes the log mutex only (and drops the lines when this thread already holds it)
    logf(&g_kit_mod, SK_ERROR,
         "exception 0x%08lX at 0x%08X %s (data 0x%08X) tid=%lu eax=%08X ebx=%08X ecx=%08X edx=%08X "
         "esi=%08X edi=%08X ebp=%08X esp=%08X",
         code, (unsigned)eip, r.where.c_str(), (unsigned)r.addr, r.tid, regs[0], regs[1], regs[2],
         regs[3], regs[4], regs[5], regs[6], regs[7]);
    if (r.rets[0])
        logf(&g_kit_mod, SK_ERROR, "  calls: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X", r.rets[0], r.rets[1],
             r.rets[2], r.rets[3], r.rets[4], r.rets[5], r.rets[6], r.rets[7], r.rets[8], r.rets[9]);
    if (r.rets[0]) { // the same chain with names (the exe's symbols)
        std::string named;
        for (int i = 0; i < 6 && r.rets[i]; i++) {
            std::string n = symbols_reverse(r.rets[i]);
            named += (i ? " < " : "") + (n.empty() ? std::string("?") : n);
        }
        logf(&g_kit_mod, SK_ERROR, "  called from: %s", named.c_str());
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// kit.test excfault: a fault while this thread holds the exception logger's lock (BUG-0115)
void exceptions_fault_test()
{
    volatile uint8_t* page = (volatile uint8_t*)test_fault_page();
    std::lock_guard<std::mutex> lk(g_mu);
    ThreadFlag tf(SK_TF_IN_EXC);
    page[0] = 1;
}

void exceptions_pump()
{
    if (!g_mod_fault.exchange(false))
        return;
    char t[256];
    memcpy(t, g_mod_fault_text, sizeof t);
    logf(&g_kit_mod, SK_WARN, "a fault in SporeKit / mod code was caught: %s (the mods are told)", t);
    fire(SK_EV_FAULT, t);
}

void exceptions_install()
{
    AddVectoredExceptionHandler(1, veh);
}

std::string exceptions_report()
{
    std::lock_guard<std::mutex> lk(g_mu);
    ThreadFlag tf(SK_TF_IN_EXC);
    std::string s = "total faults: " + std::to_string(g_total.load()) + "\n";
    if (g_so_t >= 0) {
        char b[160];
        snprintf(b, sizeof b, "0xC00000FD stack overflow at 0x%08X tid=%lu%s esp=%08X\n", (unsigned)g_so_ctx.Eip,
                 g_so_tid, g_so_tid == main_thread_id() ? " (main)" : "", (unsigned)g_so_ctx.Esp);
        s += b;
    }
    for (auto& r : g_recs) {
        char b[512];
        snprintf(b, sizeof b, "0x%08lX x%u at 0x%08X %s data=0x%08X tid=%lu%s\n  stack:", r.code, r.count,
                 (unsigned)r.eip, r.where.c_str(), (unsigned)r.addr, r.tid, r.tid == main_thread_id() ? " (main)" : "");
        s += b;
        for (int i = 0; i < 12; i++) {
            snprintf(b, sizeof b, " %08X", r.stack[i]);
            s += b;
        }
        if (r.rets[0]) {
            s += "\n  calls:";
            for (int i = 0; i < 10 && r.rets[i]; i++) {
                snprintf(b, sizeof b, " %08X", r.rets[i]);
                s += b;
            }
        }
        s += "\n";
    }
    return s;
}

void exceptions_clear()
{
    std::lock_guard<std::mutex> lk(g_mu);
    ThreadFlag tf(SK_TF_IN_EXC);
    g_recs.clear();
    g_total = 0;
}

} // namespace sk
