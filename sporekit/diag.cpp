// Diagnostics for players' bug reports (docs/02-modkit.md 2.7):
//  - the environment: SporeKit / game build, Windows or Wine, memory (Spore is a 32-bit game: its
//    address space is the usual limit), the exe's large-address flag (4GB patch), the GPU, other
//    DLLs that hook the game (overlays, wrappers, ModAPI)
//  - a crash report when the game dies of an exception nobody handles: logs/crash-<time>.txt
//    (exception, registers, the call chain with symbol names, every module, the last 300 log lines,
//    the first-chance faults, the mods' own state) and a minidump crash-<time>.dmp next to it
//  - a report on request (console `report`, the co-op window): the same without the exception
//  - ExitProcess is logged (a game that closes itself leaves its exit code in the log)
#include "core.h"
#include "inifile.h"
#include "../updater/pending.h"

#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <new>
#include <stdexcept>

namespace sk {

// ---------------- the game build ----------------
const char* build_name()
{
    static std::string name;
    std::string b = symbols_build(), e = exe_sha256();
    if (b.rfind("922af7c7", 0) == 0)
        name = "GOG 2016";
    else if (b.rfind("dc04aee5", 0) == 0)
        name = e.rfind("dc04aee5", 0) == 0   ? "GOG 2024"
               : e.rfind("3010a413", 0) == 0 ? "Steam (the GOG 2024 code)"
                                             : "the GOG 2024 code (" + e.substr(0, 8) + ")";
    else if (symbols_pending())
        name = "a packed exe (waiting for its code)";
    else
        name = "unknown build " + e.substr(0, 8) + " (co-op off)";
    return name.c_str();
}

// ---------------- the environment ----------------
static std::string os_text()
{
    char b[256];
    typedef LONG(WINAPI * RtlGetVersion_t)(OSVERSIONINFOW*);
    typedef const char*(CDECL * wine_get_version_t)();
    typedef void(CDECL * wine_get_host_version_t)(const char**, const char**);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (auto f = nt ? (RtlGetVersion_t)(void*)GetProcAddress(nt, "RtlGetVersion") : nullptr)
        f(&v);
    auto wv = nt ? (wine_get_version_t)(void*)GetProcAddress(nt, "wine_get_version") : nullptr;
    if (wv) {
        const char *sys = "?", *rel = "?";
        if (auto hv = (wine_get_host_version_t)(void*)GetProcAddress(nt, "wine_get_host_version"))
            hv(&sys, &rel);
        snprintf(b, sizeof b, "Wine %s on %s %s (Windows %lu.%lu.%lu emulated)", wv(), sys, rel, v.dwMajorVersion,
                 v.dwMinorVersion, v.dwBuildNumber);
    } else
        snprintf(b, sizeof b, "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    return b;
}

static std::string memory_text()
{
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    GlobalMemoryStatusEx(&m);
    auto* base = (uint8_t*)GetModuleHandleW(nullptr);
    auto* nt = (IMAGE_NT_HEADERS32*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    bool laa = (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
    char b[256];
    snprintf(b, sizeof b, "RAM %llu MB (%llu free), game address space %llu of %llu MB used, large-address flag %s",
             m.ullTotalPhys >> 20, m.ullAvailPhys >> 20, (m.ullTotalVirtual - m.ullAvailVirtual) >> 20,
             m.ullTotalVirtual >> 20, laa ? "on (4GB patch)" : "off");
    return b;
}

static std::string g_gpu; // read once on the main thread (a report from another thread must not call D3D)

static std::string gpu_text()
{
    if (!g_gpu.empty())
        return g_gpu;
    auto* dev = (IDirect3DDevice9*)d3d_device();
    if (!dev)
        return "GPU: no device yet";
    IDirect3D9* d3d = nullptr;
    D3DADAPTER_IDENTIFIER9 id{};
    if (FAILED(dev->GetDirect3D(&d3d)) || !d3d)
        return "GPU: ?";
    HRESULT hr = d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    d3d->Release();
    if (FAILED(hr))
        return "GPU: ?";
    int w = 0, h = 0;
    screen_size(&w, &h);
    char b[512];
    snprintf(b, sizeof b, "GPU %s (driver %s %u.%u.%u.%u), %dx%d", id.Description, id.Driver,
             HIWORD(id.DriverVersion.HighPart), LOWORD(id.DriverVersion.HighPart), HIWORD(id.DriverVersion.LowPart),
             LOWORD(id.DriverVersion.LowPart), w, h);
    if (GetCurrentThreadId() == main_thread_id())
        g_gpu = b;
    return b;
}

struct Module {
    std::string name, path;
    uintptr_t base;
    uint32_t size;
};
static std::vector<Module> modules()
{
    std::vector<Module> v;
    typedef BOOL(WINAPI * Enum_t)(HANDLE, HMODULE*, DWORD, LPDWORD);
    typedef BOOL(WINAPI * Info_t)(HANDLE, HMODULE, void*, DWORD);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    auto en = (Enum_t)(void*)GetProcAddress(k, "K32EnumProcessModules");
    auto inf = (Info_t)(void*)GetProcAddress(k, "K32GetModuleInformation");
    if (!en || !inf)
        return v;
    HMODULE mods[512];
    DWORD need = 0;
    if (!en(GetCurrentProcess(), mods, sizeof mods, &need))
        return v;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 512; i++) {
        wchar_t p[MAX_PATH];
        GetModuleFileNameW(mods[i], p, MAX_PATH);
        struct {
            void* base;
            DWORD size;
            void* entry;
        } mi{};
        inf(GetCurrentProcess(), mods[i], &mi, sizeof mi);
        std::string path = narrow(p);
        v.push_back({path.substr(path.find_last_of("\\/") + 1), path, (uintptr_t)mi.base, (uint32_t)mi.size});
    }
    return v;
}

static std::string lower(std::string s)
{
    for (auto& c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

// DLLs of other programs that hook the game (the usual suspects of crashes and lost frames)
std::string foreign_hooks()
{
    static const struct {
        const char* dll;
        const char* what;
    } known[] = {{"gameoverlayrenderer", "Steam overlay"},  {"discordhook", "Discord overlay"},
                 {"discord_hook", "Discord overlay"},       {"rtsshooks", "RivaTuner / MSI Afterburner"},
                 {"nvspcap", "NVIDIA overlay"},             {"igo32", "EA / Origin overlay"},
                 {"reshade", "ReShade"},                    {"dgvoodoo", "dgVoodoo"},
                 {"modapi", "Spore ModAPI"},                {"sporemodapi", "Spore ModAPI"},
                 {"fraps", "Fraps"},                        {"obs-", "OBS capture"},
                 {"graphics-hook", "OBS capture"}};
    std::string out;
    std::string game = lower(narrow(game_dir()));
    for (auto& m : modules()) {
        std::string n = lower(m.name), p = lower(m.path);
        for (auto& k : known)
            if (n.find(k.dll) != std::string::npos && out.find(k.what) == std::string::npos)
                out += std::string(out.empty() ? "" : ", ") + k.what;
        // a d3d9 / dxgi / dinput8 from the game folder: a wrapper (DXVK, ReShade, dgVoodoo, ...)
        if ((n == "d3d9.dll" || n == "dxgi.dll" || n == "dinput8.dll" || n == "d3d11.dll") &&
            p.rfind(game, 0) == 0)
            out += std::string(out.empty() ? "" : ", ") + m.name + " from the game folder";
    }
    return out.empty() ? "none found" : out;
}

std::string diag_env()
{
    std::string s;
    s += "SporeKit " SK_VERSION ", game " + std::string(build_name()) + " (exe " + exe_sha256().substr(0, 16) +
         "), frames from " + frame_source() + "\n";
    s += os_text() + "\n";
    // which game wrote it (BUG-0249: the lab tells a test game on lane 1 from the owner's own Spore);
    // only a flag for the profile, never its name, no path
    s += "run: instance " + std::to_string(instance_id()) + ", profile " +
         (wcsstr(GetCommandLineW(), L"-sporekit-profile:") ? "set" : "own") + ", pid " +
         std::to_string(GetCurrentProcessId()) + "\n";
    s += memory_text() + "\n";
    s += (g_gpu.empty() ? std::string("GPU: not read yet") : g_gpu) + "\n";
    s += "other hooks: " + foreign_hooks() + "\n";
    s += "game mode: " + std::string(game_mode_name(game_mode_id())) + ", frame " + std::to_string(frame_index()) +
         ", up " + std::to_string((int)now_seconds()) + " s\n";
    std::string mods;
    for (SkMod* m : mods_list())
        mods += m->name + (m->info && m->info->version ? std::string(" ") + m->info->version : "") + "  ";
    s += "mods: " + (mods.empty() ? std::string("none") : mods) + "\n";
    return s;
}

// ---------------- the mods' own state for reports ----------------
struct Ctx {
    SkMod* mod;
    const char* (*fn)(void*);
    void* user;
};
static std::mutex g_ctx_mu;
static std::vector<Ctx> g_ctx;

void diag_context(SkMod* mod, const char* (*fn)(void*), void* user)
{
    std::lock_guard<std::mutex> lk(g_ctx_mu);
    for (auto it = g_ctx.begin(); it != g_ctx.end();)
        it = it->mod == mod ? g_ctx.erase(it) : std::next(it);
    if (fn)
        g_ctx.push_back({mod, fn, user});
}

void diag_context_remove(SkMod* mod) { diag_context(mod, nullptr, nullptr); }

// a block at the top of reports (a mod's session summary), per mod
static std::vector<std::pair<SkMod*, std::string>> g_headlines; // under g_ctx_mu

void diag_headline(SkMod* mod, const char* text)
{
    std::lock_guard<std::mutex> lk(g_ctx_mu);
    for (auto it = g_headlines.begin(); it != g_headlines.end();)
        it = it->first == mod ? g_headlines.erase(it) : std::next(it);
    if (text && *text)
        g_headlines.push_back({mod, text});
}

std::string diag_headline_text()
{
    std::lock_guard<std::mutex> lk(g_ctx_mu);
    std::string s;
    for (auto& [m, t] : g_headlines)
        s += t + (t.empty() || t.back() == '\n' ? "" : "\n");
    return s;
}

// ---------------- reports ----------------
static std::string where(uintptr_t a)
{
    std::string s = symbols_reverse(a);
    if (!s.empty())
        return "SporeApp.exe " + s;
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)a, &m) &&
        m) {
        wchar_t n[MAX_PATH];
        GetModuleFileNameW(m, n, MAX_PATH);
        std::string p = narrow(n);
        char off[24];
        snprintf(off, sizeof off, "+0x%X", (unsigned)(a - (uintptr_t)m));
        return p.substr(p.find_last_of("\\/") + 1) + off;
    }
    return "?";
}

static bool readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

// executable code of a loaded module just before v: a likely return address
static bool code_return(uint32_t v)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (v < 0x10000 || !VirtualQuery((void*)(uintptr_t)v, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        mbi.Type != MEM_IMAGE || !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return false;
    const uint8_t* c = (const uint8_t*)(uintptr_t)v;
    if (!readable(c - 7, 7))
        return false;
    return c[-5] == 0xE8 || (c[-6] == 0xFF && (c[-5] & 0x38) == 0x10) || (c[-2] == 0xFF && (c[-1] & 0xF8) == 0xD0) ||
           (c[-3] == 0xFF && (c[-2] & 0x38) == 0x10) || (c[-7] == 0xFF && (c[-6] & 0x38) == 0x10);
}

static std::string stamp()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[32];
    snprintf(b, sizeof b, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

static const char* code_name(DWORD c)
{
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case 0xC0000409: return "stack buffer overrun / fast fail";
    case 0xC0000374: return "heap corruption";
    case 0xE06D7363: return "C++ exception";
    case 0x80000003: return "breakpoint";
    default: return "exception";
    }
}

static std::wstring logs_dir() { return mods_dirw() + L"\\logs"; }

std::string diag_logs_dir() { return narrow(logs_dir()); }

static void write_text(const std::wstring& path, const std::string& s)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD w = 0;
    WriteFile(f, s.data(), (DWORD)s.size(), &w, nullptr);
    CloseHandle(f);
}

// tid: the thread that faulted (the crash report is written by a thread of its own)
static bool write_minidump(const std::wstring& path, EXCEPTION_POINTERS* ep, DWORD tid)
{
    typedef BOOL(WINAPI * Dump_t)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
    HMODULE dh = LoadLibraryW(L"dbghelp.dll");
    auto dump = dh ? (Dump_t)(void*)GetProcAddress(dh, "MiniDumpWriteDump") : nullptr;
    if (!dump)
        return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    struct {
        DWORD tid;
        EXCEPTION_POINTERS* ep;
        BOOL client;
    } info{tid, ep, FALSE};
    // MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory: small, with the stacks' targets
    BOOL ok = dump(GetCurrentProcess(), GetCurrentProcessId(), f, 0x40 | 0x10, ep ? &info : nullptr, nullptr, nullptr);
    CloseHandle(f);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok != FALSE;
}

static std::string report_base(bool crash)
{
    return std::string(crash ? "crash" : "report") + "-" + stamp() +
           (instance_id() > 1 ? "-i" + std::to_string(instance_id()) : "");
}

static std::string diag_write_report_as(const std::string& base, const char* reason, EXCEPTION_POINTERS* ep,
                                        bool dump = true, DWORD fault_tid = 0);

std::string diag_write_report(const char* reason, EXCEPTION_POINTERS* ep, DWORD fault_tid)
{
    return diag_write_report_as(report_base(ep != nullptr), reason, ep, true, fault_tid);
}

// BUG-0248: the report is written by a thread of its own (or the exiting thread): the exception line
// names the thread that faulted. 0 / kit.fix crashtid 0: the current thread, as before
static DWORD fault_thread(DWORD fault_tid) { return fault_tid && fixed(g_fix_crashtid) ? fault_tid : GetCurrentThreadId(); }

static std::string diag_write_report_as(const std::string& base, const char* reason, EXCEPTION_POINTERS* ep, bool dump,
                                        DWORD fault_tid)
{
    std::string kind = ep ? "crash" : "report";
    CreateDirectoryW(logs_dir().c_str(), nullptr);
    std::wstring txt = logs_dir() + L"\\" + widen(base) + L".txt";
    std::string s;
    s += "SporeCoop " + kind + " " + stamp() + (reason && *reason ? std::string(" (") + reason + ")" : "") + "\n";
    s += "Please attach this file (and the .dmp next to it, if there is one) to an issue:\n";
    s += "https://github.com/WoodyWizard/SporeCoop/issues\n\n";
    if (std::string h = diag_headline_text(); !h.empty())
        s += "== Summary\n" + h + "\n";
    s += "== Environment\n" + diag_env() + diag_files_text() + "\n";
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        EXCEPTION_RECORD* er = ep->ExceptionRecord;
        CONTEXT* c = ep->ContextRecord;
        char b[512];
        s += "== Exception\n";
        DWORD ft = fault_thread(fault_tid);
        snprintf(b, sizeof b, "0x%08lX %s at 0x%08X %s (thread %lu%s)\n", er->ExceptionCode,
                 code_name(er->ExceptionCode), (unsigned)c->Eip, where(c->Eip).c_str(), ft,
                 ft == main_thread_id() ? ", the game's main thread" : "");
        s += b;
        if (ft != GetCurrentThreadId()) {
            snprintf(b, sizeof b, "report written by thread %lu\n", GetCurrentThreadId());
            s += b;
        }
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
            snprintf(b, sizeof b, "%s of address 0x%08X\n",
                     er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "execute",
                     (unsigned)er->ExceptionInformation[1]);
            s += b;
        }
        snprintf(b, sizeof b, "eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX\n\n",
                 c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        s += b;
        s += "== Call chain (frame pointers, then return addresses found on the stack)\n";
        uintptr_t fp = c->Ebp;
        for (int i = 0; i < 24 && readable((void*)fp, 8); i++) {
            uintptr_t ret = ((uintptr_t*)fp)[1], next = ((uintptr_t*)fp)[0];
            if (!ret)
                break;
            snprintf(b, sizeof b, "  %08X %s\n", (unsigned)ret, where(ret).c_str());
            s += b;
            if (next <= fp)
                break;
            fp = next;
        }
        s += "  -- stack scan:\n";
        const uint32_t* sp = (const uint32_t*)(uintptr_t)c->Esp;
        int n = 0;
        for (int i = 0; i < 2048 && n < 40 && readable(sp + i, 4); i++)
            if (code_return(sp[i])) {
                snprintf(b, sizeof b, "  [esp+%03X] %08X %s\n", i * 4, sp[i], where(sp[i]).c_str());
                s += b;
                n++;
            }
        s += "\n";
    }
    {
        std::lock_guard<std::mutex> lk(g_ctx_mu);
        for (auto& c : g_ctx) {
            const char* t = c.fn ? c.fn(c.user) : nullptr;
            s += "== " + c.mod->name + "\n" + (t ? t : "") + "\n";
        }
    }
    s += "== Faults caught before (first chance; most are harmless and handled by the game)\n" +
         exceptions_report() + "\n";
    s += "== Modules\n";
    for (auto& m : modules()) {
        char b[600];
        snprintf(b, sizeof b, "  %08X %8X %s\n", (unsigned)m.base, m.size, m.path.c_str());
        s += b;
    }
    s += "\n== The last log lines\n";
    for (auto& l : log_tail(300))
        s += l + "\n";
    write_text(txt, mask_paths(s)); // no private path in full (the module list, a mod's state)
    if (ep && dump) {
        // the fault's thread when it still exists (the exit report runs on the exiting thread)
        DWORD dt = GetCurrentThreadId();
        if (fault_tid && fixed(g_fix_crashtid))
            if (HANDLE th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, fault_tid)) {
                CloseHandle(th);
                dt = fault_tid;
            }
        write_minidump(logs_dir() + L"\\" + widen(base) + L".dmp", ep, dt);
    }
    return narrow(txt);
}

// ---------------- the archive for a bug report ----------------
// One file a player can send: SporeCoop-report-<time>.zip in mods\logs with a fresh report, the
// current logs (every instance), the newest crash reports with their minidumps, the newest hang /
// problem reports, the logs of the previous runs and the mod settings (the last joined address is
// hidden). About 24 MB at most (GitHub takes 25 MB attachments): the older files go first.
static constexpr size_t kBundleMax = 24u << 20, kLogCap = 8u << 20;

static std::vector<std::wstring> files_in(const std::wstring& dir, const wchar_t* pattern)
{
    std::vector<std::wstring> v;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return v;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            v.push_back(fd.cFileName);
    while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end()); // names carry the time: oldest first
    return v;
}

// a settings file without the address of the last host joined (a friend's IP)
static std::vector<uint8_t> redacted_ini(const std::vector<uint8_t>& d)
{
    std::string s = skini::to_utf8_text(std::string(d.begin(), d.end())), out; // UTF-16 since BUG-0142
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find('\n', i);
        std::string line = s.substr(i, e == std::string::npos ? std::string::npos : e - i + 1);
        if (line.rfind("last_address=", 0) == 0)
            line = "last_address=(hidden in reports)\n";
        out += line;
        if (e == std::string::npos)
            break;
        i = e + 1;
    }
    return std::vector<uint8_t>(out.begin(), out.end());
}

// the archive of a problem the game had (a crash, a hang, a fault before the game ended): named in
// logs\last-problem.txt so the next start tells the player where it is
static void mark_problem(const std::string& zip, const char* what)
{
    write_text(logs_dir() + L"\\last-problem.txt", std::string(what) + "\n" + zip + "\n");
}

static std::string g_last_problem; // read at boot: "<what>\n<zip path>" of the previous run, or ""

// Only the newest files of each kind stay in mods\logs (BUG-0137): a crash on every start wrote a
// report, a minidump and an archive of up to 24 MB per start and nothing removed them. Names carry
// the time, so the name order is the age. The files named in last-problem.txt (this run's or the
// previous run's, which the player was told about) are never removed.
static constexpr size_t kKeepCrashes = 5, kKeepReports = 5, kKeepArchives = 3;

static std::wstring file_name_of(const std::string& path)
{
    std::wstring w = widen(path);
    size_t k = w.find_last_of(L"\\/");
    return k == std::wstring::npos ? w : w.substr(k + 1);
}

void diag_prune_logs()
{
    if (!fixed(g_fix_logprune))
        return;
    std::wstring dir = logs_dir();
    std::vector<std::wstring> keep;
    auto keep_lines = [&](const std::string& t) {
        size_t i = 0;
        while (i < t.size()) {
            size_t e = t.find('\n', i);
            std::string l = t.substr(i, e == std::string::npos ? std::string::npos : e - i);
            while (!l.empty() && (l.back() == '\r' || l.back() == ' '))
                l.pop_back();
            if (!l.empty())
                keep.push_back(file_name_of(l));
            if (e == std::string::npos)
                break;
            i = e + 1;
        }
    };
    keep_lines(g_last_problem);
    std::vector<uint8_t> d;
    if (read_file_tail(dir + L"\\last-problem.txt", 4096, d))
        keep_lines(std::string(d.begin(), d.end()));
    int removed = 0;
    auto prune = [&](const wchar_t* pattern, size_t n) {
        std::vector<std::wstring> v = files_in(dir, pattern); // oldest first
        for (size_t i = 0; i + n < v.size(); i++) {
            bool kept = false;
            for (auto& k : keep)
                kept |= _wcsicmp(k.c_str(), v[i].c_str()) == 0;
            if (!kept && DeleteFileW((dir + L"\\" + v[i]).c_str()))
                removed++;
        }
    };
    prune(L"crash-*.txt", kKeepCrashes);
    prune(L"crash-*.dmp", kKeepCrashes);
    prune(L"report-*.txt", kKeepReports);
    prune(L"SporeCoop-report-*.zip", kKeepArchives);
    if (removed)
        logf(&g_kit_mod, SK_INFO, "logs: %d old crash / report files removed (kept the newest %u crashes, %u reports, %u archives)",
             removed, (unsigned)kKeepCrashes, (unsigned)kKeepReports, (unsigned)kKeepArchives);
}

void diag_read_last_problem()
{
    std::vector<uint8_t> d;
    std::wstring m = logs_dir() + L"\\last-problem.txt";
    if (!read_file_tail(m, 4096, d) || d.empty())
        return;
    g_last_problem.assign(d.begin(), d.end());
    while (!g_last_problem.empty() && (g_last_problem.back() == '\n' || g_last_problem.back() == '\r'))
        g_last_problem.pop_back();
    DeleteFileW(m.c_str()); // told once
    logf(&g_kit_mod, SK_WARN, "the previous run had a problem: %s", g_last_problem.c_str());
}

const char* diag_last_problem() { return g_last_problem.c_str(); }

std::string diag_write_bundle(const char* reason) { return diag_write_bundle_with(reason, ""); }

// existing: a report already written (a crash report): it is the archive's report
std::string diag_write_bundle_with(const char* reason, const std::string& existing)
{
    std::string report = existing.empty() ? diag_write_report(reason, nullptr) : existing;
    std::wstring dir = logs_dir();
    std::vector<ZipEntry> z;
    size_t total = 0;
    std::vector<std::string> left_out;
    auto add = [&](const std::wstring& full, const std::string& name, size_t cap, bool optional) {
        std::vector<uint8_t> d;
        if (!read_file_tail(full, cap, d))
            return;
        if (optional && total + d.size() > kBundleMax) {
            left_out.push_back(name);
            return;
        }
        total += d.size();
        z.push_back({name, std::move(d)});
    };
    std::string stamp_now = stamp();
    std::string readme =
        "SporeCoop problem report " + stamp_now + "\n"
        "Please attach this .zip to an issue: https://github.com/WoodyWizard/SporeCoop/issues\n"
        "and say what you did when the problem happened.\n\n"
        "Inside:\n"
        "  report-*.txt      the latest co-op session summary, the game build, Windows / Wine, memory, graphics\n"
        "                    card, other overlays, mods and extra game files, the co-op session, faults,\n"
        "                    loaded modules, the last log lines\n"
        "  (maintainers: docs/REPORTING.md#reporting-support says where each answer is; tools/merge_logs.py lines up the logs\n"
        "   of all players by their session clock)\n"
        "  logs/             the logs of this run (sporekit.log; sporekit.N.log of other instances)\n"
        "  crashes/          crash reports and minidumps (.dmp: memory of the crashed game, no files of yours)\n"
        "  reports/          earlier reports (hangs, problems)\n"
        "  previous/         the logs of the last runs\n"
        "  settings/         mods\\*.ini (your player name is in coop.ini; the last address you joined is hidden)\n";
    z.push_back({"README.txt", std::vector<uint8_t>(readme.begin(), readme.end())});
    std::wstring rep_w = widen(report);
    std::wstring rep_name = rep_w.substr(rep_w.find_last_of(L"\\/") + 1);
    add(rep_w, narrow(rep_name), kLogCap, false);
    for (auto& f : files_in(dir, L"sporekit*.log"))
        add(dir + L"\\" + f, "logs/" + narrow(f), kLogCap, false);
    auto newest = [](std::vector<std::wstring> v, size_t n) {
        if (v.size() > n)
            v.erase(v.begin(), v.end() - (ptrdiff_t)n);
        std::reverse(v.begin(), v.end()); // newest first: they win the space
        return v;
    };
    for (auto& f : newest(files_in(dir, L"crash-*.txt"), 3)) {
        if (f != rep_name)
            add(dir + L"\\" + f, "crashes/" + narrow(f), kLogCap, true);
        std::wstring dmp = f.substr(0, f.size() - 4) + L".dmp";
        add(dir + L"\\" + dmp, "crashes/" + narrow(dmp), kBundleMax, true);
    }
    std::vector<std::wstring> reps;
    for (auto& f : files_in(dir, L"report-*.txt"))
        if (f != rep_name)
            reps.push_back(f);
    for (auto& f : newest(reps, 3))
        add(dir + L"\\" + f, "reports/" + narrow(f), kLogCap, true);
    std::wstring mods = mods_dirw();
    for (auto& f : files_in(mods, L"*.ini")) {
        std::vector<uint8_t> d;
        if (read_file_tail(mods + L"\\" + f, 1u << 20, d)) {
            total += d.size();
            z.push_back({"settings/" + narrow(f), redacted_ini(d)});
        }
    }
    for (auto& f : newest(files_in(dir + L"\\previous", L"*.log"), 6))
        add(dir + L"\\previous\\" + f, "previous/" + narrow(f), kLogCap, true);
    if (!left_out.empty()) {
        std::string s = "Left out (the archive would be over 24 MB):\n";
        for (auto& n : left_out)
            s += "  " + n + "\n";
        z.push_back({"LEFT_OUT.txt", std::vector<uint8_t>(s.begin(), s.end())});
    }
    std::wstring zip = dir + L"\\SporeCoop-report-" + widen(stamp_now) +
                       (instance_id() > 1 ? L"-i" + std::to_wstring(instance_id()) : L"") + L".zip";
    if (!zip_write(zip, z)) {
        logf(&g_kit_mod, SK_ERROR, "report archive: cannot write %s", narrow(zip).c_str());
        return report;
    }
    logf(&g_kit_mod, SK_INFO, "report archive: %s (%u files, %u KB)", narrow(zip).c_str(), (unsigned)z.size(),
         (unsigned)(total >> 10));
    z.clear();
    diag_prune_logs();
    return narrow(zip);
}

// ---------------- the unhandled-exception filter ----------------
// SporeKit's filter stays on top: a later SetUnhandledExceptionFilter (the game's, the CRT's, or
// the NULL of __report_gsfailure) becomes the next filter in the chain instead of replacing it
static LPTOP_LEVEL_EXCEPTION_FILTER g_next_filter;
static std::atomic<int> g_crashed{0};
using SetUEF_t = LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER);
static SetUEF_t o_SetUEF_k32, o_SetUEF_kb;
using ExitProcess_t = void(WINAPI*)(UINT);
static ExitProcess_t o_ExitProcess;
using TerminateProcess_t = BOOL(WINAPI*)(HANDLE, UINT);
static TerminateProcess_t o_TerminateProcess;

// BUG-0127: a crash report runs in a process that may have crashed because memory ran out. Each
// step catches its own failure (a std::bad_alloc that left the thread ended the game through
// std::terminate before the minidump and the mark were written); the next step still runs.
static std::atomic<int> g_test_oom{0}; // crash.test oom: the archive fails like an allocation would

template <class F> static bool report_step(const char* what, F f)
{
    if (!fixed(g_fix_crashsafe)) {
        f();
        return true;
    }
    try {
        f();
        return true;
    } catch (const std::exception& e) {
        try {
            logf(&g_kit_mod, SK_ERROR, "problem report: %s failed (%s), the next step goes on", what, e.what());
        } catch (...) {
        }
    } catch (...) {
        try {
            logf(&g_kit_mod, SK_ERROR, "problem report: %s failed, the next step goes on", what);
        } catch (...) {
        }
    }
    return false;
}

static std::string bundle_for_problem(const char* why, const std::string& report)
{
    if (g_test_oom.load())
        throw std::bad_alloc();
    return diag_write_bundle_with(why, report);
}

// the game ends: after a fault of the last kFaultExitSec (its own handler caught it and closes the
// game, the unhandled filter never runs) or with an error code, a crash report of that fault
static constexpr double kFaultExitSec = 15.0;
static void exit_report(UINT code, const char* how)
{
    if (g_crashed.load())
        return;
    EXCEPTION_RECORD er;
    CONTEXT ctx;
    double t = -1;
    DWORD ftid = 0;
    bool fault = exceptions_last(&er, &ctx, &t, &ftid) && now_seconds() - t < kFaultExitSec;
    if (!fault && !code)
        return;
    g_crashed++;
    char why[160];
    snprintf(why, sizeof why, "the game ends (%s %u)%s", how, code,
             fault ? ", seconds after a fault: the report shows that fault" : "");
    EXCEPTION_POINTERS ep{&er, &ctx};
    const char* what = fault ? "closed right after an error" : "closed with an error code";
    std::string p;
    if (!report_step("the report", [&] { p = diag_write_report(why, fault ? &ep : nullptr, ftid); }))
        return;
    report_step("the mark", [&] { mark_problem(p, what); });
    report_step("the archive", [&] { mark_problem(bundle_for_problem(why, p), what); });
    report_step("the log line", [&] { logf(&g_kit_mod, SK_ERROR, "%s; crash report: %s", why, p.c_str()); });
}

// the full report is written by a thread of its own: if the crashed thread holds a lock the report
// needs (the heap's, the loader's, a game lock), the filter still returns after kReportWaitMs and
// the short report written first stays
static constexpr DWORD kReportWaitMs = 15000;
struct CrashJob {
    std::string base;
    EXCEPTION_POINTERS* ep;
    DWORD tid; // the crashed thread
};
// the report, the archive and the mark first; the minidump last (the riskiest step: it suspends the
// other threads and walks the crashed one), then the archive again with it
static DWORD WINAPI crash_report_thread_old(void* p)
{
    auto* j = (CrashJob*)p;
    std::string path = diag_write_report_as(j->base, "unhandled exception", j->ep, false, j->tid);
    logf(&g_kit_mod, SK_ERROR, "crash report: %s", path.c_str());
    std::string zip = bundle_for_problem("the game crashed", path); // one file to send
    mark_problem(zip, "crashed");
    if (write_minidump(logs_dir() + L"\\" + widen(j->base) + L".dmp", j->ep, j->tid)) {
        std::string zip2 = diag_write_bundle_with("the game crashed", path);
        if (zip2 != path && zip2 != zip) {
            mark_problem(zip2, "crashed");
            DeleteFileW(widen(zip).c_str());
        }
    } else
        logf(&g_kit_mod, SK_ERROR, "crash report: no minidump (MiniDumpWriteDump failed)");
    return 0;
}

// BUG-0127: every step guarded; the mark names the crash report before the archive is built (the
// short report written by the filter is there already), then the archive when it exists
static DWORD WINAPI crash_report_thread(void* p)
{
    if (!fixed(g_fix_crashsafe))
        return crash_report_thread_old(p);
    auto* j = (CrashJob*)p;
    std::string path = narrow(logs_dir() + L"\\" + widen(j->base) + L".txt"); // the short report
    report_step("the crash report", [&] { path = diag_write_report_as(j->base, "unhandled exception", j->ep, false, j->tid); });
    report_step("the mark", [&] { mark_problem(path, "crashed"); });
    report_step("the log line", [&] { logf(&g_kit_mod, SK_ERROR, "crash report: %s", path.c_str()); });
    std::string zip;
    report_step("the archive", [&] {
        zip = bundle_for_problem("the game crashed", path); // one file to send
        if (zip != path)
            mark_problem(zip, "crashed");
    });
    bool dumped = false;
    report_step("the minidump", [&] { dumped = write_minidump(logs_dir() + L"\\" + widen(j->base) + L".dmp", j->ep, j->tid); });
    if (!dumped) {
        report_step("the log line", [&] { logf(&g_kit_mod, SK_ERROR, "crash report: no minidump (MiniDumpWriteDump failed)"); });
        return 0;
    }
    report_step("the archive with the minidump", [&] {
        std::string zip2 = bundle_for_problem("the game crashed", path);
        if (zip2 != path && zip2 != zip) {
            mark_problem(zip2, "crashed");
            if (!zip.empty() && zip != path)
                DeleteFileW(widen(zip).c_str());
        }
    });
    return 0;
}

// BUG-0056: a stack overflow leaves the crashed thread about one page of stack; creating a thread
// there (or formatting, logging) overflows again and Windows ends the game without a report. This
// thread is made at boot and waits: the filter only signals it and waits for it.
static CrashJob g_so_job;
static HANDLE g_so_go, g_so_done;

static DWORD WINAPI so_report_thread(void*)
{
    WaitForSingleObject(g_so_go, INFINITE);
    report_step("the short report", [&] {
        g_so_job.base = report_base(true);
        DWORD code = g_so_job.ep->ExceptionRecord->ExceptionCode;
        uintptr_t eip = g_so_job.ep->ContextRecord->Eip;
        char b[512];
        snprintf(b, sizeof b,
                 "SporeCoop crash %s (short: the full report is being written over this file)\n"
                 "SporeKit " SK_VERSION ", game %s\nexception 0x%08lX %s at 0x%08X %s (thread %lu)\n",
                 g_so_job.base.c_str(), build_name(), code, code_name(code), (unsigned)eip, where(eip).c_str(),
                 g_so_job.tid);
        CreateDirectoryW(logs_dir().c_str(), nullptr);
        write_text(logs_dir() + L"\\" + widen(g_so_job.base) + L".txt", b);
        logf(&g_kit_mod, SK_ERROR, "the game crashed: exception 0x%08lX (stack overflow) at 0x%08X %s, thread %lu", code,
             (unsigned)eip, where(eip).c_str(), g_so_job.tid);
    });
    if (!g_so_job.base.empty())
        crash_report_thread(&g_so_job);
    SetEvent(g_so_done);
    return 0;
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW && g_so_go &&
        fixed(g_fix_stackov)) {
        // nothing but a signal and a wait on this stack (BUG-0056)
        if (g_crashed++ == 0) {
            g_so_job.ep = ep;
            g_so_job.tid = GetCurrentThreadId();
            SetEvent(g_so_go);
            WaitForSingleObject(g_so_done, kReportWaitMs);
        }
        return g_next_filter ? g_next_filter(ep) : EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_crashed++ == 0 && ep && ep->ExceptionRecord && ep->ContextRecord) {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        uintptr_t eip = ep->ContextRecord->Eip;
        std::string w = where(eip);
        // a short report at once (no locks but the file system's), before the log: the crashed
        // thread may hold the log's lock (BUG-0115; the log drops lines re-entered on one thread)
        static CrashJob job;
        job.base = report_base(true);
        job.ep = ep;
        job.tid = GetCurrentThreadId();
        char b[512];
        snprintf(b, sizeof b,
                 "SporeCoop crash %s (short: the full report is being written over this file)\n"
                 "SporeKit " SK_VERSION ", game %s\nexception 0x%08lX %s at 0x%08X %s\n",
                 job.base.c_str(), build_name(), code, code_name(code), (unsigned)eip, w.c_str());
        CreateDirectoryW(logs_dir().c_str(), nullptr);
        write_text(logs_dir() + L"\\" + widen(job.base) + L".txt", b);
        logf(&g_kit_mod, SK_ERROR, "the game crashed: exception 0x%08lX at 0x%08X %s", code, (unsigned)eip, w.c_str());
        HANDLE t = CreateThread(nullptr, 0, crash_report_thread, &job, 0, nullptr);
        if (t) {
            if (WaitForSingleObject(t, kReportWaitMs) != WAIT_OBJECT_0)
                logf(&g_kit_mod, SK_ERROR, "the full crash report did not finish in %lu s (the short one stays)",
                     kReportWaitMs / 1000);
            CloseHandle(t);
        }
    }
    return g_next_filter ? g_next_filter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI h_SetUEF(LPTOP_LEVEL_EXCEPTION_FILTER f)
{
    if (f == crash_filter)
        return o_SetUEF_kb ? o_SetUEF_kb(f) : o_SetUEF_k32(f);
    LPTOP_LEVEL_EXCEPTION_FILTER prev = g_next_filter;
    g_next_filter = f;
    return prev;
}

// BUG-0243: an in-game update swaps files under the update lock of the game folder; the game ending
// between a file's two moves left steam_api.dll missing (the game no longer started) or a mix of
// versions. The exit waits for the lock (at most 5 s) and releases it at once. A crash does not wait.
static void wait_for_update()
{
    if (!fixed(g_fix_exitwait))
        return;
    std::wstring root = mods_dirw();
    for (int i = 0; i < 2; i++) {
        size_t k = root.find_last_of(L"\\/");
        if (k == std::wstring::npos)
            return;
        root.resize(k);
    }
    wchar_t name[160];
    if (!sc_lock_name(L"SporeCoopUpdate-", root.c_str(), name, 160))
        return;
    HANDLE h = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
    if (!h)
        return; // no update made the lock in this run
    DWORD r = WaitForSingleObject(h, 5000);
    if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED)
        ReleaseMutex(h);
    CloseHandle(h);
}

static void WINAPI h_ExitProcess(UINT code)
{
    wait_for_update();
    logf(&g_kit_mod, code ? SK_WARN : SK_INFO, "the game exits (ExitProcess %u) after %d s, frame %llu", code,
         (int)now_seconds(), (unsigned long long)frame_index());
    exit_report(code, "ExitProcess");
    o_ExitProcess(code);
}

static BOOL WINAPI h_TerminateProcess(HANDLE h, UINT code)
{
    if (h == GetCurrentProcess() || GetProcessId(h) == GetCurrentProcessId()) {
        wait_for_update();
        logf(&g_kit_mod, SK_WARN, "the game terminates itself (TerminateProcess %u) after %d s", code,
             (int)now_seconds());
        exit_report(code, "TerminateProcess");
    }
    return o_TerminateProcess(h, code);
}

// the watchdog saw no frames for 30 s while the game window has the focus: a hang report, once
void diag_hang_report(const char* why)
{
    static std::atomic<int> once{0};
    HWND w = (HWND)game_window();
    if (!w || IsIconic(w) || GetForegroundWindow() != w || once++)
        return;
    std::string p;
    if (!report_step("the hang report", [&] { p = diag_write_report(why, nullptr); }))
        return;
    report_step("the mark", [&] { mark_problem(p, "stopped responding"); });
    report_step("the log line", [&] { logf(&g_kit_mod, SK_ERROR, "hang report: %s", p.c_str()); });
    report_step("the archive", [&] { mark_problem(bundle_for_problem(why, p), "stopped responding"); });
}

static DWORD WINAPI crash_thread(void*)
{
    volatile int* p = (int*)0x10;
    *p = 1;
    return 0;
}

// a fault on a new thread that a test handler repairs (the thread goes on): the last fault of the
// fault logger comes from a worker thread; an exit within 15 s reports it (BUG-0248)
static DWORD WINAPI repaired_fault_thread(void*)
{
    volatile uint8_t* page = (volatile uint8_t*)test_fault_page();
    page[0] = 1;
    return 0;
}

// a recursion without end with 4 KB of locals per call: a stack overflow (BUG-0056)
static volatile int g_recurse_stop; // never set: the compiler must not see an endless recursion
static int __attribute__((noinline)) recurse_forever(int depth)
{
    volatile char pad[4096];
    pad[0] = (char)depth;
    pad[4095] = (char)depth;
    if (g_recurse_stop)
        return depth;
    return recurse_forever(depth + 1) + pad[depth & 4095];
}

static DWORD WINAPI stack_overflow_thread(void*) { return (DWORD)recurse_forever(0); }

// tests: crash.test thread (a fault nobody handles, in a thread of its own) | main (in the frame) |
// stackoverflow (a thread recursing without end, BUG-0056) | oom (thread, and every archive of the
// report fails like an allocation in a full address space, BUG-0127)
void diag_crash_test(const char* where_)
{
    if (where_ && !strcmp(where_, "stackoverflow")) {
        CreateThread(nullptr, 0, stack_overflow_thread, nullptr, 0, nullptr);
        return;
    }
    if (where_ && !strcmp(where_, "oom"))
        g_test_oom = 1;
    if (where_ && !strcmp(where_, "faultthread")) { // no crash: a repaired fault on a worker thread
        if (HANDLE t = CreateThread(nullptr, 0, repaired_fault_thread, nullptr, 0, nullptr)) {
            WaitForSingleObject(t, 5000);
            CloseHandle(t);
        }
        return;
    }
    if (where_ && !strcmp(where_, "main")) {
        volatile int* p = nullptr;
        *p = 1;
        return;
    }
    CreateThread(nullptr, 0, crash_thread, nullptr, 0, nullptr);
}

void diag_install()
{
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll"), kb = GetModuleHandleW(L"kernelbase.dll");
    void* a = k32 ? (void*)GetProcAddress(k32, "SetUnhandledExceptionFilter") : nullptr;
    void* b = kb ? (void*)GetProcAddress(kb, "SetUnhandledExceptionFilter") : nullptr;
    g_next_filter = SetUnhandledExceptionFilter(crash_filter);
    if (b && b != a)
        hook_create(&g_kit_mod, b, (void*)h_SetUEF, (void**)&o_SetUEF_kb);
    if (a)
        hook_create(&g_kit_mod, a, (void*)h_SetUEF, (void**)&o_SetUEF_k32);
    if (void* e = k32 ? (void*)GetProcAddress(k32, "ExitProcess") : nullptr)
        hook_create(&g_kit_mod, e, (void*)h_ExitProcess, (void**)&o_ExitProcess);
    if (void* e = k32 ? (void*)GetProcAddress(k32, "TerminateProcess") : nullptr)
        hook_create(&g_kit_mod, e, (void*)h_TerminateProcess, (void**)&o_TerminateProcess);
    logf(&g_kit_mod, SK_INFO, "%s; %s", os_text().c_str(), memory_text().c_str());
    // BUG-0056: the stack overflow reporter waits from now on; the game's main thread keeps 64 KB
    // for the exception dispatch and the filter after an overflow (Windows; Wine may ignore it)
    g_so_go = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_so_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_so_go && g_so_done)
        CloseHandle(CreateThread(nullptr, 0, so_report_thread, nullptr, 0, nullptr));
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    // BUG-0246: the guarantee is taken from the main thread's reserve (1 MB in both GOG builds, so
    // 6 %); logged so a player's log shows it next to a stack overflow, if one is ever reported
    if (auto* dos = (IMAGE_DOS_HEADER*)GetModuleHandleW(nullptr))
        if (auto* nt = (IMAGE_NT_HEADERS32*)((uint8_t*)dos + dos->e_lfanew); nt->Signature == IMAGE_NT_SIGNATURE)
            logf(&g_kit_mod, SK_INFO, "main thread stack: reserve %lu KB, guarantee %lu KB",
                 (unsigned long)(nt->OptionalHeader.SizeOfStackReserve / 1024), 64ul); // guarantee: the old value now
    diag_prune_logs(); // BUG-0137: piles of earlier versions
}

void diag_after_device()
{
    static bool once;
    if (once)
        return;
    once = true;
    logf(&g_kit_mod, SK_INFO, "%s; other hooks: %s", gpu_text().c_str(), foreign_hooks().c_str());
    // other mods, extra packages, changed game files (docs/REPORTING.md#reporting-support-environment)
    const std::string& f = diag_files_text();
    for (size_t i = 0; i < f.size();) {
        size_t e = f.find('\n', i);
        logf(&g_kit_mod, SK_INFO, "files: %s", f.substr(i, e == std::string::npos ? std::string::npos : e - i).c_str());
        if (e == std::string::npos)
            break;
        i = e + 1;
    }
}

} // namespace sk
