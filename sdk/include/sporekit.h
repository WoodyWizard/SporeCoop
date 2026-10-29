/*
 * SporeKit mod ABI (C). Stable, versioned interface between sporekit.dll and mods.
 *
 * A mod is a 32-bit DLL exporting:
 *     SK_EXPORT const SkModInfo* sporekit_mod_main(const SkHost* host, SkMod* self);
 * Everything that touches game objects must run on the game main thread:
 * event callbacks and (non ANY_THREAD) console commands already do.
 */
#ifndef SPOREKIT_H
#define SPOREKIT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SK_ABI_VERSION 1
#define SK_EXPORT __declspec(dllexport)

typedef struct SkMod SkMod; /* opaque handle owned by sporekit */

enum SkLogLevel { SK_DEBUG = 0, SK_INFO = 1, SK_WARN = 2, SK_ERROR = 3 };

enum SkEvent {
    SK_EV_APP_PREINIT = 0,  /* after App::cAppSystem::PreInit */
    SK_EV_APP_INIT = 1,     /* after App::cAppSystem::Init (game managers exist) */
    SK_EV_POST_INIT = 2,    /* after App::cSporeApp::Init (main menu ready) */
    SK_EV_FRAME = 3,        /* every frame, before Present. arg = float* dt seconds */
    SK_EV_OVERLAY = 4,      /* every frame, draw on top. arg = IDirect3DDevice9* */
    SK_EV_SHUTDOWN = 5,     /* App::cSporeApp::Shutdown */
    SK_EV_DEVICE_LOST = 6,  /* before IDirect3DDevice9::Reset */
    SK_EV_DEVICE_RESET = 7, /* after IDirect3DDevice9::Reset */
    SK_EV_FAULT = 8,        /* since 0.3.4: a fault inside SporeKit or a mod's DLL was caught (first chance);
                             * on the main thread at the next frame. arg = const char* "address module+offset" */
    SK_EV_COUNT
};

typedef void (*SkEventFn)(void* user, int event, void* arg);

/* Console command. args[0] is the command name. Write the reply into `out`
 * with host->out_printf. Return 0 on success, non-zero on error. */
typedef struct SkOut SkOut;
typedef int (*SkCommandFn)(void* user, int argc, const char* const* argv, SkOut* out);

enum SkCommandFlags {
    SK_CMD_MAIN_THREAD = 0, /* default: executed between frames on the main thread */
    SK_CMD_ANY_THREAD = 1,  /* executed on the console thread (must be thread safe) */
    SK_CMD_OUTSIDE_FRAME = 2, /* main thread, but outside IDirect3DDevice9::Present: for code that
                                 creates D3D resources (Spore UI windows, fonts, textures) */
    SK_CMD_NOT_ON_MAIN = 4,   /* with SK_CMD_ANY_THREAD: waits for frames or stops the main thread
                                 from outside; refused when called on the main thread (the cheat
                                 console, a mod's command_exec), where it would freeze the game */
};

typedef void (*SkTaskFn)(void* user);

/* Spore UI message handler: window = UTFWin::IWindow*, msg = const UTFWin::Message*.
 * Return non-zero when the message was handled. */
typedef int (*SkUiMsgFn)(void* user, void* window, const void* msg);

typedef struct SkHost {
    uint32_t abi_version;
    uint32_t size; /* sizeof(SkHost) for forward compatibility */

    /* logging */
    void (*log)(SkMod* mod, int level, const char* fmt, ...);
    void (*logv)(SkMod* mod, int level, const char* fmt, va_list ap);

    /* symbols: rebased absolute address, or 0 if unknown */
    uintptr_t (*sym)(const char* name);
    /* returns number of missing symbols, logs each missing name */
    int (*sym_require)(SkMod* mod, const char* const* names, int count);
    uintptr_t (*image_base)(void);  /* actual load base of SporeApp.exe */
    uintptr_t (*rebase)(uintptr_t va); /* VA for base 0x400000 -> actual */

    /* hooks (MinHook). Tracked per mod, removed automatically on unload. */
    int (*hook_create)(SkMod* mod, void* target, void* detour, void** original);
    int (*hook_enable)(SkMod* mod, void* target, int enable);
    int (*hook_remove)(SkMod* mod, void* target);

    /* events */
    int (*subscribe)(SkMod* mod, int event, SkEventFn fn, void* user);
    void (*unsubscribe)(SkMod* mod, int event, SkEventFn fn, void* user);

    /* console commands */
    int (*command_add)(SkMod* mod, const char* name, const char* help, SkCommandFn fn,
                       void* user, int flags);
    void (*out_printf)(SkOut* out, const char* fmt, ...);
    /* execute a console command line as if typed into the devkit console
     * (main thread only). Result text is appended to `out` (may be NULL). */
    int (*command_exec)(const char* line, SkOut* out);

    /* main thread task queue (callable from any thread) */
    void (*run_on_main)(SkTaskFn fn, void* user);

    /* config: mods/<modname>.ini */
    int (*config_int)(SkMod* mod, const char* section, const char* key, int def);
    void (*config_str)(SkMod* mod, const char* section, const char* key, const char* def,
                       char* buf, int bufsize);

    /* environment */
    void* (*d3d_device)(void);    /* IDirect3DDevice9* or NULL */
    void* (*game_window)(void);   /* HWND or NULL */
    uint64_t (*frame_index)(void);
    double (*time_seconds)(void); /* monotonic */
    int (*instance_id)(void);     /* SPOREKIT_INSTANCE, default 1 */
    const char* (*mods_dir)(void);/* Windows path to SporebinEP1\mods, no trailing slash */

    /* overlay text (drawn by sporekit with D3DX font). Coordinates in pixels,
     * color ARGB. Valid only during SK_EV_OVERLAY. */
    void (*overlay_text)(int x, int y, uint32_t argb, const char* utf8);
    /* project world position to screen; returns 0 if behind camera */
    int (*world_to_screen)(const float* world3, float* out_xy);

    /* hex SHA-256 of SporeApp.exe (identifies the game build) */
    const char* (*exe_sha256)(void);

    /* Spore cheat console alias: typing "<keyword> <sub> args" in the game console
     * runs the devkit command "<devkit_prefix><sub> args" (e.g. "coop host" ->
     * "coop.host"). Output is printed to the game console. */
    int (*cheat_register)(const char* keyword, const char* devkit_prefix, const char* description);

    /* back buffer size in pixels (overlay coordinates) */
    void (*screen_size)(int* w, int* h);

    /* ---- since 0.3.0 (check size) ---- */
    /* run on the main thread outside Present (see SK_CMD_OUTSIDE_FRAME); callable anywhere */
    void (*run_outside_frame)(SkTaskFn fn, void* user);
    /* write mods/<modname>.ini (UTF-8 value) */
    int (*config_set_str)(SkMod* mod, const char* section, const char* key, const char* value);
    /* a UTFWin::IWinProc (reference count 1, owned by the caller) that calls fn; pass it to
     * IWindow::AddWinProc. event_flags 0 = basic input | advanced (button clicks).
     * The handler lives in sporekit: when the mod unloads it stays valid and does nothing. */
    void* (*ui_winproc)(SkMod* mod, SkUiMsgFn fn, void* user, int event_flags);
    void (*ui_winproc_disable)(void* proc);

    /* ---- since 0.3.1 (check size) ---- */
    /* hex SHA-256 of the build whose symbol files are loaded: exe_sha256, or the build with the
     * same code (code_sha256) when no symbol file names this exact exe; "" without symbols */
    const char* (*symbols_build)(void);

    /* ---- since 0.3.4 (check size) ---- */
    const char* (*kit_version)(void);
    const char* (*build_name)(void); /* "GOG 2024", "Steam (the GOG 2024 code)", "GOG 2016", "unknown build ..." */
    /* writes mods\logs\SporeCoop-report-<time>.zip: a fresh report-<time>.txt (environment, every
     * mod's diag_context, faults, modules, the last log lines), the current and previous logs, the
     * newest crash reports with minidumps, the settings; its path into out. Returns the path
     * length. Main thread. */
    int (*diag_report)(const char* reason, char* out, int cap);
    /* a text block of the mod's own state for crash and diag reports; fn's string stays valid until
     * its next call. fn NULL removes it. Removed automatically when the mod unloads. */
    void (*diag_context)(SkMod* mod, const char* (*fn)(void* user), void* user);
    const char* (*logs_dir)(void); /* Windows path of mods\logs */
    /* the previous run's problem: "<what>\n<path of its SporeCoop-report .zip>" ("crashed", "stopped
     * responding", ...), "" when it ended well. Told once: the mark is removed at this boot. */
    const char* (*last_problem)(void);

    /* ---- since 0.3.5 (check size) ---- */
    /* the session clock of every log line (a co-op session's common time): on != 0: each line
     * carries "[s <time_seconds() + offset>]" after the local time; on == 0: no session clock */
    void (*log_session_clock)(double offset, int on);
    /* a text block put at the top of reports, right after their title (a session summary); NULL or
     * "" removes it. Copied. Removed automatically when the mod unloads. */
    void (*diag_headline)(SkMod* mod, const char* text);
    /* the game files part of the environment: other mods, extra .package files in the game's Data
     * folders and the player's, known files whose size is not the build's (several lines) */
    const char* (*diag_files)(void);
} SkHost;

typedef struct SkModInfo {
    uint32_t abi_version; /* SK_ABI_VERSION */
    const char* name;
    const char* version;
    /* called before the DLL is unloaded (hot reload or shutdown) */
    void (*on_unload)(void);
} SkModInfo;

typedef const SkModInfo* (*SkModMainFn)(const SkHost* host, SkMod* self);

#ifdef __cplusplus
}
#endif
#endif /* SPOREKIT_H */
