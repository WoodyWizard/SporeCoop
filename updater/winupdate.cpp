#include "winupdate.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>

#include "../mods/coop/fix_switch.h"
#include "pending.h"
#include "sha256.h"

namespace upd {

FIX_SWITCH(g_old_suffix_fix); // update.old_suffix, BUG-0114: our own .sporecoop-old suffix, only ours cleaned
FIX_SWITCH(g_keep_failed_fix); // update.keep_failed, BUG-0150: a failed pending swap keeps its files
int g_swap_delay_ms = 0;
std::string g_swap_delay_file; // not a lab switch: test hook (BUG-0243)
int g_fail_swap_at = 0;         // not a lab switch: test hook (BUG-0242)
FIX_SWITCH(g_lock_fix); // update.lock, BUG-0243: the update lock of a game folder

// BUG-0243: the update lock of a game folder, made so that a game run by the player and a setup run
// as administrator can open each other's (a NULL DACL); waits at most wait_ms. nullptr: not taken
// (another update holds it that long) or it cannot be made: the update goes on without it.
static HANDLE take_update_lock(const std::wstring& root, DWORD wait_ms)
{
    sc_update_lock_fix = g_lock_fix ? 1 : 0;
    wchar_t name[160];
    if (!g_lock_fix || !sc_lock_name(L"SporeCoopUpdate-", root.c_str(), name, 160))
        return nullptr;
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES sa{sizeof sa, &sd, FALSE};
    bool sd_ok = InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
                 SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
    HANDLE h = CreateMutexW(sd_ok ? &sa : nullptr, FALSE, name);
    if (!h)
        h = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
    if (!h)
        return nullptr;
    DWORD r = WaitForSingleObject(h, wait_ms);
    if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED)
        return h;
    CloseHandle(h);
    return nullptr;
}

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool http_get(const std::string& url, std::vector<uint8_t>& out, std::string& err, const Progress& progress,
              uint32_t max_bytes)
{
    out.clear();
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        err = "bad address: " + url;
        return false;
    }
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET s = WinHttpOpen(L"SporeCoop-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        err = "no network (WinHTTP)";
        return false;
    }
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    bool ok = false;
    if (r) {
        // GitHub answers the API in JSON and redirects downloads to its file host
        const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
        if (WinHttpSendRequest(r, hdr, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, nullptr)) {
            DWORD status = 0, len = sizeof status;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &len, WINHTTP_NO_HEADER_INDEX);
            DWORD total = 0;
            len = sizeof total;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &total, &len, WINHTTP_NO_HEADER_INDEX);
            if (status != 200) {
                err = "the server answered " + std::to_string(status) + " for " + url;
            } else {
                ok = true;
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(r, &avail)) {
                        ok = false;
                        err = "the download broke off";
                        break;
                    }
                    if (avail == 0)
                        break;
                    size_t at = out.size();
                    if (at + avail > max_bytes) {
                        ok = false;
                        err = "the download is too big";
                        break;
                    }
                    out.resize(at + avail);
                    DWORD got = 0;
                    if (!WinHttpReadData(r, out.data() + at, avail, &got)) {
                        ok = false;
                        err = "the download broke off";
                        break;
                    }
                    out.resize(at + got);
                    if (progress)
                        progress(out.size(), total);
                }
            }
        } else
            err = "cannot reach " + narrow(host) + " (error " + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(r);
    } else
        err = "cannot reach " + narrow(host);
    if (c)
        WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

bool latest_release(const std::string& api_url, Release& out, std::string& err)
{
    std::vector<uint8_t> body;
    if (!http_get(api_url, body, err, {}, 4u << 20))
        return false;
    if (!parse_release(std::string(body.begin(), body.end()), out)) {
        err = "no release found";
        return false;
    }
    return true;
}

bool download_pack(const Release& r, Pack& out, std::string& err, const Progress& progress)
{
    if (r.pack_url.empty() || r.sums_url.empty()) {
        err = "the release " + r.tag + " has no update pack";
        return false;
    }
    std::vector<uint8_t> sums, pack;
    if (!http_get(r.sums_url, sums, err, {}, 1u << 20) || !http_get(r.pack_url, pack, err, progress))
        return false;
    std::string name = r.pack_url.substr(r.pack_url.find_last_of('/') + 1);
    std::string want = sums_lookup(std::string(sums.begin(), sums.end()), name);
    if (want.empty() || sha256_hex(pack) != want) {
        err = "the downloaded update does not match the release checksum";
        return false;
    }
    return parse_pack(pack, out, err);
}

bool read_pack_file(const std::wstring& path, Pack& out, std::string& err)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = "cannot open " + narrow(path);
        return false;
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    std::vector<uint8_t> b((size_t)std::min<LONGLONG>(sz.QuadPart, 96 << 20));
    DWORD got = 0;
    bool ok = ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr) && got == b.size();
    CloseHandle(f);
    if (!ok) {
        err = "cannot read " + narrow(path);
        return false;
    }
    return parse_pack(b, out, err);
}

static std::wstring native(const std::wstring& root, const std::string& rel)
{
    std::wstring p = root + L"\\" + widen(rel);
    std::replace(p.begin(), p.end(), L'/', L'\\');
    return p;
}

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void make_dirs(const std::wstring& file)
{
    for (size_t i = 3; i < file.size(); i++)
        if (file[i] == L'\\')
            CreateDirectoryW(file.substr(0, i).c_str(), nullptr);
}

static bool write_all(const std::wstring& p, const std::vector<uint8_t>& d)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD w = 0;
    bool ok = WriteFile(f, d.data(), (DWORD)d.size(), &w, nullptr) && w == d.size();
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    return ok;
}

static std::string why(const char* what, const std::wstring& p)
{
    DWORD e = GetLastError();
    std::string s = std::string(what) + " " + narrow(p);
    if (e == ERROR_ACCESS_DENIED)
        s += " (access denied: run as administrator?)";
    else if (e == ERROR_SHARING_VIOLATION)
        s += " (in use: close Spore?)";
    else
        s += " (error " + std::to_string(e) + ")";
    return s;
}

bool apply_pack(const std::wstring& root, const Pack& p, std::string& err, int* pending)
{
    std::string later; // relative paths that wait for the next start
    if (std::string d = g_dup_fix ? duplicate_path(p.files) : std::string(); !d.empty()) {
        err = "the pack lists a file twice: " + d; // BUG-0089 (packs built in memory too)
        return false;
    }
    // the game's own steam_api.dll is kept as steam_api_orig.dll (the loader forwards to it)
    // before the loader takes its place
    std::wstring sa = root + L"\\SporebinEP1\\steam_api.dll", orig = root + L"\\SporebinEP1\\steam_api_orig.dll";
    if (!exists(orig)) {
        if (!exists(sa) || is_loader(sa)) {
            err = "the game's steam_api.dll is missing: reinstall the game files first";
            return false;
        }
        if (!CopyFileW(sa.c_str(), orig.c_str(), TRUE)) {
            err = why("cannot keep the original", sa);
            return false;
        }
    }
    struct Step {
        std::wstring target, fresh, old;
        std::string rel;
        bool had = false, moved_old = false, placed = false, deferred = false;
    };
    std::vector<Step> steps;
    // 1. every new file next to its target
    for (auto& f : p.files) {
        if (!allowed_path(f.path)) {
            err = "refusing to write " + f.path;
            return false;
        }
        Step s;
        s.rel = f.path;
        s.target = native(root, f.path);
        s.had = exists(s.target);
        if ((f.flags & kOnlyIfMissing) && s.had)
            continue;
        s.fresh = s.target + L".new";
        // our own suffix (BUG-0114): "<file>.old" may be the player's own backup
        s.old = s.target + (g_old_suffix_fix ? SC_OLD_SUFFIX : L".old");
        make_dirs(s.target);
        if (!write_all(s.fresh, f.data)) {
            err = why("cannot write", s.fresh);
            for (auto& d : steps)
                DeleteFileW(d.fresh.c_str());
            return false;
        }
        steps.push_back(s);
    }
    // 2. swap them in: the current file becomes *.old (a loaded DLL can be renamed, not overwritten).
    // Under the update lock (BUG-0243): only file operations until it is released
    HANDLE lock = take_update_lock(root, 10000);
    int swap_no = 0;
    for (auto& s : steps) {
        if (g_fail_swap_at > 0 && ++swap_no == g_fail_swap_at) { // test hook: fails like "cannot replace"
            err = why("cannot replace", s.target);
            goto rollback;
        }
        if (s.had) {
            DeleteFileW(s.old.c_str());
            if (!MoveFileExW(s.target.c_str(), s.old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                if (GetLastError() == ERROR_SHARING_VIOLATION) { // open in the game: next start
                    s.deferred = true;
                    later += s.rel + "\n";
                    continue;
                }
                err = why("cannot replace", s.target);
                goto rollback;
            }
            s.moved_old = true;
            if (g_swap_delay_ms > 0 && (g_swap_delay_file.empty() || g_swap_delay_file == s.rel))
                Sleep((DWORD)g_swap_delay_ms); // tests (BUG-0139): the target is gone until the next move
        }
        if (!MoveFileExW(s.fresh.c_str(), s.target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            err = why("cannot install", s.target);
            goto rollback;
        }
        s.placed = true;
    }
    if (!later.empty()) {
        std::wstring list = root + L"\\SporebinEP1\\mods\\sporecoop.pending";
        std::vector<uint8_t> old;
        HANDLE f = CreateFileW(list.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) { // keep entries of an earlier update
            char b[4096];
            DWORD got = 0;
            ReadFile(f, b, sizeof b, &got, nullptr);
            CloseHandle(f);
            old.assign(b, b + got);
        }
        old.insert(old.end(), later.begin(), later.end());
        if (!write_all(list, old)) {
            err = why("cannot write", list);
            goto rollback;
        }
    }
    sc_update_unlock(lock);
    if (pending) {
        *pending = 0;
        for (auto& s : steps)
            *pending += s.deferred;
    }
    return true;
rollback:
    for (auto& s : steps) {
        if (s.placed)
            DeleteFileW(s.target.c_str());
        if (s.moved_old)
            MoveFileExW(s.old.c_str(), s.target.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(s.fresh.c_str());
        s.deferred = false;
    }
    sc_update_unlock(lock);
    return false;
}

void apply_pending(const std::wstring& root)
{
    sc_pending_keep_failed = g_keep_failed_fix ? 1 : 0;
    sc_update_lock_fix = g_lock_fix ? 1 : 0;
    sc_apply_pending(root.c_str(), 0); // the replaced files: cleanup_old
}

// The backups an update made: "<file>.sporecoop-old" (BUG-0114). Before, every "*.old" in these
// folders was deleted, also a player's own SporeApp.exe.old or another mod's backup. Backups from
// versions before the fix were named "<file>.old": only those of SporeCoop's own files go.
bool own_legacy_backup(const std::wstring& dir_rel, const std::wstring& name, bool loader_old)
{
    std::wstring n;
    for (wchar_t c : name)
        n += (wchar_t)towlower(c);
    auto ends = [&](const wchar_t* t) {
        size_t k = wcslen(t);
        return n.size() >= k && n.compare(n.size() - k, k, t) == 0;
    };
    if (!ends(L".old"))
        return false;
    if (dir_rel == L"\\SporebinEP1")
        return n == L"sporekit.dll.old" || (n == L"steam_api.dll.old" && loader_old);
    if (dir_rel == L"\\SporebinEP1\\mods")
        return n == L"coop.dll.old" || n == L"sporecoop.version.old";
    if (dir_rel == L"\\SporebinEP1\\mods\\sporekit\\symbols")
        return ends(L".sym.old");
    if (dir_rel == L"\\Data")
        return n == L"sporecoop.package.old";
    return false;
}

static void cleanup_old_unlocked(const std::wstring& root);

void cleanup_old(const std::wstring& root)
{
    // BUG-0243: an update running in this folder (an old image's thread after a hot reload, another
    // instance) still needs its backups for a rollback: they go at the next start
    sc_update_lock_fix = g_lock_fix ? 1 : 0;
    HANDLE h;
    if (!sc_update_trylock(root.c_str(), &h))
        return;
    cleanup_old_unlocked(root);
    sc_update_unlock(h);
}

static void cleanup_old_unlocked(const std::wstring& root)
{
    for (const wchar_t* dir : {L"\\SporebinEP1", L"\\SporebinEP1\\mods", L"\\SporebinEP1\\mods\\sporekit\\symbols",
                               L"\\Data"}) {
        std::wstring d = root + dir;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::wstring n = fd.cFileName, p = d + L"\\" + n;
            size_t k = wcslen(SC_OLD_SUFFIX);
            bool ours = n.size() > k && _wcsicmp(n.c_str() + n.size() - k, SC_OLD_SUFFIX) == 0;
            if (!g_old_suffix_fix) // the old behaviour: every *.old
                ours = n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".old") == 0;
            else if (!ours)
                ours = own_legacy_backup(dir, n, _wcsicmp(n.c_str(), L"steam_api.dll.old") == 0 && is_loader(p));
            if (ours)
                DeleteFileW(p.c_str()); // still loaded: removed next time
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

std::string installed_version(const std::wstring& root)
{
    HANDLE f = CreateFileW((root + L"\\SporebinEP1\\mods\\sporecoop.version").c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    char b[64] = {};
    DWORD got = 0;
    ReadFile(f, b, sizeof b - 1, &got, nullptr);
    CloseHandle(f);
    std::string v(b, got);
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' '))
        v.pop_back();
    return v;
}

bool is_loader(const std::wstring& dll)
{
    HANDLE f = CreateFileW(dll.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    std::vector<char> b(256 << 10);
    DWORD got = 0;
    ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr);
    CloseHandle(f);
    static const char tag[] = "sporekit-loader";
    return std::search(b.begin(), b.begin() + got, tag, tag + sizeof tag - 1) != b.begin() + got;
}

} // namespace upd
