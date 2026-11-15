// Function probes: hook any address with a generated stub that records the call
// (ecx/this, first stack arguments, return address, caller chain) and continues.
// Works without knowing the function's signature - an RE helper.
#include "core.h"

#include <atomic>

#include <cstdio>
#include <intrin.h>
#include <mutex>

#include "../sdk/include/sporekit_game.hpp"

namespace sk {

struct ProbeCall {
    uint32_t ecx, edx, eax;
    uint32_t ret;
    uint32_t args[6];
    uint32_t stack_rets[4]; // further return addresses found on the stack
    DWORD tid;
    uint64_t frame;
};

struct Probe {
    uintptr_t target;
    void* trampoline; // original function (MinHook)
    uint8_t* stub;
    uint32_t hits;
    ProbeCall last[8];
};

static std::mutex g_mu;
static std::vector<Probe*> g_probes;

extern "C" void __cdecl sk_probe_record(Probe* p, uint32_t* regs)
{
    // regs: pushad block (EDI ESI EBP ESP EBX EDX ECX EAX), followed by [ret][args...]
    uint32_t* stack = regs + 8;
    ProbeCall c{};
    c.ecx = regs[6];
    c.edx = regs[5];
    c.eax = regs[7];
    c.ret = stack[0];
    for (int i = 0; i < 6; i++)
        c.args[i] = stack[1 + i];
    int n = 0;
    uintptr_t lo = image_base(), hi = image_base() + 0x1000000;
    for (int i = 7; i < 512 && n < 4; i++) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(stack + i, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) // BUG-1028: committed is not readable
            break;
        if (stack[i] > lo && stack[i] < hi)
            c.stack_rets[n++] = stack[i];
    }
    c.tid = GetCurrentThreadId();
    c.frame = frame_index();
    std::lock_guard<std::mutex> lk(g_mu);
    p->last[p->hits % 8] = c;
    p->hits++;
}

static uint8_t* make_stub(Probe* p)
{
    uint8_t* s = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!s)
        return nullptr;
    uint8_t* w = s;
    *w++ = 0x60; // pushad
    *w++ = 0x9C; // pushfd
    // lea eax,[esp+4] -> pushad block
    *w++ = 0x8D; *w++ = 0x44; *w++ = 0x24; *w++ = 0x04;
    *w++ = 0x50; // push eax
    *w++ = 0x68; *(uint32_t*)w = (uint32_t)(uintptr_t)p; w += 4; // push p
    *w++ = 0xE8; // call sk_probe_record
    *(int32_t*)w = (int32_t)((uint8_t*)&sk_probe_record - (w + 4)); w += 4;
    *w++ = 0x83; *w++ = 0xC4; *w++ = 0x08; // add esp,8
    *w++ = 0x9D; // popfd
    *w++ = 0x61; // popad
    *w++ = 0xFF; *w++ = 0x25; *(uint32_t*)w = (uint32_t)(uintptr_t)&p->trampoline; w += 4; // jmp [tramp]
    return s;
}

std::string probe_add(uintptr_t addr)
{
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto* p : g_probes)
        if (p->target == addr)
            return "already probed";
    auto* p = new Probe{};
    p->target = addr;
    p->stub = make_stub(p);
    if (!p->stub || hook_create(&g_kit_mod, (void*)addr, p->stub, &p->trampoline) != 0) {
        delete p;
        return "hook failed";
    }
    g_probes.push_back(p);
    return "probe installed";
}

std::string probe_report(bool clear)
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::string out;
    char b[512];
    for (auto* p : g_probes) {
        snprintf(b, sizeof b, "probe 0x%08X %s hits=%u\n", (unsigned)p->target,
                 symbols_reverse(p->target).c_str(), p->hits);
        out += b;
        uint32_t n = p->hits < 8 ? p->hits : 8;
        for (uint32_t i = 0; i < n; i++) {
            const ProbeCall& c = p->last[(p->hits - n + i) % 8];
            snprintf(b, sizeof b,
                     "  frame %llu tid %lu ecx=%08X eax=%08X edx=%08X args=%08X %08X %08X %08X %08X %08X\n"
                     "    ret %08X %s | %08X %s | %08X %s\n",
                     (unsigned long long)c.frame, c.tid, c.ecx, c.eax, c.edx, c.args[0], c.args[1], c.args[2],
                     c.args[3], c.args[4], c.args[5], c.ret, symbols_reverse(c.ret).c_str(), c.stack_rets[0],
                     symbols_reverse(c.stack_rets[0]).c_str(), c.stack_rets[1],
                     symbols_reverse(c.stack_rets[1]).c_str());
            out += b;
        }
        if (clear)
            p->hits = 0;
    }
    return out.empty() ? "no probes" : out;
}

void probe_clear_all()
{
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto* p : g_probes)
        hook_remove(&g_kit_mod, (void*)p->target); // stub memory is kept (a thread may be inside)
    g_probes.clear();
}

} // namespace sk

// ---- file trace: log CreateFileW calls whose path contains a substring ----
namespace sk {

typedef HANDLE(WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileW_t o_CreateFileW;
// BUG-1031: ftrace (main thread) assigned the string while the detour read it on every thread that opens a file (a
// read of the freed old buffer). Readers take the lock shared, the assignment exclusive; with no filter the detour
// only reads the flag.
static std::wstring g_ftrace;
static SRWLOCK g_ftrace_lock = SRWLOCK_INIT;
static std::atomic<bool> g_ftrace_on{false};

static bool ftrace_match(LPCWSTR name)
{
    if (!g_ftrace_on.load(std::memory_order_acquire) || !name)
        return false;
    AcquireSRWLockShared(&g_ftrace_lock);
    bool m = !g_ftrace.empty() && wcsstr(name, g_ftrace.c_str());
    ReleaseSRWLockShared(&g_ftrace_lock);
    return m;
}

static HANDLE WINAPI h_CreateFileW(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                   DWORD flags, HANDLE tmpl)
{
    if (ftrace_match(name)) {
        uint32_t* sp = (uint32_t*)__builtin_frame_address(0);
        std::string chain;
        uintptr_t lo = image_base(), hi = image_base() + 0x1000000;
        int n = 0;
        for (int i = 0; i < 1024 && n < 12; i++) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(sp + i, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
                (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) // BUG-1028
                break;
            if (sp[i] > lo && sp[i] < hi) {
                char b[96];
                snprintf(b, sizeof b, " %08X", sp[i]);
                chain += b;
                n++;
            }
        }
        logf(&g_kit_mod, SK_INFO, "ftrace %s %s:%s", (acc & GENERIC_WRITE) ? "W" : "R", narrow(name).c_str(),
             chain.c_str());
    }
    return o_CreateFileW(name, acc, share, sa, disp, flags, tmpl);
}

std::string ftrace_set(const std::string& substr)
{
    if (!o_CreateFileW)
        hook_create(&g_kit_mod, (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateFileW"),
                    (void*)h_CreateFileW, (void**)&o_CreateFileW);
    std::wstring w = widen(substr);
    AcquireSRWLockExclusive(&g_ftrace_lock);
    g_ftrace.swap(w); // the old buffer is freed below, after the lock
    g_ftrace_on.store(!g_ftrace.empty(), std::memory_order_release);
    ReleaseSRWLockExclusive(&g_ftrace_lock);
    return substr.empty() ? "file trace off" : "tracing CreateFileW paths containing '" + substr + "'";
}

} // namespace sk
