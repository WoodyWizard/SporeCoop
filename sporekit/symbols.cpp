// Symbol database: name -> VA (for preferred image base), selected by the exe's SHA-256, or by
// its code_sha256 (sha256.cpp) when no file names this exact exe: a build that differs only in
// the PE header, resources or a signature uses the symbols of the build with the same code.
#include "core.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace sk {

static std::unordered_map<std::string, uintptr_t> g_syms; // VA at preferred base
static std::map<uintptr_t, std::string> g_by_addr;
static std::string g_hash, g_code_hash, g_build;
static std::string g_files;
static uintptr_t g_pref_base = 0x400000, g_base = 0x400000;
// a packed exe (Steam DRM): the build its data belongs to, and that build's unpacked .text hash
static std::string g_pending_build, g_pending_text, g_data_hash;

uintptr_t image_base() { return g_base; }
uintptr_t rebase(uintptr_t va) { return va - g_pref_base + g_base; }
std::string exe_sha256() { return g_hash; }
std::string symbols_build() { return g_build.empty() ? g_hash : g_build; }
const char* symbols_file_name() { return g_files.c_str(); }
size_t symbols_count() { return g_syms.size(); }

uintptr_t sym(const char* name)
{
    auto it = g_syms.find(name);
    return it == g_syms.end() ? 0 : rebase(it->second);
}

static void load_file(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f)
        return;
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        char* eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        uintptr_t va = (uintptr_t)strtoul(eq + 1, nullptr, 16);
        if (!va)
            continue;
        g_syms[line] = va;
        n++;
    }
    fclose(f);
    SKLOG(SK_INFO, "symbols: %d entries from %s", n, narrow(path).c_str());
}

// the "exe_sha256=" and "code_sha256=" values in the first lines of a symbol file
struct SymHeader {
    std::string exe, code, data, text;
};
static SymHeader file_hash_header(const std::wstring& path)
{
    SymHeader r;
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f)
        return r;
    char line[512];
    auto value = [&](const char* key, std::string& out) {
        const char* p = strstr(line, key);
        if (!p || !out.empty())
            return;
        p += strlen(key);
        while (*p && *p != '\n' && *p != '\r' && *p != ' ')
            out += *p++;
    };
    for (int i = 0; i < 10 && fgets(line, sizeof line, f); i++) {
        value("exe_sha256=", r.exe);
        value("code_sha256=", r.code);
        value("data_sha256=", r.data);
        value("text_sha256=", r.text);
    }
    fclose(f);
    return r;
}

// ImageBase as written in the exe file. The header in memory cannot be used: when Windows
// relocates the exe (ASLR, e.g. to 0x410000) it rewrites OptionalHeader.ImageBase to the actual
// base, so every symbol would be off by the relocation. Wine does not relocate the exe.
static uintptr_t file_image_base(const wchar_t* path, uintptr_t fallback)
{
    FILE* f = _wfopen(path, L"rb");
    if (!f)
        return fallback;
    uint8_t hdr[0x400];
    size_t n = fread(hdr, 1, sizeof hdr, f);
    fclose(f);
    if (n < sizeof(IMAGE_DOS_HEADER))
        return fallback;
    auto* dos = (const IMAGE_DOS_HEADER*)hdr;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS32) > n)
        return fallback;
    auto* nt = (const IMAGE_NT_HEADERS32*)(hdr + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return fallback;
    return nt->OptionalHeader.ImageBase;
}

struct Cand {
    std::wstring name;
    SymHeader h;
};
static std::vector<Cand> g_cands;
static std::wstring g_dir;

// the files of build g_build: the ported/ModAPI ones first, then the hand-written overrides
static bool load_build()
{
    std::vector<std::wstring> normal, overrides;
    for (auto& c : g_cands) {
        if (g_build.empty() || c.h.exe != g_build)
            continue;
        std::wstring p = g_dir + c.name;
        if (wcsncmp(c.name.c_str(), L"overrides", 9) == 0)
            overrides.push_back(p);
        else
            normal.push_back(p);
    }
    std::sort(normal.begin(), normal.end());
    std::sort(overrides.begin(), overrides.end());
    for (auto& p : normal) {
        load_file(p);
        g_files += narrow(p.substr(g_dir.size())) + " ";
    }
    for (auto& p : overrides) {
        load_file(p);
        g_files += narrow(p.substr(g_dir.size())) + " ";
    }
    for (auto& [n, a] : g_syms)
        g_by_addr[a] = n;
    if (g_syms.empty()) {
        SKLOG(SK_ERROR, "no symbol database for this SporeApp.exe build (%s, code %s); "
                        "game-specific features are disabled",
              g_hash.c_str(), g_code_hash.c_str());
        return false;
    }
    return true;
}

bool symbols_load()
{
    view_cache_drop(); // BUG-9333: the camera symbol is looked up again
    g_syms.clear();
    g_by_addr.clear();
    g_files.clear();
    g_pending_build.clear();
    HMODULE exe = GetModuleHandleW(nullptr);
    g_base = (uintptr_t)exe;

    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(exe, exe_path, MAX_PATH);
    g_pref_base = file_image_base(exe_path, 0x400000);
    double t0 = now_seconds();
    g_hash = sha256_file_hex(exe_path);
    SKLOG(SK_INFO, "exe %s sha256=%s base=0x%08X (preferred 0x%08X) hashed in %.2fs",
          narrow(exe_path).c_str(), g_hash.c_str(), (unsigned)g_base, (unsigned)g_pref_base,
          now_seconds() - t0);

    g_dir = mods_dirw() + L"\\sporekit\\symbols\\";
    g_cands.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_dir + L"*.sym").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            g_cands.push_back({fd.cFileName, file_hash_header(g_dir + fd.cFileName)});
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // 1. files for this exact exe; 2. else files of a build with the same code (one build only)
    g_build.clear();
    for (auto& c : g_cands)
        if (c.h.exe == g_hash)
            g_build = g_hash;
    if (g_build.empty()) {
        t0 = now_seconds();
        g_code_hash = pe_code_sha256_hex(exe_path);
        SKLOG(SK_INFO, "exe code_sha256=%s (%.2fs)", g_code_hash.c_str(), now_seconds() - t0);
        for (auto& c : g_cands)
            if (!g_code_hash.empty() && c.h.code == g_code_hash && !c.h.exe.empty()) {
                g_build = c.h.exe;
                break;
            }
        if (!g_build.empty())
            SKLOG(SK_INFO, "symbols: no files for this exe; its code is the same as build %s, using those",
                  g_build.c_str());
    }
    if (g_build.empty()) {
        // 3. a packed exe: its data sections are those of a known build, its code is encrypted in
        // the file (Steam's SteamStub DRM since the October 2024 update): wait for the unpacked code
        g_data_hash = pe_data_sha256_hex(exe_path);
        for (auto& c : g_cands)
            if (!g_data_hash.empty() && c.h.data == g_data_hash && !c.h.exe.empty() && !c.h.text.empty()) {
                g_pending_build = c.h.exe;
                g_pending_text = c.h.text;
                SKLOG(SK_INFO, "symbols: a packed exe with the data of build %s (data_sha256=%s): "
                               "waiting for the game's unpacked code",
                      g_pending_build.c_str(), g_data_hash.c_str());
                return false;
            }
    }
    return load_build();
}

bool symbols_pending() { return !g_pending_build.empty() && g_build.empty(); }

bool symbols_try_unpacked()
{
    if (!symbols_pending())
        return false;
    double t0 = now_seconds();
    std::string mem = mem_text_sha256_hex(GetModuleHandleW(nullptr), g_pref_base);
    bool ok = !mem.empty() && mem == g_pending_text;
    SKLOG(ok ? SK_INFO : SK_ERROR, "symbols: the unpacked code %s build %s (text_sha256=%s, %.2fs)",
          ok ? "is the code of" : "is NOT the code of", g_pending_build.c_str(), mem.c_str(), now_seconds() - t0);
    std::string build = g_pending_build;
    g_pending_build.clear();
    if (!ok) {
        SKLOG(SK_ERROR, "no symbol database for this SporeApp.exe build (%s); game-specific features are disabled",
              g_hash.c_str());
        return false;
    }
    g_build = build;
    return load_build();
}

std::vector<std::pair<std::string, uintptr_t>> symbols_find(const std::string& needle, size_t limit)
{
    std::vector<std::pair<std::string, uintptr_t>> out;
    std::string ln = needle;
    std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
    for (auto& [a, n] : g_by_addr) {
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        if (l.find(ln) != std::string::npos) {
            out.emplace_back(n, rebase(a));
            if (out.size() >= limit)
                break;
        }
    }
    return out;
}

std::string symbols_reverse(uintptr_t addr)
{
    uintptr_t va = addr - g_base + g_pref_base;
    auto it = g_by_addr.upper_bound(va);
    if (it == g_by_addr.begin())
        return {};
    --it;
    uintptr_t off = va - it->first;
    if (off > 0x4000)
        return {};
    char b[32];
    snprintf(b, sizeof b, "+0x%X", (unsigned)off);
    return it->second + (off ? b : "");
}

} // namespace sk
