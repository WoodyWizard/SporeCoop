// Mod manager: loads mods\<name>.dll through shadow copies so they can be rebuilt
// and hot-reloaded while the game is running.
#include "core.h"

#include <memory>
#include <mutex>

namespace sk {

static std::vector<std::unique_ptr<SkMod>> g_mods;
static int g_gen = 0;
static const DWORD kUnloadGraceMs = 50;
static const DWORD kCmdWaitMs = 10000; // BUG-1018: coop.sample runs up to 30 s; longer ones keep the old image

static std::wstring shadow_dir() { return mods_dirw() + L"\\.shadow"; }

std::vector<SkMod*> mods_list()
{
    std::vector<SkMod*> v;
    for (auto& m : g_mods)
        v.push_back(m.get());
    return v;
}

// BUG-1022: the mods line of a report written off the main thread (watchdog, crash report thread) walked g_mods and
// m->info while mod.load / mod.reload changed them. A copy of the line is kept under a lock instead, made on the main
// thread after every load and unload.
static std::mutex g_line_mu;
static std::string g_line;

static void mods_line_update()
{
    std::string s;
    for (auto& m : g_mods)
        if (m->module)
            s += m->name + (m->info && m->info->version ? std::string(" ") + m->info->version : "") + "  ";
    std::lock_guard<std::mutex> lk(g_line_mu);
    g_line = s;
}

std::string mods_line()
{
    std::lock_guard<std::mutex> lk(g_line_mu);
    return g_line;
}

static SkMod* find_mod(const std::string& name)
{
    for (auto& m : g_mods)
        if (m->name == name)
            return m.get();
    return nullptr;
}

static bool load_into(SkMod* m, std::string& err)
{
    CreateDirectoryW(shadow_dir().c_str(), nullptr);
    m->generation = ++g_gen;
    // instance id in the name: several game instances share the mods folder
    m->live_path = shadow_dir() + L"\\" + widen(m->name) + L".i" + std::to_wstring(instance_id()) + L"." +
                   std::to_wstring(m->generation) + L".dll";
    if (!CopyFileW(m->src_path.c_str(), m->live_path.c_str(), FALSE)) {
        err = "cannot copy " + narrow(m->src_path) + " (err " + std::to_string(GetLastError()) + ")";
        return false;
    }
    m->module = LoadLibraryW(m->live_path.c_str());
    if (!m->module) {
        err = "LoadLibrary failed (err " + std::to_string(GetLastError()) + ")";
        DeleteFileW(m->live_path.c_str());
        return false;
    }
    auto main_fn = (SkModMainFn)(void*)GetProcAddress(m->module, "sporekit_mod_main");
    if (!main_fn) {
        err = "export sporekit_mod_main missing";
        FreeLibrary(m->module);
        m->module = nullptr;
        return false;
    }
    m->info = main_fn(&g_host, m);
    if (!m->info) {
        err = "mod refused to initialise (returned NULL)";
        hooks_remove_all(m);
        unsubscribe_all(m);
        diag_context_remove(m);
        diag_headline(m, nullptr);
        commands_remove_all(m);
        ui_winprocs_disable_mod(m);
        FreeLibrary(m->module);
        m->module = nullptr;
        return false;
    }
    logf(m, SK_INFO, "loaded %s v%s (gen %d)", m->info->name, m->info->version, m->generation);
    mods_line_update();
    return true;
}

static void unload_from(SkMod* m)
{
    if (!m->module)
        return;
    // BUG-1018: a console command of the mod running on another thread (any-thread commands, coop.sample) would come
    // back into unmapped code: no new call starts once its commands are gone, the running ones are waited for before
    // the mod tears down its state; one that does not return in time keeps the old image mapped (a leak of one DLL
    // copy in a dev path, not a crash)
    bool keep_image = false;
    if (fixed(g_fix_cmdkeep)) {
        commands_remove_all(m);
        std::string why;
        if (!commands_wait_idle(m, kCmdWaitMs, why)) {
            keep_image = true;
            logf(m, SK_WARN, "unload: %s; the old image stays mapped (BUG-1018)", why.c_str());
        }
    }
    if (m->info && m->info->on_unload)
        m->info->on_unload();
    int hooks = hooks_remove_all(m);
    unsubscribe_all(m);
    diag_context_remove(m);
    diag_headline(m, nullptr);
    commands_remove_all(m);
    ui_winprocs_disable_mod(m);
    // a game worker thread may still be inside one of the mod's detours (they are no longer entered:
    // the original bytes are back): give it time to return before the DLL's code is unmapped
    // (BUG-0132). Not a guarantee for a detour that blocks, only for short ones (RandomInt).
    if (hooks && fixed(g_fix_hookkeep))
        Sleep(kUnloadGraceMs);
    {
        // tasks the mod queued and that have not run: their code goes with the image (BUG-0060)
        auto base = (uintptr_t)m->module;
        auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        if (size_t n = drop_tasks_in(base, base + nt->OptionalHeader.SizeOfImage))
            logf(m, SK_INFO, "dropped %u queued task(s) of the old image", (unsigned)n);
    }
    if (!keep_image) {
        FreeLibrary(m->module);
        DeleteFileW(m->live_path.c_str());
    }
    m->module = nullptr;
    m->info = nullptr;
    mods_line_update();
    logf(m, SK_INFO, "unloaded");
}

bool mod_load(const std::string& name, std::string& err)
{
    if (find_mod(name) && find_mod(name)->module) {
        err = "already loaded";
        return false;
    }
    SkMod* m = find_mod(name);
    if (!m) {
        g_mods.push_back(std::make_unique<SkMod>());
        m = g_mods.back().get();
        m->name = name;
        m->src_path = mods_dirw() + L"\\" + widen(name) + L".dll";
    }
    if (GetFileAttributesW(m->src_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "file not found: " + narrow(m->src_path);
        return false;
    }
    return load_into(m, err);
}

bool mod_unload(const std::string& name, std::string& err)
{
    SkMod* m = find_mod(name);
    if (!m || !m->module) {
        err = "not loaded";
        return false;
    }
    unload_from(m);
    return true;
}

bool mod_reload(const std::string& name, std::string& err)
{
    SkMod* m = find_mod(name);
    if (m && m->module)
        unload_from(m);
    return mod_load(name, err);
}

void mods_load_all()
{
    // clean stale shadow copies from previous runs
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((shadow_dir() + L"\\*.i" + std::to_wstring(instance_id()) + L".*.dll").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            DeleteFileW((shadow_dir() + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    std::string list = ini_str(mods_dirw() + L"\\sporekit.ini", "mods", "load", "*");
    std::vector<std::string> names;
    if (list == "*") {
        h = FindFirstFileW((mods_dirw() + L"\\*.dll").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring f = fd.cFileName;
                names.push_back(narrow(f.substr(0, f.size() - 4)));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    } else {
        size_t s = 0;
        while (s <= list.size()) {
            size_t e = list.find(',', s);
            if (e == std::string::npos)
                e = list.size();
            std::string n = list.substr(s, e - s);
            while (!n.empty() && n.front() == ' ') n.erase(0, 1);
            while (!n.empty() && n.back() == ' ') n.pop_back();
            if (!n.empty())
                names.push_back(n);
            s = e + 1;
        }
    }
    for (auto& n : names) {
        std::string err;
        if (!mod_load(n, err))
            SKLOG(SK_ERROR, "mod %s: %s", n.c_str(), err.c_str());
    }
}

void mods_shutdown()
{
    for (auto it = g_mods.rbegin(); it != g_mods.rend(); ++it)
        unload_from(it->get());
}

} // namespace sk
