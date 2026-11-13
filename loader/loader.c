/*
 * SporeKit loader: proxy steam_api.dll.
 *
 * All Steam API exports are forwarded to steam_api_orig.dll by the .def file.
 * DllMain patches the exe entry point with a jump to our stub. The stub runs on
 * the main thread after every DLL is initialised (no loader lock), restores the
 * original bytes, loads sporekit.dll, calls sporekit_boot() and continues into
 * the original entry point.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../updater/pending.h"

typedef int(__cdecl *SporekitBootFn)(HMODULE loader);
typedef DWORD(WINAPI *EntryFn)(void *);

/* BUG-0851: the loader's tag as a line of its own: uninstall.bat (without the setup) finds it with
 * findstr, which reads a file line by line (in one long binary "line" Wine's findstr misses it) */
__attribute__((used)) static const char g_marker_line[] = "\r\nsporekit-loader (SporeCoop)\r\n";

static HMODULE g_self;
static BYTE *g_entry;
static BYTE g_saved[5];

static void dbg(const char *s)
{
    OutputDebugStringA(s);
}

static void restore_entry(void)
{
    DWORD old;
    VirtualProtect(g_entry, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_entry, g_saved, 5);
    VirtualProtect(g_entry, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entry, 5);
}

/* An update could not replace a file the game had open (Data\SporeCoop.package): it left
 * <file>.new and listed the file (relative to the game folder) in mods\sporecoop.pending.
 * Here, before the game opens anything, the new files take their place (updater/pending.h:
 * an entry that still cannot be swapped stays listed for the next start, BUG-0150). */
static void apply_pending(void)
{
    wchar_t root[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, root, MAX_PATH);
    /* BUG-1033: 0 (no path) or MAX_PATH (cut): no root to look the pending list up in (it would be the drive's) */
    if (n == 0 || n >= MAX_PATH) {
        dbg("[sporekit-loader] the game folder's path is too long or unknown: pending updates not applied\n");
        return;
    }
    int seps = 0;
    while (n > 0 && seps < 2) /* ...\SPORE\SporebinEP1\steam_api.dll -> ...\SPORE */
        if (root[--n] == L'\\' || root[n] == L'/')
            seps++;
    root[n] = 0;
    sc_apply_pending(root, 1);
}

static void boot_sporekit(void)
{
    char env[16];
    if (GetEnvironmentVariableA("SPOREKIT_DISABLE", env, sizeof env) && env[0] == '1') {
        dbg("[sporekit-loader] disabled by SPOREKIT_DISABLE=1\n");
        return;
    }
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    /* BUG-1033: a 0 return loaded "sporekit.dll" by the DLL search order; a path cut at MAX_PATH let the append below
     * write past path[] (the folder part can be 259 characters then) */
    if (n == 0 || n >= MAX_PATH) {
        dbg("[sporekit-loader] the game folder's path is too long or unknown, starting game without mods\n");
        return;
    }
    while (n > 0 && path[n - 1] != L'\\' && path[n - 1] != L'/')
        n--;
    if (n + 12 >= MAX_PATH) { /* "sporekit.dll" and the terminator must fit */
        dbg("[sporekit-loader] the game folder's path is too long, starting game without mods\n");
        return;
    }
    path[n] = 0;
    lstrcatW(path, L"sporekit.dll");
    HMODULE kit = LoadLibraryW(path);
    if (!kit) {
        dbg("[sporekit-loader] sporekit.dll not found, starting game without mods\n");
        return;
    }
    SporekitBootFn boot = (SporekitBootFn)(void *)GetProcAddress(kit, "sporekit_boot");
    if (!boot) {
        dbg("[sporekit-loader] sporekit_boot export missing\n");
        return;
    }
    boot(g_self);
}

static DWORD WINAPI entry_stub(void *peb)
{
    restore_entry();
    apply_pending();
    boot_sporekit();
    return ((EntryFn)(void *)g_entry)(peb);
}

static void patch_entry(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    g_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    DWORD old;
    if (!VirtualProtect(g_entry, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(g_saved, g_entry, 5);
    g_entry[0] = 0xE9;
    *(INT32 *)(g_entry + 1) = (INT32)((BYTE *)entry_stub - (g_entry + 5));
    VirtualProtect(g_entry, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entry, 5);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        patch_entry();
    }
    return TRUE;
}
