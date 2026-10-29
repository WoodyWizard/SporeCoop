// Windows-side unit tests: the file handling of the updater and of received worlds against the
// real Win32 file API (Wine in a lane's bottle: tools/tests/win_unit.py; or Windows). Each test
// works in its own folder under %TEMP%. `win_tests.exe --old` switches the fixes off: every
// check of a fixed bug must then fail.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../installer/install.h"
#include "../mods/coop/slotfs.h"
#include "../sporekit/inifile.h"
#include "../updater/winupdate.h"

static int g_fail;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);   \
            g_fail++;                                               \
        }                                                           \
    } while (0)

static std::wstring g_tmp;
static bool g_old;          // --old
static std::wstring g_bat;  // --bat <install.bat to test> (tools/tests/win_unit.py)
static std::wstring g_excl; // --exclude <installer/ini.exclude>

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void put(const std::wstring& p, const std::string& text)
{
    for (size_t q = g_tmp.size() + 1; q < p.size(); q++)
        if (p[q] == L'\\')
            CreateDirectoryW(p.substr(0, q).c_str(), nullptr);
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w;
    WriteFile(f, text.data(), (DWORD)text.size(), &w, nullptr);
    CloseHandle(f);
}

static std::string get(const std::wstring& p)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return "<missing>";
    char b[4096];
    DWORD got = 0;
    ReadFile(f, b, sizeof b, &got, nullptr);
    CloseHandle(f);
    return std::string(b, got);
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
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                rmtree(p);
            else
                DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}

static std::wstring fresh_dir(const wchar_t* name)
{
    std::wstring d = g_tmp + L"\\" + name;
    rmtree(d);
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

// BUG-0114: only the updater's own backups are removed, never the player's *.old files
static void test_cleanup_old()
{
    printf("updater: cleanup_old keeps the player's *.old files\n");
    std::wstring g = fresh_dir(L"cleanup");
    put(g + L"\\SporebinEP1\\SporeApp.exe.old", "player's backup");
    put(g + L"\\Data\\keep.old", "another mod");
    put(g + L"\\SporebinEP1\\mods\\othermod.ini.old", "another mod");
    put(g + L"\\SporebinEP1\\steam_api.dll.old", "the player's own steam_api backup (not our loader)");
    put(g + L"\\SporebinEP1\\sporekit.dll.sporecoop-old", "ours");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old", "ours");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.old", "ours, from a version before the fix");
    put(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym.old", "ours, old version");
    put(g + L"\\Data\\SporeCoop.package.sporecoop-old", "ours");
    upd::cleanup_old(g);
    CHECK(exists(g + L"\\SporebinEP1\\SporeApp.exe.old"));
    CHECK(exists(g + L"\\Data\\keep.old"));
    CHECK(exists(g + L"\\SporebinEP1\\mods\\othermod.ini.old"));
    CHECK(exists(g + L"\\SporebinEP1\\steam_api.dll.old"));
    CHECK(!exists(g + L"\\SporebinEP1\\sporekit.dll.sporecoop-old") || !upd::g_old_suffix_fix);
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old") || !upd::g_old_suffix_fix);
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\coop.dll.old"));
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym.old"));
    CHECK(!exists(g + L"\\Data\\SporeCoop.package.sporecoop-old") || !upd::g_old_suffix_fix);
}

// BUG-0089 (in apply_pack too) and the backup name: a pack replaces a file, the backup is ours
static void test_apply_pack()
{
    printf("updater: apply_pack backups and a file listed twice\n");
    std::wstring g = fresh_dir(L"apply");
    put(g + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
    put(g + L"\\SporebinEP1\\mods\\coop.dll", "ORIGINAL");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.old", "player's backup");
    upd::Pack p;
    p.version = "9";
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'N', '1'}});
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'N', '2'}});
    std::string err;
    bool ok = upd::apply_pack(g, p, err);
    CHECK(!ok); // a file listed twice: nothing installed
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "ORIGINAL");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.old") == "player's backup");
    p.files.pop_back();
    CHECK(upd::apply_pack(g, p, err));
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "N1");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.old") == "player's backup");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old") == "ORIGINAL");
}

// BUG-0150: a deferred file whose swap fails at the next start stays listed and is applied later
static void test_pending()
{
    printf("updater: apply_pending keeps an entry whose swap failed\n");
    std::wstring g = fresh_dir(L"pending");
    std::wstring pkg = g + L"\\Data\\SporeCoop.package", list = g + L"\\SporebinEP1\\mods\\sporecoop.pending";
    put(pkg, "OLD");
    put(pkg + L".new", "NEW");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.new", "NEWDLL");
    put(g + L"\\SporebinEP1\\mods\\coop.dll", "OLDDLL");
    put(list, "Data/SporeCoop.package\nSporebinEP1/mods/coop.dll\n");
    // another instance of the game holds the package (no sharing at all)
    HANDLE hold = CreateFileW(pkg.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    upd::apply_pending(g);
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "NEWDLL");
    CloseHandle(hold);
    CHECK(exists(list));
    CHECK(get(list) == "Data/SporeCoop.package\n");
    upd::apply_pending(g); // the start after that
    CHECK(get(pkg) == "NEW");
    CHECK(!exists(pkg + L".new"));
    CHECK(!exists(list));
}

// BUG-0035, BUG-0087, BUG-0105: where a received world goes and how it is written
static std::vector<slotfs::File> world(const char* planet, const char* data)
{
    std::vector<slotfs::File> v;
    v.push_back({std::string(planet) + ".spo", std::vector<uint8_t>(data, data + strlen(data))});
    v.push_back({"stars.db", std::vector<uint8_t>(data, data + strlen(data))});
    v.push_back({"sub/x.pkp", std::vector<uint8_t>(data, data + strlen(data))});
    return v;
}

static bool wr(const std::wstring& slot, const std::vector<slotfs::File>& w, const char* host, const char* planet,
               uint32_t id, std::string& err)
{
    std::vector<const slotfs::File*> v;
    for (auto& f : w)
        v.push_back(&f);
    return slotfs::write(slot, v, host, planet, id, err);
}

static std::wstring lower(std::wstring s)
{
    for (auto& c : s)
        c = towlower(c);
    return s;
}

static void test_slot_busy()
{
    printf("world: the slot the game plays is never moved or written (BUG-0035)\n");
    std::wstring games = fresh_dir(L"busy") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    auto w = world("Terra", "A1");
    std::wstring s0 = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(s0, w, "Alice", "Terra", 0x11, err));
    // the guest loads that slot by hand (auto_load_world=0) and plays it; the host shares again
    put(s0 + L"\\guest-progress.txt", "the guest's game");
    auto w2 = world("Terra", "A2");
    std::wstring s1 = slotfs::pick(games, lower(s0), "Alice", "Terra", "Terra.spo", true, note);
    CHECK(!s1.empty());
    CHECK(lower(s1) != lower(s0));
    if (!s1.empty() && lower(s1) != lower(s0))
        CHECK(wr(s1, w2, "Alice", "Terra", 0x22, err));
    CHECK(get(s0 + L"\\Terra.spo") == "A1");
    CHECK(get(s0 + L"\\guest-progress.txt") == "the guest's game");
    CHECK(get(s1 + L"\\Terra.spo") == "A2");
    // the next share while the guest still plays s0: s1 is updated, s0 still untouched
    std::wstring s2 = slotfs::pick(games, lower(s0), "Alice", "Terra", "Terra.spo", true, note);
    CHECK(lower(s2) == lower(s1));
}

static void test_slot_match()
{
    printf("world: a slot is reused only for the same host world and replaced entirely (BUG-0087)\n");
    std::wstring games = fresh_dir(L"match") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A"), "Alice", "Terra", 1, err));
    std::wstring b = slotfs::pick(games, L"", "Bob", "Vulcan", "Vulcan.spo", true, note);
    CHECK(lower(a) != lower(b));
    if (lower(a) != lower(b))
        CHECK(wr(b, world("Vulcan", "B"), "Bob", "Vulcan", 2, err));
    CHECK(get(a + L"\\Terra.spo") == "A");
    CHECK(!exists(b + L"\\Terra.spo"));
    // the same host world again: its slot, with no file of the old copy left
    put(a + L"\\Oldplanet.spo", "an old planet of that slot");
    std::wstring a2 = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(lower(a2) == lower(a));
    CHECK(wr(a2, world("Terra", "A3"), "Alice", "Terra", 3, err));
    CHECK(get(a + L"\\Terra.spo") == "A3");
    CHECK(!exists(a + L"\\Oldplanet.spo") || !slotfs::g_write_fix);
}

static void test_slot_write_fails()
{
    printf("world: a world whose files could not all be written is not kept (BUG-0105)\n");
    std::wstring games = fresh_dir(L"wfail") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A"), "Alice", "Terra", 1, err));
    // a file of the slot is held open by another program (antivirus, backup tool)
    HANDLE hold = CreateFileW((a + L"\\stars.db").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    err.clear();
    bool ok = wr(a, world("Terra", "NEW"), "Alice", "Terra", 2, err);
    CloseHandle(hold);
    // Windows refuses to move a folder with a file open in it: the write fails and says why; Wine
    // moves it. Either way the slot holds one whole world, never a mix of both.
    CHECK(ok || !err.empty());
    printf("  held file: write %s (%s)\n", ok ? "succeeded" : "refused", err.c_str());
    CHECK(get(a + L"\\Terra.spo") == get(a + L"\\stars.db"));
    // a file that cannot be created (a name Windows refuses): the world is not written at all
    std::wstring b = slotfs::pick(games, L"", "Bob", "Vulcan", "Vulcan.spo", true, note);
    auto bad = world("Vulcan", "B");
    bad.push_back({"sub/a\x01:b", {1}});
    err.clear();
    CHECK(!wr(b, bad, "Bob", "Vulcan", 3, err));
    CHECK(!exists(b + L"\\sporecoop.txt"));
}

static int count_same_host(const std::wstring& games, const char* host)
{
    int n = 0;
    for (int k = 0; k < 64; k++) {
        std::string m = get(games + L"\\Game" + std::to_wstring(k) + L"\\sporecoop.txt");
        if (m.find(std::string("host ") + host + "\n") != std::string::npos)
            n++;
    }
    return n;
}

// BUG-0254: the old copy of the host's world cannot be replaced (Windows: a file in it is open by
// another program; slotfs::g_test_fail_replace makes Wine refuse the same way)
static void test_slot_fallback()
{
    printf("world: a slot that cannot be replaced gives way to another slot, tidied later (BUG-0254)\n");
    std::wstring games = fresh_dir(L"fallback") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    auto files = [](const std::vector<slotfs::File>& w) {
        std::vector<const slotfs::File*> v;
        for (auto& f : w)
            v.push_back(&f);
        return v;
    };
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A1"), "Alice", "Terra", 1, err));
    // the guest plays a save of its own (busy, never chosen)
    std::wstring own = games + L"\\Game5";
    put(own + L"\\Mine.spo", "the guest's own game");
    slotfs::g_test_held = lower(a);
    auto w2 = world("Terra", "A2");
    std::wstring slot = slotfs::pick(games, lower(own), "Alice", "Terra", "Terra.spo", true, note);
    CHECK(lower(slot) == lower(a));
    bool moved = false;
    err.clear();
    bool ok = slotfs::write_or_fallback(games, slot, lower(own), files(w2), "Alice", "Terra", "Terra.spo", 2, err, moved);
    printf("  held slot: %s, moved %d (%s)\n", ok ? "written" : "refused", (int)moved, err.c_str());
    CHECK(ok && moved);
    CHECK(lower(slot) != lower(a) && lower(slot) != lower(own));
    CHECK(get(slot + L"\\Terra.spo") == "A2");
    CHECK(get(slot + L"\\sporecoop.txt").find("host Alice\n") != std::string::npos);
    CHECK(get(a + L"\\Terra.spo") == "A1"); // the held copy unchanged
    CHECK(get(own + L"\\Mine.spo") == "the guest's own game");
    slotfs::g_test_held.clear();
    // the hold is released: the next share writes the lowest same-host slot and tidies the extra copy
    std::wstring s3 = slotfs::pick(games, lower(own), "Alice", "Terra", "Terra.spo", true, note);
    err.clear();
    CHECK(slotfs::write_or_fallback(games, s3, lower(own), files(world("Terra", "A3")), "Alice", "Terra", "Terra.spo", 3, err, moved));
    CHECK(count_same_host(games, "Alice") == 1);
    CHECK(get(s3 + L"\\Terra.spo") == "A3");
    CHECK(get(own + L"\\Mine.spo") == "the guest's own game");
}

static void set_age(const std::wstring& dir, int seconds)
{
    HANDLE h = CreateFileW(dir.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart -= (ULONGLONG)seconds * 10000000ull;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    if (h != INVALID_HANDLE_VALUE) {
        SetFileTime(h, &ft, nullptr, nullptr);
        CloseHandle(h);
    }
}

static DWORD WINAPI busy_writer(void* p)
{
    auto* games = (std::wstring*)p;
    std::string e;
    slotfs::WriteFail why = slotfs::WriteFail::None;
    std::vector<slotfs::File> w = world("Terra", "B");
    std::vector<const slotfs::File*> v;
    for (auto& f : w)
        v.push_back(&f);
    bool ok = slotfs::write(*games + L"\\Game1", v, "Bob", "Terra", 2, e, &why);
    return ok ? 0 : (DWORD)why;
}

// BUG-0254: folders left in Games-sporecoop-tmp by writes that were interrupted
static void test_slot_leftovers()
{
    printf("world: leftovers of interrupted writes are tidied, an old world is never lost (BUG-0254)\n");
    std::wstring root = fresh_dir(L"leftover"), games = root + L"\\Games", tmp = games + L"-sporecoop-tmp",
                 bak = games + L"-sporecoop-backup";
    CreateDirectoryW(games.c_str(), nullptr);
    CreateDirectoryW(tmp.c_str(), nullptr);
    std::string err;
    put(tmp + L"\\Game3-20260101-101010-123\\stars.db", "old temp");
    set_age(tmp + L"\\Game3-20260101-101010-123", 3600);
    Sleep(3000); // (Wine may not keep a creation time set by hand: the age rule is 2 s here)
    put(tmp + L"\\Game3-20260101-101011-124\\stars.db", "fresh temp");
    put(tmp + L"\\Game4-old-20260101-101010-125\\Terra.spo", "the only copy of Game4");
    put(games + L"\\Game6\\Vulcan.spo", "Game6 now");
    put(tmp + L"\\Game6-old-20260101-101010-126\\Vulcan.spo", "an older Game6");
    put(tmp + L"\\notes\\x.txt", "not ours");
    slotfs::g_test_cleanup_age_s = 2;
    CHECK(wr(games + L"\\Game0", world("Terra", "T"), "Alice", "Terra", 1, err));
    slotfs::g_test_cleanup_age_s = 600;
    CHECK(!exists(tmp + L"\\Game3-20260101-101010-123"));    // an old temp copy: deleted
    CHECK(exists(tmp + L"\\Game3-20260101-101011-124"));     // a fresh one (maybe another write): kept
    CHECK(get(games + L"\\Game4\\Terra.spo") == "the only copy of Game4"); // back into its slot
    CHECK(get(games + L"\\Game6\\Vulcan.spo") == "Game6 now");
    CHECK(get(bak + L"\\Game6-old-20260101-101010-126\\Vulcan.spo") == "an older Game6"); // kept aside
    CHECK(exists(tmp + L"\\notes\\x.txt"));
    // another instance holds the write lock: nothing is touched
    HANDLE mx = CreateMutexW(nullptr, TRUE, L"Local\\SporeCoopSlotWrite");
    if (slotfs::g_cleanup_fix) {
        // the mutex is owned by this thread here, so take it from another thread
        HANDLE th = CreateThread(nullptr, 0, busy_writer, &games, 0, nullptr);
        WaitForSingleObject(th, 30000);
        DWORD code = 99;
        GetExitCodeThread(th, &code);
        CloseHandle(th);
        CHECK(code == (DWORD)slotfs::WriteFail::Busy);
        CHECK(!exists(games + L"\\Game1"));
    }
    ReleaseMutex(mx);
    CloseHandle(mx);
}

// BUG-0142: a name outside the ANSI codepage survives a write and a read through the profile API;
// a coop.ini edited by hand in UTF-8 (with or without a BOM) is read as UTF-8
static void test_ini_unicode()
{
    std::wstring f = g_tmp + L"\\coop.ini";
    const char* name = "\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F \xE5\xA4\xAA\xE9\x83\x8E"; // "Вася 太郎"
    put(f, "; SporeCoop settings\r\n[coop]\r\nname=Player\r\nshare_world=1\r\n");
    CHECK(skini::write_str(f, "coop", "name", name));
    CHECK(skini::read_str(f, "coop", "name", "") == name);
    CHECK(skini::read_int(f, "coop", "share_world", 0) == 1); // the rest of the file is kept
    std::wstring g = g_tmp + L"\\hand.ini";
    put(g, "[coop]\r\nname=J\xC3\xB6rg\r\n"); // UTF-8 without a BOM (Notepad, any Linux editor)
    CHECK(skini::read_str(g, "coop", "name", "") == "J\xC3\xB6rg");
    std::wstring h = g_tmp + L"\\bom.ini";
    put(h, "\xEF\xBB\xBF[coop]\r\nname=\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F\r\n"); // UTF-8 with a BOM
    CHECK(skini::read_str(h, "coop", "name", "") == "\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F");
    std::wstring a = g_tmp + L"\\ascii.ini";
    put(a, "[sporekit]\r\nlog_level=1\r\n");
    CHECK(skini::read_str(a, "sporekit", "log_level", "") == "1");
    CHECK(get(a) == "[sporekit]\r\nlog_level=1\r\n"); // ASCII files stay ASCII (tools edit them with sed)
    std::wstring m = g_tmp + L"\\missing.ini"; // a new file gets a non-ASCII value
    CHECK(skini::write_str(m, "coop", "name", name));
    CHECK(skini::read_str(m, "coop", "name", "") == name);

    // BUG-0244: the coop.ini every upgrading player has, written by 0.7.2.x through the profile API in
    // the ANSI codepage ("J\xF6rg" in cp1252): read as CP_ACP (it is not UTF-8) and converted once
    UINT acp = GetACP();
    printf("  ANSI codepage %u\n", acp);
    if (acp == CP_UTF8) {
        printf("  NOT RUN: the ANSI codepage is UTF-8 (the ANSI case equals the UTF-8 case)\n");
        return;
    }
    const wchar_t* cands[] = {L"J\u00f6rg", L"\u0412\u0430\u0441\u044f", L"\u03a3\u03bf\u03c6\u03af\u03b1", L"\u592a\u90ce"};
    std::string ansi;
    std::wstring wname;
    for (const wchar_t* c : cands) {
        BOOL used = FALSE;
        char buf[64];
        int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, c, -1, buf, sizeof buf, nullptr, &used);
        if (n > 1 && !used) {
            ansi.assign(buf, n - 1);
            wname = c;
            break;
        }
    }
    CHECK(!ansi.empty());
    if (ansi.empty())
        return;
    std::wstring o = g_tmp + L"\\ansi.ini";
    put(o, "[coop]\r\nname=" + ansi + "\r\nshare_world=1\r\n");
    char u8[64];
    int un = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, u8, sizeof u8, nullptr, nullptr);
    CHECK(skini::read_str(o, "coop", "name", "") == std::string(u8, un - 1));
    CHECK(skini::read_int(o, "coop", "share_world", 0) == 1);
    std::string disk = get(o); // converted once: UTF-16 LE with a BOM (the caches have no reset: read the bytes)
    std::wstring want = L"name=" + wname;
    CHECK(disk.size() >= 2 && (unsigned char)disk[0] == 0xFF && (unsigned char)disk[1] == 0xFE &&
          disk.find(std::string((const char*)want.data(), want.size() * sizeof(wchar_t))) != std::string::npos);
}

// ---------------- the setup and install.bat ----------------

static std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// a pack of version v as tools/mkpack.py builds it (the files a release installs)
static upd::Pack make_pack(const std::string& v)
{
    upd::Pack p;
    p.version = v;
    p.files.push_back({"SporebinEP1/steam_api.dll", 0, bytes("sporekit-loader " + v)});
    p.files.push_back({"SporebinEP1/sporekit.dll", 0, bytes("sporekit " + v)});
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, bytes("coop " + v)});
    p.files.push_back({"SporebinEP1/mods/sporekit/symbols/gog2024_dc04aee5.sym", 0, bytes("sym " + v)});
    p.files.push_back({"Data/SporeCoop.package", 0, bytes("package " + v)});
    p.files.push_back({"SporebinEP1/mods/coop.ini", upd::kOnlyIfMissing, bytes("[coop]\r\nname=Player\r\n")});
    p.files.push_back({"SporebinEP1/mods/sporecoop.version", 0, bytes(v + "\n")});
    return p;
}

static void put_bytes(const std::wstring& p, const std::vector<uint8_t>& b) { put(p, std::string(b.begin(), b.end())); }

static std::wstring fake_game(const wchar_t* name)
{
    std::wstring g = fresh_dir(name);
    put(g + L"\\SporebinEP1\\SporeApp.exe", "not the game");
    put(g + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
    CreateDirectoryW((g + L"\\Data").c_str(), nullptr);
    return g;
}

// every file of version v in place (the setup's result is one version, never a mix)
static bool all_of_version(const std::wstring& g, const std::string& v, bool quiet = false)
{
    bool ok = get(g + L"\\SporebinEP1\\steam_api.dll") == "sporekit-loader " + v &&
              get(g + L"\\SporebinEP1\\sporekit.dll") == "sporekit " + v &&
              get(g + L"\\SporebinEP1\\mods\\coop.dll") == "coop " + v &&
              get(g + L"\\Data\\SporeCoop.package") == "package " + v && upd::installed_version(g) == v;
    if (!ok && !quiet)
        printf("  state: steam_api=%s sporekit=%s coop=%s version=%s\n",
               get(g + L"\\SporebinEP1\\steam_api.dll").c_str(), get(g + L"\\SporebinEP1\\sporekit.dll").c_str(),
               get(g + L"\\SporebinEP1\\mods\\coop.dll").c_str(), upd::installed_version(g).c_str());
    return ok;
}

// BUG-0094: offline the setup took the first SporeCoop-*.pack listed and installed it over a newer
// version without asking
static void test_setup_packs()
{
    printf("setup: the newest local pack, no silent downgrade (BUG-0094)\n");
    std::wstring g = fake_game(L"setup_packs");
    std::wstring dir = fresh_dir(L"setup_packs_dl");
    for (const char* v : {"0.10.0", "0.11.0", "0.9.0"})
        put_bytes(dir + L"\\SporeCoop-" + upd::widen(v) + L".pack", upd::write_pack(make_pack(v)));
    inst::Options o;
    o.pack_dir = dir;
    o.api = "http://127.0.0.1:9/"; // offline
    std::wstring lp = inst::local_pack(o);
    printf("  local pack: %s\n", upd::narrow(lp).c_str());
    CHECK(lp == dir + L"\\SporeCoop-0.11.0.pack" || g_old); // the listing order under Wine is the host's
    upd::ReleaseBox box;
    std::string msg;
    o.pack = dir + L"\\SporeCoop-0.11.0.pack";
    CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok);
    CHECK(all_of_version(g, "0.11.0"));
    put(g + L"\\SporebinEP1\\mods\\coop.ini", "[coop]\r\nname=Alice\r\n");
    // an older setup next to an old pack, offline: "Reinstall"
    o.pack = dir + L"\\SporeCoop-0.9.0.pack";
    std::string older, have;
    inst::Result r = inst::do_install(g, o, box, msg, {}, &older, &have);
    printf("  older pack: %s\n", msg.c_str());
    CHECK(r == inst::Result::Older);
    CHECK(older == "0.9.0" && have == "0.11.0");
    CHECK(all_of_version(g, "0.11.0"));
    // the player said yes (or /downgrade)
    o.downgrade = true;
    CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok);
    CHECK(all_of_version(g, "0.9.0"));
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.ini") == "[coop]\r\nname=Alice\r\n");
}

struct FindWnd {
    DWORD pid;
    HWND w;
};

static BOOL CALLBACK find_wnd(HWND w, LPARAM lp)
{
    auto* f = (FindWnd*)lp;
    DWORD p = 0;
    wchar_t cls[64] = {};
    GetWindowThreadProcessId(w, &p);
    GetClassNameW(w, cls, 64);
    if (p == f->pid && !wcscmp(cls, L"SporeCoopSetup")) {
        f->w = w;
        return FALSE;
    }
    return TRUE;
}

static HWND setup_window(DWORD pid)
{
    FindWnd f{pid, nullptr};
    EnumWindows(find_wnd, (LPARAM)&f);
    return f.w;
}

static std::wstring exe_dir()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring s = self;
    return s.substr(0, s.find_last_of(L"\\/"));
}

// BUG-0139: the window could be closed while the job swapped files: ExitProcess killed the job
// between a file's two moves (here: with a test delay of 1.5 s between them)
static void test_setup_close()
{
    printf("setup: closing the window during an install (BUG-0139)\n");
    std::wstring setup = exe_dir() + L"\\SporeCoopSetup.exe";
    if (!exists(setup)) {
        printf("  FAIL no %s\n", upd::narrow(setup).c_str());
        g_fail++;
        return;
    }
    std::wstring g = fake_game(L"setup_close");
    std::wstring dir = fresh_dir(L"setup_close_dl");
    put_bytes(dir + L"\\old.pack", upd::write_pack(make_pack("0.9.0")));
    put_bytes(dir + L"\\new.pack", upd::write_pack(make_pack("0.11.0")));
    {
        inst::Options o;
        o.pack = dir + L"\\old.pack";
        o.pack_dir = dir;
        upd::ReleaseBox box;
        std::string msg;
        CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok); // 0.9.0 installed
    }
    std::wstring cmd = L"\"" + setup + L"\" \"/dir:" + g + L"\" \"/pack:" + dir + L"\\new.pack\" /api:http://127.0.0.1:9/" +
                       (g_old ? L" /oldfix" : L"");
    SetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DELAY", L"1500");
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    SetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DELAY", nullptr);
    CHECK(started);
    if (!started)
        return;
    HWND w = nullptr;
    for (int i = 0; i < 100 && !w; i++) {
        Sleep(100);
        w = setup_window(pi.dwProcessId);
    }
    CHECK(w != nullptr);
    HWND btn = w ? GetDlgItem(w, 11) : nullptr; // ID_INSTALL
    for (int i = 0; i < 50 && btn && !IsWindowEnabled(btn); i++)
        Sleep(100);
    if (w) {
        PostMessageW(w, WM_COMMAND, MAKEWPARAM(11, BN_CLICKED), (LPARAM)btn);
        Sleep(800);                       // the new files are written, the first swap waits
        PostMessageW(w, WM_CLOSE, 0, 0);  // the window's X (Alt+F4)
        PostMessageW(w, WM_COMMAND, MAKEWPARAM(13, BN_CLICKED), (LPARAM)GetDlgItem(w, 13)); // Close
    }
    bool closed_early = WaitForSingleObject(pi.hProcess, 2000) == WAIT_OBJECT_0;
    printf("  closed during the job: %s\n", closed_early ? "yes" : "no (refused)");
    // the job ends (5 files x 1.5 s), then the window closes
    for (int i = 0; i < 200 && !closed_early && !all_of_version(g, "0.11.0", true); i++)
        Sleep(100);
    if (!closed_early && w)
        PostMessageW(w, WM_CLOSE, 0, 0);
    bool ended = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
    if (!ended)
        TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CHECK(ended);
    CHECK(exists(g + L"\\SporebinEP1\\steam_api.dll"));
    CHECK(all_of_version(g, "0.11.0"));
}

static bool run_bat(const std::wstring& bin)
{
    std::wstring cmd = L"cmd.exe /c install.bat < NUL > NUL";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, bin.c_str(), &si, &pi))
        return false;
    bool ok = WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0;
    if (!ok)
        TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return ok;
}

// BUG-0080: install.bat run again over an installed SporeCoop (the documented manual update)
// replaced mods\coop.ini: the player's name and the [update] check=0 opt-out were lost
static void test_install_bat()
{
    printf("install.bat keeps the player's .ini files (BUG-0080)\n");
    if (g_bat.empty()) {
        printf("  (skipped: no --bat)\n");
        return;
    }
    std::wstring g = fake_game(L"bat");
    std::wstring bin = g + L"\\SporebinEP1", sc = bin + L"\\SporeCoop";
    CHECK(CopyFileW(g_bat.c_str(), (bin + L"\\install.bat").c_str(), FALSE));
    put(sc + L"\\steam_api.sporekit.dll", "sporekit-loader 1");
    put(sc + L"\\sporekit.dll", "sporekit 1");
    put(sc + L"\\SporeCoop.package", "package 1");
    put(sc + L"\\mods\\coop.dll", "coop 1");
    put(sc + L"\\mods\\coop.ini", "[coop]\r\nname=Player\r\n");
    put(sc + L"\\mods\\sporekit.ini", "[kit]\r\nconsole=1\r\n");
    put(sc + L"\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym", "sym 1");
    if (!g_excl.empty()) // tools/package.sh puts it there
        CHECK(CopyFileW(g_excl.c_str(), (sc + L"\\ini.exclude").c_str(), FALSE));
    CHECK(run_bat(bin));
    CHECK(get(bin + L"\\mods\\coop.dll") == "coop 1");
    CHECK(get(bin + L"\\mods\\coop.ini") == "[coop]\r\nname=Player\r\n"); // a fresh install gets the defaults
    CHECK(get(bin + L"\\mods\\sporekit.ini") == "[kit]\r\nconsole=1\r\n");
    CHECK(get(bin + L"\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym") == "sym 1");
    CHECK(get(bin + L"\\steam_api.dll") == "sporekit-loader 1");
    CHECK(get(bin + L"\\steam_api_orig.dll") == "the game's steam api");
    CHECK(get(g + L"\\Data\\SporeCoop.package") == "package 1");
    // the player plays: a name, no update check; then the next zip over it
    const std::string mine = "[coop]\r\nname=Alice\r\n[update]\r\ncheck=0\r\n";
    put(bin + L"\\mods\\coop.ini", mine);
    DeleteFileW((bin + L"\\mods\\sporekit.ini").c_str());
    put(sc + L"\\mods\\coop.dll", "coop 2");
    put(sc + L"\\sporekit.dll", "sporekit 2");
    put(sc + L"\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym", "sym 2");
    CHECK(run_bat(bin));
    printf("  coop.ini after the reinstall: %s\n", get(bin + L"\\mods\\coop.ini") == mine ? "kept" : "replaced");
    CHECK(get(bin + L"\\mods\\coop.ini") == mine);
    CHECK(get(bin + L"\\mods\\sporekit.ini") == "[kit]\r\nconsole=1\r\n"); // a missing one comes back
    CHECK(get(bin + L"\\mods\\coop.dll") == "coop 2");
    CHECK(get(bin + L"\\sporekit.dll") == "sporekit 2");
    CHECK(get(bin + L"\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym") == "sym 2");
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--old"))
            g_old = true;
        else if (!strcmp(argv[i], "--old-suffix")) // BUG-0114 alone (update.old_suffix)
            upd::g_old_suffix_fix = false;
        else if (!strcmp(argv[i], "--old-keep")) // BUG-0150 alone (update.keep_failed)
            upd::g_keep_failed_fix = false;
        else if (!strcmp(argv[i], "--bat") && i + 1 < argc)
            g_bat = upd::widen(argv[++i]);
        else if (!strcmp(argv[i], "--exclude") && i + 1 < argc)
            g_excl = upd::widen(argv[++i]);
    }
    if (g_old) {
        inst::g_fix = false;
        upd::g_old_suffix_fix = upd::g_keep_failed_fix = false;
        upd::g_dup_fix = false;
        slotfs::g_busy_fix = slotfs::g_match_fix = slotfs::g_write_fix = false;
        slotfs::g_fallback_fix = slotfs::g_cleanup_fix = false;
        skini::g_unicode_fix = false;
    }
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    g_tmp = std::wstring(t) + L"sporecoop-win-tests";
    rmtree(g_tmp);
    CreateDirectoryW(g_tmp.c_str(), nullptr);
    test_cleanup_old();
    test_apply_pack();
    test_pending();
    test_slot_busy();
    test_slot_match();
    test_slot_write_fails();
    test_slot_fallback();
    test_slot_leftovers();
    test_ini_unicode();
    test_setup_packs();
    test_setup_close();
    test_install_bat();
    rmtree(g_tmp);
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
