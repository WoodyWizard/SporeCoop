// Persistent, profile-wide shared creations, separate from campaign model sync.
#include "library.h"
#include "../../updater/sha256.h"
#include "creations.h"
#include "fixes.h"
#include "net/dbpf.h"
#include "net/library_sync.h"
#include "net/utf8.h"
#include "slotfs.h"
#include "sporekit_game.hpp"
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>

namespace library {
using namespace skg;
static const SkHost *H;
static constexpr size_t kMaxEntries = 50000;
static SkMod *M;
static net::Session *S;
static uintptr_t a_Reserve, a_GetKeys, a_GetProps;
struct Entry {
    library_sync::Item item;
    dbpf::Key key;
    bool local = false, failed = false;
    unsigned tries = 0; // BUG-9106: decode attempts this game start (not saved)
};
// BUG-9106: decodes that failed and wait for another try (hash, when)
static std::deque<std::pair<std::string, double>> g_later;
static double g_now = 0;
static std::map<std::string, Entry> g_entries;
static std::set<std::string> g_relay;
static std::set<uint8_t> g_peers;
static std::deque<dbpf::Entry> g_scan;
static std::vector<dbpf::Entry> g_source_index;
static std::map<std::pair<uint32_t, uint32_t>, dbpf::Entry> g_metadata;
static std::deque<std::string> g_imports;
static std::set<std::string> g_active_local;
static std::set<std::string> g_scan_active;
static bool g_scanning = false;
static unsigned g_local_dirty = 0, g_repaired = 0, g_export_failed = 0;
static uint64_t g_editor_stamp = 0;
static std::wstring g_dir, g_editor;
static std::string g_identity, g_error;
static bool g_loaded = false, g_pending = false, g_index_bad = false, g_safe = false;
struct Incoming {
    uint8_t peer;
    std::vector<uint8_t> data;
};
static std::deque<Incoming> g_incoming;
static double g_scan_at = -100, g_step_at = -100;
static library_sync::Exchange g_exchange;
static std::vector<library_sync::Item> g_offers;
static std::set<std::string> g_offered;
// BUG-9104: the received creations' keys, kept out of My Creations (h_FeedKeys).
static void refresh_received();
static void load_onward(); // BUG-9105
static bool g_retry_fix = true;
COOP_FIX("library.retry", "BUG-9106", g_retry_fix, "Lineages Cache: a failed import is tried again at the next game start");
static unsigned g_retried = 0;
// BUG-9213: the Cell-to-Creature provisional creature keeps the cell's picture (BUG-9095 repaired only the
// model), so partners saw a cell as its icon. It is not shared; the creature saved in the editor is.
static bool g_skip_provisional = true;
COOP_FIX("library.skip_provisional", "BUG-9213", g_skip_provisional,
         "Lineages Cache: the provisional Cell-to-Creature creation (a cell picture) is not shared");
static unsigned g_provisional = 0;
static bool g_split_fix = true;
COOP_FIX("library.split", "BUG-9104", g_split_fix,
         "Lineages Cache: received creations show in the Lineages Cache, not in My Creations");
static SRWLOCK g_received_lock = SRWLOCK_INIT;
static std::set<std::pair<uint32_t, uint32_t>> g_received; // (instance, group) of imported items
static unsigned g_mine_hidden = 0;
// BUG-9222: a player named with "-NoShare-" (a QA game joining the owner's) offers none of its own creations, and
// nobody keeps a creation such a player made or passed on. `coop.fix library.no_share off`: shared as before (56-dev).
static bool g_no_share_fix = true;
COOP_FIX("library.no_share", "BUG-9222", g_no_share_fix,
         "Lineages Cache: a player whose name holds -NoShare- shares no creations, and none of theirs are kept");
static uint32_t g_no_share_dropped, g_no_share_unsent; // BUG-9331: received ones a -NoShare- player did not pass on

// BUG-9329 (decision 7Z A): test creations made before the -NoShare- rule spread into real players' caches. The mod
// ships a ban list (mods\lineages\banned.txt): `source <identity>` (the hidden Lineages identity of a test profile),
// `creation <png sha256>` (one creation), `keep <png sha256>` (never banned: a real player's creation that a test
// profile copied with a save). At the first library step of a game start (the main menu, before a save loads) every
// received creation of the list is moved out of the cache into LineagesCache\banned (the PNG, never deleted; a line
// per creation in banned\moved.txt) and out of the index; it is never imported again, refused when offered (it counts
// as had, so nothing is sent) and never offered. Its copy in the game's Sporepedia cache (Pollination) stays, so a save
// that holds it loads as before, but the game's object template DB never hands it out again (FindObjects without it:
// no new visiting ship, NPC or other pick of it). The player's own creations (local entries) are never moved.
// `coop.fix library.banned off`: nothing is banned (57-dev).
static bool g_ban_fix = true;
COOP_FIX("library.banned", "BUG-9329", g_ban_fix,
         "Lineages Cache: creations on the mod's ban list (test profiles) are moved out, refused and never picked by the game");
static std::set<std::string> g_ban_sources, g_ban_items, g_ban_keep;
static bool g_ban_list_ok = false;
static SRWLOCK g_hidden_lock = SRWLOCK_INIT;
static std::set<std::pair<uint32_t, uint32_t>> g_hidden; // (instance, group) of moved-out creations
static unsigned g_ban_moved = 0, g_ban_refused = 0, g_ban_unsent = 0;
static std::atomic<unsigned> g_ban_picks{0}, g_ban_retries{0};
static bool banned(const library_sync::Item &i) {
    if (!g_ban_fix || g_ban_keep.count(i.hash))
        return false;
    return g_ban_items.count(i.hash) || (!i.source.empty() && g_ban_sources.count(i.source));
}
static bool ban_token(const std::string &s) { // a hex digest or a plain id such as "manual-test"
    if (s.empty() || s.size() > 64)
        return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '-'; });
}
static void load_ban_list() {
    const std::wstring path = slotfs::widen(H->mods_dir()) + L"\\lineages\\banned.txt";
    std::vector<uint8_t> bytes;
    WIN32_FILE_ATTRIBUTE_DATA st{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &st) || st.nFileSizeHigh || st.nFileSizeLow > (4u << 20)) {
        H->log(M, SK_WARN, "library: no ban list (mods\\lineages\\banned.txt): nothing is banned (BUG-9329)");
        return;
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    bytes.resize(st.nFileSizeLow);
    DWORD got = 0;
    const bool ok = ReadFile(f, bytes.data(), DWORD(bytes.size()), &got, nullptr) && got == bytes.size();
    CloseHandle(f);
    if (!ok)
        return;
    unsigned bad = 0;
    size_t at = 0;
    const std::string text(bytes.begin(), bytes.end());
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (auto hash = line.find('#'); hash != std::string::npos)
            line.resize(hash);
        line.erase(std::remove_if(line.begin(), line.end(), [](char c) { return c == '\r' || c == '\t'; }), line.end());
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        while (!line.empty() && line.front() == ' ')
            line.erase(line.begin());
        if (line.empty())
            continue;
        const size_t sp = line.find(' ');
        const std::string what = line.substr(0, sp), value = sp == std::string::npos ? "" : line.substr(line.find_first_not_of(' ', sp));
        if (!ban_token(value) || value.find(' ') != std::string::npos)
            ++bad;
        else if (what == "source")
            g_ban_sources.insert(value);
        else if (what == "creation" && library_sync::valid_hash(value))
            g_ban_items.insert(value);
        else if (what == "keep" && library_sync::valid_hash(value))
            g_ban_keep.insert(value);
        else
            ++bad;
    }
    g_ban_list_ok = true;
    H->log(M, SK_INFO, "library: ban list: %zu source(s), %zu creation(s), %zu kept, %u bad line(s) (BUG-9329)",
           g_ban_sources.size(), g_ban_items.size(), g_ban_keep.size(), bad);
}
static void hide_key(const dbpf::Key &k) {
    // a key the player's own database answers for (the same creation in My Creations, e.g. a partner's species the
    // game kept there) stays pickable: the player's own creations are never touched
    if (!k.instance || creations::key_in_editor_saves(k))
        return;
    AcquireSRWLockExclusive(&g_hidden_lock);
    g_hidden.emplace(k.instance, k.group);
    ReleaseSRWLockExclusive(&g_hidden_lock);
}
static void purge_banned(); // (below: needs the cache's paths)
static size_t hidden_count() {
    AcquireSRWLockShared(&g_hidden_lock);
    const size_t n = g_hidden.size();
    ReleaseSRWLockShared(&g_hidden_lock);
    return n;
}

static void offer(const Entry &e) {
    if (banned(e.item)) { // BUG-9329: never sent
        ++g_ban_unsent;
        return;
    }
    // BUG-9222: a -NoShare- player offers none of its own; BUG-9331: and passes on none it received (a test copy's
    // cache holds other test games' creations; receivers drop a -NoShare- player's offers anyway, after the download)
    if (g_no_share_fix && utf8::no_share_name(S->name_raw_of(S->my_id()))) {
        if (!e.local && g_offered.insert(e.item.hash).second) // once a session: offer_all asks again every step
            ++g_no_share_unsent;
        return;
    }
    if (!S->connected() || !g_offered.insert(e.item.hash).second)
        return;
    auto item = e.item;
    if (e.local)
        item.sharer = S->name_raw_of(S->my_id());
    g_offers.push_back(std::move(item));
}
static bool g_roots_added = false;

static void add_category() {
    if (g_roots_added || !a_GetProps || !a_GetKeys)
        return;
    auto set = H->sym("App::Property::Set");
    void *pm = cdecl_call<void *>(a_GetProps);
    if (!set || !pm)
        return;
    void *category = nullptr;
    if (!vcall<bool>(pm, 0x2C / 4, uint32_t(0xC2FEF27B), uint32_t(0x06B38241), &category) || !category)
        return;
    vcall<int>(category, 0x04 / 4);
    bool all = true;
    for (uint32_t root : {0x2EA8FB98u, 0x1B327962u, 0xFDECEF02u}) {
        void *list = nullptr;
        if (!vcall<bool>(pm, 0x2C / 4, root, uint32_t(0x6EDC12D4), &list) || !list) {
            all = false;
            continue;
        }
        size_t count = 0;
        dbpf::Key *keys = nullptr;
        void *property = nullptr;
        if (cdecl_call<bool>(a_GetKeys, list, uint32_t(0x744717C0), &count, &keys) && count < 1000 &&
            (!count || readable(keys, count * sizeof(*keys))) &&
            vcall<bool>(list, 0x24 / 4, uint32_t(0x744717C0), &property) && property) {
            std::vector<dbpf::Key> values;
            if (count)
                values.assign(keys, keys + count);
            const dbpf::Key our{0xC2FEF27B, 0, 0};
            if (std::find(values.begin(), values.end(), our) == values.end()) {
                values.push_back(our);
                if (!thiscall<bool>(set, property, int(0x20), int(0x14), values.data(), sizeof(dbpf::Key),
                                    values.size()))
                    all = false;
            }
        } else
            all = false;
        vcall<int>(list, 0x04 / 4);
    }
    g_roots_added = all;
}

static bool read_at(const std::wstring &path, uint64_t offset, size_t count, std::vector<uint8_t> &out) {
    if (count > (64u << 20))
        return false;
    HANDLE f =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER where{};
    where.QuadPart = offset;
    out.resize(count);
    DWORD got = 0;
    bool ok = SetFilePointerEx(f, where, nullptr, FILE_BEGIN) && ReadFile(f, out.data(), DWORD(count), &got, nullptr) &&
              got == count;
    CloseHandle(f);
    if (!ok)
        out.clear();
    return ok;
}
static bool read_file(const std::wstring &path, std::vector<uint8_t> &out, size_t limit = library_sync::kMaxFile) {
    WIN32_FILE_ATTRIBUTE_DATA st{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &st) || st.nFileSizeHigh || st.nFileSizeLow > limit)
        return false;
    return read_at(path, 0, st.nFileSizeLow, out);
}
static bool atomic_file(const std::wstring &path, const std::vector<uint8_t> &bytes) {
    const auto temporary = path + L".tmp";
    HANDLE f = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() &&
              FlushFileBuffers(f);
    CloseHandle(f);
    return ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
static std::wstring object_path(const std::string &hash) {
    return g_dir + L"\\objects\\" + slotfs::widen(hash) + L".png";
}
static bool save_index() {
    if (g_index_bad)
        return false;
    net::Writer w;
    w.u32(0x31434E4C);
    w.u32(uint32_t(g_entries.size()));
    for (auto &[hash, e] : g_entries) {
        library_sync::write_item(w, e.item);
        w.u32(e.key.instance);
        w.u32(e.key.type);
        w.u32(e.key.group);
        w.u8(e.local);
        w.u8(e.failed);
    }
    if (!atomic_file(g_dir + L"\\index.bin", w.data)) {
        g_error = "Cannot save library index.";
        return false;
    }
    return true;
}
static bool load_index() {
    auto path = g_dir + L"\\index.bin";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return true;
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes, 64u << 20))
        return false;
    net::Reader r(bytes.data(), bytes.size());
    if (r.u32() != 0x31434E4C)
        return false;
    uint32_t count = r.u32();
    if (count > kMaxEntries)
        return false;
    std::map<std::string, Entry> entries;
    for (uint32_t n = 0; n < count; ++n) {
        Entry e;
        if (!library_sync::read_item(r, e.item))
            return false;
        e.key = {r.u32(), r.u32(), r.u32()};
        uint8_t local = r.u8(), failed = r.u8();
        if (!r.ok() || local > 1 || failed > 1 || entries.count(e.item.hash))
            return false;
        e.local = local;
        e.failed = failed;
        entries.emplace(e.item.hash, std::move(e));
    }
    if (!r.ok() || r.left())
        return false;
    g_entries = std::move(entries);
    for (auto &[hash, e] : g_entries)
        if (!e.local) {
            // BUG-9106: a failed import gets one more try per game start. A decode that ran while
            // the game was loading failed once and the creation never came back: its partner's
            // offer finds the file already here, so it is not sent again.
            if (e.failed && g_retry_fix) {
                e.failed = false;
                ++g_retried;
            }
            if (!e.failed)
                g_imports.push_back(hash);
        }
    return true;
}
// BUG-9329: moves the listed received creations out (see banned() above); the keys moved out before stay hidden from the
// game's picks (banned\moved.txt: "<hash> <instance> <type> <group> <source>" per line, hex)
static void purge_banned() {
    const std::wstring dir = g_dir + L"\\banned", log = dir + L"\\moved.txt";
    std::vector<uint8_t> old;
    // a creation that came back from a source not on the list (the same PNG from its real author) is not hidden
    std::set<std::pair<uint32_t, uint32_t>> wanted;
    for (auto &[hash, e] : g_entries)
        if (!banned(e.item) && e.key.instance)
            wanted.emplace(e.key.instance, e.key.group);
    if (read_file(log, old, 16u << 20)) {
        const std::string text(old.begin(), old.end());
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos)
                end = text.size();
            char hash[80] = {};
            dbpf::Key k{};
            if (sscanf(text.substr(at, end - at).c_str(), "%79s %x %x %x", hash, &k.instance, &k.type, &k.group) == 4 &&
                !wanted.count({k.instance, k.group}))
                hide_key(k);
            at = end + 1;
        }
    }
    if (!g_ban_fix || (g_ban_sources.empty() && g_ban_items.empty()))
        return;
    std::string lines;
    unsigned moved = 0, missing = 0;
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        const Entry &e = it->second;
        if (e.local || !banned(e.item)) { // the player's own creations stay
            ++it;
            continue;
        }
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            g_error = "Cannot create the banned folder.";
            break;
        }
        const std::wstring from = object_path(e.item.hash), to = dir + L"\\" + slotfs::widen(e.item.hash) + L".png";
        if (GetFileAttributesW(from.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
                H->log(M, SK_WARN, "library: banned %s could not be moved out (error %lu): kept", e.item.hash.c_str(), GetLastError());
                ++it;
                continue;
            }
        } else
            ++missing;
        char line[256];
        snprintf(line, sizeof line, "%s %08X %08X %08X %s\n", e.item.hash.c_str(), e.key.instance, e.key.type, e.key.group,
                 e.item.source.c_str());
        lines += line;
        H->log(M, SK_INFO, "library: banned creation moved out: %s key %08X/%08X/%08X source %.16s (BUG-9329)",
               e.item.hash.c_str(), e.key.instance, e.key.type, e.key.group, e.item.source.c_str());
        hide_key(e.key);
        it = g_entries.erase(it);
        ++moved;
    }
    if (!moved)
        return;
    std::vector<uint8_t> all = old;
    all.insert(all.end(), lines.begin(), lines.end());
    if (!atomic_file(log, all))
        H->log(M, SK_WARN, "library: banned\\moved.txt could not be written");
    save_index();
    g_ban_moved += moved;
    H->log(M, SK_INFO, "library: %u banned creation(s) moved out of the Lineages Cache into its banned folder (%u without "
           "their PNG); their keys are hidden from the game's picks (BUG-9329)", moved, missing);
}

// BUG-9329: the object template DB's FindObjects (Sporepedia::OTDB::cObjectTemplateDB vt 0x24, SporeCoop::OTDB_FindObjects)
// without the moved-out keys. A limited query (the visiting ship asks for one) only offers the templates used least
// lately (runtime 2026-11-21: with five never-used banned ships, every one of 200 picks offered just those five, also
// when asked for 44), and the caller stamps only what it takes as used (vt 0x58). So each banned template a query
// offers is stamped as used here (vt 0x58(key, 1), as the caller does) and the query runs again, up to 8 times: the next
// templates come up, and a pick is a creation that is not banned whenever the game has one.
struct KeyVector {
    dbpf::Key *begin, *end, *capacity;
};
static bool(SK_THISCALL *o_FindObjects)(void *, KeyVector *, int, void *);
static size_t drop_hidden(void *db, KeyVector *out) {
    if (!out || !out->begin || out->end <= out->begin || out->end - out->begin > 1000000 ||
        !readable(out->begin, size_t(out->end - out->begin) * sizeof(dbpf::Key)))
        return 0;
    std::vector<dbpf::Key> gone;
    AcquireSRWLockShared(&g_hidden_lock);
    dbpf::Key *w = out->begin;
    for (dbpf::Key *r = out->begin; r != out->end; ++r)
        if (!g_hidden.count({r->instance, r->group}))
            *w++ = *r;
        else if (gone.size() < 256)
            gone.push_back(*r);
    ReleaseSRWLockShared(&g_hidden_lock);
    const size_t n = size_t(out->end - w);
    out->end = w;
    for (auto &k : gone) // stamped as used: the next query offers others first
        vcall<bool>(db, 0x58 / 4, &k, true);
    return n;
}
static bool SK_THISCALL h_FindObjects(void *db, KeyVector *out, int max, void *params) {
    bool ok = o_FindObjects(db, out, max, params);
    if (!g_ban_fix || !out || !hidden_count())
        return ok;
    size_t dropped = drop_hidden(db, out);
    g_ban_picks += unsigned(dropped);
    for (int tries = 0; dropped && max > 0 && out->begin == out->end && tries < 8; ++tries) {
        ++g_ban_retries;
        ok = o_FindObjects(db, out, max, params);
        dropped = drop_hidden(db, out);
        g_ban_picks += unsigned(dropped);
    }
    return out->begin != out->end && ok;
}

static bool paths() {
    if (g_loaded)
        return !g_index_bad;
    auto get = H->sym("Resource::GetDatabaseByID");
    void *db = get ? cdecl_call<void *>(get, uint32_t(0x11AC199)) : nullptr;
    const wchar_t *loc = db ? vcall<const wchar_t *>(db, 0x28 / 4) : nullptr;
    if (!loc || !*loc)
        return false;
    g_editor = loc;
    g_dir = std::filesystem::path(g_editor).parent_path().wstring() + L"\\SporeCoop\\LineagesCache";
    std::error_code ec;
    std::filesystem::create_directories(g_dir + L"\\objects", ec);
    if (!ec)
        std::filesystem::create_directories(g_dir + L"\\exports", ec);
    if (ec) {
        g_error = "Cannot create library cache.";
        return false;
    }
    std::vector<uint8_t> id;
    if (read_file(g_dir + L"\\identity", id, 64) && library_sync::valid_hash(std::string(id.begin(), id.end())))
        g_identity.assign(id.begin(), id.end());
    else {
        FILETIME ft{};
        LARGE_INTEGER counter{};
        GetSystemTimeAsFileTime(&ft);
        QueryPerformanceCounter(&counter);
        net::Writer seed;
        seed.raw(&ft, sizeof ft);
        seed.raw(&counter, sizeof counter);
        seed.u32(GetCurrentProcessId());
        seed.str16(slotfs::narrow(g_dir));
        g_identity = upd::sha256_hex(seed.data);
        if (!atomic_file(g_dir + L"\\identity", std::vector<uint8_t>(g_identity.begin(), g_identity.end())))
            return false;
    }
    g_loaded = true;
    if (!load_index()) {
        g_index_bad = true;
        g_error = "Library index is unreadable; preserved without overwriting.";
        return false;
    }
    purge_banned(); // BUG-9329: before anything is imported, offered or loaded
    refresh_received();
    load_onward();
    return true;
}
static bool read_record(const dbpf::Entry &entry, std::vector<uint8_t> &bytes) {
    std::vector<uint8_t> raw;
    return entry.size <= library_sync::kMaxFile && entry.usize <= library_sync::kMaxFile &&
           read_at(g_editor, entry.offset, entry.size, raw) && dbpf::decode(entry, raw.data(), bytes);
}
static std::string clean(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; }), s.end());
    s.resize(utf8::cut_bytes(s, 255));
    return s;
}
static void scan_index() {
    WIN32_FILE_ATTRIBUTE_DATA st{};
    if (!GetFileAttributesExW(g_editor.c_str(), GetFileExInfoStandard, &st))
        return;
    uint64_t stamp = (uint64_t(st.ftLastWriteTime.dwHighDateTime) << 32) | st.ftLastWriteTime.dwLowDateTime;
    if (stamp == g_editor_stamp)
        return;
    std::vector<uint8_t> hdr, idx;
    dbpf::IndexInfo info;
    if (!read_at(g_editor, 0, 96, hdr) || !dbpf::index_info(hdr.data(), hdr.size(), info) ||
        !read_at(g_editor, info.offset, info.size, idx) ||
        !dbpf::parse_index(idx.data(), idx.size(), info, g_source_index))
        return;
    g_editor_stamp = stamp;
    g_metadata.clear();
    g_scan_active.clear();
    g_scanning = true;
    for (auto &e : g_source_index)
        if (e.key.type == 0x030BDEE3)
            g_metadata[{e.key.group, e.key.instance}] = e;
    for (auto &e : g_source_index)
        if (e.key.type == 0x2F7D0004 && (e.key.group & 0xFF) == 0)
            g_scan.push_back(e);
}
static void scan_one() {
    if (g_scan.empty())
        return;
    auto resource = g_scan.front();
    g_scan.pop_front();
    // A thumbnail alone is not an authored creation. Native metadata must share its key.
    auto meta = g_metadata.find({resource.key.group, resource.key.instance});
    if (meta == g_metadata.end())
        return;
    std::vector<uint8_t> png, metadata;
    if (!read_record(resource, png) || !library_sync::valid_png(png) || !read_record(meta->second, metadata))
        return;
    std::string hash = upd::sha256_hex(png);
    const auto input = g_dir + L"\\inspect.png";
    uint32_t group = 0;
    std::string author, title;
    if (!atomic_file(input, png) || !creations::inspect_library_png(input, group, author, title)) {
        ++g_export_failed;
        g_error = "An authored PNG could not be decoded.";
        return;
    }
    if (group != resource.key.group) {
        if (g_skip_provisional) {
            if (!g_provisional++)
                H->log(M, SK_INFO, "library: a provisional creation (its PNG holds group %08X) is not shared (BUG-9213)", group);
            return;
        }
        // The Cell-to-Creature provisional catalog copies a thumbnail whose
        // steganographic payload is still a cell. Encode the CURRENT model into
        // a separate cache PNG, keeping the original records and saves intact.
        uint32_t type = 0;
        switch (resource.key.group) {
        case 0x40616200:
            type = 0x3D97A8E4;
            break;
        case 0x40626200:
            type = 0x2B978C46;
            break;
        case 0x40636200:
            type = 0x2399BE55;
            break;
        case 0x40646200:
            type = 0x24682294;
            break;
        case 0x40656200:
            type = 0x476A98C7;
            break;
        case 0x40666200:
            type = 0x438F6347;
            break;
        }
        std::vector<uint8_t> signature = metadata;
        signature.insert(signature.end(), png.begin(), png.end());
        for (auto &rec : g_source_index)
            if (rec.key.instance == resource.key.instance && rec.key.group == resource.key.group &&
                rec.key.type == 0x1A99B06B) {
                std::vector<uint8_t> model;
                if (read_record(rec, model))
                    signature.insert(signature.end(), model.begin(), model.end());
            }
        auto output = g_dir + L"\\exports\\" + slotfs::widen(upd::sha256_hex(signature)) + L".png";
        std::vector<uint8_t> repaired;
        bool ready = read_file(output, repaired) && library_sync::valid_png(repaired) &&
                     creations::inspect_library_png(output, group, author, title) && group == resource.key.group;
        if (!ready && type) {
            ready =
                creations::regenerate_library_png(input, {resource.key.instance, type, resource.key.group}, output) &&
                read_file(output, repaired) && library_sync::valid_png(repaired) &&
                creations::inspect_library_png(output, group, author, title) && group == resource.key.group;
        }
        if (!ready) {
            ++g_export_failed;
            g_error = "A generated creation could not be exported consistently.";
            return;
        }
        // Retire an earlier development cache entry that held the stale PNG.
        auto old = g_entries.find(hash);
        if (old != g_entries.end() && old->second.local)
            g_entries.erase(old);
        png = std::move(repaired);
        hash = upd::sha256_hex(png);
        ++g_repaired;
    }
    auto old = g_entries.find(hash);
    if (old != g_entries.end() && old->second.local) {
        g_active_local.insert(hash);
        g_scan_active.insert(hash);
        offer(old->second);
        return;
    }
    if (old == g_entries.end() && g_entries.size() >= kMaxEntries) {
        ++g_export_failed;
        g_error = "Library index is full.";
        return;
    }
    Entry e;
    e.local = true;
    e.key = resource.key;
    e.item.hash = hash;
    e.item.size = uint32_t(png.size());
    e.item.author = std::move(author);
    e.item.name = std::move(title);
    e.item.author = clean(e.item.author);
    e.item.name = clean(e.item.name);
    e.item.source = g_identity;
    e.item.sharer = clean(S->name_raw_of(S->my_id()));
    if (!atomic_file(object_path(hash), png)) {
        g_error = "Cannot cache authored creation.";
        return;
    }
    // A locally edited copy has its own native author metadata and PNG digest.
    g_entries[hash] = std::move(e);
    g_active_local.insert(hash);
    g_scan_active.insert(hash);
    ++g_local_dirty;
    offer(g_entries.at(hash));
}
static void import_one() {
    while (!g_imports.empty()) {
        std::string hash = std::move(g_imports.front());
        g_imports.pop_front();
        auto it = g_entries.find(hash);
        if (it == g_entries.end())
            continue;
        auto &e = it->second;
        if (e.local || e.failed || (e.key.instance && creations::key_known(e.key)))
            continue;
        std::vector<uint8_t> png;
        if (!read_file(object_path(hash), png) || png.size() != e.item.size || upd::sha256_hex(png) != hash ||
            !library_sync::valid_png(png)) {
            e.failed = true;
            g_error = "Cached creation failed its integrity check.";
            save_index();
            return;
        }
        uint32_t group = 0;
        std::string author, name;
        if (!creations::inspect_library_png(object_path(hash), group, author, name)) {
            // BUG-9106: right after the game starts the decoder fails on PNGs that read fine a
            // minute later (MSI: the same file inspected fine from the console). Try again later,
            // three times per game start, before giving up until the next start.
            if (g_retry_fix && ++e.tries < 3) {
                g_later.emplace_back(hash, g_now + 60);
                H->log(M, SK_WARN, "library: %s could not be decoded yet, trying again in 60 s", hash.c_str());
                return;
            }
            e.failed = true;
            g_error = "Received PNG has no readable creation.";
            save_index();
            H->log(M, SK_WARN, "library: import rejected %s (no readable creation)", hash.c_str());
            return;
        }
        // Native decoding/import must wait until the editor/loading cinematic
        // releases the shared thumbnail service. Wire captions are only hints.
        e.item.author = clean(author);
        e.item.name = clean(name);
        if (!creations::import_library_png(object_path(hash), e.key, g_error))
            e.failed = true;
        else if (e.key.instance) { // BUG-9329: kept again from a source not on the list: pickable again
            AcquireSRWLockExclusive(&g_hidden_lock);
            g_hidden.erase({e.key.instance, e.key.group});
            ReleaseSRWLockExclusive(&g_hidden_lock);
        }
        save_index();
        refresh_received();
        H->log(M, e.failed ? SK_WARN : SK_INFO, "library: %s %s", e.failed ? "import rejected" : "imported",
               hash.c_str());
        return;
    }
}
static bool g_feed_fix = true;
COOP_FIX("library.feed", "BUG-9093", g_feed_fix, "Lineages Cache lists its imported creation keys");
struct Asset {
    dbpf::Key key;
    uint32_t flags;
};
struct Vector {
    Asset *begin, *end, *capacity;
};
static void(SK_THISCALL *o_FeedKeys)(void *, Vector *);
static void SK_THISCALL h_FeedKeys(void *self, Vector *out) {
    // Only the cached property list of our own feed is intercepted. Native
    // FeedItemKeyList otherwise enumerates resources by group, not this array.
    void *pm = a_GetProps ? cdecl_call<void *>(a_GetProps) : nullptr;
    void *list = nullptr;
    if (g_feed_fix && pm && a_GetKeys && a_Reserve &&
        vcall<bool>(pm, 0x2C / 4, uint32_t(0x97954265), uint32_t(0x4E5892EB), &list) && list) {
        const bool ours = list == field<void *>(self, 0x180);
        if (ours && paths()) {
            std::vector<dbpf::Key> keys;
            std::set<std::tuple<uint32_t, uint32_t, uint32_t>> unique;
            for (auto &[hash, e] : g_entries)
                if (!e.local && !e.failed && e.key.instance && creations::key_known(e.key) &&
                    unique.emplace(e.key.instance, e.key.type, e.key.group).second)
                    keys.push_back(e.key);
            thiscall<void>(a_Reserve, out, keys.size());
            out->end = out->begin;
            for (auto &key : keys)
                *out->end++ = {key, 0};
        }
        vcall<int>(list, 0x04 / 4);
        if (ours)
            return;
    }
    o_FeedKeys(self, out);
    // BUG-9104: My Creations (FeedItems 146C1D8C, query C84E51B6 on filter "userCreated") lists
    // every creation authored offline (-2), so partners' creations showed there too. The
    // received ones are taken out of that list; they stay in the Lineages Cache feed.
    void *mine = nullptr;
    if (g_split_fix && pm && out && out->begin && vcall<bool>(pm, 0x2C / 4, uint32_t(0x146C1D8C), uint32_t(0x4E5892EB), &mine) &&
        mine) {
        const bool is_mine = mine == field<void *>(self, 0x180);
        vcall<int>(mine, 0x04 / 4);
        if (is_mine) {
            AcquireSRWLockShared(&g_received_lock);
            Asset *w = out->begin;
            for (Asset *r = out->begin; r != out->end; ++r)
                if (!g_received.count({r->key.instance, r->key.group}))
                    *w++ = *r;
            ReleaseSRWLockShared(&g_received_lock);
            g_mine_hidden += unsigned(out->end - w);
            out->end = w;
        }
    }
}

// BUG-9104: a creation made without an EA account carries the offline author ID -2, which Spore
// treats as the player's own (Sporepedia 66DE73..66DE90, metadata +0x68), so every partner's
// creation counted as the player's and showed in My Creations. The Sporepedia index (author hash,
// parameter 0518AF1D, 56C5D9) is built when an asset is imported and kept on disk; answering
// another author during the import did not move it out of My Creations (the list's own query is
// not the author hash), so h_FeedKeys removes the received keys from that list instead.
static void refresh_received() {
    std::set<std::pair<uint32_t, uint32_t>> keys;
    for (auto &[hash, e] : g_entries)
        if (!e.local && !e.failed && e.key.instance)
            keys.emplace(e.key.instance, e.key.group);
    AcquireSRWLockExclusive(&g_received_lock);
    g_received.swap(keys);
    ReleaseSRWLockExclusive(&g_received_lock);
}

// BUG-9112 (43-dev, player decision; replaces BUG-9105's Share Onward): the receiver decides. Every
// player always gets the connected players' own creations. "Foreign Creations" (coop.ini [library]
// foreign_creations=1, `coop.library foreign on`, default off) also brings everything else in the
// connected players' Lineages Caches (creations they got from other people), and those count for Player
// Cells in the player's worlds. The library exchange runs through the host, so the host offers each
// peer by that peer's choice, and a guest offers its received creations to the host only when the host
// wants them. Each player tells the others its identity and choice: MSG_LIBRARY kind 5 (u8 5, str8
// source identity, u8 foreign); the same layout Share Onward used (same Lineages build required).
static constexpr uint8_t kChoiceMsg = 5;
static bool g_foreign = false;                   // this player's own choice
static std::set<uint8_t> g_choice_told;          // peers told our choice this session
static std::map<uint8_t, std::string> g_peer_identity; // peer -> its source identity (its own creations)
static std::map<uint8_t, bool> g_peer_foreign;   // peer -> it wants foreign creations
static std::set<std::pair<uint8_t, std::string>> g_peer_sources; // creations a peer offered this session
static unsigned g_foreign_offered = 0, g_foreign_refused = 0;
static void load_onward() {} // relay.bin (Share Onward's opted-in authors) is no longer read
static void tell_choice(uint8_t peer) {
    if (g_identity.empty())
        return;
    net::Writer w;
    w.u8(kChoiceMsg);
    w.str8(g_identity);
    w.u8(g_foreign);
    if (S->send_reliable_to(peer, net::MSG_LIBRARY, w))
        g_choice_told.insert(peer);
}
static void choice_message(uint8_t peer, net::Reader &r) {
    std::string source = r.str8();
    uint8_t foreign = r.u8();
    if (!r.ok() || r.left() || foreign > 1 || !library_sync::valid_hash(source))
        return;
    g_peer_identity[peer] = source;
    const bool was = g_peer_foreign.count(peer) && g_peer_foreign[peer];
    g_peer_foreign[peer] = foreign != 0;
    if (foreign && !was && g_peers.count(peer)) { // switched on: what was skipped for it is offered again
        g_exchange.leave(peer);
        g_exchange.join(peer);
    }
}
static void onward_seen(uint8_t peer, const library_sync::Item &item) {
    if (!item.source.empty() && g_peer_sources.size() < kMaxEntries)
        g_peer_sources.emplace(peer, item.source);
}
// the identities of the players connected now (their own creations), ours included
static std::set<std::string> connected_sources() {
    std::set<std::string> out;
    if (!g_identity.empty())
        out.insert(g_identity);
    if (S && S->connected())
        for (auto &p : S->players()) {
            auto it = g_peer_identity.find(p.id);
            if (it != g_peer_identity.end())
                out.insert(it->second);
        }
    return out;
}
static bool wants_foreign(uint8_t peer) {
    auto it = g_peer_foreign.find(peer);
    return it != g_peer_foreign.end() && it->second;
}
// may this creation go to that peer
static bool eligible_to(uint8_t peer, const Entry &e) {
    if (banned(e.item)) // BUG-9329
        return false;
    if (e.local)
        return g_active_local.count(e.item.hash) > 0;
    if (e.failed)
        return false;
    if (S->role() != net::Role::Host) // a guest talks only to the host: its received ones if the host wants them
        return peer == 0 && wants_foreign(0);
    if (!e.item.source.empty() && connected_sources().count(e.item.source))
        return true; // a connected player's own creation
    return wants_foreign(peer);
}
static void offer_all() {
    for (auto &[hash, e] : g_entries)
        if (!e.local && !e.failed && !g_offered.count(hash)) {
            offer(e);
            ++g_foreign_offered;
        }
}

static bool store_item(uint8_t peer, const library_sync::Item &item, const std::vector<uint8_t> &png) {
    if (banned(item)) { // BUG-9329
        if (!g_ban_refused++)
            H->log(M, SK_INFO, "library: a banned creation (test profile) is not kept (BUG-9329)");
        return true; // taken and dropped: the sender moves on
    }
    if (g_no_share_fix && (utf8::no_share_name(S->name_raw_of(peer)) || utf8::no_share_name(item.sharer))) {
        if (!g_no_share_dropped++)
            H->log(M, SK_INFO, "library: a creation from a -NoShare- player is not kept (BUG-9222)");
        return true; // taken and dropped: the sender moves on
    }
    // BUG-9112: the host keeps a guest's received (foreign) creation only when it wants foreign ones
    if (S->role() == net::Role::Host && !g_foreign && peer != S->my_id() && item.source != "manual-test") {
        auto id = g_peer_identity.find(peer);
        if (id == g_peer_identity.end() || id->second != item.source) {
            ++g_foreign_refused;
            return true; // taken and dropped: the sender moves on
        }
    }
    if (!paths() || (g_entries.size() >= kMaxEntries && !g_entries.count(item.hash))) {
        g_error = "Library index unavailable or full.";
        return false;
    }
    if (!atomic_file(object_path(item.hash), png)) {
        g_error = "Cannot save received creation.";
        return false;
    }
    Entry e;
    e.item = item;
    // The direct sender's co-op name is transport attribution, not an EA author ID.
    if (S->role() == net::Role::Host)
        e.item.sharer = clean(S->name_raw_of(peer));
    auto old = g_entries.find(item.hash);
    if (old == g_entries.end()) {
        g_entries.emplace(item.hash, std::move(e));
        g_imports.push_back(item.hash);
    } else if (!old->second.local && old->second.failed) {
        old->second = std::move(e);
        g_imports.push_back(item.hash);
    }
    return save_index();
}
void init(const SkHost *host, SkMod *mod, net::Session *session) {
    H = host;
    M = mod;
    S = session;
    a_Reserve = H->sym("SporeCoop::AssetKeyVector_Reserve");
    a_GetKeys = H->sym("App::Property::GetArrayKey");
    a_GetProps = H->sym("App::IPropManager::Get");
    auto feed = H->sym("SporeCoop::FeedItem_GetAssetKeys");
    if (feed && a_Reserve && a_GetKeys && a_GetProps)
        H->hook_create(mod, (void *)feed, (void *)h_FeedKeys, (void **)&o_FeedKeys);
    load_ban_list(); // BUG-9329
    if (auto find = H->sym("SporeCoop::OTDB_FindObjects"))
        H->hook_create(mod, (void *)find, (void *)h_FindObjects, (void **)&o_FindObjects);
    else
        H->log(M, SK_WARN, "library: SporeCoop::OTDB_FindObjects missing: banned creations already in the Sporepedia stay pickable");
    g_exchange.inventory = []() -> const std::vector<library_sync::Item> & { return g_offers; };
    g_exchange.eligible_to = [](uint8_t peer, const library_sync::Item &i) { // BUG-9112: by the peer's choice
        auto e = g_entries.find(i.hash);
        return e != g_entries.end() && eligible_to(peer, e->second);
    };
    g_exchange.have = [](const library_sync::Item &item) {
        if (banned(item)) { // BUG-9329: refused on receive (counted as had: nothing is sent)
            ++g_ban_refused;
            return true;
        }
        auto it = g_entries.find(item.hash);
        if (it == g_entries.end() || it->second.item.size != item.size)
            return false;
        std::vector<uint8_t> data;
        return read_file(object_path(item.hash), data) && data.size() == item.size &&
               upd::sha256_hex(data) == item.hash;
    };
    g_exchange.load = [](const std::string &hash, std::vector<uint8_t> &data) {
        return read_file(object_path(hash), data);
    };
    g_exchange.store = store_item;
    g_exchange.seen = [](uint8_t peer, const library_sync::Item &item) {
        if (S->role() == net::Role::Host) {
            onward_seen(peer, item);
            g_relay.insert(item.hash);
            auto e = g_entries.find(item.hash);
            if (e != g_entries.end())
                offer(e->second);
        }
    };
    g_exchange.send = [](uint8_t peer, const net::Writer &w) { return S->send_reliable_to(peer, net::MSG_LIBRARY, w); };
    g_foreign = H->config_int(mod, "library", "foreign_creations", 0) != 0; // BUG-9112
}
void on_session_end() {
    g_exchange.reset();
    g_peers.clear();
    g_relay.clear();
    g_incoming.clear();
    g_offers.clear();
    g_offered.clear();
    g_choice_told.clear();
    g_peer_identity.clear();
    g_peer_foreign.clear();
    g_peer_sources.clear();
    g_editor_stamp = 0;
    g_scan_at = -100;
}
void on_player_leave(uint8_t id) {
    g_exchange.leave(id);
    g_peers.erase(id);
    g_choice_told.erase(id);
    g_peer_identity.erase(id);
    g_peer_foreign.erase(id);
    std::erase_if(g_peer_sources, [&](const auto &p) { return p.first == id; });
    std::erase_if(g_incoming, [&](const Incoming &m) { return m.peer == id; });
}
static bool peer_ready(uint8_t peer) {
    return S && S->connected() && peer != S->my_id() && (S->role() == net::Role::Host || peer == 0) &&
           (S->peer_caps(peer) & net::CAP_LIBRARY);
}
void on_message(uint8_t peer, net::Reader &r) {
    if (!peer_ready(peer) || !r.ok() || r.left() > 2048 || g_incoming.size() >= 2048)
        return;
    g_incoming.push_back({peer, std::vector<uint8_t>(r.cur(), r.cur() + r.left())});
}
static void work(void *) {
    g_pending = false;
    add_category();
    if (!paths())
        return;
    if (S->connected()) {
        if (g_offers.empty()) {
            for (auto &hash : g_active_local) {
                auto e = g_entries.find(hash);
                if (e != g_entries.end())
                    offer(e->second);
            }
            offer_all(); // BUG-9112: received creations too; eligible_to picks per peer
        }
        for (auto &player : S->players())
            if (peer_ready(player.id)) {
                if (g_peers.insert(player.id).second)
                    g_exchange.join(player.id);
                if (!g_choice_told.count(player.id))
                    tell_choice(player.id);
            }
    } else if (!g_peers.empty())
        on_session_end();
    for (unsigned n = 0; n < 32 && !g_incoming.empty(); ++n) {
        auto m = std::move(g_incoming.front());
        g_incoming.pop_front();
        if (peer_ready(m.peer) && !m.data.empty() && m.data[0] == kChoiceMsg) {
            net::Reader r(m.data.data() + 1, m.data.size() - 1);
            choice_message(m.peer, r);
        } else if (peer_ready(m.peer)) {
            net::Reader r(m.data.data(), m.data.size());
            g_exchange.receive(m.peer, r);
        }
    }
    if (g_safe) {
        scan_one();
        if (g_scanning && g_scan.empty()) {
            for (auto &hash : g_active_local)
                if (!g_scan_active.count(hash))
                    g_offered.erase(hash);
            g_active_local = std::move(g_scan_active);
            g_scan_active.clear();
            g_scanning = false;
        }
        while (!g_later.empty() && g_later.front().second <= g_now) { // BUG-9106
            g_imports.push_back(std::move(g_later.front().first));
            g_later.pop_front();
            ++g_retried;
        }
        import_one();
        if (g_local_dirty && (g_local_dirty >= 16 || g_scan.empty())) {
            if (save_index())
                g_local_dirty = 0;
        }
    }
    for (auto peer : g_peers)
        if (peer_ready(peer) && S->pending_to(peer) < 16)
            g_exchange.tick(peer);
}
void tick(double now, bool safe) {
    g_safe = safe;
    g_now = now;
    if (g_pending || now - g_step_at < 0.05)
        return;
    g_step_at = now;
    if (g_loaded && !g_index_bad && g_scan.empty() && now - g_scan_at >= 30 && S->connected() && safe) {
        g_scan_at = now;
        // Index reading is deferred with the other work, not performed in Present.
        H->run_outside_frame(
            [](void *) {
                if (paths())
                    scan_index();
            },
            nullptr);
    }
    g_pending = true;
    H->run_outside_frame(work, nullptr);
}
bool foreign_creations() { return g_foreign; }
std::string hash_of(uint32_t instance, uint32_t group) {
    if (!instance || !g_loaded)
        return {};
    for (auto &[hash, e] : g_entries)
        if (e.key.instance == instance && e.key.group == group && !e.failed)
            return hash;
    return {};
}
bool key_of(const std::string &hash, dbpf::Key &out) {
    if (!g_loaded)
        return false;
    auto it = g_entries.find(hash);
    if (it == g_entries.end() || it->second.failed || !it->second.key.instance || banned(it->second.item))
        return false;
    out = it->second.key;
    return true;
}
// BUG-9111 / BUG-9112: Lineages cells for the host's Cell pool: our own, those authored by a player in
// this session, and, with Foreign Creations on, every other cell of the Cache.
std::vector<CacheCell> pool_cells() {
    std::vector<CacheCell> out;
    const std::set<std::string> lobby = connected_sources();
    for (auto &[hash, e] : g_entries) {
        if (e.failed || !e.key.instance || e.key.type != 0x3D97A8E4u)
            continue;
        const bool ok = e.local || lobby.count(e.item.source) || g_foreign;
        if (!ok || !creations::key_known(e.key))
            continue;
        out.push_back({e.key, e.item.source, e.item.name, e.item.author, e.local});
        if (out.size() >= 512)
            break;
    }
    return out;
}
void set_foreign_creations(bool on) {
    g_foreign = on;
    H->config_set_str(M, "library", "foreign_creations", on ? "1" : "0");
    g_choice_told.clear(); // the next work tick tells the session
}
std::string debug(const std::vector<std::string> &args) {
    if (!paths())
        return g_error.empty() ? "Native library database unavailable." : g_error;
    if (args.size() == 2 && args[0] == "foreign") // BUG-9112: receive the partners' foreign creations?
        set_foreign_creations(args[1] == "on" || args[1] == "1");
    if (!args.empty() && args[0] == "scan") {
        g_editor_stamp = 0;
        g_scan.clear();
        scan_index();
    }
    if (args.size() == 2 && args[0] == "pickships") {
        // BUG-9329 tests: the visiting UFO's random ship pick (UFOAbductionEvent_PickShip, the OTDB query the event's start
        // runs) N times: how many picks named a moved-out creation (0 with library.banned on)
        auto pick = H->sym("SporeCoop::UFOAbductionEvent_PickShip");
        if (!pick)
            return "no symbol";
        const int n = std::clamp(atoi(args[1].c_str()), 1, 500);
        int hidden = 0, none = 0;
        std::map<uint32_t, int> seen;
        for (int i = 0; i < n; ++i) {
            dbpf::Key k{};
            cdecl_call<void>(pick, &k, false);
            AcquireSRWLockShared(&g_hidden_lock);
            hidden += g_hidden.count({k.instance, k.group}) ? 1 : 0;
            ReleaseSRWLockShared(&g_hidden_lock);
            none += !k.instance;
            ++seen[k.instance];
        }
        std::string out = "picks=" + std::to_string(n) + " hidden=" + std::to_string(hidden) + " none=" + std::to_string(none) +
                          " distinct=" + std::to_string(seen.size()) + " fix=" + (g_ban_fix ? "on" : "off");
        for (auto &[inst, c] : seen) {
            char t[32];
            snprintf(t, sizeof t, " %08X:%d", inst, c);
            out += t;
        }
        return out;
    }
    if (args.size() == 2 && args[0] == "inspect") {
        uint32_t group = 0;
        std::string author, name;
        bool ok = creations::inspect_library_png(slotfs::widen(args[1]), group, author, name);
        char text[32];
        snprintf(text, sizeof text, "%08X", group);
        return std::string(ok ? "PNG " : "Invalid PNG ") + text + " " + name + " by " + author;
    }
    if (args.size() == 2 && args[0] == "import") {
        std::vector<uint8_t> png;
        if (!read_file(slotfs::widen(args[1]), png) || !library_sync::valid_png(png))
            return "Invalid creation PNG.";
        library_sync::Item item;
        item.hash = upd::sha256_hex(png);
        item.size = uint32_t(png.size());
        item.source = "manual-test";
        item.sharer = "QA import";
        if (!store_item(S->my_id(), item, png))
            return g_error;
        import_one();
    }
    size_t local = 0, imported = 0, pending = 0, failed = 0;
    std::string list;
    for (auto &[hash, e] : g_entries) {
        if (e.local)
            ++local;
        else if (e.failed)
            ++failed;
        else if (e.key.instance && creations::key_known(e.key))
            ++imported;
        else
            ++pending;
        if (!args.empty() && args[0] == "list") {
            char key[40];
            snprintf(key, sizeof key, "%08X/%08X/%08X", e.key.instance, e.key.type, e.key.group);
            list += "\n" + hash + " " + key + " " + (e.local ? "local " : "received ") + e.item.name + " by " +
                    e.item.author + " shared by " + e.item.sharer;
        }
    }
    return "library local=" + std::to_string(local) + " imported=" + std::to_string(imported) +
           " pending=" + std::to_string(pending) + " failed=" + std::to_string(failed) +
           " scan=" + std::to_string(g_scan.size()) + " peers=" + std::to_string(g_peers.size()) +
           " incoming=" + std::to_string(g_exchange.incoming_bytes()) + " repaired=" + std::to_string(g_repaired) + " provisional=" + std::to_string(g_provisional) +
           " export_failed=" + std::to_string(g_export_failed) + " mine_hidden=" + std::to_string(g_mine_hidden) + " foreign=" + (g_foreign ? "on" : "off") + " foreign_offered=" + std::to_string(g_foreign_offered) +
           " foreign_refused=" + std::to_string(g_foreign_refused) + " no_share_dropped=" + std::to_string(g_no_share_dropped) + " no_share_unsent=" + std::to_string(g_no_share_unsent) +
           " ban_list=" + (g_ban_list_ok ? std::to_string(g_ban_sources.size()) + "/" + std::to_string(g_ban_items.size()) + "/" +
                                            std::to_string(g_ban_keep.size()) : std::string("none")) +
           " ban_moved=" + std::to_string(g_ban_moved) + " ban_refused=" + std::to_string(g_ban_refused) + " ban_unsent=" +
           std::to_string(g_ban_unsent) + " ban_hidden=" + std::to_string(hidden_count()) + " ban_picks_dropped=" +
           std::to_string(g_ban_picks.load()) + " ban_retries=" + std::to_string(g_ban_retries.load()) + " peers_foreign=" +
           std::to_string(std::count_if(g_peer_foreign.begin(), g_peer_foreign.end(), [](auto &p) { return p.second; })) + " retried=" + std::to_string(g_retried) +
           (g_error.empty() ? "" : " error=" + g_error) + list;
}
} // namespace library
