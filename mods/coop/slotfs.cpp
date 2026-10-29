// Received worlds on disk: see slotfs.h
#include "slotfs.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

namespace slotfs {

bool g_busy_fix = true, g_match_fix = true, g_write_fix = true;
bool g_fallback_fix = true, g_cleanup_fix = true;
std::wstring g_test_held;
int g_test_cleanup_age_s = 600;

std::string narrow(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool same_dir(std::wstring a, const std::wstring& lower_b);
static bool held(const std::wstring& p) { return !g_test_held.empty() && same_dir(p, g_test_held); }

static bool same_dir(std::wstring a, const std::wstring& lower_b)
{
    for (auto& ch : a)
        ch = towlower(ch);
    return a == lower_b;
}

static std::string read_text(const std::wstring& p)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    char b[1024];
    DWORD got = 0;
    if (!ReadFile(f, b, sizeof b, &got, nullptr))
        got = 0;
    CloseHandle(f);
    return std::string(b, got);
}

// "host_slot <id>\nhost <name>\nplanet <name>\n" (world.cpp writes it into every received slot)
static std::string marker_field(const std::string& m, const char* key)
{
    std::string k = std::string(key) + " ";
    for (size_t p = 0; p < m.size();) {
        size_t e = m.find('\n', p);
        if (e == std::string::npos)
            e = m.size();
        std::string line = m.substr(p, e - p);
        while (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.compare(0, k.size(), k) == 0)
            return line.substr(k.size());
        p = e + 1;
    }
    return {};
}

static void rmtree(const std::wstring& d)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..")
                continue;
            std::wstring p = d + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(p.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                rmtree(p);
            else
                DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}

static std::wstring stamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[64];
    swprintf(b, 64, L"-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

std::wstring pick(const std::wstring& dir, const std::wstring& busy, const std::string& host,
                  const std::string& planet, const std::string& planet_spo, bool backup_planet, std::string& note,
                  const std::vector<std::wstring>& avoid)
{
    CreateDirectoryW(dir.c_str(), nullptr);
    note.clear();
    // never the slot the game is playing now (a guest gets the next phase in-game): co-op worlds
    // then alternate between two slots. Nor a slot the caller could not replace (BUG-0254).
    auto is_busy = [&](const std::wstring& p) {
        if (!busy.empty() && same_dir(p, busy))
            return true;
        for (auto& a : avoid)
            if (same_dir(p, a))
                return true;
        return false;
    };
    std::wstring any_coop; // a co-op slot of another world (last resort when no GameN is free)
    for (int k = 0; k < 64; k++) {
        std::wstring p = dir + L"\\Game" + std::to_wstring(k);
        if (is_busy(p))
            continue;
        std::string m = read_text(p + L"\\sporecoop.txt");
        if (m.find("host_slot") == std::string::npos)
            continue;
        // the same host world: update it. Before (BUG-0087) any co-op slot was taken, also one of
        // another host, and the new files were mixed with the old ones. The world id (a crc of
        // the files) changes with every save, so the host's name and the planet identify it.
        if (!g_match_fix || (marker_field(m, "host") == host && marker_field(m, "planet") == planet))
            return p;
        if (any_coop.empty())
            any_coop = p;
    }
    if (!planet_spo.empty() && backup_planet) { // menu loading needs the disc of this planet free
        for (int k = 0; k < 64; k++) {
            std::wstring p = dir + L"\\Game" + std::to_wstring(k);
            // BUG-0035: not the slot being played (a guest with auto_load_world=0 playing its co-op
            // slot had it moved away and rewritten under the running game)
            if (g_busy_fix && is_busy(p))
                continue;
            if (!exists(p + L"\\" + widen(planet_spo)))
                continue;
            // keep the other copy of this planet outside the Games folder
            bool coop = read_text(p + L"\\sporecoop.txt").find("host_slot") != std::string::npos;
            std::wstring bdir = dir + L"-sporecoop-backup";
            CreateDirectoryW(bdir.c_str(), nullptr);
            std::wstring dst = bdir + L"\\Game" + std::to_wstring(k) + stamp();
            if (!MoveFileW(p.c_str(), dst.c_str()) && g_busy_fix)
                continue; // files in use: leave it, take another slot
            note = std::string(coop ? "an older co-op copy" : "your own save") + " of this planet was backed up to " +
                   narrow(dst);
            return p;
        }
    }
    for (int k = 0; k < 64; k++) {
        std::wstring p = dir + L"\\Game" + std::to_wstring(k);
        if (!exists(p))
            return p;
    }
    return g_match_fix ? any_coop : std::wstring();
}

int tidy(const std::wstring& dir, const std::wstring& kept, const std::wstring& busy, const std::string& host,
         const std::string& planet)
{
    std::wstring kl = kept;
    for (auto& c : kl)
        c = towlower(c);
    int moved = 0;
    for (int k = 0; k < 64; k++) {
        std::wstring p = dir + L"\\Game" + std::to_wstring(k);
        if (same_dir(p, kl) || (!busy.empty() && same_dir(p, busy)))
            continue;
        std::string m = read_text(p + L"\\sporecoop.txt");
        if (m.find("host_slot") == std::string::npos || marker_field(m, "host") != host || marker_field(m, "planet") != planet)
            continue;
        std::wstring bdir = dir + L"-sporecoop-backup";
        CreateDirectoryW(bdir.c_str(), nullptr);
        if (!held(p) && MoveFileW(p.c_str(), (bdir + L"\\Game" + std::to_wstring(k) + stamp()).c_str()))
            moved++;
    }
    return moved;
}

static bool write_one(const std::wstring& path, const std::vector<uint8_t>& d, std::string& err)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = "cannot create " + narrow(path) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    DWORD w = 0;
    bool ok = d.empty() || (WriteFile(f, d.data(), (DWORD)d.size(), &w, nullptr) && w == d.size());
    DWORD e = GetLastError();
    ok = CloseHandle(f) && ok;
    if (!ok)
        err = "cannot write " + narrow(path) + (e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL ? " (disk full)"
                                                : " (error " + std::to_string(e) + ")");
    return ok;
}

static std::wstring native(const std::wstring& root, const std::string& rel)
{
    std::wstring p = root + L"\\" + widen(rel);
    for (auto& c : p)
        if (c == L'/')
            c = L'\\';
    return p;
}

static bool write_into(const std::wstring& root, const std::vector<const File*>& files, const std::string& marker,
                       bool check, std::string& err)
{
    if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS && check) {
        err = "cannot create the folder " + narrow(root);
        return false;
    }
    for (const File* f : files) {
        std::wstring p = native(root, f->rel);
        for (size_t q = root.size() + 1; q < p.size(); q++) // sub folders
            if (p[q] == L'\\' && !CreateDirectoryW(p.substr(0, q).c_str(), nullptr) &&
                GetLastError() != ERROR_ALREADY_EXISTS && check) {
                err = "cannot create the folder " + narrow(p.substr(0, q));
                return false;
            }
        if (!write_one(p, f->data, err) && check)
            return false;
    }
    std::vector<uint8_t> m(marker.begin(), marker.end());
    return write_one(root + L"\\sporecoop.txt", m, err) || !check;
}

// "GameN-YYYYMMDD-HHMMSS-<n>" (a temp copy) or "GameN-old-YYYYMMDD-HHMMSS-<n>" (an old slot moved
// aside); `slot_name` gets "GameN". 0: neither.
static int leftover_kind(const std::wstring& n, std::wstring& slot_name)
{
    if (n.compare(0, 4, L"Game") != 0)
        return 0;
    size_t i = 4;
    while (i < n.size() && iswdigit(n[i]))
        i++;
    if (i == 4 || i >= n.size() || n[i] != L'-')
        return 0;
    slot_name = n.substr(0, i);
    std::wstring rest = n.substr(i + 1);
    bool old = rest.compare(0, 4, L"old-") == 0;
    if (old)
        rest = rest.substr(4);
    // YYYYMMDD-HHMMSS-<n>
    if (rest.size() < 17 || rest[8] != L'-' || rest[15] != L'-')
        return 0;
    for (size_t q = 0; q < rest.size(); q++)
        if (q != 8 && q != 15 && !iswdigit(rest[q]))
            return 0;
    return old ? 2 : 1;
}

// BUG-0254: folders left by a write that was interrupted (the game killed or crashed between the
// steps). Temp copies (incomplete new worlds) are deleted once older than 10 minutes (creation
// time, UTC: the name's stamp is local time). An aside copy is never deleted: it goes back into
// its slot when the slot is missing (the game died between the two moves), else to the backup
// folder. Runs under the write mutex, so no other instance is between its moves.
static void cleanup_leftovers(const std::wstring& parent, const std::wstring& tmproot)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((tmproot + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);
    ULARGE_INTEGER now{};
    now.LowPart = now_ft.dwLowDateTime;
    now.HighPart = now_ft.dwHighDateTime;
    std::vector<std::pair<std::wstring, int>> found;
    do {
        std::wstring n = fd.cFileName, slot_name;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || n == L"." || n == L"..")
            continue;
        int kind = leftover_kind(n, slot_name);
        if (kind == 1) {
            ULARGE_INTEGER c{};
            c.LowPart = fd.ftCreationTime.dwLowDateTime;
            c.HighPart = fd.ftCreationTime.dwHighDateTime;
            if (now.QuadPart > c.QuadPart && (now.QuadPart - c.QuadPart) / 10000000ull >= (uint64_t)g_test_cleanup_age_s)
                found.push_back({n, 1});
        } else if (kind == 2)
            found.push_back({n, 2});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (auto& [n, kind] : found) {
        std::wstring src = tmproot + L"\\" + n, slot_name;
        leftover_kind(n, slot_name);
        if (kind == 1) {
            rmtree(src);
            continue;
        }
        std::wstring slot = parent + L"\\" + slot_name;
        if (exists(slot) || !MoveFileW(src.c_str(), slot.c_str())) {
            std::wstring bdir = parent + L"-sporecoop-backup";
            CreateDirectoryW(bdir.c_str(), nullptr);
            MoveFileW(src.c_str(), (bdir + L"\\" + n).c_str());
        }
    }
}

static bool write_locked(const std::wstring& slot, const std::vector<const File*>& files, const std::string& host,
                         const std::string& planet, uint32_t world_id, std::string& err, WriteFail* why);

bool write(const std::wstring& slot, const std::vector<const File*>& files, const std::string& host,
           const std::string& planet, uint32_t world_id, std::string& err, WriteFail* why)
{
    if (why)
        *why = WriteFail::None;
    if (!g_write_fix || !g_cleanup_fix)
        return write_locked(slot, files, host, planet, world_id, err, why);
    // one writer per machine: another instance on the same profile must not see this one's aside
    // folder halfway (and tidy it away)
    HANDLE mx = CreateMutexW(nullptr, FALSE, L"Local\\SporeCoopSlotWrite");
    DWORD w = mx ? WaitForSingleObject(mx, 10000) : WAIT_FAILED;
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        if (mx)
            CloseHandle(mx);
        err = "another Spore is writing a world";
        if (why)
            *why = WriteFail::Busy;
        return false;
    }
    size_t cut = slot.find_last_of(L'\\');
    std::wstring parent = slot.substr(0, cut);
    cleanup_leftovers(parent, parent + L"-sporecoop-tmp");
    bool ok = write_locked(slot, files, host, planet, world_id, err, why);
    ReleaseMutex(mx);
    CloseHandle(mx);
    return ok;
}

static bool write_locked(const std::wstring& slot, const std::vector<const File*>& files, const std::string& host,
                         const std::string& planet, uint32_t world_id, std::string& err, WriteFail* why)
{
    char head[32];
    snprintf(head, sizeof head, "host_slot %08X\n", world_id);
    std::string marker = head + ("host " + host + "\nplanet " + planet + "\n");
    if (!g_write_fix) { // the old behaviour: in place, every failure skipped
        write_into(slot, files, marker, false, err);
        err.clear();
        return true;
    }
    size_t cut = slot.find_last_of(L'\\');
    std::wstring parent = slot.substr(0, cut), name = slot.substr(cut + 1);
    std::wstring tmproot = parent + L"-sporecoop-tmp"; // next to Games: the galaxy menu never sees it
    CreateDirectoryW(tmproot.c_str(), nullptr);
    std::wstring tag = stamp() + L"-" + std::to_wstring(GetTickCount64() % 100000);
    std::wstring tmp = tmproot + L"\\" + name + tag, aside = tmproot + L"\\" + name + L"-old" + tag;
    rmtree(tmp);
    if (!write_into(tmp, files, marker, true, err)) {
        rmtree(tmp);
        if (why)
            *why = WriteFail::Files;
        return false;
    }
    bool had = exists(slot);
    if (had && (held(slot) || !MoveFileW(slot.c_str(), aside.c_str()))) {
        err = "cannot replace " + narrow(slot) + " (a file in it is in use, error " +
              std::to_string(held(slot) ? 32 : GetLastError()) + ")";
        rmtree(tmp);
        RemoveDirectoryW(tmproot.c_str());
        if (why)
            *why = WriteFail::Replace;
        return false;
    }
    if (!MoveFileW(tmp.c_str(), slot.c_str())) {
        if (why)
            *why = WriteFail::Move;
        err = "cannot move the world into " + narrow(slot) + " (error " + std::to_string(GetLastError()) + ")";
        if (had)
            MoveFileW(aside.c_str(), slot.c_str());
        rmtree(tmp);
        return false;
    }
    if (had)
        rmtree(aside);
    RemoveDirectoryW(tmproot.c_str()); // only when empty
    return true;
}

bool write_or_fallback(const std::wstring& games, std::wstring& slot, const std::wstring& busy,
                       const std::vector<const File*>& files, const std::string& host, const std::string& planet,
                       const std::string& planet_spo, uint32_t world_id, std::string& err, bool& moved)
{
    moved = false;
    WriteFail why = WriteFail::None;
    bool ok = write(slot, files, host, planet, world_id, err, &why);
    if (!ok && why == WriteFail::Replace && g_fallback_fix) {
        std::wstring held = slot;
        for (auto& c : held)
            c = towlower(c);
        std::string note;
        // one other slot at most: the held one is avoided, so a second same-host slot (an extra copy
        // of an earlier fallback) is reused before a free one
        std::wstring other = pick(games, busy, host, planet, planet_spo, false, note, {held});
        if (other.empty()) {
            err += "; no other free slot";
            return false;
        }
        std::string err2;
        if (!write(other, files, host, planet, world_id, err2, &why)) {
            err += "; " + err2;
            return false;
        }
        slot = other;
        moved = true;
        ok = true;
        err.clear();
    }
    if (ok && g_fallback_fix)
        tidy(games, slot, busy, host, planet);
    return ok;
}

} // namespace slotfs
