// Built-in devkit console commands.
#include "core.h"

#include <cstdio>
#include <malloc.h>
#include <mutex>

#include "../sdk/include/sporekit_game.hpp"

namespace sk {

static void outf(SkOut* o, const char* fmt, ...)
{
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0)
        reinterpret_cast<Out*>(o)->text.append(buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
}

// "0x1234", "1234" (hex), "Sym::Name", "Sym::Name+0x10"
static bool parse_addr(const char* s, uintptr_t& out)
{
    std::string str = s;
    uintptr_t add = 0;
    size_t plus = str.find('+');
    if (plus != std::string::npos && plus > 0) {
        add = strtoul(str.c_str() + plus + 1, nullptr, 0);
        str = str.substr(0, plus);
    }
    if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        out = strtoul(str.c_str(), nullptr, 16) + add;
        return true;
    }
    bool hex = !str.empty();
    for (char c : str)
        if (!isxdigit((unsigned char)c))
            hex = false;
    if (hex) {
        out = strtoul(str.c_str(), nullptr, 16) + add;
        return true;
    }
    uintptr_t a = sym(str.c_str());
    if (!a)
        return false;
    out = a + add;
    return true;
}

#define NEED(n, usage)                                   \
    if (argc < (n)) {                                    \
        outf(o, "usage: %s", usage);                     \
        return 1;                                        \
    }

static int c_help(void*, int argc, const char* const* argv, SkOut* o)
{
    for (auto& [n, h] : commands_list())
        if (argc < 2 || n.find(argv[1]) != std::string::npos)
            outf(o, "%-18s %s\n", n.c_str(), h.c_str());
    return 0;
}

static int c_ping(void*, int, const char* const*, SkOut* o)
{
    outf(o, "pong");
    return 0;
}

static int c_info(void*, int, const char* const*, SkOut* o)
{
    uint32_t mode = game_mode_id();
    outf(o, "sporekit %s instance=%d pid=%lu\n", SK_VERSION, instance_id(), GetCurrentProcessId());
    outf(o, "exe sha256=%s base=0x%08X\n", exe_sha256().c_str(), (unsigned)image_base());
    outf(o, "symbols=%u files=%s\n", (unsigned)symbols_count(), symbols_file_name());
    if (symbols_count() && symbols_build() != exe_sha256())
        outf(o, "symbols of build %s (same code as this exe)\n", symbols_build().c_str());
    outf(o, "frame=%llu fps=%.1f device=%p hwnd=%p\n", (unsigned long long)frame_index(), fps(),
         d3d_device(), game_window());
    outf(o, "frames from: %s\n", frame_source());
    outf(o, "mode=0x%08X (%s)\n", mode, game_mode_name(mode));
    outf(o, "mods:");
    for (auto* m : mods_list())
        outf(o, " %s%s", m->name.c_str(), m->module ? "" : "(unloaded)");
    return 0;
}

static int c_frame(void*, int, const char* const*, SkOut* o)
{
    outf(o, "frame=%llu fps=%.1f", (unsigned long long)frame_index(), fps());
    return 0;
}

static int c_wait_frames(void*, int argc, const char* const* argv, SkOut* o)
{
    int n = argc > 1 ? atoi(argv[1]) : 1;
    int timeout_ms = argc > 2 ? atoi(argv[2]) : 30000;
    uint64_t target = frame_index() + n;
    DWORD t0 = GetTickCount();
    while (frame_index() < target) {
        if (GetTickCount() - t0 > (DWORD)timeout_ms) {
            outf(o, "timeout at frame %llu", (unsigned long long)frame_index());
            return 1;
        }
        Sleep(5);
    }
    outf(o, "frame=%llu", (unsigned long long)frame_index());
    return 0;
}

static int c_log(void*, int argc, const char* const* argv, SkOut* o)
{
    size_t n = argc > 1 ? (size_t)atoi(argv[1]) : 40;
    for (auto& l : log_tail(n))
        outf(o, "%s\n", l.c_str());
    return 0;
}

static int c_sym(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "sym <name|addr>");
    uintptr_t a;
    if (parse_addr(argv[1], a)) {
        std::string r = symbols_reverse(a);
        outf(o, "0x%08X %s", (unsigned)a, r.c_str());
        return 0;
    }
    outf(o, "not found");
    return 1;
}

static int c_sym_reload(void*, int, const char* const*, SkOut* o)
{
    bool ok = symbols_load();
    outf(o, "%u symbols from %s (mods resolve symbols at load: mod.reload them)", (unsigned)symbols_count(),
         symbols_file_name());
    return ok ? 0 : 1;
}

static int c_sym_find(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "sym.find <substring> [limit]");
    size_t lim = argc > 2 ? atoi(argv[2]) : 50;
    for (auto& [n, a] : symbols_find(argv[1], lim))
        outf(o, "0x%08X %s\n", (unsigned)a, n.c_str());
    return 0;
}

static int c_mem_read(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "mem.read <addr> [len=64]");
    uintptr_t a;
    if (!parse_addr(argv[1], a)) { outf(o, "bad address"); return 1; }
    size_t len = argc > 2 ? strtoul(argv[2], nullptr, 0) : 64;
    if (len > 65536) len = 65536;
    if (!skg::readable((void*)a, len)) { outf(o, "unreadable 0x%08X", (unsigned)a); return 1; }
    const uint8_t* p = (const uint8_t*)a;
    for (size_t i = 0; i < len; i += 16) {
        outf(o, "%08X ", (unsigned)(a + i));
        for (size_t j = 0; j < 16 && i + j < len; j++)
            outf(o, " %02X", p[i + j]);
        outf(o, "\n");
    }
    return 0;
}

// committed private memory of the process, walked with VirtualQuery (read-only, any thread). Under
// Wine the `env` line reports the working set, which moves by tens of MB on its own (BUG-0241 test)
static int c_mem_commit(void*, int, const char* const*, SkOut* o)
{
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t a = 0;
    uint64_t priv = 0, all = 0;
    while (VirtualQuery((void*)a, &mbi, sizeof mbi) == sizeof mbi) {
        if (mbi.State == MEM_COMMIT) {
            all += mbi.RegionSize;
            if (mbi.Type == MEM_PRIVATE)
                priv += mbi.RegionSize;
        }
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= a)
            break;
        a = next;
    }
    outf(o, "commit_private_kb=%llu commit_kb=%llu\n", (unsigned long long)(priv / 1024),
         (unsigned long long)(all / 1024));
    return 0;
}

static int c_mem_write(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(3, "mem.write <addr> <hexbytes>");
    uintptr_t a;
    if (!parse_addr(argv[1], a)) { outf(o, "bad address"); return 1; }
    std::string hx;
    for (int i = 2; i < argc; i++) hx += argv[i];
    std::vector<uint8_t> b;
    for (size_t i = 0; i + 1 < hx.size(); i += 2)
        b.push_back((uint8_t)strtoul(hx.substr(i, 2).c_str(), nullptr, 16));
    DWORD old;
    if (!VirtualProtect((void*)a, b.size(), PAGE_EXECUTE_READWRITE, &old)) { outf(o, "VirtualProtect failed"); return 1; }
    memcpy((void*)a, b.data(), b.size());
    VirtualProtect((void*)a, b.size(), old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)a, b.size());
    outf(o, "wrote %u bytes", (unsigned)b.size());
    return 0;
}

static int c_mem_ptr(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "mem.ptr <addr> [off...]   (prints each dereference)");
    uintptr_t a;
    if (!parse_addr(argv[1], a)) { outf(o, "bad address"); return 1; }
    outf(o, "0x%08X\n", (unsigned)a);
    for (int i = 2; i < argc; i++) {
        if (!skg::readable((void*)a, 4)) { outf(o, "unreadable 0x%08X", (unsigned)a); return 1; }
        a = *(uintptr_t*)a + strtol(argv[i], nullptr, 0);
        outf(o, "-> 0x%08X %s\n", (unsigned)a, symbols_reverse(a).c_str());
    }
    return 0;
}

// call <addr> <cdecl|stdcall|thiscall> [args]  args: 123, 0x10, f:1.5, s:text, @addr(sym)
static int c_call(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(3, "call <addr|sym> <cdecl|stdcall|thiscall> [args: 12 0x1 f:1.5 s:text @Sym]");
    uintptr_t a;
    if (!parse_addr(argv[1], a)) { outf(o, "bad address"); return 1; }
    std::string conv = argv[2];
    uint32_t v[8] = {0};
    std::vector<std::string> strs;
    strs.reserve(8);
    int n = 0;
    for (int i = 3; i < argc && n < 8; i++, n++) {
        const char* s = argv[i];
        if (!strncmp(s, "f:", 2)) {
            float f = (float)atof(s + 2);
            memcpy(&v[n], &f, 4);
        } else if (!strncmp(s, "s:", 2)) {
            strs.push_back(s + 2);
            v[n] = (uint32_t)(uintptr_t)strs.back().c_str();
        } else if (s[0] == '@') {
            uintptr_t x;
            if (!parse_addr(s + 1, x)) { outf(o, "bad arg %s", s); return 1; }
            v[n] = (uint32_t)x;
        } else
            v[n] = (uint32_t)strtoul(s, nullptr, 0);
    }
    uint32_t r;
    using namespace skg;
    if (conv == "thiscall") {
        if (n < 1) { outf(o, "thiscall needs this as first arg"); return 1; }
        void* t = (void*)(uintptr_t)v[0];
        switch (n - 1) {
        case 0: r = thiscall<uint32_t>(a, t); break;
        case 1: r = thiscall<uint32_t>(a, t, v[1]); break;
        case 2: r = thiscall<uint32_t>(a, t, v[1], v[2]); break;
        case 3: r = thiscall<uint32_t>(a, t, v[1], v[2], v[3]); break;
        case 4: r = thiscall<uint32_t>(a, t, v[1], v[2], v[3], v[4]); break;
        case 5: r = thiscall<uint32_t>(a, t, v[1], v[2], v[3], v[4], v[5]); break;
        case 6: r = thiscall<uint32_t>(a, t, v[1], v[2], v[3], v[4], v[5], v[6]); break;
        default: r = thiscall<uint32_t>(a, t, v[1], v[2], v[3], v[4], v[5], v[6], v[7]); break;
        }
    } else if (conv == "stdcall") {
        switch (n) {
        case 0: r = stdcall_call<uint32_t>(a); break;
        case 1: r = stdcall_call<uint32_t>(a, v[0]); break;
        case 2: r = stdcall_call<uint32_t>(a, v[0], v[1]); break;
        case 3: r = stdcall_call<uint32_t>(a, v[0], v[1], v[2]); break;
        case 4: r = stdcall_call<uint32_t>(a, v[0], v[1], v[2], v[3]); break;
        case 5: r = stdcall_call<uint32_t>(a, v[0], v[1], v[2], v[3], v[4]); break;
        case 6: r = stdcall_call<uint32_t>(a, v[0], v[1], v[2], v[3], v[4], v[5]); break;
        case 7: r = stdcall_call<uint32_t>(a, v[0], v[1], v[2], v[3], v[4], v[5], v[6]); break;
        default: r = stdcall_call<uint32_t>(a, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]); break;
        }
    } else {
        // cdecl: caller cleans, extra zero args are harmless
        r = cdecl_call<uint32_t>(a, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    }
    outf(o, "eax=0x%08X (%d) %s", r, (int)r, symbols_reverse(r).c_str());
    return 0;
}

static int c_cheat(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "cheat <text>");
    std::string line;
    for (int i = 1; i < argc; i++) {
        if (i > 1) line += ' ';
        line += argv[i];
    }
    bool ok = cheat_exec(line.c_str());
    outf(o, "cheat '%s' -> %s", line.c_str(), ok ? "true" : "false");
    return ok ? 0 : 1;
}

static int c_shot(void*, int argc, const char* const* argv, SkOut* o)
{
    std::wstring path;
    if (argc > 1)
        path = to_windows_path(argv[1]);
    else {
        CreateDirectoryW((mods_dirw() + L"\\shots").c_str(), nullptr);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t b[64];
        swprintf(b, 64, L"\\shots\\shot-%02d%02d%02d-%03d.png", st.wHour, st.wMinute, st.wSecond,
                 st.wMilliseconds);
        path = mods_dirw() + b;
    }
    if (!d3d_device()) { outf(o, "no D3D device yet"); return 1; }
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto res = std::make_shared<std::pair<bool, std::string>>(false, "");
    request_screenshot(path, [ev, res](bool ok, std::string msg) {
        res->first = ok;
        res->second = msg;
        SetEvent(ev);
    });
    if (WaitForSingleObject(ev, 15000) != WAIT_OBJECT_0) {
        outf(o, "timeout (no frames?)");
        return 1;
    }
    CloseHandle(ev);
    outf(o, "%s", res->second.c_str());
    return res->first ? 0 : 1;
}

static int c_key(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "key <vk|name> [tap|down|up]");
    std::string k = argv[1];
    int vk = 0;
    struct { const char* n; int vk; } names[] = {
        {"esc", VK_ESCAPE}, {"enter", VK_RETURN}, {"space", VK_SPACE}, {"tab", VK_TAB},
        {"shift", VK_SHIFT}, {"ctrl", VK_CONTROL}, {"alt", VK_MENU}, {"left", VK_LEFT},
        {"right", VK_RIGHT}, {"up", VK_UP}, {"down", VK_DOWN}, {"back", VK_BACK},
        {"f1", VK_F1}, {"f2", VK_F2}, {"f3", VK_F3}, {"f4", VK_F4}, {"f5", VK_F5},
        {"f6", VK_F6}, {"f7", VK_F7}, {"f8", VK_F8}, {"f9", VK_F9}, {"f10", VK_F10}, {"f11", VK_F11},
        {"f12", VK_F12}, {"del", VK_DELETE}, {"home", VK_HOME}, {"end", VK_END}, {"backspace", VK_BACK}};
    for (auto& nm : names)
        if (k == nm.n) vk = nm.vk;
    if (!vk && k.size() == 1)
        vk = toupper((unsigned char)k[0]);
    if (!vk)
        vk = (int)strtoul(k.c_str(), nullptr, 0);
    bool ok = input_key(vk, argc > 2 ? argv[2] : "tap");
    outf(o, "key 0x%02X %s", vk, ok ? "sent" : "failed (no window?)");
    return ok ? 0 : 1;
}

static int c_window(void*, int argc, const char* const* argv, SkOut* o)
{
    HWND w = (HWND)game_window();
    if (!w) {
        outf(o, "no window");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "test"))
        place_test_window();
    else if (argc > 2)
        SetWindowPos(w, nullptr, atoi(argv[1]), atoi(argv[2]), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT r;
    GetWindowRect(w, &r);
    outf(o, "window %ld %ld %ld %ld", r.left, r.top, r.right, r.bottom);
    return 0;
}

static int c_text(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "text <string>");
    bool ok = input_text(argv[1]);
    outf(o, ok ? "sent" : "failed");
    return ok ? 0 : 1;
}

static int c_click(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(3, "click <x> <y> [left|right|middle]");
    int b = argc > 3 ? (!strcmp(argv[3], "right") ? 1 : (!strcmp(argv[3], "middle") ? 2 : 0)) : 0;
    bool ok = input_click(atoi(argv[1]), atoi(argv[2]), b);
    outf(o, ok ? "clicked" : "failed");
    return ok ? 0 : 1;
}

static int c_drag(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(5, "drag <x1> <y1> <x2> <y2> [left|right]");
    int b = argc > 5 && !strcmp(argv[5], "right") ? 1 : 0;
    bool ok = input_drag(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), b);
    outf(o, ok ? "dragged" : "failed");
    return ok ? 0 : 1;
}

static int c_button(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(4, "button <x> <y> <down|up> [left|right|middle]");
    int b = argc > 4 ? (!strcmp(argv[4], "right") ? 1 : (!strcmp(argv[4], "middle") ? 2 : 0)) : 0;
    bool ok = input_button(atoi(argv[1]), atoi(argv[2]), b, !strcmp(argv[3], "down"));
    outf(o, ok ? "button %s" : "failed", argv[3]);
    return ok ? 0 : 1;
}

static int c_move(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(3, "move <x> <y>");
    bool ok = input_move(atoi(argv[1]), atoi(argv[2]));
    outf(o, ok ? "moved" : "failed");
    return ok ? 0 : 1;
}

static int c_wheel(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(4, "wheel <x> <y> <notches>");
    bool ok = input_wheel(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
    outf(o, ok ? "wheel" : "failed");
    return ok ? 0 : 1;
}

static int c_mods(void*, int, const char* const*, SkOut* o)
{
    for (auto* m : mods_list())
        outf(o, "%-12s %-8s gen=%d %s\n", m->name.c_str(), m->module ? "loaded" : "unloaded",
             m->generation, m->info ? m->info->version : "");
    return 0;
}

static int c_mod_op(void* user, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "mod.<reload|load|unload> <name>");
    std::string err;
    int op = (int)(intptr_t)user;
    bool ok = op == 0 ? mod_reload(argv[1], err) : op == 1 ? mod_load(argv[1], err) : mod_unload(argv[1], err);
    outf(o, ok ? "ok" : "error: %s", err.c_str());
    return ok ? 0 : 1;
}

static int c_hooks(void*, int, const char* const*, SkOut* o)
{
    for (auto& h : hooks_describe())
        outf(o, "%s\n", h.c_str());
    return 0;
}

static int c_mode(void*, int, const char* const*, SkOut* o)
{
    uint32_t m = game_mode_id();
    outf(o, "0x%08X %s", m, game_mode_name(m));
    return 0;
}

static int c_stack(void*, int argc, const char* const* argv, SkOut* o)
{
    int depth = argc > 1 ? atoi(argv[1]) : 24;
    static ThreadSnap snap; // 16 KB, one console thread
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    // formatting allocates: it runs after the resume (BUG-0050), unless kit.fix stackdump 0
    auto format = [&] {
        const CONTEXT& c = snap.c;
        outf(o, "eip=0x%08lX %s\nesp=0x%08lX ebp=0x%08lX\n", c.Eip, symbols_reverse(c.Eip).c_str(), c.Esp, c.Ebp);
        // heuristic: scan stack for return addresses into the exe image
        uintptr_t lo = image_base(), hi = image_base() + 0x1000000;
        int found = 0;
        for (int i = 0; i < snap.n && found < depth; i++) {
            uint32_t v = snap.words[i];
            if (v > lo && v < hi) {
                std::string r = symbols_reverse(v);
                outf(o, "  [esp+0x%04X] 0x%08X %s\n", i * 4, v, r.c_str());
                found++;
            }
        }
    };
    std::string err;
    bool now = fixed(g_fix_stackdump);
    if (!thread_snapshot(main_thread_id(), snap, &err, now ? nullptr : call_lambda<decltype(format)>, &format)) {
        outf(o, "%s", err.c_str());
        return 1;
    }
    if (now)
        format();
    return 0;
}

// stack.scan: return addresses into the exe on the main thread's stack, read from its TEB stack
// range WITHOUT suspending the thread (suspending it under Wine can hang the console when the
// thread sits in a wineserver call). Lists from the stack base (outermost frames) down; values
// below the current esp are stale leftovers, so trust the outer part most.
static int c_stack_scan(void*, int argc, const char* const* argv, SkOut* o)
{
    int depth = argc > 1 ? atoi(argv[1]) : 60;
    typedef LONG(WINAPI * NtQIT)(HANDLE, int, void*, ULONG, ULONG*);
    auto q = (NtQIT)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    HANDLE t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, main_thread_id());
    if (!q || !t) { outf(o, "cannot query the main thread"); return 1; }
    struct { LONG status; void* teb; uintptr_t pid, tid; uintptr_t mask; LONG prio, base; } bi{};
    LONG st = q(t, 0 /* ThreadBasicInformation */, &bi, sizeof bi, nullptr);
    CloseHandle(t);
    if (st < 0 || !bi.teb) { outf(o, "NtQueryInformationThread failed 0x%08lX", st); return 1; }
    uint32_t* hi = *(uint32_t**)((uint8_t*)bi.teb + 4); // NT_TIB StackBase
    uint32_t* lo = *(uint32_t**)((uint8_t*)bi.teb + 8); // NT_TIB StackLimit
    outf(o, "main thread stack %p..%p (scan from the base down)\n", lo, hi);
    uintptr_t ilo = image_base(), ihi = image_base() + 0x1000000;
    int found = 0;
    for (uint32_t* p = hi - 1; p >= lo && found < depth; p--) {
        if (!skg::readable(p, 4)) break;
        uint32_t v = *p;
        if (v > ilo && v < ihi) {
            outf(o, "  [%p] 0x%08X %s\n", (void*)p, v, symbols_reverse(v).c_str());
            found++;
        }
    }
    return 0;
}

// stack.here: runs ON the main thread (from its message loop or the frame) and lists the return
// addresses into the exe from its own esp up to the stack base: the real call chain of whatever
// the main thread is doing while it still pumps messages (a nested loop, a modal wait).
static int c_stack_here(void*, int argc, const char* const* argv, SkOut* o)
{
    int depth = argc > 1 ? atoi(argv[1]) : 80;
    uint8_t* teb = (uint8_t*)NtCurrentTeb();
    uint32_t* hi = *(uint32_t**)(teb + 4);
    volatile uint32_t marker = 0;
    uint32_t* p = (uint32_t*)&marker;
    uintptr_t ilo = image_base(), ihi = image_base() + 0x1000000;
    bool all = argc > 2 && !strcmp(argv[2], "all"); // also other modules (user32 dispatch, ntdll)
    int found = 0;
    for (; p < hi && found < depth; p++) {
        uint32_t v = *p;
        if (v > ilo && v < ihi) {
            outf(o, "  [%p] 0x%08X %s\n", (void*)p, v, symbols_reverse(v).c_str());
            found++;
        } else if (all && v > 0x10000) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery((void*)(uintptr_t)v, &mbi, sizeof mbi) && mbi.Type == MEM_IMAGE &&
                (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE | PAGE_EXECUTE_READWRITE))) {
                wchar_t name[MAX_PATH] = L"?";
                GetModuleFileNameW((HMODULE)mbi.AllocationBase, name, MAX_PATH);
                const wchar_t* b = wcsrchr(name, L'\\');
                outf(o, "  [%p] 0x%08X %s+0x%X\n", (void*)p, v, narrow(b ? b + 1 : name).c_str(),
                     (unsigned)(v - (uintptr_t)mbi.AllocationBase));
                found++;
            }
        }
    }
    return 0;
}

static int c_bg(void*, int argc, const char* const* argv, SkOut* o)
{
    if (argc > 1)
        set_background_mode(strcmp(argv[1], "off") != 0);
    outf(o, "background mode %s", background_mode() ? "on" : "off");
    return 0;
}

// frames.source [auto|loop]: loop ignores Present and runs the frames from the game loop, as when
// another overlay takes Present over (tests of that fallback); any thread, it only sets a flag
static int c_frames_source(void*, int argc, const char* const* argv, SkOut* o)
{
    if (argc > 1)
        set_frame_source_mode(strcmp(argv[1], "loop") == 0);
    outf(o, "frames from: %s", frame_source());
    return 0;
}

static int c_exc(void*, int argc, const char* const* argv, SkOut* o)
{
    outf(o, "%s", exceptions_report().c_str());
    if (argc > 1 && !strcmp(argv[1], "clear"))
        exceptions_clear();
    return 0;
}

static int c_hang(void*, int, const char* const*, SkOut* o)
{
    outf(o, "%s", watchdog_dump_now().c_str());
    return 0;
}

static int c_probe(void*, int argc, const char* const* argv, SkOut* o)
{
    NEED(2, "probe <addr|sym> - record calls (this, args, callers)");
    uintptr_t a;
    if (!parse_addr(argv[1], a)) { outf(o, "bad address"); return 1; }
    outf(o, "%s", probe_add(a).c_str());
    return 0;
}

static int c_probe_list(void*, int argc, const char* const* argv, SkOut* o)
{
    outf(o, "%s", probe_report(argc > 1 && !strcmp(argv[1], "clear")).c_str());
    return 0;
}

static int c_ftrace(void*, int argc, const char* const* argv, SkOut* o)
{
    outf(o, "%s", ftrace_set(argc > 1 ? argv[1] : "").c_str());
    return 0;
}

static int c_probe_clear(void*, int, const char* const*, SkOut* o)
{
    probe_clear_all();
    outf(o, "probes removed");
    return 0;
}

static int c_quit(void*, int, const char* const*, SkOut* o)
{
    outf(o, "bye");
    run_on_main([] { TerminateProcess(GetCurrentProcess(), 0); });
    return 0;
}

// kit.fix [name [0|1]]: the fix switches (tests show with 0 that they still catch the bug)
static int c_kit_fix(void*, int argc, const char* const* argv, SkOut* o)
{
    if (argc < 2) {
        for (KitFix* f : kit_fixes())
            outf(o, "%-10s %d  %s\n", f->name, (int)f->on, f->what);
        return 0;
    }
    KitFix* f = kit_fix_find(argv[1]);
    if (!f) { outf(o, "unknown fix '%s' (kit.fix lists them)", argv[1]); return 1; }
    if (argc > 2) {
        InterlockedExchange(&f->on, atoi(argv[2]) ? 1 : 0);
        SKLOG(SK_WARN, "kit.fix %s %d", f->name, (int)f->on);
    }
    outf(o, "%s %d", f->name, (int)f->on);
    return 0;
}

// kit.test.stackheap [stack|hangdump] [hold_ms]: the main thread holds the heap locks (as when it is
// suspended inside malloc) while the console thread runs `stack` / `hangdump` (BUG-0050)
static int c_test_stackheap(void*, int argc, const char* const* argv, SkOut* o)
{
    bool hang = argc > 1 && !strcmp(argv[1], "hangdump");
    int hold = argc > 2 ? atoi(argv[2]) : 3000;
    HANDLE held = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE heaps[2] = {(HANDLE)_get_heap_handle(), GetProcessHeap()};
    run_on_main([held, hold, heaps] {
        for (HANDLE h : heaps)
            HeapLock(h);
        SetEvent(held);
        Sleep(hold); // nothing allocates here: the heaps stay locked until the end
        for (HANDLE h : heaps)
            HeapUnlock(h);
    });
    if (WaitForSingleObject(held, 30000) != WAIT_OBJECT_0) {
        outf(o, "the main thread did not take the heap locks");
        return 1;
    }
    // from here until the main thread lets go nothing on this thread may allocate before the suspend
    DWORD t0 = GetTickCount();
    std::string answer;
    if (hang)
        answer = watchdog_dump_now();
    else {
        const char* av[] = {"stack", "8"};
        Out sub;
        c_stack(nullptr, 2, av, reinterpret_cast<SkOut*>(&sub));
        answer = sub.text;
    }
    DWORD dt = GetTickCount() - t0;
    CloseHandle(held);
    outf(o, "%s answered after %lu ms (heaps held %d ms): %d bytes\n", hang ? "hangdump" : "stack", dt, hold,
         (int)answer.size());
    return 0;
}

static int c_test_fault(void* user, int, const char* const*, SkOut* o)
{
    int which = (int)(intptr_t)user;
    if (which == 0)
        log_fault_test();
    else
        exceptions_fault_test();
    outf(o, "%s: the thread goes on", which == 0 ? "logfault" : "excfault");
    return 0;
}

static int c_test_hookchurn(void*, int argc, const char* const* argv, SkOut* o)
{
    std::string r;
    hooks_churn_test(argc > 1 ? atoi(argv[1]) : 200, argc > 2 ? atoi(argv[2]) : 3, r,
                     argc > 3 && !strcmp(argv[3], "nogate"));
    outf(o, "%s", r.c_str());
    return 0;
}

void register_builtin_commands()
{
    SkMod* k = &g_kit_mod;
    const int A = SK_CMD_ANY_THREAD, M = SK_CMD_MAIN_THREAD;
    const int W = SK_CMD_ANY_THREAD | SK_CMD_NOT_ON_MAIN; // waits for the main thread (BUG-0065)
    command_add(k, "help", "list commands [filter]", c_help, nullptr, A);
    command_add(k, "ping", "liveness check (any thread)", c_ping, nullptr, A);
    command_add(k, "info", "versions, exe hash, frame, mode, mods", c_info, nullptr, M);
    command_add(k, "env", "the environment: game build, Windows / Wine, memory, GPU, other hooks, mods",
                [](void*, int, const char* const*, SkOut* out) {
                    outf(out, "%s", diag_env().c_str());
                    return 0;
                },
                nullptr, M);
    command_add(k, "crash.test", "crash.test [thread|main|stackoverflow|oom|faultthread] - make the game crash (tests of the crash report; faultthread: a repaired fault on a worker thread, no crash)",
                [](void*, int argc, const char* const* argv, SkOut* out) {
                    outf(out, "crashing");
                    diag_crash_test(argc > 1 ? argv[1] : "thread");
                    return 0;
                },
                nullptr, M);
    command_add(k, "report", "report [reason] - write mods\\logs\\SporeCoop-report-<time>.zip (report, logs, crashes, settings) for a bug report",
                [](void*, int argc, const char* const* argv, SkOut* out) {
                    outf(out, "%s", diag_write_bundle(argc > 1 ? argv[1] : "requested").c_str());
                    return 0;
                },
                nullptr, M);
    command_add(k, "frame.limit", "frame.limit <ms>|auto - frame time cap (33 = the game's 30 fps, 0 = none)",
                [](void*, int argc, const char* const* argv, SkOut* out) {
                    if (argc < 2)
                        return 1;
                    set_frame_limit(strcmp(argv[1], "auto") == 0 ? -1 : atoi(argv[1]));
                    outf(out, "frame limit set");
                    return 0;
                },
                nullptr, A);
    command_add(k, "frame", "frame counter and fps", c_frame, nullptr, A);
    command_add(k, "wait.frames", "wait.frames <n> [timeout_ms]", c_wait_frames, nullptr, W);
    command_add(k, "log", "log [n] - last log lines", c_log, nullptr, A);
    command_add(k, "sym", "sym <name|addr> - resolve", c_sym, nullptr, A);
    command_add(k, "sym.reload", "reload symbol files from mods/sporekit/symbols", c_sym_reload, nullptr, M);
    command_add(k, "sym.find", "sym.find <substring> [limit]", c_sym_find, nullptr, A);
    command_add(k, "mem.read", "mem.read <addr> [len]", c_mem_read, nullptr, A);
    command_add(k, "mem.write", "mem.write <addr> <hex>", c_mem_write, nullptr, M);
    command_add(k, "mem.ptr", "mem.ptr <addr> [off...] - follow pointers", c_mem_ptr, nullptr, A);
    command_add(k, "mem.commit", "committed private memory of the game (VirtualQuery walk)", c_mem_commit, nullptr, A);
    command_add(k, "call", "call <addr> <conv> [args] - call game function", c_call, nullptr, M);
    command_add(k, "cheat", "cheat <text> - run a Spore cheat", c_cheat, nullptr, M);
    command_add(k, "shot", "shot [linux path] - PNG screenshot", c_shot, nullptr, W);
    command_add(k, "key", "key <vk|name> [tap|down|up]", c_key, nullptr, A);
    command_add(k, "text", "text <string> - WM_CHAR input", c_text, nullptr, A);
    command_add(k, "window", "window [x y | test] - game window position (move it)", c_window, nullptr,
                SK_CMD_OUTSIDE_FRAME);
    command_add(k, "click", "click <x> <y> [button]", c_click, nullptr, A);
    command_add(k, "move", "move <x> <y> - mouse move", c_move, nullptr, A);
    command_add(k, "button", "button <x> <y> <down|up> [left|right|middle] - press and hold / release", c_button,
                nullptr, A);
    command_add(k, "drag", "drag <x1> <y1> <x2> <y2> [left|right] - press, move, release", c_drag, nullptr, A);
    command_add(k, "wheel", "wheel <x> <y> <notches> - mouse wheel (+ zooms in)", c_wheel, nullptr, A);
    command_add(k, "mods", "list mods", c_mods, nullptr, M);
    command_add(k, "mod.reload", "mod.reload <name> - hot reload", c_mod_op, (void*)0, M);
    command_add(k, "mod.load", "mod.load <name>", c_mod_op, (void*)1, M);
    command_add(k, "mod.unload", "mod.unload <name>", c_mod_op, (void*)2, M);
    command_add(k, "hooks", "list installed hooks", c_hooks, nullptr, A);
    command_add(k, "game.mode", "active game mode", c_mode, nullptr, M);
    command_add(k, "stack", "stack [depth] - main thread stack (works when hung)", c_stack, nullptr, W);
    command_add(k, "quit", "terminate the game", c_quit, nullptr, A);
    command_add(k, "exc", "exc [clear] - faults caught by the exception logger", c_exc, nullptr, A);
    command_add(k, "hangdump", "write the main thread stack (with DLL names) to the log", c_hang, nullptr, W);
    command_add(k, "probe", "probe <addr|sym> - trace calls without knowing the signature", c_probe, nullptr, M);
    command_add(k, "probe.list", "probe.list [clear] - recorded calls", c_probe_list, nullptr, A);
    command_add(k, "ftrace", "ftrace [substring] - log file opens (with callers) whose path contains it", c_ftrace,
                nullptr, M);
    command_add(k, "probe.clear", "remove all probes", c_probe_clear, nullptr, M);
    command_add(k, "stack.scan", "stack.scan [n] - main thread return addresses without suspending it", c_stack_scan,
                nullptr, A);
    command_add(k, "stack.here", "stack.here [n] [all] - main thread call chain, read on the main thread itself",
                c_stack_here, nullptr, M);
    command_add(k, "kit.fix", "kit.fix [name [0|1]] - fix switches (tests: 0 = the old behaviour)", c_kit_fix, nullptr, A);
    command_add(k, "kit.test.stackheap", "kit.test.stackheap [stack|hangdump] [hold_ms] - stack dump while the main thread holds the heap (BUG-0050)",
                c_test_stackheap, nullptr, A);
    command_add(k, "kit.test.logfault", "kit.test.logfault - a fault inside the log's lock (BUG-0115)", c_test_fault,
                (void*)0, M);
    command_add(k, "kit.test.excfault", "kit.test.excfault - a fault inside the exception logger's lock (BUG-0115)",
                c_test_fault, (void*)1, M);
    command_add(k, "kit.test.hookchurn", "kit.test.hookchurn [rounds] [threads] [nogate] - hook / unhook while threads run the hook (BUG-0132; nogate: also while the hook is made, BUG-0202)",
                c_test_hookchurn, nullptr, M);
    command_add(k, "bg", "bg [on|off] - keep running when the window is unfocused", c_bg, nullptr, A);
    command_add(k, "frames.source", "frames.source [auto|loop] - where frames come from (loop: ignore Present, tests)",
                c_frames_source, nullptr, A);
}

} // namespace sk
