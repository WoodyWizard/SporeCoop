// Hang watchdog: if the main thread makes no progress (boot steps / frames) for a while,
// dump its stack to the log. Uses only calls that do not take the loader lock, so it
// also works when the main thread is stuck inside LoadLibrary/DllMain.
#include "core.h"

#include <atomic>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace sk {

static std::atomic<uint32_t> g_progress{0};
static std::atomic<const char*> g_stage{"boot"};
static std::atomic<uint32_t> g_loop_ticks{0};

void watchdog_mark(const char* stage)
{
    g_stage = stage;
    g_progress++;
}

// BUG-0347: App::Canvas::Tick returned. Minimized (it sleeps 200 ms per tick), unfocused without
// background mode, or behind a device that is lost (alt-tab out of full screen) the game draws
// nothing and SporeKit sees no frame, while the loop goes on: a pause, not a hang.
void watchdog_loop_tick() { g_loop_ticks.fetch_add(1, std::memory_order_relaxed); }

// module name from an address without the loader lock: AllocationBase -> PE export name
static std::string module_of(uintptr_t a, uintptr_t* base_out)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE)
        return {};
    auto* b = (uint8_t*)mbi.AllocationBase;
    *base_out = (uintptr_t)b;
    auto* dos = (IMAGE_DOS_HEADER*)b;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return "?";
    auto* nt = (IMAGE_NT_HEADERS*)(b + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return b == (uint8_t*)GetModuleHandleW(nullptr) ? "SporeApp.exe" : "?";
    auto* exp = (IMAGE_EXPORT_DIRECTORY*)(b + dir.VirtualAddress);
    return (const char*)(b + exp->Name);
}

static void dump_thread_stack(DWORD tid, const char* why);
static void dump_main_stack(const char* why) { dump_thread_stack(main_thread_id(), why); }

// PEB->LoaderLock (RTL_CRITICAL_SECTION*) : OwningThread at +0x0C
static DWORD loader_lock_owner()
{
    uint8_t* peb = (uint8_t*)__readfsdword(0x30);
    auto* cs = *(RTL_CRITICAL_SECTION**)(peb + 0xA0);
    return cs ? (DWORD)(uintptr_t)cs->OwningThread : 0;
}

// While a thread is suspended nothing may allocate or take a lock it might hold: the heap's (the
// main thread allocates all the time), the loader's, the log's. Only GetThreadContext, VirtualQuery
// and copies into the caller's buffer run before the resume; names and text come after (BUG-0050).
bool thread_snapshot(DWORD tid, ThreadSnap& s, std::string* why, void (*while_suspended)(void*), void* ctx)
{
    s.n = 0;
    if (tid == GetCurrentThreadId() && fixed(g_fix_cheatwait)) { // a thread must never suspend itself (BUG-0065)
        if (why)
            *why = "cannot read the stack of the calling thread itself (use stack.here)";
        return false;
    }
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, tid);
    if (!t) {
        if (why)
            *why = "OpenThread failed";
        return false;
    }
    if (SuspendThread(t) == (DWORD)-1) {
        CloseHandle(t);
        if (why)
            *why = "SuspendThread failed";
        return false;
    }
    memset(&s.c, 0, sizeof s.c);
    s.c.ContextFlags = CONTEXT_FULL;
    GetThreadContext(t, &s.c);
    s.esp = s.c.Esp;
    const uint32_t* sp = (const uint32_t*)s.esp;
    const int cap = (int)(sizeof s.words / sizeof s.words[0]);
    while (s.n < cap) {
        MEMORY_BASIC_INFORMATION mbi;
        const uint32_t* p = sp + s.n;
        if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !mbi.Protect)
            break;
        const uint32_t* end = (const uint32_t*)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize);
        while (p < end && s.n < cap)
            s.words[s.n++] = *p++;
    }
    if (while_suspended)
        while_suspended(ctx); // kit.fix stackdump 0: the old order (format while the thread is suspended)
    ResumeThread(t);
    CloseHandle(t);
    return true;
}

static void dump_thread_stack(DWORD tid, const char* why)
{
    static ThreadSnap snap; // 16 KB; the watchdog thread and the console both come here
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    std::string out;
    auto format = [&] {
        char b[256];
        snprintf(b, sizeof b, "WATCHDOG (%s, stage '%s'): thread %lu%s eip=%08lX esp=%08lX\n", why,
                 g_stage.load(), tid, tid == main_thread_id() ? " (main)" : "", snap.c.Eip, snap.c.Esp);
        out += b;
        auto describe = [&](uintptr_t a) {
            uintptr_t base = 0;
            std::string m = module_of(a, &base);
            std::string s = symbols_reverse(a);
            snprintf(b, sizeof b, "%s+0x%X %s", m.c_str(), (unsigned)(a - base), s.c_str());
            return std::string(b);
        };
        out += "  eip " + describe(snap.c.Eip) + "\n";
        int found = 0;
        for (int i = 0; i < snap.n && i < 2048 && found < 40; i++) {
            uint32_t v = snap.words[i];
            MEMORY_BASIC_INFORMATION vm;
            if (VirtualQuery((void*)(uintptr_t)v, &vm, sizeof vm) && vm.State == MEM_COMMIT &&
                vm.Type == MEM_IMAGE && (vm.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE | PAGE_EXECUTE_READWRITE))) {
                snprintf(b, sizeof b, "  [esp+%04X] %08X ", i * 4, v);
                out += b + describe(v) + "\n";
                found++;
            }
        }
    };
    std::string err;
    bool now = fixed(g_fix_stackdump);
    if (!thread_snapshot(tid, snap, &err, now ? nullptr : call_lambda<decltype(format)>, &format)) {
        logf(&g_kit_mod, SK_ERROR, "WATCHDOG (%s): thread %lu: %s", why, tid, err.c_str());
        return;
    }
    if (now)
        format();
    logf(&g_kit_mod, SK_ERROR, "%s", out.c_str());
}

static HANDLE g_started;

static DWORD WINAPI watchdog_thread(void*)
{
    SetEvent(g_started);
    uint32_t last = g_progress;
    uint64_t last_frame = frame_index();
    DWORD since = GetTickCount();
    bool dumped = false;
    DWORD ll_owner = 0, ll_since = 0;
    bool ll_dumped = false;
    uint32_t last_loop = g_loop_ticks;
    DWORD loop_seen = since;
    bool paused_logged = false;
    for (;;) {
        Sleep(1000);
        DWORD o = loader_lock_owner();
        if (o != ll_owner) {
            ll_owner = o;
            ll_since = GetTickCount();
            ll_dumped = false;
        } else if (o && !ll_dumped && GetTickCount() - ll_since > 10000) {
            dump_thread_stack(o, "loader lock held >10s");
            ll_dumped = true;
        }
        uint32_t p = g_progress;
        uint64_t f = frame_index();
        uint32_t lt = g_loop_ticks.load(std::memory_order_relaxed);
        DWORD now = GetTickCount();
        if (lt != last_loop) {
            last_loop = lt;
            loop_seen = now;
        }
        if (p != last || f != last_frame) {
            if (paused_logged)
                logf(&g_kit_mod, SK_INFO, "watchdog: frames again after %lu s without frames (the game loop ran)",
                     (now - since) / 1000);
            last = p;
            last_frame = f;
            since = now;
            dumped = false;
            paused_logged = false;
            continue;
        }
        // frames legitimately stop in menus only when minimized; boot must progress
        DWORD limit = frame_index() == 0 ? 15000 : 30000;
        DWORD stall_from = since;
        if (fixed(g_fix_hangloop) && frame_index() > 0) {
            // the game loop ran in the last few seconds: no frames, but no hang either (BUG-0347);
            // a real stall is counted from the later of the last frame and the last loop pass
            if ((int)(loop_seen - since) > 0)
                stall_from = loop_seen;
            if (now - loop_seen < 5000) {
                if (!paused_logged && now - since > limit) {
                    HWND w = (HWND)game_window();
                    logf(&g_kit_mod, SK_INFO,
                         "watchdog: no frames for %lu s, the game loop runs (window minimized %d, visible %d, "
                         "foreground %d): the game is paused or not drawing, not a hang",
                         (now - since) / 1000, w ? (int)IsIconic(w) : -1, w ? (int)IsWindowVisible(w) : -1,
                         w ? (int)(GetForegroundWindow() == w) : -1);
                    paused_logged = true;
                }
                continue;
            }
        }
        if (!dumped && now - stall_from > limit) {
            dump_main_stack(frame_index() == 0 ? "no boot progress" : "no frames");
            dumped = true;
            if (frame_index() > 0)
                diag_hang_report("hang: no frames for 30 s while the game window has the focus");
        }
    }
}

void watchdog_start()
{
    g_started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
    if (t) {
        WaitForSingleObject(g_started, 2000);
        CloseHandle(t);
    }
}

std::string watchdog_dump_now()
{
    if (main_thread_id() == GetCurrentThreadId() && fixed(g_fix_cheatwait))
        return "hangdump cannot run on the main thread itself (use stack.here)";
    dump_main_stack("requested");
    return "dumped to log";
}

} // namespace sk
