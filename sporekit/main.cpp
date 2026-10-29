// SporeKit entry point (called by the loader at the exe entry point) and the SkHost table.
#include "core.h"
#include "inifile.h"

#include <cstdio>

namespace sk {

SkMod g_kit_mod;
static std::wstring g_game_dir, g_mods_dir;
static std::string g_mods_dir_utf8;
static int g_instance = 1;

const std::wstring& game_dir() { return g_game_dir; }
const std::wstring& mods_dirw() { return g_mods_dir; }
int instance_id() { return g_instance; }

// mods\*.ini in Unicode (BUG-0142, sporekit/inifile.h; kit.fix iniutf 0: the old ANSI files)
int ini_int(const std::wstring& file, const char* section, const char* key, int def)
{
    return skini::read_int(file, section, key, def);
}

// the kit.fix switches of skini (inifile.cpp is also built into win_tests without SporeKit)
static void ini_fixes()
{
    skini::g_unicode_fix = fixed(g_fix_iniutf);
    skini::g_encode_fix = fixed(g_fix_iniencode);
    skini::g_lock_fix = fixed(g_fix_inilock);
    skini::g_log = [](const std::string& s) { SKLOG(SK_WARN, "%s", s.c_str()); };
}

std::string ini_str(const std::wstring& file, const char* section, const char* key, const char* def)
{
    ini_fixes();
    return skini::read_str(file, section, key, def);
}

// ---------------- SkHost implementation ----------------
static void h_log(SkMod* m, int lvl, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logv(m ? m : &g_kit_mod, lvl, fmt, ap);
    va_end(ap);
}
static void h_logv(SkMod* m, int lvl, const char* fmt, va_list ap) { logv(m ? m : &g_kit_mod, lvl, fmt, ap); }
static uintptr_t h_sym(const char* n) { return sym(n); }
static int h_sym_require(SkMod* m, const char* const* names, int count)
{
    int miss = 0;
    for (int i = 0; i < count; i++)
        if (!sym(names[i])) {
            logf(m, SK_ERROR, "missing symbol: %s", names[i]);
            miss++;
        }
    return miss;
}
static uintptr_t h_image_base() { return image_base(); }
static uintptr_t h_rebase(uintptr_t va) { return rebase(va); }
static int h_hook_create(SkMod* m, void* t, void* d, void** o) { return hook_create(m, t, d, o); }
static int h_hook_enable(SkMod* m, void* t, int e) { return hook_enable(m, t, e); }
static int h_hook_remove(SkMod* m, void* t) { return hook_remove(m, t); }
static int h_subscribe(SkMod* m, int ev, SkEventFn fn, void* u) { return subscribe(m, ev, fn, u); }
static void h_unsubscribe(SkMod* m, int ev, SkEventFn fn, void* u) { unsubscribe(m, ev, fn, u); }
static int h_command_add(SkMod* m, const char* n, const char* h, SkCommandFn fn, void* u, int f)
{
    return command_add(m, n, h, fn, u, f);
}
static void h_out_printf(SkOut* out, const char* fmt, ...)
{
    if (!out)
        return;
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0)
        reinterpret_cast<Out*>(out)->text.append(buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
}
static int h_command_exec(const char* line, SkOut* out)
{
    std::string o;
    int r = command_exec_line(line, o, true);
    if (out)
        reinterpret_cast<Out*>(out)->text += o;
    return r;
}
static void h_run_on_main(SkTaskFn fn, void* u)
{
    run_on_main_from((const void*)fn, [fn, u] { fn(u); });
}
static std::wstring mod_ini(SkMod* m) { return g_mods_dir + L"\\" + widen(m ? m->name : "sporekit") + L".ini"; }
static int h_config_int(SkMod* m, const char* s, const char* k, int d) { return ini_int(mod_ini(m), s, k, d); }
static void h_config_str(SkMod* m, const char* s, const char* k, const char* d, char* buf, int sz)
{
    std::string v = ini_str(mod_ini(m), s, k, d ? d : "");
    if (sz > 0) {
        size_t n = v.size() < (size_t)sz - 1 ? v.size() : (size_t)sz - 1;
        if (n < v.size())
            while (n > 0 && ((unsigned char)v[n] & 0xC0) == 0x80) // never inside a UTF-8 character
                n--;
        memcpy(buf, v.data(), n);
        buf[n] = 0;
    }
}
static int h_config_set_str(SkMod* m, const char* s, const char* k, const char* v)
{
    ini_fixes();
    return skini::write_str(mod_ini(m), s, k, v) ? 0 : 1;
}
static void h_run_outside_frame(SkTaskFn fn, void* u)
{
    run_outside_frame_from((const void*)fn, [fn, u] { fn(u); });
}
static void* h_d3d_device() { return d3d_device(); }
static void* h_game_window() { return game_window(); }
static uint64_t h_frame_index() { return frame_index(); }
static double h_time() { return now_seconds(); }
static int h_instance() { return g_instance; }
static const char* h_mods_dir() { return g_mods_dir_utf8.c_str(); }
static void h_overlay_text(int x, int y, uint32_t c, const char* t) { overlay_text(x, y, c, t); }
static int h_w2s(const float* w, float* o) { return world_to_screen(w, o); }
static const char* h_exe_sha256()
{
    static std::string h = exe_sha256();
    return h.c_str();
}
static const char* h_kit_version() { return SK_VERSION; }
static const char* h_build_name() { return build_name(); }
static int h_diag_report(const char* reason, char* out, int cap)
{
    std::string p = diag_write_bundle(reason);
    if (out && cap > 0)
        snprintf(out, (size_t)cap, "%s", p.c_str());
    return (int)p.size();
}
static void h_diag_context(SkMod* mod, const char* (*fn)(void*), void* user) { diag_context(mod, fn, user); }
static const char* h_last_problem() { return diag_last_problem(); }
static void h_log_session_clock(double off, int on) { log_session_clock(off, on); }
static void h_diag_headline(SkMod* mod, const char* text) { diag_headline(mod, text); }
static const char* h_diag_files() { return diag_files_text().c_str(); }
static const char* h_logs_dir()
{
    static std::string d = diag_logs_dir();
    return d.c_str();
}

static const char* h_symbols_build()
{
    static std::string h = symbols_count() ? symbols_build() : std::string();
    return h.c_str();
}

SkHost g_host = {
    SK_ABI_VERSION, sizeof(SkHost),
    h_log, h_logv,
    h_sym, h_sym_require, h_image_base, h_rebase,
    h_hook_create, h_hook_enable, h_hook_remove,
    h_subscribe, h_unsubscribe,
    h_command_add, h_out_printf, h_command_exec,
    h_run_on_main,
    h_config_int, h_config_str,
    h_d3d_device, h_game_window, h_frame_index, h_time, h_instance, h_mods_dir,
    h_overlay_text, h_w2s,
    h_exe_sha256,
    cheat_register,
    screen_size,
    h_run_outside_frame,
    h_config_set_str,
    ui_winproc_new,
    ui_winproc_disable,
    h_symbols_build,
    h_kit_version,
    h_build_name,
    h_diag_report,
    h_diag_context,
    h_logs_dir,
    h_last_problem,
    h_log_session_clock,
    h_diag_headline,
    h_diag_files,
};

} // namespace sk

using namespace sk;

// ---- a packed exe (Steam DRM): the game-specific part of the boot waits for the unpacked code ----
// The loader runs the boot at the exe's entry point, which in a SteamStub exe is the unpacker in
// .bind: the game's .text is still encrypted there. The first call the game's own CRT start code
// makes (__security_init_cookie: GetSystemTimeAsFileTime; GetStartupInfoW as a second chance),
// coming from .text, is after the unpacking and before anything of the game runs: the symbols
// are checked and loaded there, then the lifecycle hooks and the mods (docs/05-re-notes.md 5.1.2).
static bool g_late_done;
static uintptr_t g_text_lo, g_text_hi;
using GetSystemTimeAsFileTime_t = void(WINAPI*)(LPFILETIME);
using GetStartupInfoW_t = void(WINAPI*)(LPSTARTUPINFOW);
static GetSystemTimeAsFileTime_t o_GetSystemTimeAsFileTime;
static GetStartupInfoW_t o_GetStartupInfoW;

static void late_boot(uintptr_t caller)
{
    if (g_late_done || caller < g_text_lo || caller >= g_text_hi)
        return;
    g_late_done = true;
    SKLOG(SK_INFO, "the game's code runs (first call from 0x%08X): the game-specific boot", (unsigned)caller);
    watchdog_mark("symbols");
    if (symbols_pending())
        symbols_try_unpacked();
    SKLOG(SK_INFO, "game build: %s", build_name());
    watchdog_mark("mods");
    gamehooks_install();
    mods_load_all();
    watchdog_mark("game");
    SKLOG(SK_INFO, "late boot complete");
}

static void WINAPI h_GetSystemTimeAsFileTime(LPFILETIME ft)
{
    late_boot((uintptr_t)__builtin_return_address(0));
    o_GetSystemTimeAsFileTime(ft);
}

static void WINAPI h_GetStartupInfoW(LPSTARTUPINFOW si)
{
    late_boot((uintptr_t)__builtin_return_address(0));
    o_GetStartupInfoW(si);
}

static bool late_boot_install()
{
    auto* base = (uint8_t*)GetModuleHandleW(nullptr);
    auto* nt = (IMAGE_NT_HEADERS32*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (!memcmp(sec[i].Name, ".text\0\0\0", 8)) {
            g_text_lo = (uintptr_t)base + sec[i].VirtualAddress;
            g_text_hi = g_text_lo + sec[i].Misc.VirtualSize;
        }
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    void* a = k32 ? (void*)GetProcAddress(k32, "GetSystemTimeAsFileTime") : nullptr;
    void* b = k32 ? (void*)GetProcAddress(k32, "GetStartupInfoW") : nullptr;
    bool ok = g_text_lo && a && b &&
              hook_create(&g_kit_mod, a, (void*)h_GetSystemTimeAsFileTime, (void**)&o_GetSystemTimeAsFileTime) == 0 &&
              hook_create(&g_kit_mod, b, (void*)h_GetStartupInfoW, (void**)&o_GetStartupInfoW) == 0;
    SKLOG(ok ? SK_INFO : SK_ERROR, "late boot: %s (game code 0x%08X..0x%08X)",
          ok ? "waiting for the game's first call" : "cannot watch for the game's code", (unsigned)g_text_lo,
          (unsigned)g_text_hi);
    return ok;
}

extern "C" __declspec(dllexport) int __cdecl sporekit_boot(HMODULE loader)
{
    (void)loader;
    static bool booted = false;
    if (booted)
        return 0;
    booted = true;
    set_main_thread();

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_game_dir = exe;
    g_game_dir.resize(g_game_dir.find_last_of(L"\\/"));
    g_mods_dir = g_game_dir + L"\\mods";
    g_mods_dir_utf8 = narrow(g_mods_dir);
    CreateDirectoryW(g_mods_dir.c_str(), nullptr);
    CreateDirectoryW((g_mods_dir + L"\\logs").c_str(), nullptr);

    char env[16] = {0};
    if (GetEnvironmentVariableA("SPOREKIT_INSTANCE", env, sizeof env))
        g_instance = atoi(env) > 0 ? atoi(env) : 1;
    // Bottles filters environment variables, so also accept a command line switch:
    //   SporeApp.exe -sporekit-instance:2
    if (const wchar_t* p = wcsstr(GetCommandLineW(), L"-sporekit-instance:"))
        g_instance = _wtoi(p + 19) > 0 ? _wtoi(p + 19) : 1;

    g_kit_mod.name = "sporekit";
    g_kit_mod.builtin = true;
    std::wstring ini = g_mods_dir + L"\\sporekit.ini";
    g_log_level = ini_int(ini, "sporekit", "log_level", SK_DEBUG);
    std::wstring logname = g_instance > 1 ? L"\\logs\\sporekit." + std::to_wstring(g_instance) + L".log"
                                          : L"\\logs\\sporekit.log";
    log_init(g_mods_dir + logname);
    SKLOG(SK_INFO, "SporeKit %s booting (instance %d, pid %lu)", SK_VERSION, g_instance,
          GetCurrentProcessId());

    diag_read_last_problem(); // the previous run crashed / hung: its archive, told to the player
    watchdog_start();
    exceptions_install();
    watchdog_mark("hooks_init");
    hooks_init();
    diag_install(); // the crash report, the environment into the log
    watchdog_mark("symbols");
    symbols_load();
    if (!symbols_pending())
        SKLOG(SK_INFO, "game build: %s", build_name());
    // [sporekit] defer_symbols=1: the late boot of a packed exe on any exe (tests of the path)
    bool late = symbols_pending() || ini_int(ini, "sporekit", "defer_symbols", 0) != 0;
    watchdog_mark("instance");
    instance_install();
    focus_install_hooks();
    watchdog_mark("d3d9");
    d3d9_install();
    if (!late)
        gamehooks_install();
    register_builtin_commands();
    cheats_register_builtin();
    int port = ini_int(ini, "sporekit", "console_port", 47800) + (g_instance - 1);
    watchdog_mark("console");
    console_start(port);
    if (late && late_boot_install()) {
        SKLOG(SK_INFO, "boot complete (the game-specific part waits for the game's code)");
        return 0;
    }
    if (late)
        gamehooks_install();
    watchdog_mark("mods");
    mods_load_all();
    watchdog_mark("game");
    SKLOG(SK_INFO, "boot complete");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(inst);
    return TRUE;
}
