// SporeKit internal declarations (not part of the mod ABI).
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../sdk/include/sporekit.h"

#define SK_VERSION "0.3.4"

// ---- SkMod (opaque for mods) ----
struct SkMod {
    std::string name;       // file stem, e.g. "coop"
    std::wstring src_path;  // mods\coop.dll
    std::wstring live_path; // shadow copy actually loaded
    HMODULE module = nullptr;
    const SkModInfo* info = nullptr;
    bool builtin = false; // sporekit itself
    int generation = 0;
};

namespace sk {

// log.cpp
void log_init(const std::wstring& path);
void logf(SkMod* mod, int level, const char* fmt, ...);
void logv(SkMod* mod, int level, const char* fmt, va_list ap);
std::vector<std::string> log_tail(size_t n);
extern int g_log_level;

// util
std::string narrow(const std::wstring& w);
std::wstring widen(const std::string& s);
std::wstring to_windows_path(const std::string& p); // "/home/x" -> "Z:\home\x"
std::string json_escape(const std::string& s);
double now_seconds();

// paths/config (main.cpp)
extern SkMod g_kit_mod;
const std::wstring& game_dir();  // ...\SporebinEP1 (no trailing slash)
const std::wstring& mods_dirw(); // ...\SporebinEP1\mods
int instance_id();
int ini_int(const std::wstring& file, const char* section, const char* key, int def);
std::string ini_str(const std::wstring& file, const char* section, const char* key,
                    const char* def);

// sha256.cpp
std::string sha256_file_hex(const std::wstring& path);
std::string pe_code_sha256_hex(const std::wstring& path); // code and data, without header and resources
std::string pe_data_sha256_hex(const std::wstring& path); // every section but .text/.bind/.rsrc (packed exes)
std::string mem_text_sha256_hex(HMODULE mod, uintptr_t preferred_base); // .text in memory, relocations undone

// symbols.cpp
bool symbols_load(); // picks file by exe hash
// a packed exe (Steam DRM) whose data is that of a known build: symbols wait for the unpacked
// code (symbols_try_unpacked from the first call of the game's own code)
bool symbols_pending();
bool symbols_try_unpacked(); // true: the code in memory is the build's, its symbols are loaded
uintptr_t sym(const char* name);
uintptr_t image_base();
uintptr_t rebase(uintptr_t va);
std::string exe_sha256();
std::string symbols_build(); // exe_sha256 of the build whose symbol files are loaded
const char* symbols_file_name();
size_t symbols_count();
std::vector<std::pair<std::string, uintptr_t>> symbols_find(const std::string& needle,
                                                            size_t limit);
std::string symbols_reverse(uintptr_t addr); // "Name+0x10" or ""

// hooks.cpp
bool hooks_init();
int hook_create(SkMod* mod, void* target, void* detour, void** original);
int hook_enable(SkMod* mod, void* target, int enable);
int hook_remove(SkMod* mod, void* target);
int hooks_remove_all(SkMod* mod); // the count removed
std::vector<std::string> hooks_describe();

// events.cpp
int subscribe(SkMod* mod, int event, SkEventFn fn, void* user);
void unsubscribe(SkMod* mod, int event, SkEventFn fn, void* user);
void unsubscribe_all(SkMod* mod);
void fire(int event, void* arg);

// tasks / main thread (console.cpp)
void run_on_main(std::function<void()> fn);
void pump_main_tasks(); // called on main thread every frame
void run_after_frames(int frames, std::function<void()> fn);
void run_outside_frame(std::function<void()> fn); // main thread, from the window procedure
// the same for a mod's task: `code` is the mod's function, so an unload can drop what it queued
void run_on_main_from(const void* code, std::function<void()> fn);
void run_outside_frame_from(const void* code, std::function<void()> fn);
size_t drop_tasks_in(uintptr_t lo, uintptr_t hi); // queued mod tasks whose code is in [lo, hi)
void pump_outside_tasks();
bool in_frame(); // inside the Present hook
DWORD main_thread_id();
void set_main_thread();

// console.cpp
struct Out {
    std::string text;
};
bool console_start(int port);
int command_add(SkMod* mod, const char* name, const char* help, SkCommandFn fn, void* user,
                int flags);
void commands_remove_all(SkMod* mod);
int command_exec_line(const std::string& line, std::string& out, bool from_main);
std::vector<std::string> split_args(const std::string& line);
std::vector<std::pair<std::string, std::string>> commands_list();

// commands.cpp
void register_builtin_commands();

// d3d9hook.cpp
bool d3d9_install();
void* d3d_device();
void* game_window();
uint64_t frame_index();
double fps();
void frame_from_game_loop();          // after App::Canvas::Tick: the frame when Present is lost
const char* frame_source();           // "Present" / "game loop (...)" for info
void set_frame_source_mode(bool loop); // tests: true = ignore Present, frames from the game loop
void request_screenshot(const std::wstring& path, std::function<void(bool, std::string)> done);
void overlay_text(int x, int y, uint32_t argb, const char* utf8);
int world_to_screen(const float* w, float* out);
void screen_size(int* w, int* h);

// gamehooks.cpp
bool gamehooks_install();
uint32_t game_mode_id();
const char* game_mode_name(uint32_t id);
bool cheat_exec(const char* line);

// mods.cpp
void mods_load_all();
bool mod_reload(const std::string& name, std::string& err);
bool mod_unload(const std::string& name, std::string& err);
bool mod_load(const std::string& name, std::string& err);
std::vector<SkMod*> mods_list();
void mods_shutdown();
extern SkHost g_host;

// input.cpp
bool input_key(int vk, const char* mode);
bool input_text(const std::string& utf8);
bool input_click(int x, int y, int button);
bool input_drag(int x1, int y1, int x2, int y2, int button);
bool input_move(int x, int y);
// press or release a mouse button and keep it held (steering by holding the button: cell stage)
bool input_button(int x, int y, int button, bool down);
bool input_wheel(int x, int y, int notches); // notches > 0: away from the user (zoom in)

// uiproc.cpp
void* ui_winproc_new(SkMod* mod, SkUiMsgFn fn, void* user, int event_flags);
void ui_winproc_disable(void* proc);
void ui_winprocs_disable_mod(SkMod* mod);

// png.cpp
bool png_write_rgb(const std::wstring& path, int w, int h, const uint8_t* rgb);

// instance.cpp
void instance_install();

// probe.cpp
std::string probe_add(uintptr_t addr);
std::string probe_report(bool clear);
void probe_clear_all();
std::string ftrace_set(const std::string& substr);

// exceptions.cpp (more below)
void exceptions_pump(); // every frame: SK_EV_FAULT for a fault caught in SporeKit / mod code
bool exceptions_last(EXCEPTION_RECORD* er, CONTEXT* ctx, double* t, DWORD* tid = nullptr); // the last fault seen (copies)
bool exceptions_stack_overflow(EXCEPTION_RECORD* er, CONTEXT* ctx, DWORD* tid, double* t); // BUG-0056
// zip.cpp
struct ZipEntry {
    std::string name; // UTF-8, '/' separated
    std::vector<uint8_t> data;
};
bool zip_write(const std::wstring& path, const std::vector<ZipEntry>& entries); // stored entries
bool read_file_tail(const std::wstring& path, size_t cap, std::vector<uint8_t>& out);
// diag.cpp
std::string diag_write_bundle(const char* reason); // report + logs + crash files + settings as one .zip
std::string diag_write_bundle_with(const char* reason, const std::string& existing_report);
void diag_read_last_problem();  // at boot: the previous run's problem (logs\last-problem.txt)
const char* diag_last_problem(); // "<what>\n<zip path>" or ""
const char* build_name();     // "GOG 2024", "Steam (the GOG 2024 code)", ...
std::string diag_env();       // the environment, several lines
std::string foreign_hooks();  // overlays / wrappers / ModAPI loaded in the game
// fault_tid: the thread that faulted (0 = unknown: the current thread, as for hang reports)
std::string diag_write_report(const char* reason, EXCEPTION_POINTERS* ep, DWORD fault_tid = 0); // path of the .txt
std::string diag_logs_dir();
void diag_context(SkMod* mod, const char* (*fn)(void*), void* user);
void diag_context_remove(SkMod* mod);
void diag_install();          // the crash filter, ExitProcess log
void diag_after_device();     // GPU and other hooks into the log (once)
void diag_hang_report(const char* why); // the watchdog: no frames while focused (once per run)
void diag_crash_test(const char* where); // tests: "thread" (unhandled) | "main"
// watchdog.cpp
void watchdog_start();
void watchdog_mark(const char* stage);
std::string watchdog_dump_now();

// cheats.cpp
int cheat_register(const char* keyword, const char* devkit_prefix, const char* desc);
void cheats_flush_pending();
void cheats_register_builtin();

// exceptions.cpp
void exceptions_install();
std::string exceptions_report();
void exceptions_clear();

// window message used to run main-thread tasks when no frames are presented
#define WM_SK_PUMP (WM_APP + 0x5C0)
#define WM_SK_PUMP_OUT (WM_APP + 0x5C1)

// focus.cpp
void focus_install_hooks();
void focus_attach_window(void* hwnd);
bool background_mode();
void set_background_mode(bool on);
void vcursor_set(int x, int y); // client coords, activates the virtual cursor
void vcursor_tick();            // auto-release after inactivity
void vcursor_hold(bool on);     // a held button keeps the virtual cursor active
void place_test_window();
void apply_frame_limit();
void set_frame_limit(int ms);    // console: frame.limit <ms> (33 = the game's own, 0 = none)        // test runs: lift the game's 30 fps cap ([sporekit] test_frame_ms)       // test runs: move the game window to a secondary monitor

// fixes.cpp: fixes with a switch for tests (`kit.fix <name> 0|1`): 1 (default) = fixed, 0 = the old
// behaviour, so a test can show that it still catches the bug. Plain atomics: readable from an
// exception handler without locks or allocation.
struct KitFix {
    const char* name;
    const char* what;
    volatile LONG on;
};
extern KitFix g_fix_stackdump; // BUG-0050: copy a suspended thread's stack, resume, then format
extern KitFix g_fix_cheatwait; // BUG-0065: commands that wait for the main thread refuse to run on it
extern KitFix g_fix_reentry;   // BUG-0115: the fault logger and the log are not re-entered by a fault
extern KitFix g_fix_hookkeep;  // BUG-0132: removed hooks keep their trampolines, a grace before FreeLibrary
extern KitFix g_fix_stackov;   // BUG-0056: a stack overflow is reported by a thread that waits beforehand
extern KitFix g_fix_crashsafe; // BUG-0127: a failed step of a crash report does not end the game
extern KitFix g_fix_hookpark;  // BUG-0202: hooks switch a stub, the function's code is written once
extern KitFix g_fix_iniutf;    // BUG-0142: settings with non-ASCII text are kept in UTF-16
extern KitFix g_fix_iniencode; // BUG-0245: UTF-16 BE converted, unknown encodings left alone
extern KitFix g_fix_inilock;   // BUG-0245: a named mutex per settings file; no "????" writes
extern KitFix g_fix_exitwait;  // BUG-0243: the exit waits for an update's file swaps
extern KitFix g_fix_crashtid;  // BUG-0248: a crash report names the faulting thread
extern KitFix g_fix_logprune;  // BUG-0137: old crash reports, dumps, reports and archives are removed
inline bool fixed(const KitFix& f) { return f.on != 0; }
KitFix* kit_fix_find(const char* name);
std::vector<KitFix*> kit_fixes();

// per-thread flags (a TLS slot: no allocation, safe in an exception handler), BUG-0115
enum : uintptr_t {
    SK_TF_IN_LOG = 1, // inside the log's locked part
    SK_TF_IN_EXC = 2, // inside the exception logger or a holder of its lock
};
uintptr_t thread_flags();
void thread_flags_set(uintptr_t f, bool on);
struct ThreadFlag {
    uintptr_t f;
    bool was;
    explicit ThreadFlag(uintptr_t f_) : f(f_), was((thread_flags() & f_) != 0) { thread_flags_set(f, true); }
    ~ThreadFlag() { if (!was) thread_flags_set(f, false); }
};

// watchdog.cpp: a copy of a thread's registers and stack, taken while it is suspended for as short
// as possible (nothing that allocates or locks runs while it is suspended), BUG-0050
struct ThreadSnap {
    CONTEXT c;
    uint32_t esp;
    int n; // dwords copied from esp up
    uint32_t words[4096];
};
// suspends tid, copies, resumes; false (and why) for the calling thread itself or a failure.
// while_suspended: called before the resume (the old behaviour for kit.fix stackdump 0)
// (a plain function pointer: nothing may allocate on the way between the caller and the suspend)
bool thread_snapshot(DWORD tid, ThreadSnap& s, std::string* why, void (*while_suspended)(void*) = nullptr,
                     void* ctx = nullptr);
// calls a lambda through thread_snapshot's function pointer
template <class F> void call_lambda(void* f) { (*(F*)f)(); }
// test commands (kit.test ...): force the conditions of BUG-0050 / 0115 / 0132
void log_fault_test();
void exceptions_fault_test();
void hooks_churn_test(int rounds, int threads, std::string& out, bool nogate = false);
void* test_fault_page(); // a PAGE_NOACCESS page whose first access is repaired by a test handler

} // namespace sk

#define SKLOG(level, ...) ::sk::logf(&::sk::g_kit_mod, level, __VA_ARGS__)
