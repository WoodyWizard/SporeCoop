// MinHook wrapper with per-mod ownership so hot reload can remove a mod's hooks.
#include "core.h"

#include <atomic>
#include <mutex>

#include "MinHook.h"

namespace sk {

struct HookRec {
    SkMod* mod;
    void* target;
    void* detour;
    bool enabled;
};

static std::mutex g_mu;
static std::vector<HookRec> g_hooks;

// Parked hooks (BUG-0202). Writing the 5-byte jmp over a function while another thread runs its first
// instructions crashes that thread (it continues inside the jmp; MinHook's thread freeze is off under
// Wine). So the code of a target is written once per process: the jmp goes to a stub of SporeKit,
// `jmp [cur]`, and making, enabling, disabling or removing the hook only changes `cur` (one aligned
// dword: atomic for the threads). A removed hook is parked (cur = the trampoline: the original code);
// the next hook on that target (the same mod after a hot reload) takes the stub over.
struct Stub {
    void* target;
    void* trampoline;      // MinHook's: the original first instructions, then a jmp back
    void* volatile cur;    // where the stub jumps: the detour, or the trampoline when parked
    SkMod* owner;          // nullptr = parked
    uint8_t* code;         // FF 25 <&cur>: jmp dword ptr [&cur]
};
static std::vector<Stub*> g_stubs;

static uint8_t* stub_code()
{
    static uint8_t* page;
    static size_t used;
    if (!page || used + 8 > 4096) {
        page = (uint8_t*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        used = 0;
        if (!page)
            return nullptr;
    }
    uint8_t* c = page + used;
    used += 8;
    return c;
}

static Stub* stub_of(void* target)
{
    for (Stub* s : g_stubs)
        if (s->target == target)
            return s;
    return nullptr;
}

// the hook through a stub; 0 or a MinHook status
// The detour calls the original through *original, so *original is set before any thread can reach
// the detour (BUG-0398): the old order made the jmp live first, and a game thread calling the
// function in that window (the main thread polls GetAsyncKeyState every frame while the console
// thread hooks it at the first injected input) called address 0.
static int stub_hook(SkMod* mod, void* target, void* detour, void** original)
{
    bool first = fixed(g_fix_hookorig);
    if (Stub* s = stub_of(target)) {
        if (s->owner)
            return MH_ERROR_ALREADY_CREATED;
        s->owner = mod;
        *original = s->trampoline;
        std::atomic_signal_fence(std::memory_order_seq_cst); // the store above is not moved below
        s->cur = detour; // no code written: a thread in the function's first bytes is not hurt
        return 0;
    }
    auto* s = new Stub{target, nullptr, nullptr, mod, stub_code()};
    if (!s->code) {
        delete s;
        return MH_ERROR_MEMORY_ALLOC;
    }
    s->code[0] = 0xFF;
    s->code[1] = 0x25;
    void* volatile* slot = &s->cur;
    memcpy(s->code + 2, &slot, 4);
    void* before = *original;
    if (!first)
        s->cur = detour; // the old order: the detour is reachable before *original is set
    MH_STATUS st = MH_CreateHook(target, s->code, &s->trampoline);
    if (st == MH_OK && first) {
        // the jmp is not written yet: nothing reaches the stub until MH_EnableHook
        *original = s->trampoline;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        s->cur = detour;
    }
    if (st == MH_OK && (st = MH_EnableHook(target)) != MH_OK)
        MH_RemoveHook(target);
    if (st != MH_OK) {
        *original = before;
        delete s; // its 8 bytes of code stay unused
        return (int)st;
    }
    *original = s->trampoline;
    g_stubs.push_back(s);
    return 0;
}

static void stub_park(Stub* s)
{
    s->cur = s->trampoline;
    s->owner = nullptr;
}

bool hooks_init()
{
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        SKLOG(SK_ERROR, "MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }
    return true;
}

int hook_create(SkMod* mod, void* target, void* detour, void** original)
{
    if (!target)
        return -1;
    std::lock_guard<std::mutex> lk(g_mu);
    if (fixed(g_fix_hookpark) || stub_of(target)) {
        int r = stub_hook(mod, target, detour, original);
        if (r) {
            logf(mod, SK_ERROR, "hook_create %p failed: %s", target, MH_StatusToString((MH_STATUS)r));
            return r;
        }
        g_hooks.push_back({mod, target, detour, true});
        logf(mod, SK_DEBUG, "hooked %p %s (parked stub)", target, symbols_reverse((uintptr_t)target).c_str());
        return 0;
    }
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s != MH_OK) {
        logf(mod, SK_ERROR, "hook_create %p failed: %s", target, MH_StatusToString(s));
        return (int)s;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK) {
        logf(mod, SK_ERROR, "hook_enable %p failed: %s", target, MH_StatusToString(s));
        MH_RemoveHook(target);
        return (int)s;
    }
    g_hooks.push_back({mod, target, detour, true});
    std::string nm = symbols_reverse((uintptr_t)target);
    logf(mod, SK_DEBUG, "hooked %p %s", target, nm.c_str());
    return 0;
}

int hook_enable(SkMod* mod, void* target, int enable)
{
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& h : g_hooks)
        if (h.target == target && h.mod == mod) {
            if (Stub* st = stub_of(target)) {
                st->cur = enable ? h.detour : st->trampoline;
                h.enabled = enable != 0;
                return 0;
            }
            MH_STATUS s = enable ? MH_EnableHook(target) : MH_DisableHook(target);
            if (s == MH_OK)
                h.enabled = enable != 0;
            return (int)s;
        }
    return -1;
}

int hook_remove(SkMod* mod, void* target)
{
    std::lock_guard<std::mutex> lk(g_mu);
    for (size_t i = 0; i < g_hooks.size(); i++)
        if (g_hooks[i].target == target && g_hooks[i].mod == mod) {
            if (Stub* st = stub_of(target)) {
                stub_park(st);
                g_hooks.erase(g_hooks.begin() + i);
                return 0;
            }
            MH_SetKeepTrampolines(fixed(g_fix_hookkeep));
            MH_DisableHook(target);
            MH_STATUS s = MH_RemoveHook(target);
            g_hooks.erase(g_hooks.begin() + i);
            return (int)s;
        }
    return -1;
}

// Hooks are removed without stopping the other threads (freezing them deadlocks under Wine). A game
// worker thread can be inside a detour at that moment (coop hooks RandomInt, which worker threads
// call): it may still call the original through the trampoline, so the trampoline's memory is kept
// (MinHook's free list wrote into it and reused it for the next hook; BUG-0132). Returns the count.
int hooks_remove_all(SkMod* mod)
{
    std::lock_guard<std::mutex> lk(g_mu);
    MH_SetKeepTrampolines(fixed(g_fix_hookkeep));
    int n = 0;
    for (size_t i = 0; i < g_hooks.size();) {
        if (g_hooks[i].mod == mod) {
            if (Stub* st = stub_of(g_hooks[i].target))
                stub_park(st);
            else {
                MH_DisableHook(g_hooks[i].target);
                MH_RemoveHook(g_hooks[i].target);
            }
            g_hooks.erase(g_hooks.begin() + i);
            n++;
        } else
            i++;
    }
    return n;
}

// ---- kit.test hookchurn: threads call a hooked function while its hook is created and removed ----
static SkMod g_churn_mod;
static std::atomic<bool> g_churn_run{false};
static std::atomic<uint32_t> g_churn_calls{0};
using churn_fn = int (*)(int);
static churn_fn volatile o_churn;
static churn_fn volatile g_churn_ptr;

__attribute__((noinline)) static int churn_target(int x)
{
    volatile int y = x;
    y = y * 3 + 1;
    return y;
}
// a detour that takes a while before it calls the original: a thread is inside it most of the time
__attribute__((noinline)) static int churn_detour(int x)
{
    for (int i = 0; i < 400; i++)
        __asm__ volatile("" ::: "memory");
    return o_churn(x);
}
// the threads stop at a gate while the hook is made: making a hook while a thread runs the first bytes
// of the function is another race (BUG-0202); this test is about the removal (BUG-0132)
static std::atomic<bool> g_churn_go{false};
static std::atomic<int> g_churn_parked{0};
static DWORD WINAPI churn_thread(void*)
{
    while (g_churn_run.load()) {
        if (!g_churn_go.load()) {
            g_churn_parked++;
            while (!g_churn_go.load() && g_churn_run.load())
                Sleep(0);
            g_churn_parked--;
            continue;
        }
        g_churn_ptr(1);
        g_churn_calls++;
    }
    return 0;
}

// nogate: after the first round the hook is made while the threads run through the function
// (BUG-0202; the first hook of a process is made before anything calls it, like at the game start)
void hooks_churn_test(int rounds, int threads, std::string& out, bool nogate)
{
    g_churn_mod.name = "kit.test";
    g_churn_ptr = churn_target;
    g_churn_calls = 0;
    g_churn_go = false;
    g_churn_parked = 0;
    g_churn_run = true;
    std::vector<HANDLE> ts;
    for (int i = 0; i < threads && i < 8; i++)
        if (HANDLE t = CreateThread(nullptr, 0, churn_thread, nullptr, 0, nullptr))
            ts.push_back(t);
    int made = 0;
    for (int r = 0; r < rounds; r++) {
        g_churn_go = nogate && r > 0;
        for (int w = 0; w < 2000 && !g_churn_go.load() && g_churn_parked.load() < (int)ts.size(); w++)
            Sleep(1);
        void* orig = nullptr;
        if (hook_create(&g_churn_mod, (void*)churn_target, (void*)churn_detour, &orig) == 0)
            made++;
        o_churn = (churn_fn)orig;
        g_churn_go = true;
        Sleep(2);
        hooks_remove_all(&g_churn_mod); // the threads run through the detour meanwhile
        Sleep(1);
    }
    g_churn_run = false;
    if (!ts.empty())
        WaitForMultipleObjects((DWORD)ts.size(), ts.data(), TRUE, 5000);
    for (HANDLE t : ts)
        CloseHandle(t);
    char b[160];
    snprintf(b, sizeof b, "hookchurn: %d rounds (%d hooked%s), %u calls on %d threads, trampolines %s, hooks %s", rounds,
             made, nogate ? ", threads running" : "", g_churn_calls.load(), (int)ts.size(),
             fixed(g_fix_hookkeep) ? "kept" : "freed", fixed(g_fix_hookpark) ? "parked" : "rewritten");
    out = b;
}

// ---- kit.test hooknew: a NEW hook is made while threads call the function (BUG-0398) ----
// Every round hooks a function that was never hooked before (a fresh copy of a small function in an
// executable page), as the first injected input hooks GetAsyncKeyState while the game's main thread
// polls it. The detour calls the original through the pointer hook_create fills, like
// h_GetAsyncKeyState; when that pointer is still null the real detour calls address 0, this one
// counts it and returns. Each round has its own pointer (a thread still inside an older round's
// detour reads that round's pointer, which stays set) and its own detour thunk
// (mov eax, &pointer; jmp new_detour).
static SkMod g_new_mod;
using new_fn = int(__cdecl*)(int);
static const int kNewMax = 2000;
static new_fn volatile g_new_orig[kNewMax]; // filled by hook_create, as o_GetAsyncKeyState is
static new_fn volatile g_new_ptr;           // the function the threads call now
static std::atomic<bool> g_new_run{false};
static std::atomic<uint32_t> g_new_calls{0}, g_new_null{0};

__attribute__((noinline, regparm(1))) static int new_detour(new_fn volatile* orig, int x)
{
    new_fn o = *orig;
    if (!o) {
        g_new_null++; // the game's detour would call address 0 here
        return x + 1;
    }
    return o(x);
}
static DWORD WINAPI new_thread(void*)
{
    while (g_new_run.load()) {
        if (new_fn f = g_new_ptr)
            f(1);
        g_new_calls++;
    }
    return 0;
}

void hooks_new_test(int rounds, int threads, std::string& out)
{
    g_new_mod.name = "kit.test";
    if (rounds < 1)
        rounds = 1;
    if (rounds > kNewMax)
        rounds = kNewMax;
    // the function starts at +3 with `jmp +0` (E9 00000000) to its body at +8 (mov eax,[esp+4]; inc eax;
    // ret): MinHook's jmp replaces exactly that instruction with one aligned 4-byte store of the
    // operand (the opcode stays E9), so no thread is caught between instructions of the first 5 bytes
    // (that is BUG-0202, a different race); only the order of the jmp and *original is tested
    static const uint8_t body[16] = {0xCC, 0xCC, 0xCC, 0xE9, 0x00, 0x00, 0x00, 0x00,
                                     0x8B, 0x44, 0x24, 0x04, 0x40, 0xC3, 0xCC, 0xCC};
    size_t size = ((size_t)rounds * 32 + 4095) & ~(size_t)4095;
    auto* page = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!page) {
        out = "hooknew: no memory";
        return;
    }
    for (int r = 0; r < rounds; r++) {
        memcpy(page + r * 16, body, 16);
        g_new_orig[r] = nullptr; // a static of the mod that nobody set yet
        uint8_t* t = page + rounds * 16 + r * 16; // the round's detour thunk
        uint32_t slot = (uint32_t)(uintptr_t)&g_new_orig[r];
        int32_t rel = (int32_t)((uintptr_t)(void*)new_detour - (uintptr_t)(t + 10));
        t[0] = 0xB8;
        memcpy(t + 1, &slot, 4);
        t[5] = 0xE9;
        memcpy(t + 6, &rel, 4);
        memset(t + 10, 0xCC, 6);
    }
    FlushInstructionCache(GetCurrentProcess(), page, size);
    g_new_calls = 0;
    g_new_null = 0;
    g_new_ptr = nullptr;
    g_new_run = true;
    std::vector<HANDLE> ts;
    for (int i = 0; i < threads && i < 8; i++)
        if (HANDLE t = CreateThread(nullptr, 0, new_thread, nullptr, 0, nullptr))
            ts.push_back(t);
    int made = 0, null_rounds = 0;
    for (int r = 0; r < rounds; r++) {
        void* target = page + r * 16 + 3;
        g_new_ptr = (new_fn)target;
        Sleep(1); // the threads run through the function now
        uint32_t n0 = g_new_null.load();
        if (hook_create(&g_new_mod, target, page + rounds * 16 + r * 16, (void**)&g_new_orig[r]) == 0)
            made++;
        Sleep(1);
        if (g_new_null.load() != n0)
            null_rounds++;
        hooks_remove_all(&g_new_mod); // parked: the stub of this target is never used again
    }
    g_new_ptr = nullptr;
    g_new_run = false;
    if (!ts.empty())
        WaitForMultipleObjects((DWORD)ts.size(), ts.data(), TRUE, 5000);
    for (HANDLE t : ts)
        CloseHandle(t);
    // the page stays: parked stubs jump to trampolines that run its code
    char b[256];
    snprintf(b, sizeof b,
             "hooknew: %d rounds (%d hooked), %u calls on %d threads, detour ran with a null original %u times "
             "in %d rounds, originals %s",
             rounds, made, g_new_calls.load(), (int)ts.size(), g_new_null.load(), null_rounds,
             fixed(g_fix_hookorig) ? "set before the jump" : "set after the jump");
    out = b;
}

std::vector<std::string> hooks_describe()
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<std::string> out;
    for (auto& h : g_hooks) {
        char b[256];
        snprintf(b, sizeof b, "%-10s %p %s %s", h.mod->name.c_str(), h.target,
                 h.enabled ? "on " : "off", symbols_reverse((uintptr_t)h.target).c_str());
        out.push_back(b);
    }
    return out;
}

} // namespace sk
