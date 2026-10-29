// World sharing: the host streams its current save folder (AppData\Spore\Games\GameN)
// to a joining guest, who stores it as a new save slot. See docs/03-gameplay.md 3.2.
#include "world.h"

#include <winsock2.h>
#define WIN32_LEAN_AND_MEAN
#include <shlobj.h>
#include <windows.h>

#include <cstdio>
#include <algorithm>
#include <deque>
#include <map>

#include "game.h"
#include "sporekit_game.hpp"
#include "modes.h"
#include "slotfs.h"
#include "sui.h"
#include "fixes.h"

namespace world {

static const SkHost* H;
static SkMod* M;
static std::function<void(const std::string&)> g_feed;

using File = slotfs::File;

struct Out {
    uint8_t peer;
    std::vector<File> files;
    uint32_t world_id = 0;
    std::string planet;
    size_t fi = 0, off = 0;
    bool header_sent = false;
    bool done = false;
};

struct In {
    bool active = false;
    net::WorldOffer offer;
    std::map<uint16_t, File> files;
    uint32_t received = 0;
};

// A received world comes from another player's machine: only plain relative names inside the
// save slot, sane sizes (a save slot is a few MB; a Space save with many systems stays far below)
constexpr uint32_t kMaxWorldBytes = 128u << 20, kMaxWorldFiles = 2048;

static bool safe_rel(const std::string& rel)
{
    if (rel.empty() || rel.size() > 200)
        return false;
    size_t start = 0;
    int depth = 0;
    while (start <= rel.size()) {
        size_t end = rel.find_first_of("/\\", start);
        if (end == std::string::npos)
            end = rel.size();
        std::string part = rel.substr(start, end - start);
        if (part.empty() || part.back() == '.' || part.back() == ' ' || ++depth > 4)
            return false; // also rejects leading or doubled separators, "." and ".." (Windows drops trailing dots)
        // a planet file is named as the player typed it ("Bob's World!.spo"): any character a Windows
        // file name may have, but no ':' (drives, streams), wildcards or control characters
        for (unsigned char c : part)
            if (c < 0x20 || c == 0x7F || strchr("<>:\"|?*", c))
                return false;
        start = end + 1;
    }
    return true;
}

static std::deque<Out> g_out;
static std::function<void(uint8_t, const std::vector<const std::vector<uint8_t>*>&)> g_share_hook;
void set_share_hook(std::function<void(uint8_t, const std::vector<const std::vector<uint8_t>*>&)> f) { g_share_hook = std::move(f); }
static bool g_auto_load = true;
static In g_in;

static std::wstring games_dir()
{
    wchar_t p[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) // profile-aware (hooked)
        return {};
    return std::wstring(p) + L"\\Spore\\Games";
}

static std::string narrow(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static bool read_file(const std::wstring& p, std::vector<uint8_t>& out)
{
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n <= 0 || fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

static void collect(const std::wstring& root, const std::wstring& rel, std::vector<File>& out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\" + rel + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..")
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            collect(root, rel + n + L"\\", out);
        } else {
            File f;
            std::wstring r = rel + n;
            if (r == L"sporecoop.txt")
                continue;
            if (!read_file(root + L"\\" + r, f.data))
                continue;
            std::string s = narrow(r);
            for (auto& c : s)
                if (c == '\\')
                    c = '/';
            f.rel = s;
            out.push_back(std::move(f));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// newest Games\Game* folder = the slot the host played/saved last
static std::wstring newest_slot()
{
    std::wstring dir = games_dir(), best;
    FILETIME bt{};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\Game*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        // the folder time does not change when files inside are rewritten: use lastSave.pld
        WIN32_FILE_ATTRIBUTE_DATA a;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (!GetFileAttributesExW((p + L"\\lastSave.pld").c_str(), GetFileExInfoStandard, &a))
            continue;
        if (best.empty() || CompareFileTime(&a.ftLastWriteTime, &bt) > 0) {
            best = p;
            bt = a.ftLastWriteTime;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

static int c_worldfix(void*, int argc, const char* const* argv, SkOut* out);

void init(const SkHost* host, SkMod* mod, std::function<void(const std::string&)> feed)
{
    H = host;
    M = mod;
    g_feed = std::move(feed);
    H->command_add(M, "coop.worldfix",
                   "coop.worldfix [<name> 0|1 | queue <id>...] - world sharing fix switches (tests: 0 = the old behaviour); "
                   "queue: shares for these players in one frame, each with a save",
                   c_worldfix, nullptr, SK_CMD_MAIN_THREAD);
}

static std::wstring current_db_dir(bool lower = true);
static bool dir_has(const std::wstring& dir, const std::wstring& file);

// "<name>.spo" of a planet ("snapshot.spo" is the game's temporary file of an unsaved game)
static bool is_planet_file(const std::string& n)
{
    return n.size() > 4 && n.compare(n.size() - 4, 4, ".spo") == 0 && n != "snapshot.spo" &&
           n.find_first_of("/\\") == std::string::npos;
}

// The save slot to send and the planet in it. A slot (Games\GameN) is a galaxy: every game the
// player starts on a free disc of the galaxy menu is one more <planet>.spo in the same folder (a
// new Cell or Creature game next to an old one: Game0 holds both), so the folder does not tell which
// game is played; the planet does: CurrentGameName, set when a game is started or loaded.
//   1. the folder the save database points to (where the game writes this game's saves), if it holds
//      the planet's file; 2. else the newest copy of that file in any Games\GameN.
// No file of the played planet (not saved yet: a save request the game did not carry out): empty,
// `missing` set; the caller saves again. Another planet of the slot is never offered instead: the
// guest then loaded an old game of the host (bug report 2026-10-24: a planet named in Cyrillic was
// not found, the slot's other planet went out). Only from a menu (no game played, CurrentGameName
// "SaveGame") the newest slot and a planet in it are sent, as before.
static std::wstring current_slot(std::string& planet, bool& missing)
{
    planet = modes::current_world_name(); // "Name.spo", UTF-8
    missing = false;
    if (!is_planet_file(planet)) {
        planet.clear();
        return newest_slot();
    }
    std::wstring file = widen(planet), db = current_db_dir(false);
    if (!db.empty() && dir_has(db, file))
        return db;
    std::wstring dir = games_dir(), best;
    FILETIME bt{};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\Game*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            std::wstring p = dir + L"\\" + fd.cFileName;
            WIN32_FILE_ATTRIBUTE_DATA a;
            if (!GetFileAttributesExW((p + L"\\" + file).c_str(), GetFileExInfoStandard, &a))
                continue;
            if (best.empty() || CompareFileTime(&a.ftLastWriteTime, &bt) > 0) {
                best = p;
                bt = a.ftLastWriteTime;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (best.empty())
        missing = true;
    else
        H->log(M, SK_WARN, "world: %s is not in the save folder %s: sending the newest copy in %s", planet.c_str(),
               narrow(db).c_str(), narrow(best).c_str());
    return best;
}

// The game confirms every save with a native box "The game on planet <name> is saved." (UTFWin
// window, control id 0x06318E78, OK button; closed boxes stay in the tree without the visible
// flag). While it is up the simulation is paused (a cell does not move), and sometimes the box
// also holds the frames. It used to be confirmed with one Enter posted 2.5 s after our save: under
// CPU load the box came later, stayed open and the host's game stood still - its frames too, so
// the guest got no host state (no partner cell). Now the box is watched for after each save and
// confirmed once it is visible (UI reads outside the frame); the world is shared when it was seen.
constexpr uint32_t kSavedBoxId = 0x06318E78;
static uint64_t g_saved_at, g_box_watch_until, g_box_seen_at, g_box_next_check, g_box_last_enter;
static bool g_box_task;

static sui::Win first_visible(sui::Win w, uint32_t id, int depth = 0)
{
    if (!w || depth > 12 || !sui::visible(w))
        return nullptr;
    if (sui::control_id(w) == id)
        return w;
    for (sui::Win c : sui::children(w))
        if (sui::Win f = first_visible(c, id, depth + 1))
            return f;
    return nullptr;
}

// the box's OK button (the check mark, control id 0xFFFFFFF1, 2026-10-23 tree dump)
constexpr uint32_t kSavedBoxOk = 0xFFFFFFF1;

static void box_task(void*)
{
    g_box_task = false;
    sui::Win box = first_visible(sui::main_window(), kSavedBoxId);
    if (!box) {
        if (g_box_seen_at) // confirmed and closed: stop watching (a later Enter hit other menus)
            g_box_watch_until = 0;
        return;
    }
    uint64_t now = GetTickCount64();
    if (!g_box_seen_at)
        H->log(M, SK_INFO, "world: the game saved box is up %llu ms after the save: confirming it",
               (unsigned long long)(now - g_saved_at));
    g_box_seen_at = now;
    if (now - g_box_last_enter < 1000)
        return;
    g_box_last_enter = now;
    // Click its OK button: the box is in front of everything. An Enter went to the window with the
    // keyboard focus: with the pause menu open (a guest joining makes the host save) it pressed a
    // pause menu button and the host left the game to the main menu or quit Spore (chaos tests,
    // 2026-10-23: both 3-4 s after such an Enter).
    if (sui::Win ok = first_visible(box, kSavedBoxOk)) {
        sui::Rect r = sui::screen_rect(ok);
        char cmd[64];
        snprintf(cmd, sizeof cmd, "click %d %d", (int)((r.x1 + r.x2) / 2), (int)((r.y1 + r.y2) / 2));
        if (H->command_exec(cmd, nullptr) == 0)
            return;
    }
    if (HWND w = (HWND)H->game_window()) {
        PostMessageW(w, WM_KEYDOWN, VK_RETURN, 0x001C0001);
        PostMessageW(w, WM_KEYUP, VK_RETURN, 0xC01C0001);
    }
}

static void box_tick()
{
    uint64_t now = GetTickCount64();
    if (now >= g_box_watch_until || g_box_task || now < g_box_next_check)
        return;
    g_box_next_check = now + 200;
    g_box_task = true;
    H->run_outside_frame(box_task, nullptr);
}

static const char* (*g_save_blocker)();
static unsigned g_saves_posted; // tests: every save_game that posted the game's save
void set_save_blocker(const char* (*probe)()) { g_save_blocker = probe; }

static FILETIME g_saved_ft; // when the last save was posted (system time, as file times)
static unsigned g_stale_saves; // tests: shares whose save wrote nothing: saved again (BUG-0276)
static unsigned g_settle_waits; // tests: share saves held while the stage settled (BUG-0276)
static constexpr double kShareSettle = 2.0;

// the newest write time of a file in the slot folder (and one level below)
static uint64_t newest_write(const std::wstring& dir, int depth = 0)
{
    uint64_t best = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 1)
                best = std::max(best, newest_write(dir + L"\\" + fd.cFileName, depth + 1));
        } else {
            best = std::max(best, ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

bool save_game(std::string& err)
{
    using namespace skg;
    // A save written while the game switches modes does not load again: the host's save for a
    // world share went into the game's message queue on the frame its evolution vote ended, the
    // editor opened while it was written, and the save (no cVisiblePlanet, extra rocks and
    // ornaments) kept every game that loaded it in LoadGame forever, frames running
    // (docs/re/creature.md "Saves made while an editor opens"). Not while an editor opens, is open,
    // or a mode switch is pending.
    if (const char* why = g_save_blocker ? g_save_blocker() : nullptr) {
        err = why;
        return false;
    }
    uintptr_t get = H->sym("App::IMessageManager::Get");
    void* mm = get ? cdecl_call<void*>(get) : nullptr;
    if (!mm) {
        err = "no message manager";
        return false;
    }
    // space: the game saves only outside planets (a save made on a planet loads into a broken
    // state - verified: the space UI faults every frame), so wait until the player leaves it
    if (game::mode_id() == 0x1654C05) {
        uintptr_t ctx = H->sym("Simulator::GetCurrentContext");
        if (ctx && skg::cdecl_call<int>(ctx) == 0) {
            err = "leave the planet to save (space stage saves only in a solar system or the galaxy)";
            return false;
        }
    }
    // MessagePost like the pause menu button (4 arguments, as pushed at 0xE03149): the save then runs
    // in the game's own message processing. A synchronous MessageSend from our frame hook (inside
    // Present) deadlocked DXVK threads.
    vcall<void>(mm, 0x18 / 4, 0x1CD20F0u, (void*)nullptr, (void*)nullptr, 0);
    g_saves_posted++;
    GetSystemTimeAsFileTime(&g_saved_ft);
    g_saved_at = GetTickCount64();
    g_box_seen_at = 0;
    g_box_watch_until = g_saved_at + 30000;
    return true;
}

static bool g_planet_missing; // the last share_now failed because the played planet has no file yet

struct PendingShare {
    uint8_t peer;
    uint64_t at;   // share from this tick count (files are written asynchronously after a save)
    bool need_save;
    uint64_t latest = 0; // ... once the saved box was seen, or at this tick count without it
    uint64_t next_try = 0;
    bool told = false;
    uint32_t mode = 0; // the game mode the save was made in
    int saves = 0;     // saves made for this share (the planet's file did not appear: save again)
};
static std::vector<PendingShare> g_pending;
extern bool g_share_reset_fix;

bool share_with(uint8_t peer, std::string& err, bool save_first)
{
    if (save_first) {
        for (auto& p : g_pending)
            if (p.peer == peer && p.need_save)
                return true; // already waiting for a save
        g_pending.push_back({peer, 0, true});
        return true;
    }
    if (share_now(peer, err))
        return true;
    if (!g_planet_missing)
        return false;
    // a new game that was never saved (coop.share, save_before_share=0): save it first; sending
    // the slot without its file sent an older planet of the host
    err.clear();
    return share_with(peer, err, true);
}

bool save_busy()
{
    uint64_t now = GetTickCount64();
    for (auto& p : g_pending)
        if (!p.need_save && (!g_share_reset_fix || !p.latest || now < p.latest + 5000))
            return true; // saved for a share, not written completely yet (at most 15 s: BUG-0140)
    return g_saved_at && now < g_saved_at + 10000 && g_box_seen_at < g_saved_at;
}

// BUG-0140: a host that left the session within the 10 s after a share save kept the entry for
// good (only tick(), which runs while hosting, removed it): save_busy() stayed true and coop.cpp
// re-posted every quit message, so the host could not leave the stage. The session end drops the
// shares and transfers that were for its players.
bool g_share_reset_fix = true;

// wave 2 fixes (tests: `coop.worldfix <name> 0` = the old behaviour)
static bool g_world_reset_fix = true;  // BUG-0074: the session end drops the guest's queued world loads
static bool g_drop_peer_fix = true;    // BUG-0166: shares for a player who left are dropped
static bool g_one_save_fix = true;     // BUG-0112: one save for every share that waits at the same time
static bool g_editor_load_fix = true;  // BUG-0157: a queued load never starts inside an editor
COOP_FIX("core.share_reset", "BUG-0140", g_share_reset_fix, "world: the session end drops the shares and transfers of its players");
COOP_FIX("world.world_reset", "BUG-0074", g_world_reset_fix, "world: the session end drops the guest's queued world loads");
COOP_FIX("world.share_drop_peer", "BUG-0166", g_drop_peer_fix, "world: shares for a player who left are dropped");
COOP_FIX("world.share_one_save", "BUG-0112", g_one_save_fix, "world: one save for every share that waits at the same time");
// BUG-0276: the host's share save posted 70 ms after the game entered the Tribe stage was never
// written (no "game saved" box, no file); after 10 s the share went out anyway with the slot's
// previous files, a Creature save, and every guest loaded Creature while the host played Tribe.
// A share save now waits until the stage has been playable for kShareSettle, and a share without
// its box is sent only when a file of the slot was written after the save was posted (else: save
// again, up to three times). Off: the old timing (0.7.3.0).
bool g_share_settle_fix = true;
COOP_FIX("world.share_settle", "BUG-0276", g_share_settle_fix, "world: a share save waits for a settled stage and is not sent stale");
COOP_FIX("world.load_in_editor", "BUG-0157", g_editor_load_fix, "world: a queued load never starts inside an editor");
COOP_FIX("world.busy_slot", "BUG-0035", slotfs::g_busy_fix, "world: a received world never moves or overwrites the slot being played");
COOP_FIX("world.slot_match", "BUG-0087", slotfs::g_match_fix, "world: a received world goes only into its own host's co-op slot");
COOP_FIX("world.write_check", "BUG-0105", slotfs::g_write_fix, "world: a world whose files could not be written is not loaded");
COOP_FIX("world.slot_fallback", "BUG-0254", slotfs::g_fallback_fix, "world: a slot that cannot be replaced (a file in use) gives way to another slot");
COOP_FIX("world.tmp_cleanup", "BUG-0254", slotfs::g_cleanup_fix, "world: leftovers of interrupted world writes are tidied (under a mutex)");
static unsigned g_share_saves;         // saves posted for shares (tests)
static unsigned g_shares_dropped;      // shares dropped because their player left (tests)
static unsigned g_loads_dropped;       // guest: queued loads dropped at a session end (tests)
static unsigned g_loads_held;          // guest: queued loads moved behind an editor (tests)

void reset_shares()
{
    if (!g_share_reset_fix)
        return;
    if (!g_pending.empty() || !g_out.empty())
        H->log(M, SK_INFO, "world: the session ended: %u share(s) and %u transfer(s) dropped",
               (unsigned)g_pending.size(), (unsigned)g_out.size());
    g_pending.clear();
    g_out.clear();
}

// BUG-0074: a guest's received world that waits for its editor to close (g_deferred), a load
// queued for the galaxy menu (g_via_menu) and a half-received transfer (g_in) belonged to the session:
// after a Leave the player's own game was quit and the old host's world loaded once the editor closed.
// The player's own coop.load stays. (g_deferred and g_via_menu are defined further down.)
static void drop_share_loads();

void on_session_end()
{
    if (g_world_reset_fix)
        drop_share_loads();
}

bool save_pending()
{
    for (auto& p : g_pending)
        if (p.need_save)
            return true;
    return false;
}

bool share_now(uint8_t peer, std::string& err)
{
    std::string cur_planet;
    std::wstring slot = current_slot(cur_planet, g_planet_missing);
    if (g_planet_missing) {
        err = "your game (" + cur_planet.substr(0, cur_planet.size() - 4) + ") is not saved yet: save it (pause menu)";
        return false;
    }
    if (slot.empty()) {
        err = "no saved game found (save the game before hosting)";
        return false;
    }
    Out o;
    o.peer = peer;
    collect(slot, L"", o.files);
    if (o.files.empty()) {
        err = "save folder is empty";
        return false;
    }
    uint32_t crc = 0, total = 0;
    for (auto& f : o.files) {
        crc = net::crc32_update(crc, (const uint8_t*)f.rel.data(), f.rel.size());
        crc = net::crc32_update(crc, f.data.data(), f.data.size());
        total += (uint32_t)f.data.size();
    }
    o.world_id = crc;
    if (g_share_hook) { // reliable and ordered: ahead of the offer (sent by tick)
        std::vector<const std::vector<uint8_t>*> fs;
        for (auto& f : o.files)
            fs.push_back(&f.data);
        g_share_hook(peer, fs);
    }
    o.planet = narrow(slot.substr(slot.find_last_of(L'\\') + 1));
    if (cur_planet.size() > 4) // a slot holds several planets: the one being played
        o.planet = cur_planet.substr(0, cur_planet.size() - 4);
    else // from a menu: some planet of the slot
        for (auto& f : o.files)
            if (is_planet_file(f.rel)) {
                o.planet = f.rel.substr(0, f.rel.size() - 4);
                break;
            }
    net::WorldOffer off;
    off.world_id = crc;
    off.total_size = total;
    off.file_count = (uint16_t)o.files.size();
    off.planet = o.planet;
    H->log(M, SK_INFO, "world: sharing %s, planet %s (%u files, %u bytes, id %08X) with player %u",
           narrow(slot).c_str(), o.planet.c_str(), (unsigned)o.files.size(), total, crc, peer);
    o.header_sent = false;
    // a newer world replaces a transfer to the same player that is still running (a guest that left
    // and joined again gets the same id): both used to send chunks in turn, the guest wrote the old
    // one's chunks into the new world and dropped it (checksum mismatch). The guest starts over at
    // the new offer; whatever of the old transfer is already on the way arrives before it.
    for (auto& q : g_out)
        if (q.peer == peer && !q.done) {
            q.done = true;
            H->log(M, SK_INFO, "world: the transfer to player %u is replaced by a newer one", peer);
        }
    g_out.push_back(std::move(o));
    // offer is sent by tick() so it is ordered before the files
    (void)off;
    return true;
}

// Every load runs from the galaxy menu, outside the frame, once the menu has settled (see load_slot)
struct ViaMenu {
    bool on = false;
    std::wstring slot;
    std::string planet;
    double since = 0, posted = 0;
    bool share = false; // a received world (not the player's own coop.load)
    std::string host;
};
static ViaMenu g_via_menu;

// a load that waits while the player is in an editor (host: "" for the player's own coop.load)
struct Deferred {
    bool on = false;
    std::wstring slot;
    std::string planet, host;
    double back_since = 0;
};
static Deferred g_deferred;
static const double kMenuSettle = 3.0, kStageSettle = 3.0;
static double g_menu_since; // when the galaxy menu came up (0: not in it, or a load started from it)
static double g_stage_since;    // when the current stage mode began (0: not in a stage)
static double g_playable_since; // when the local player got something to control in it (0: not yet)
static uint32_t g_stage_mode;
static bool (*g_playing)();
static bool load_now(const std::wstring& slot, const std::string& planet_file, std::string& err);
static bool in_stage(uint32_t m);

// The galaxy menu's intro cinematic "GGE_intro" (camera flight over the galaxy, ~10 s, started ~16 s
// after the menu came up; docs/re/space.md 7.9). A load started from the menu while it plays left it
// "playing" in the next stage forever: cCinematicManager +0x2C stays 2, the HUD is hidden and the
// input lock it holds (cGameInputManager +0x110) eats every click; nothing in a stage ends it (the
// menu's handler of its end message is gone, Enter only ends captions). The mod ends it natively
// (EndCinematic: stop, release the input lock, post the end message 0x44F1189 that the menu handles)
// before every load from the menu, and in a stage whenever it is found still running.
constexpr uint32_t kMenuIntroId = 0x8915F9BBu; // FNV hash of "GGE_Intro" (the registry id, both builds)
constexpr uintptr_t kCineState = 0x2C, kCineId = 0x148;
static uint32_t g_intros_ended;
static double g_intro_seen;

static void* cinematic_mgr()
{
    uintptr_t get = H->sym("Simulator::cCinematicManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

// ends the running cinematic when it is the menu intro (or any, with any=true); true if it did
static bool end_menu_intro(bool any, const char* where)
{
    uintptr_t end = H->sym("SporeCoop::cCinematicManager_EndCinematic");
    uint8_t* cm = (uint8_t*)cinematic_mgr();
    if (!cm || !end || !*(uint32_t*)(cm + kCineState))
        return false;
    uint32_t id = *(uint32_t*)(cm + kCineId);
    if (id != kMenuIntroId && !any)
        return false;
    H->log(M, SK_WARN, "world: cinematic %08X still playing %s: ended it", id, where);
    skg::thiscall<void>(end, cm);
    g_intros_ended++;
    return true;
}

// in a stage: the menu intro left over from a load (a world received while it played); outside the
// frame, after 1 s (it is never legitimately running there)
static void menu_intro_tick()
{
    uint8_t* cm = (uint8_t*)cinematic_mgr();
    bool stuck = cm && in_stage(game::mode_id()) && *(uint32_t*)(cm + kCineState) &&
                 *(uint32_t*)(cm + kCineId) == kMenuIntroId;
    double now = H->time_seconds();
    if (!stuck) {
        g_intro_seen = 0;
        return;
    }
    if (!g_intro_seen)
        g_intro_seen = now;
    if (now - g_intro_seen < 1.0 || g_intro_seen < 0)
        return;
    g_intro_seen = -1; // once per sighting
    H->run_outside_frame([](void*) { end_menu_intro(false, "in a stage"); }, nullptr);
}

// coop_ui.cpp: the galaxy menu plays its intro right now (its input lock drops every click on the
// menu, our "Co-op" item too); cheap, callable in the frame
bool menu_intro_playing()
{
    uint8_t* cm = game::mode_id() == modes::kMenu ? (uint8_t*)cinematic_mgr() : nullptr;
    return cm && *(uint32_t*)(cm + kCineState) && *(uint32_t*)(cm + kCineId) == kMenuIntroId;
}

bool cinematic_playing()
{
    uint8_t* cm = (uint8_t*)cinematic_mgr();
    return cm && *(uint32_t*)(cm + kCineState);
}

// the game's quit message: saves and leaves any stage for the galaxy menu (in space the planet first)
static bool post_quit_to_menu(std::string& err)
{
    uintptr_t get = H->sym("App::IMessageManager::Get");
    void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!mm) {
        err = "no message manager";
        return false;
    }
    // MessagePost (4 arguments, as save_game): handled in the game's own message processing,
    // outside our frame hook
    skg::vcall<void>(mm, 0x18 / 4, 0x4715068u, (void*)nullptr, (void*)nullptr, 0);
    game::stage_leaving("quit to the galaxy menu posted"); // BUG-0264
    return true;
}

static void via_menu_tick()
{
    double now = H->time_seconds();
    uint32_t mode = game::mode_id();
    if (mode != modes::kMenu)
        g_menu_since = 0;
    else if (!g_menu_since)
        g_menu_since = now;
    if (!in_stage(mode))
        g_stage_since = 0;
    else if (!g_stage_since || mode != g_stage_mode)
        g_stage_since = now;
    g_stage_mode = mode;
    if (!g_stage_since || (g_playing && !g_playing()))
        g_playable_since = 0;
    else if (!g_playable_since)
        g_playable_since = now;
    if (!g_via_menu.on)
        return;
    // other modes (an adventure, an editor, before the first menu): load from there as before, but
    // outside the frame; a load under way (kLoad) ends in a stage, which is left first
    bool other = mode != modes::kMenu && mode != modes::kLoad && !in_stage(mode);
    // BUG-0157: an editor that opened after the load was queued (the player's own, or the own
    // editor after a vote): loading under an open editor crashed the game (see finish). The
    // received world waits like one that arrives in the editor: 2 s after the editor is left.
    if (g_editor_load_fix && (mode == modes::kEditor || mode == modes::kEdit)) {
        g_deferred = Deferred{true, g_via_menu.slot, g_via_menu.planet, g_via_menu.share ? g_via_menu.host : "", 0};
        g_via_menu = ViaMenu{};
        g_loads_held++;
        H->log(M, SK_INFO, "world: an editor opened before the load of %s: it waits until the editor is left",
               g_deferred.planet.c_str());
        return;
    }
    if (mode == modes::kMenu || other) {
        // the galaxy menu reads the stars of its slot and builds the planets it shows when it opens
        // (terrain spheres that draw their textures in render jobs over many frames): a load from
        // inside Present disposed spheres whose jobs the render frame had already taken, and they
        // were drawn without terrain (docs/re/space.md 8.3). Let the menu settle first.
        if (!other && now - g_menu_since < kMenuSettle)
            return;
        // switching the save folder and starting the load reads files and builds UI: outside the
        // frame (this tick runs inside Present)
        static ViaMenu v;
        v = g_via_menu;
        g_via_menu = ViaMenu{};
        g_menu_since = 0; // a newer request settles again (the mode switches to the load later)
        H->log(M, SK_INFO, "world: loading from %s %.1f s after the request", other ? modes::mode_name(mode) : "the galaxy menu",
               now - v.since);
        H->run_outside_frame(
            [](void*) {
                std::string err;
                if (!load_now(v.slot, v.planet, err))
                    g_feed("the world could not be loaded: " + err);
            },
            nullptr);
        return;
    }
    if (in_stage(mode) && !g_via_menu.posted) {
        // a stage runs (also after a load that was under way when the request came): leave it
        // (once: a second quit message could arrive in the menu). Not before the stage has been
        // playable for a few seconds: the game is in the stage mode at once while its loading screen
        // runs, and the stage sets itself up after that (tribe: 0xCD78E0 points an object of the
        // simulator, [0x167FAAC], at the tribe); a guest in a 4-player churn faulted there with that
        // object null (0xB12D32, data 0x2B4; not reproduced: docs/re/space.md 8.3)
        // (a stage without anything to control for the co-op adapter is left after 20 s)
        bool settled = g_playable_since ? now - g_playable_since >= kStageSettle : now - g_stage_since >= 20.0;
        if (!settled)
            return;
        std::string err;
        if (!post_quit_to_menu(err)) {
            g_via_menu = ViaMenu{};
            g_feed("the world could not be loaded: " + err);
            return;
        }
        g_via_menu.posted = now;
        H->log(M, SK_INFO, "world: leaving the stage to the galaxy menu before loading %s", g_via_menu.planet.c_str());
        return;
    }
    if (now - g_via_menu.since > 60.0) {
        g_via_menu = ViaMenu{};
        game::stage_leave_cancel(); // BUG-0264: the stage stays, its avatar too
        H->log(M, SK_WARN, "world: the game did not go to the galaxy menu, load cancelled");
        g_feed("the world could not be loaded: the game did not go to the galaxy menu");
    }
}


// a received world waits while the player is in an editor; loaded 2 s after it is back in a stage
static void deferred_tick()
{
    if (!g_deferred.on)
        return;
    uint32_t mode = game::mode_id();
    double now = H->time_seconds();
    if (mode == modes::kEditor || mode == modes::kEdit || mode == modes::kLoad) {
        g_deferred.back_since = 0;
        return;
    }
    if (!g_deferred.back_since) {
        g_deferred.back_since = now;
        return;
    }
    if (now - g_deferred.back_since < 2.0)
        return;
    Deferred d = g_deferred;
    g_deferred = Deferred{};
    std::string err;
    std::string what = d.host.empty() ? "the save" : "the world of " + d.host;
    if (load_slot(d.slot, d.planet, err, d.host))
        g_feed("loading " + what + "...");
    else
        g_feed(what + " could not be loaded: " + err);
}

static void drop_share_loads()
{
    unsigned n = 0;
    if (g_deferred.on && !g_deferred.host.empty()) {
        g_deferred = Deferred{};
        n++;
    }
    if (g_via_menu.on && g_via_menu.share) {
        g_via_menu = ViaMenu{};
        n++;
    }
    if (g_in.active || !g_in.files.empty()) {
        g_in = In{};
        n++;
    }
    if (n) {
        g_loads_dropped += n;
        H->log(M, SK_INFO, "world: the session ended: %u queued world load(s) / transfer(s) dropped", n);
    }
}

bool load_queued() { return g_editor_load_fix && (g_via_menu.on || g_deferred.on); }

void guest_tick()
{
    via_menu_tick();
    menu_intro_tick();
    deferred_tick();
    box_tick();
}

void tick(net::Session& s)
{
    via_menu_tick();
    menu_intro_tick();
    deferred_tick();
    box_tick();
    if (g_drop_peer_fix) {
        // BUG-0166: a player who left takes its shares and transfers with it (the host saved for
        // nobody, held its editors up to 15 s and read the slot for a peer that was gone)
        auto players = s.players();
        auto here = [&](uint8_t id) {
            for (auto& pl : players)
                if (pl.id == id)
                    return true;
            return false;
        };
        for (size_t i = 0; i < g_pending.size();)
            if (!here(g_pending[i].peer)) {
                H->log(M, SK_INFO, "world: player %u left: its share is dropped", g_pending[i].peer);
                g_shares_dropped++;
                g_pending.erase(g_pending.begin() + i);
            } else {
                i++;
            }
        for (auto& o : g_out)
            if (!o.done && !here(o.peer))
                o.done = true;
    }
    for (size_t i = 0; i < g_pending.size();) {
        auto& p = g_pending[i];
        uint64_t now = GetTickCount64();
        if (p.need_save && g_one_save_fix) {
            // BUG-0112: a save for another share is posted already and not written yet (2+ guests
            // waiting at a stage change, or a guest that joins during another one's save): this
            // share goes out with it. N saves in one frame (or a second one during the first) may
            // break the slot and left a "game saved" box open on the host.
            for (auto& q : g_pending)
                if (&q != &p && !q.need_save && q.latest && now < q.latest) {
                    p.need_save = false;
                    p.at = q.at;
                    p.latest = q.latest;
                    p.mode = q.mode;
                    H->log(M, SK_INFO, "world: the share for player %u goes out with the save for player %u",
                           p.peer, q.peer);
                    break;
                }
        }
        if (p.need_save) {
            // a stage that just began (loading screen, setup) is saved once the player controls
            // something, like a share at join (coop.cpp player_join)
            double ts = H->time_seconds();
            if (now >= p.next_try && g_playing && !g_playing()) {
                p.next_try = now + 1000;
            } else if (now >= p.next_try && g_share_settle_fix && in_stage(game::mode_id()) &&
                       (!g_playable_since || ts - g_playable_since < kShareSettle)) {
                p.next_try = now + 250; // BUG-0276: the stage just began, the game drops a save now
                g_settle_waits++;
            } else if (now >= p.next_try) {
                std::string why;
                if (save_game(why)) {
                    g_share_saves++;
                    p.need_save = false;
                    p.at = now + 2500;
                    p.latest = now + 10000;
                    p.mode = game::mode_id();
                } else {
                    p.next_try = now + 2000;
                    if (!p.told)
                        g_feed("a player is waiting for the world: " + why);
                    p.told = true;
                }
            }
            i++;
            continue;
        }
        // the save is complete when its box shows up (box_tick confirms it)
        if (now < p.at || (p.latest && now < p.latest && g_box_seen_at < g_saved_at)) {
            i++;
            continue;
        }
        // the game left the mode the save was made in while it was written (an editor opened):
        // that save may not load (see save_game); save again once the game allows it
        if (game::mode_id() != p.mode) {
            H->log(M, SK_WARN, "world: the game went from %s to %s during the save for player %u: saving again",
                   modes::mode_name(p.mode), modes::mode_name(game::mode_id()), p.peer);
            p.need_save = true;
            p.next_try = now + 2000;
            p.latest = 0;
            i++;
            continue;
        }
        if (p.latest && g_box_seen_at < g_saved_at && g_share_settle_fix && p.saves < 3) {
            // BUG-0276: no box: was anything written at all? A save the game dropped leaves the
            // slot's older files, possibly of another stage
            std::string planet;
            bool missing = false;
            std::wstring slot = current_slot(planet, missing);
            uint64_t posted = ((uint64_t)g_saved_ft.dwHighDateTime << 32) | g_saved_ft.dwLowDateTime;
            uint64_t wrote = slot.empty() ? 0 : newest_write(slot);
            if (wrote && posted && wrote + 20000000ull < posted) { // 2 s of slack for coarse file times
                H->log(M, SK_WARN, "world: no saved box and no file written %llu ms after the save for player %u: saving again (try %d)",
                       (unsigned long long)(now - g_saved_at), p.peer, p.saves + 2);
                g_stale_saves++;
                p.saves++;
                p.need_save = true;
                p.next_try = now + 1000;
                p.latest = 0;
                i++;
                continue;
            }
        }
        if (p.latest && g_box_seen_at < g_saved_at)
            H->log(M, SK_INFO, "world: no saved box %llu ms after the save: sharing anyway",
                   (unsigned long long)(now - g_saved_at));
        std::string err;
        if (!share_now(p.peer, err) && g_planet_missing && ++p.saves < 3) {
            // the save did not write the played planet's file (yet): save again rather than send
            // another planet of the slot
            H->log(M, SK_WARN, "world: %s: saving again (try %d)", err.c_str(), p.saves + 1);
            p.need_save = true;
            p.next_try = now + 2000;
            p.latest = 0;
            i++;
            continue;
        }
        if (!err.empty())
            g_feed("cannot share the world: " + err);
        g_pending.erase(g_pending.begin() + i); // p is invalid from here
    }
    for (auto& o : g_out) {
        if (o.done)
            continue;
        if (!o.header_sent) {
            net::WorldOffer off;
            off.world_id = o.world_id;
            off.file_count = (uint16_t)o.files.size();
            for (auto& f : o.files)
                off.total_size += (uint32_t)f.data.size();
            off.planet = o.planet;
            net::Writer w;
            off.write(w);
            if (!s.send_reliable_to(o.peer, net::MSG_WORLD_OFFER, w)) {
                o.done = true; // peer gone
                continue;
            }
            o.header_sent = true;
        }
        // flow control: keep a bounded number of reliable messages in flight
        // a LAN / local peer (short round trip) gets a much bigger window
        int rtt = s.rtt_of(o.peer);
        size_t window = rtt >= 0 && rtt < 40 ? 600 : 160;
        int budget = 300;
        while (budget-- > 0 && s.pending_to(o.peer) < window && !o.done) {
            if (o.fi >= o.files.size()) {
                net::Writer w;
                w.u32(o.world_id);
                s.send_reliable_to(o.peer, net::MSG_WORLD_DONE, w);
                o.done = true;
                H->log(M, SK_INFO, "world: transfer to %u queued completely", o.peer);
                break;
            }
            File& f = o.files[o.fi];
            if (o.off == 0) {
                net::WorldFile wf;
                wf.index = (uint16_t)o.fi;
                wf.size = (uint32_t)f.data.size();
                wf.path = f.rel;
                net::Writer w;
                wf.write(w);
                s.send_reliable_to(o.peer, net::MSG_WORLD_FILE, w);
            }
            net::WorldChunk ch;
            ch.index = (uint16_t)o.fi;
            ch.offset = (uint32_t)o.off;
            size_t n = std::min<size_t>(1000, f.data.size() - o.off);
            ch.data.assign(f.data.begin() + o.off, f.data.begin() + o.off + n);
            net::Writer w;
            ch.write(w);
            s.send_reliable_to(o.peer, net::MSG_WORLD_CHUNK, w);
            o.off += n;
            if (o.off >= f.data.size()) {
                o.fi++;
                o.off = 0;
            }
        }
    }
    while (!g_out.empty() && g_out.front().done)
        g_out.pop_front();
}

// the planet file to load from a received slot: the one the host plays (a slot holds every planet
// of the host's galaxy, old games too); an older host that names none: the first planet in it
static std::string planet_file(const std::vector<std::string>& rels, const std::string& named)
{
    for (auto& r : rels)
        if (named.empty() ? is_planet_file(r) : r == named + ".spo")
            return r;
    return {};
}

static bool dir_has(const std::wstring& dir, const std::wstring& file)
{
    return GetFileAttributesW((dir + L"\\" + file).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Spore shows a save as a disc at its star in the galaxy menu, so two slots of the
// same planet overlap. Reuse, in order: the slot that already holds this host world,
// a slot of the same planet (backed up first), else the first free GameN (slotfs::pick).
// folder the save database points to now (the slot being played), lower case, no trailing '\'
static std::wstring current_db_dir(bool lower)
{
    uintptr_t getdb = H->sym("Resource::GetDatabaseByID");
    void* db = getdb ? skg::cdecl_call<void*>(getdb, 0x4729A47u) : nullptr;
    const char16_t* loc = db ? skg::vcall<const char16_t*>(db, 0x28 / 4) : nullptr;
    std::wstring w;
    for (const char16_t* c = loc; c && *c && w.size() < 520; c++)
        w += (wchar_t)(*c == u'/' ? u'\\' : *c);
    while (!w.empty() && w.back() == L'\\')
        w.pop_back();
    if (lower)
        for (auto& ch : w)
            ch = towlower(ch);
    return w;
}

// Which slot a received world goes into and how it is written: slotfs.cpp (never the slot being
// played, BUG-0035; a co-op slot only for the same host world, BUG-0087; all files written or none,
// BUG-0105).

// Point the save-folder database (resource DB 0x4729A47) to `slot` and load `planet_file`
// the way the galaxy menu's Play button does (see docs/05-re-notes.md).
void set_auto_load(bool on) { g_auto_load = on; }
void set_playing_probe(bool (*probe)()) { g_playing = probe; }

static bool in_stage(uint32_t m)
{
    return m == modes::kCell || m == modes::kCreature || m == modes::kTribe || m == modes::kCiv ||
           m == modes::kSpace;
}

// A save is loaded from the galaxy menu, the way the game itself does it, never straight from a
// running stage (docs/re/space.md "Loading a save while a stage runs"): from Space that crashed
// (also the game's own ReloadCurrentGame: kLoadGameMode is entered with the active star already
// cleared and the space mode's exit 0xFDCED0 -> 0xFDA9E0 leaves the solar system of a null cStar),
// from Cell the cell pool was freed under our adapter, and a space save loaded from Cell kept the
// old CurrentGameName ("Karke.spo"), so the next save wrote the wrong planet. The game's quit
// message 0x4715068 (handled by 0xB63610) saves the game and leaves any stage for the galaxy menu
// (in space: the planet first); the save is loaded once the menu is up (~1 s + kMenuSettle). A
// request that comes while the game is in the menu waits for the settle too and also loads outside
// the frame: a world received in the menu right after it opened was loaded at once from inside
// Present and the menu's planet faulted (docs/re/space.md 8.3).
bool load_slot(const std::wstring& slot, const std::string& planet_file, std::string& err, const std::string& share_host)
{
    if (!H->sym("Resource::GetDatabaseByID") || !H->sym("Simulator::cGamePersistenceManager::LoadGameByName")) {
        err = "load symbols missing";
        return false;
    }
    // queued: via_menu_tick (every frame) leaves a running stage for the galaxy menu, waits there
    // until the menu has settled and loads outside the frame; the newest request wins
    double since = g_via_menu.on ? g_via_menu.since : H->time_seconds();
    double posted = g_via_menu.on ? g_via_menu.posted : 0;
    g_via_menu = ViaMenu{true, slot, planet_file, since, posted, !share_host.empty(), share_host};
    via_menu_tick();
    return true;
}

static bool load_now(const std::wstring& slot, const std::string& planet_file, std::string& err)
{
    uintptr_t getdb = H->sym("Resource::GetDatabaseByID");
    uintptr_t getmgr = H->sym("Simulator::cGamePersistenceManager::Get");
    uintptr_t loadby = H->sym("Simulator::cGamePersistenceManager::LoadGameByName");
    if (!getdb || !getmgr || !loadby) {
        err = "load symbols missing";
        return false;
    }
    void* db = skg::cdecl_call<void*>(getdb, 0x4729A47u);
    void* mgr = skg::cdecl_call<void*>(getmgr);
    if (!db || !mgr) {
        err = "save database not available";
        return false;
    }
    std::u16string dir;
    for (wchar_t c : slot)
        dir += (char16_t)(c == L'\\' ? L'/' : c);
    if (dir.empty() || dir.back() != u'/')
        dir += u'/';
    std::u16string name; // UTF-8 -> UTF-16 (a planet named "Земля" did not load)
    for (wchar_t c : widen(planet_file))
        name += (char16_t)c;
    skg::vcall<bool>(db, 0x1C / 4); // Close
    if (!skg::vcall<bool>(db, 0x2C / 4, dir.c_str())) { // SetLocation
        err = "SetLocation failed";
        return false;
    }
    if (!skg::vcall<bool>(db, 0x18 / 4, 3, 6, 0)) { // Open(ReadWrite, CD::Default, autoOpen=false)
        err = "Open failed";
        return false;
    }
    // the star manager keeps the stars of the previously selected slot: reload them
    uintptr_t starget = H->sym("Simulator::cStarManager::Get"), readstars = H->sym("Simulator::cStarManager::ReadStarsFromSaveDB");
    if (starget && readstars)
        if (void* sm = skg::cdecl_call<void*>(starget))
            skg::thiscall<void>(readstars, sm);
    // the menu intro must not survive into the stage (see end_menu_intro); in the menu nothing else plays
    end_menu_intro(game::mode_id() == modes::kMenu, "at the load");
    skg::thiscall<void>(loadby, mgr, name.c_str());
    H->log(M, SK_INFO, "world: loading %s from %s", planet_file.c_str(), narrow(slot).c_str());
    return true;
}

static void finish(const std::string& host_name)
{
    uint32_t crc = 0;
    for (auto& [i, f] : g_in.files) {
        crc = net::crc32_update(crc, (const uint8_t*)f.rel.data(), f.rel.size());
        crc = net::crc32_update(crc, f.data.data(), f.data.size());
    }
    if (crc != g_in.offer.world_id) {
        g_feed("world transfer failed: checksum mismatch");
        g_in = In{};
        return;
    }
    std::vector<std::string> rels;
    for (auto& [i, f] : g_in.files) {
        if (!safe_rel(f.rel)) { // checked on arrival already; never write outside the slot
            g_in = In{};
            return;
        }
        rels.push_back(f.rel);
    }
    std::string planet = planet_file(rels, g_in.offer.planet);
    std::string note;
    std::wstring slot = slotfs::pick(games_dir(), current_db_dir(), host_name, g_in.offer.planet, planet, !g_auto_load, note);
    if (slot.empty()) {
        g_feed("world transfer failed: no free save slot");
        g_in = In{};
        return;
    }
    if (!note.empty())
        g_feed(note);
    std::vector<const File*> files;
    for (auto& [i, f] : g_in.files)
        files.push_back(&f);
    std::string werr;
    bool moved = false;
    bool wrote = slotfs::write_or_fallback(games_dir(), slot, current_db_dir(), files, host_name, g_in.offer.planet,
                                           planet, crc, werr, moved);
    if (moved) {
        H->log(M, SK_WARN, "world: the old copy was in use: the world went into %s", narrow(slot).c_str());
        g_feed("the old copy of " + host_name + "'s world was in use: the world went into another save slot");
    }
    if (!wrote) {
        // not marked, not loaded: a half-written world hung or faulted in LoadGame
        H->log(M, SK_WARN, "world: could not write the world into %s: %s", narrow(slot).c_str(), werr.c_str());
        g_feed("could not save the world of " + host_name + ": " + werr);
        g_in = In{};
        return;
    }
    std::string slot_name = narrow(slot.substr(slot.find_last_of(L'\\') + 1));
    H->log(M, SK_INFO, "world: received %u bytes into %s", g_in.received, narrow(slot).c_str());
    std::string err;
    if (planet == "snapshot.spo") // the game's temporary file of an unsaved game: loading it crashes
        planet.clear();
    // never another planet of the slot: that is another game of the host (an old one)
    if (planet.empty())
        err = g_in.offer.planet.empty() ? "the host has not saved its game yet"
                                         : "the planet " + g_in.offer.planet + " is not in it";
    H->log(M, SK_INFO, "world: the host plays %s: loading %s", g_in.offer.planet.c_str(),
           planet.empty() ? "nothing" : planet.c_str());
    g_in = In{};
    // a player in its own editor (everybody evolves its own creature) loads it when it is back:
    // loading a world under an open editor crashed the game
    uint32_t mode = game::mode_id();
    if (g_auto_load && !planet.empty() && (mode == modes::kEditor || mode == modes::kEdit)) {
        g_deferred = Deferred{true, slot, planet, host_name, 0};
        g_feed("the world of " + host_name + " is loaded when you leave the editor");
        return;
    }
    if (g_auto_load && !planet.empty() && load_slot(slot, planet, err, host_name)) {
        g_feed("loading the world of " + host_name + "...");
        return;
    }
    g_feed("world of " + host_name + " saved as " + slot_name + ": load it from the main menu" +
           (err.empty() ? "" : " (" + err + ")"));
}

void on_message(uint8_t type, net::Reader& r, const std::string& host_name)
{
    switch (type) {
    case net::MSG_WORLD_OFFER:
        g_in = In{};
        if (g_in.offer.read(r) && g_in.offer.total_size <= kMaxWorldBytes && g_in.offer.file_count <= kMaxWorldFiles) {
            g_in.active = true;
            g_feed("receiving world from " + host_name + " (" + std::to_string(g_in.offer.total_size / 1024) +
                   " KB)");
        }
        break;
    case net::MSG_WORLD_FILE: {
        net::WorldFile wf;
        if (g_in.active && wf.read(r)) {
            uint64_t sum = wf.size;
            for (auto& [i, f] : g_in.files)
                if (i != wf.index)
                    sum += f.data.size();
            if (!safe_rel(wf.path) || wf.index >= g_in.offer.file_count || sum > g_in.offer.total_size) {
                H->log(M, SK_WARN, "world: rejected file %u '%s' (%u bytes) from %s", wf.index, wf.path.c_str(),
                       wf.size, host_name.c_str());
                g_feed("the world from " + host_name + " was rejected (bad file name or size)");
                g_in = In{};
                break;
            }
            File& f = g_in.files[wf.index];
            f.rel = wf.path;
            f.data.resize(wf.size);
        }
        break;
    }
    case net::MSG_WORLD_CHUNK: {
        net::WorldChunk ch;
        if (g_in.active && ch.read(r)) {
            auto it = g_in.files.find(ch.index);
            if (it != g_in.files.end() && (uint64_t)ch.offset + ch.data.size() <= it->second.data.size()) {
                memcpy(it->second.data.data() + ch.offset, ch.data.data(), ch.data.size());
                g_in.received += (uint32_t)ch.data.size();
            }
        }
        break;
    }
    case net::MSG_WORLD_DONE:
        if (g_in.active)
            finish(host_name);
        break;
    }
}

bool progress(uint32_t& got, uint32_t& total)
{
    got = g_in.received;
    total = g_in.offer.total_size;
    return g_in.active;
}

std::string status()
{
    std::string s;
    char b[200];
    for (auto& o : g_out) {
        snprintf(b, sizeof b, "sending world to %u: file %u/%u\n", o.peer, (unsigned)o.fi, (unsigned)o.files.size());
        s += b;
    }
    for (auto& p : g_pending) { // tests (BUG-0140): a share waiting for its save, or for the save to be written
        snprintf(b, sizeof b, "share for %u %s\n", p.peer, p.need_save ? "waits for its save" : "saved, being written");
        s += b;
    }
    // tests (BUG-0112, BUG-0166, BUG-0074, BUG-0157)
    snprintf(b, sizeof b, "world saves posted %u, for shares %u, shares dropped %u, loads dropped %u, loads held %u%s%s\n",
             g_saves_posted, g_share_saves, g_shares_dropped, g_loads_dropped, g_loads_held,
             g_via_menu.on ? ", load queued" : "", g_deferred.on ? ", load after the editor" : "");
    s += b;
    snprintf(b, sizeof b, "share saves held while the stage settled %u, shares saved again (nothing written) %u\n",
             g_settle_waits, g_stale_saves); // tests (BUG-0276)
    s += b;
    if (g_in.active) {
        snprintf(b, sizeof b, "receiving world: %u/%u bytes\n", g_in.received, g_in.offer.total_size);
        s += b;
    }
    // tests: a cinematic running (the menu intro before a join, a caption) holds the input
    if (uint8_t* cm = (uint8_t*)cinematic_mgr(); cm && *(uint32_t*)(cm + kCineState)) {
        snprintf(b, sizeof b, "cinematic %08X playing%s\n", *(uint32_t*)(cm + kCineId),
                 *(uint32_t*)(cm + kCineId) == kMenuIntroId ? " (menu intro)" : "");
        s += b;
    }
    if (g_intros_ended) {
        snprintf(b, sizeof b, "menu intro ended %u\n", g_intros_ended);
        s += b;
    }
    return s;
}

bool load_local(const std::string& slot_name, const std::string& planet, std::string& err)
{
    std::string file = planet.size() > 4 && planet.compare(planet.size() - 4, 4, ".spo") == 0 ? planet : planet + ".spo";
    std::wstring slot = games_dir() + L"\\" + widen(slot_name);
    if (GetFileAttributesW((slot + L"\\" + widen(file)).c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "no " + file + " in " + narrow(slot);
        return false;
    }
    return load_slot(slot, file, err);
}

static int c_worldfix(void*, int argc, const char* const* argv, SkOut* out)
{
    struct Sw {
        const char* name;
        bool* on;
    } sw[] = {{"busy_slot", &slotfs::g_busy_fix},       {"slot_match", &slotfs::g_match_fix},
              {"write_check", &slotfs::g_write_fix},    {"world_reset", &g_world_reset_fix},
              {"share_drop_peer", &g_drop_peer_fix},    {"share_one_save", &g_one_save_fix},
              {"load_in_editor", &g_editor_load_fix}};
    if (argc > 2 && !strcmp(argv[1], "queue")) { // tests: several guests wait for the world at once
        for (int i = 2; i < argc; i++) {
            std::string err;
            share_with((uint8_t)atoi(argv[i]), err, true);
        }
        H->out_printf(out, "queued %d share(s)\n", argc - 2);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "autoload")) { // tests: auto_load_world of coop.ini
        g_auto_load = atoi(argv[2]) != 0;
        H->out_printf(out, "auto_load_world %d\n", g_auto_load ? 1 : 0);
        return 0;
    }
    for (auto& x : sw)
        if (argc > 2 && !strcmp(argv[1], x.name))
            *x.on = atoi(argv[2]) != 0;
    if (argc > 2 && !strcmp(argv[1], "all"))
        for (auto& x : sw)
            *x.on = atoi(argv[2]) != 0;
    for (auto& x : sw)
        H->out_printf(out, "%s %d\n", x.name, *x.on ? 1 : 0);
    return 0;
}

} // namespace world
