#include "creations.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "game.h"
#include "modes.h"
#include "slotfs.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"

namespace creations {

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;

static const SkHost* H;
static SkMod* M;
static Ctx C;

struct KindInfo {
    const char* name;
    uint32_t type, group;
};
static const KindInfo kInfo[kKinds] = {
    {"creature", 0x2B978C46, 0x40626200}, // crt
    {"cell", 0x3D97A8E4, 0x40616200},     // cll
    {"ufo", 0x476A98C7, 0x40656200},      // ufo
};
constexpr uint32_t kBem = 0x1A99B06B, kMeta = 0x030BDEE3, kCreatureData = 0x0F43029A;
constexpr uint32_t kEditorSavesArea = 0x11AC199;
constexpr int kPrioPlayer = 1000, kPrioWorld = 2000; // EditorSaves is 1000; the list is searched highest first
// MSG_CREATION kinds after the player creations (older versions drop kind >= kKinds): a species of
// the host's world for a guest that lacks it (owner 0, id = the crt instance), and a guest's request
// and a species of the host's world that is one of the host's own creations (owner 0, id = the crt
// instance): the guest uses it under a key of its own (see creations.h)
constexpr uint8_t kWorldSpecies = 3, kAskSpecies = 4, kOwnSpecies = 5;
static bool g_own_species_fix = true; // off: a guest drops the host's own creations (before 0.7.2.4, tests)
COOP_FIX("creations.own_species", "BUG-0017", g_own_species_fix, "creations: a guest registers the host's own creations of its world (nest species)");

// BUG-0278: a guest's own editor (the vote's, docs/re/creations.md#creations-own-editor-save) saves
// its creature into My Creations but leaves the avatar on the world's species: the host's creature
// from before the host's editor (the legless one at the start of the Creature stage). The return
// from that editor took the avatar's key as the guest's own creature (choices.ini), and "Save New"
// keeps the edited creation's key, the host's: the host's package answers for it (priority 2000).
// Either way the guest played the host's legless creature from then on, over every world the host
// sent, and its next editor started from it again. Now only an own creation is taken over, a key
// the host's world owns is never the own one (a pinned one is ignored, an own creation under such
// a key is used under its shared remapped key), and the co-op window offers "the world's own".
// Off: as 0.7.3.0.
static bool g_pre_editor_fix = true;
COOP_FIX("creations.pre_editor", "BUG-0278", g_pre_editor_fix, "creations: a guest's own editor never makes the host's pre-editor creature its own");

static uintptr_t a_GetDb, a_OpNew, a_DbCtor, a_RmPtr, a_SetSpecies, a_GetEmpire, a_CellPreload;

// ---- own creations (My Creations = EditorSaves.package) ----
static std::wstring g_es_path, g_dir; // EditorSaves.package and our folder next to it
static std::vector<Option> g_opts[kKinds];
static dbpf::Package g_es;  // My Creations, read every few seconds
static bool g_es_ok;
static double g_opts_read = -100;
static Key g_choice[kKinds];  // explicit choice ({} = default)
static bool g_chosen[kKinds]; // the player chose (even "none")

// ---- bundles: a creation with its records ----
struct Bundle {
    Key key;                         // the creation (crt/cll/ufo key)
    std::vector<dbpf::Record> recs;  // bem + metadata, empty for a premade (everybody has it)
};
static std::map<std::pair<uint8_t, uint8_t>, std::vector<uint8_t>> g_store; // host: (owner, kind) -> bundle
static uint32_t g_sent_id[kKinds];                                             // what we sent of our own
static double g_last_check;
static std::map<std::pair<uint8_t, uint8_t>, net::ChunkRx> g_rx;
static unsigned g_rx_dropped; // pieces that did not fit the creation they belong to
struct Applied {
    uint32_t id = 0;
    Key key;  // usable here (remapped instance)
    Key orig; // the owner's own key
    void* db = nullptr;
    std::string name;
    bool unusable = false; // no records and an unknown key: not used here
};
static std::map<std::pair<uint8_t, uint8_t>, Applied> g_applied; // (owner, kind)
static unsigned g_bundles_in, g_bundles_out, g_registered, g_swaps;
static unsigned g_waits_on_disk; // own creations not sent yet: saved by the editor, not in the file yet
static unsigned g_unusable;      // received creations without records whose key this game does not know
static unsigned g_world_swaps;   // a newer version of the host's world creation replaced the older one
static std::string g_last_error;
// world species (want_species): guest asks / registers, host answers
static std::map<uint32_t, double> g_asked;        // guest: crt instance -> when asked
static std::map<uint32_t, void*> g_species_db;    // guest: registered packages of world species
static unsigned g_species_asked, g_species_got, g_species_sent, g_species_missing;
// the host's own creations among its world's species (kOwnSpecies)
static std::map<uint8_t, std::set<uint32_t>> g_own_sent; // host: guest -> crt instances checked / sent
static std::map<uint32_t, uint32_t> g_remap, g_unmap;   // guest: host instance -> ours, and back
static unsigned g_own_out, g_own_in;

// ---- helpers ----
static uint32_t fnv(const void* p, size_t n, uint32_t h = 0x811C9DC5)
{
    for (size_t i = 0; i < n; i++)
        h = (h * 0x1000193) ^ ((const uint8_t*)p)[i];
    return h;
}

static bool read_file(const std::wstring& path, std::vector<uint8_t>& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    bool ok = sz.QuadPart < (256 << 20) && ReadFile(f, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size();
    CloseHandle(f);
    return ok;
}

static bool write_file(const std::wstring& path, const std::vector<uint8_t>& data)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD wr = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
    CloseHandle(f);
    return ok;
}

static std::string hex(uint32_t v)
{
    char b[16];
    snprintf(b, sizeof b, "%08X", v);
    return b;
}

// where My Creations live: the game's EditorSaves database location
static bool find_paths()
{
    if (!g_es_path.empty())
        return true;
    void* db = a_GetDb ? cdecl_call<void*>(a_GetDb, kEditorSavesArea) : nullptr;
    if (!db)
        return false;
    const wchar_t* loc = vcall<const wchar_t*>(db, 0x28 / 4); // GetLocation
    if (!loc || IsBadReadPtr(loc, 8) || !(loc[1] == L':' || loc[0] == L'\\'))
        return false;
    std::wstring p = loc;
    if (p.size() < 10 || p.size() > 1000)
        return false;
    g_es_path = p;
    g_dir = p.substr(0, p.find_last_of(L"\\/")) + L"\\SporeCoop";
    CreateDirectoryW(g_dir.c_str(), nullptr);
    // the choices belong to this profile (creation ids are per profile): SporeCoop\choices.ini
    for (int k = 0; k < kKinds; k++) {
        wchar_t buf[32] = {};
        GetPrivateProfileStringW(L"creations", std::wstring(kInfo[k].name, kInfo[k].name + strlen(kInfo[k].name)).c_str(),
                                 L"-", buf, 32, (g_dir + L"\\choices.ini").c_str());
        if (buf[0] != L'-') {
            g_chosen[k] = true;
            uint32_t inst = (uint32_t)wcstoul(buf, nullptr, 16);
            g_choice[k] = inst ? Key{inst, kInfo[k].type, kInfo[k].group} : Key{};
        }
    }
    // UTF-8, not %ls: the C library converts %ls in the "C" locale and cut the line at the first
    // letter that is not ASCII (a Cyrillic Windows user name, BUG-0164)
    H->log(M, SK_INFO, "creations: My Creations %s", slotfs::narrow(g_es_path).c_str());
    return true;
}

static bool load_editor_saves(dbpf::Package& pk)
{
    std::vector<uint8_t> f;
    return find_paths() && read_file(g_es_path, f) && pk.load(f);
}

static void refresh_options(double now)
{
    if (now - g_opts_read < 10)
        return;
    g_opts_read = now;
    g_es_ok = load_editor_saves(g_es);
    if (!g_es_ok)
        return;
    const dbpf::Package& pk = g_es;
    for (int k = 0; k < kKinds; k++) {
        g_opts[k].clear();
        for (auto& e : pk.entries()) {
            if (e.key.type != kBem || e.key.group != kInfo[k].group)
                continue;
            Option o;
            o.key = {e.key.instance, kInfo[k].type, kInfo[k].group};
            std::vector<uint8_t> m;
            std::string author;
            if (const dbpf::Entry* me = pk.find({e.key.instance, kMeta, e.key.group}))
                if (pk.read(*me, m))
                    dbpf::metadata_names(m, author, o.name);
            if (o.name.empty())
                o.name = hex(o.key.instance);
            g_opts[k].push_back(o);
        }
        // creation ids are handed out in order: the highest is the newest
        std::sort(g_opts[k].begin(), g_opts[k].end(),
                  [](const Option& a, const Option& b) { return a.key.instance > b.key.instance; });
    }
}

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }

static uint8_t* cell_S()
{
    uint8_t* g = game::mode_id() == modes::kCell ? modes::cell_game() : nullptr;
    return g ? *(uint8_t**)(g + 0x5190) : nullptr; // cCellSerializableData
}

static Key cell_key()
{
    uint8_t* s = cell_S();
    if (!s)
        return {};
    auto* k = (uint32_t*)(s + 0x10); // the player's cell model
    return {k[0], k[1], k[2]};
}

static Key ufo_key()
{
    void* emp = game::mode_id() == modes::kSpace && a_GetEmpire ? cdecl_call<void*>(a_GetEmpire) : nullptr;
    if (!emp)
        return {};
    auto* k = (uint32_t*)((uint8_t*)emp + 0xB8); // cEmpire::mUFOKey
    return {k[0], k[1], k[2]};
}

// a key some registered database has (RM FindRecord)
static bool key_exists(const Key& k)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    uint32_t key[3] = {k.instance, k.type, k.group}, real[3] = {};
    return rm && vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) != nullptr;
}

static Key avatar_species_key()
{
    void* av = game::mode_id() == modes::kCreature ? game::avatar() : nullptr;
    if (!av)
        return {};
    auto* k = (uint32_t*)((uint8_t*)av + 0xB28);
    return {k[0], k[1], k[2]};
}

static bool host_world_key(Kind k, uint32_t inst); // a creation of the host's world (BUG-0278)
static bool own_option(Kind k, const Key& key)
{
    for (auto& o : g_opts[k])
        if (o.key == key)
            return true;
    return false;
}

Option mine(Kind k)
{
    bool guest = C.sess && C.sess->role() == net::Role::Client;
    // BUG-0278: a choice of a creation of the host's world that is not one of ours (taken over from
    // an editor by 0.7.3.0) is not a choice; "the world's own" follows the host's creature
    bool chosen = g_chosen[k];
    if (g_pre_editor_fix && chosen && guest && k != kUfo && g_choice[k].instance &&
        host_world_key(k, g_choice[k].instance) && !own_option(k, g_choice[k]))
        chosen = false;
    if (g_pre_editor_fix && chosen && guest && k != kUfo && !g_choice[k].instance) {
        auto h = g_applied.find({0, (uint8_t)k});
        Key a = h != g_applied.end() && h->second.orig.instance ? h->second.orig
                : k == kCreature                                ? avatar_species_key()
                                                                : cell_key();
        return {a, ""}; // shown as "the world's own"
    }
    if (chosen && k != kUfo) { // the ship is always the own empire's
        for (auto& o : g_opts[k])
            if (o.key == g_choice[k])
                return o;
        return {g_choice[k], g_choice[k].instance ? hex(g_choice[k].instance) : ""};
    }
    auto named = [&](const Key& a) -> Option {
        for (auto& o : g_opts[k])
            if (o.key.instance == a.instance)
                return o;
        return {a, ""};
    };
    // a spaceship: everybody plays its own galaxy, the ship of its own empire
    if (k == kUfo) {
        Key u = ufo_key();
        return u.instance ? named(u) : Option{};
    }
    // default: a guest plays its newest creation; the host keeps the one of its world
    if (guest && !g_opts[k].empty())
        return g_opts[k].front();
    Key a = k == kCreature ? avatar_species_key() : cell_key();
    if (a.instance)
        return named(a);
    return {};
}

// ---- bundle serialization ----
static std::vector<uint8_t> pack(const Bundle& b)
{
    net::Writer w;
    w.u32(b.key.instance); w.u32(b.key.type); w.u32(b.key.group);
    w.u16((uint16_t)b.recs.size());
    for (auto& r : b.recs) {
        w.u32(r.key.type); w.u32(r.key.group); w.u32(r.key.instance);
        w.u32((uint32_t)r.data.size());
        for (uint8_t c : r.data)
            w.u8(c);
    }
    return w.data;
}

static bool unpack(const std::vector<uint8_t>& d, Bundle& b)
{
    net::Reader r(d.data(), d.size());
    b.key.instance = r.u32(); b.key.type = r.u32(); b.key.group = r.u32();
    uint16_t n = r.u16();
    for (int i = 0; i < n && r.ok(); i++) {
        dbpf::Record rec;
        rec.key.type = r.u32(); rec.key.group = r.u32(); rec.key.instance = r.u32();
        uint32_t sz = r.u32();
        if (!r.ok() || sz > (4u << 20) || sz > r.left())
            return false;
        rec.data.resize(sz);
        if (sz)
            r.raw(rec.data.data(), sz);
        b.recs.push_back(std::move(rec));
    }
    return r.ok();
}

static bool make_bundle(Kind k, const Key& key, Bundle& b)
{
    b.key = key;
    if (!key.instance || !g_es_ok)
        return key.instance != 0; // a premade: no records (everybody has it)
    const dbpf::Package& pk = g_es;
    for (auto& e : pk.entries()) {
        if (e.key.instance != key.instance || (e.key.type != kBem && e.key.type != kMeta))
            continue;
        if (e.key.group != kInfo[k].group)
            continue;
        dbpf::Record r;
        r.key = e.key;
        if (pk.read(e, r.data))
            b.recs.push_back(std::move(r));
    }
    return true;
}

// ---- registering a package with the game's resource manager (docs/re/creations.md §3) ----
static void* register_package(const std::wstring& path, int priority)
{
    if (!a_OpNew || !a_DbCtor || !a_RmPtr)
        return nullptr;
    void* rm = *(void**)a_RmPtr;
    if (!rm)
        return nullptr;
    void* db = cdecl_call<void*>(a_OpNew, 0x388u, "SporeCoop creation", 0, 0, 0, 0);
    if (!db)
        return nullptr;
    thiscall<void*>(a_DbCtor, db, (const wchar_t*)path.c_str(), (void*)nullptr);
    vcall<int>((uint8_t*)db + 4, 0);   // AddRef
    vcall<bool>(db, 0x04 / 4);          // Initialize
    if (!vcall<bool>(db, 0x18 / 4, 1, 6, false)) { // Open(read, default, not lazy)
        g_last_error = "Open failed";
        return nullptr;
    }
    if (!vcall<bool>(rm, 0x50 / 4, true, db, priority)) { // RegisterDatabase
        g_last_error = "RegisterDatabase failed";
        return nullptr;
    }
    g_registered++;
    return db;
}

static void unregister_package(void* db)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    if (rm && db)
        vcall<bool>(rm, 0x50 / 4, false, db, 0);
}

// Packages we registered, by file: a bundle applied again (the same version after a rejoin) uses
// its registered package. Writing the file again failed while the game had it open, and the
// creation was then not applied at all (the partner's puppet fell back to the own avatar's body).
static std::map<std::wstring, void*> g_pkgs;
// the host's creations registered under their original key (the world's species), by that key
struct WorldPkg {
    uint32_t id = 0;
    void* db = nullptr;
};
static std::map<std::pair<uint8_t, uint32_t>, WorldPkg> g_world_pkgs; // (kind, original instance)

// guest: a key the host's world uses for a creation of the host (registered here above our own My
// Creations, so the host's creation answers for it)
static bool host_world_key(Kind k, uint32_t inst)
{
    if (g_world_pkgs.count({(uint8_t)k, inst}))
        return true;
    auto h = g_applied.find({0, (uint8_t)k});
    if (h != g_applied.end() && h->second.orig.instance == inst)
        return true;
    return k == kCreature && (g_remap.count(inst) || g_species_db.count(inst));
}
// BUG-0055: the session end drops them (a guest's own creation of the same key is its own again).
// `coop.creations worlddrop off` keeps them registered for the rest of the process (tests).
static bool g_world_drop = true;
COOP_FIX("creations.world_drop", "BUG-0055", g_world_drop, "creations: the host's world creations are dropped at the session end (coop.creations worlddrop)");
static unsigned g_world_dropped;

static void* package_for(std::wstring path, const std::vector<dbpf::Record>& recs, int priority)
{
    auto it = g_pkgs.find(path);
    if (it != g_pkgs.end())
        return it->second;
    std::vector<uint8_t> file = dbpf::write(recs);
    if (!write_file(path, file)) { // in use (a package we stopped using is still open): another name
        std::wstring base = path.substr(0, path.size() - 8); // without ".package"
        bool ok = false;
        for (int n = 2; n < 50 && !ok; n++) {
            path = base + L"_" + std::to_wstring(n) + L".package";
            ok = !g_pkgs.count(path) && write_file(path, file);
        }
        if (!ok)
            return nullptr;
    }
    void* db = register_package(path, priority);
    if (db)
        g_pkgs[path] = db;
    return db;
}

static void drop_package(void* db)
{
    if (!db)
        return;
    unregister_package(db);
    for (auto it = g_pkgs.begin(); it != g_pkgs.end();)
        it = it->second == db ? g_pkgs.erase(it) : std::next(it);
}

// a bundle arrived (or is our own): store it here under a collision-free instance and register it
static void apply(uint8_t owner, Kind k, uint32_t id, const std::vector<uint8_t>& data)
{
    Bundle b;
    if (!unpack(data, b)) {
        g_last_error = "bad bundle";
        return;
    }
    auto& ap = g_applied[{owner, (uint8_t)k}];
    if (ap.id == id && (ap.key.instance || ap.unusable))
        return;
    std::string name;
    for (auto& r : b.recs)
        if (r.key.type == kMeta) {
            std::string author;
            dbpf::metadata_names(r.data, author, name);
        }
    Key use = b.key;
    void* old = ap.db;
    void* db = nullptr;
    bool unusable = false;
    if (!b.recs.empty() && find_paths()) {
        // the same id everywhere: owner + content hash (a new version after an editor = a new key)
        uint8_t seed[5] = {owner, (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16), (uint8_t)(id >> 24)};
        uint32_t inst = 0x7C000000u | (fnv(seed, 5) & 0x00FFFFFFu);
        std::vector<dbpf::Record> recs = b.recs;
        for (auto& r : recs)
            if (r.key.instance == b.key.instance)
                r.key.instance = inst;
        // the host's own creation is also its world's species: register it under the original
        // key too, above everybody's own My Creations (a guest may own another creation there).
        // A newer version under the same key replaces the older package (both at the same
        // priority, the older one could keep answering for the key).
        if (owner == 0 && C.sess && C.sess->role() == net::Role::Client) {
            WorldPkg& wp = g_world_pkgs[{(uint8_t)k, b.key.instance}];
            if (wp.id != id) {
                std::wstring path = g_dir + L"\\world_" + std::to_wstring(k) + L"_" + std::to_wstring(id) + L".package";
                if (void* wdb = package_for(path, b.recs, kPrioWorld)) {
                    if (wp.db && wp.db != wdb) {
                        drop_package(wp.db);
                        g_world_swaps++;
                    }
                    wp = {id, wdb};
                }
            }
        }
        std::wstring p = g_dir + L"\\p" + std::to_wstring(owner) + L"_" + std::to_wstring(k) + L"_" +
                         std::to_wstring(id) + L".package";
        db = package_for(p, recs, kPrioPlayer);
        if (!db) {
            g_last_error = "cannot write the package";
            return;
        }
        use.instance = inst;
    } else if (b.key.instance && !key_exists(b.key)) {
        // no records and this game does not know the key (an own creation sent before it was on
        // disk, or by an older version): nothing to build from; puppets keep a body of their own
        use = {};
        unusable = true;
        g_unusable++;
    }
    if (old && old != db)
        drop_package(old);
    ap.id = id;
    ap.key = use;
    ap.orig = b.key;
    ap.db = db;
    ap.name = name;
    ap.unusable = unusable;
    g_bundles_in++;
    H->log(M, SK_INFO, "creations: player %u %s = %08X/%08X/%08X '%s' (%u records)%s", owner, kInfo[k].name,
           use.instance, use.type, use.group, name.c_str(), (unsigned)b.recs.size(),
           unusable ? ": unknown here, not used" : "");
    if (C.appearance_changed)
        C.appearance_changed(owner);
}

// ---- sending ----
constexpr size_t kChunk = 900;
static void send_bundle(int to, uint8_t owner, Kind k, uint32_t id, const std::vector<uint8_t>& data);

// The records of a creation from the package file the game reads it from (any registered package:
// My Creations (DBPF), the Sporepedia cache Pollination.package (DBBF), one of ours). Read piece by
// piece: Pollination grows to hundreds of MB and the game has little address space left.
static bool records_from_game(const Key& k, std::vector<dbpf::Record>& out)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    uint32_t key[3] = {k.instance, k.type, k.group}, real[3] = {};
    void* db = rm ? vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) : nullptr; // FindRecord
    if (!db)
        return false;
    const wchar_t* loc = vcall<const wchar_t*>(db, 0x28 / 4); // GetLocation (as for EditorSaves)
    if (!loc || IsBadReadPtr(loc, 8) || !(loc[1] == L':' || loc[0] == L'\\'))
        return false;
    HANDLE f = CreateFileW(loc, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    auto read_at = [&](uint64_t off, uint32_t n, std::vector<uint8_t>& buf) {
        buf.resize(n);
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)off;
        DWORD rd = 0;
        return SetFilePointerEx(f, li, nullptr, FILE_BEGIN) && (n == 0 || (ReadFile(f, buf.data(), n, &rd, nullptr) && rd == n));
    };
    std::vector<uint8_t> hdr, idx, raw;
    dbpf::IndexInfo info;
    std::vector<dbpf::Entry> all;
    bool ok = read_at(0, 96, hdr) && dbpf::index_info(hdr.data(), hdr.size(), info) && info.size < (64u << 20) &&
              read_at(info.offset, info.size, idx) && dbpf::parse_index(idx.data(), idx.size(), info, all);
    for (auto& e : all) {
        if (!ok || e.key.instance != k.instance || e.key.group != k.group ||
            (e.key.type != kBem && e.key.type != kMeta && e.key.type != k.type) || e.size > (4u << 20))
            continue;
        dbpf::Record r;
        r.key = e.key;
        if (read_at(e.offset, e.size, raw) && dbpf::decode(e, raw.data(), r.data))
            out.push_back(std::move(r));
    }
    CloseHandle(f);
    bool model = false;
    for (auto& r : out)
        model |= r.key.type != kMeta;
    return model;
}

// host: a guest asks for a species of our world
static void serve_species(uint8_t guest, uint32_t instance)
{
    Bundle b;
    b.key = {instance, kInfo[kCreature].type, kInfo[kCreature].group};
    if (!records_from_game(b.key, b.recs)) {
        g_species_missing++;
        H->log(M, SK_WARN, "creations: player %u asks for species %08X: not found here", guest, instance);
        return;
    }
    std::vector<uint8_t> data = pack(b);
    send_bundle(guest, 0, (Kind)kWorldSpecies, instance, data);
    g_species_sent++;
    H->log(M, SK_INFO, "creations: species %08X (%u records, %u bytes) to player %u", instance, (unsigned)b.recs.size(),
           (unsigned)data.size(), guest);
}

// guest: the records of a species of the host's world, registered under its own key (the world
// names it so). Above My Creations: asked only for keys this game did not know.
static void apply_species(const std::vector<uint8_t>& data)
{
    Bundle b;
    if (!unpack(data, b) || b.recs.empty() || !find_paths() || g_species_db.count(b.key.instance))
        return;
    std::wstring path = g_dir + L"\\species_" + std::to_wstring(b.key.instance) + L".package";
    void* db = package_for(path, b.recs, kPrioWorld);
    if (!db) {
        g_last_error = "cannot write a species package";
        return;
    }
    g_species_db[b.key.instance] = db;
    g_species_got++;
    H->log(M, SK_INFO, "creations: species %08X of the host's world registered (%u records)", b.key.instance,
           (unsigned)b.recs.size());
}

void want_species(const Key& k)
{
    if (!C.sess || C.sess->role() != net::Role::Client || !C.sess->connected() || !k.instance ||
        k.type != kInfo[kCreature].type || k.group != kInfo[kCreature].group || g_species_db.count(k.instance))
        return;
    double now = H->time_seconds();
    auto it = g_asked.find(k.instance);
    if (it != g_asked.end() && now - it->second < 30.0)
        return; // asked recently (an older host does not answer: ask again now and then)
    g_asked[k.instance] = now;
    net::Writer w;
    w.u8(C.sess->my_id());
    w.u8(kAskSpecies);
    w.u32(k.instance);
    w.u32(0);
    w.u32(0);
    w.u16(0);
    C.sess->send_reliable_to(0, net::MSG_CREATION, w);
    g_species_asked++;
    H->log(M, SK_INFO, "creations: species %08X of the host's world is unknown here: asked the host", k.instance);
}

// ---- the host's own creations among its world's species (creations.h) ----
static bool in_my_creations(const Key& k);
static bool is_crt(const Key& k) { return k.type == kInfo[kCreature].type && k.group == kInfo[kCreature].group; }

void host_species_out(uint8_t peer, const Key& k)
{
    if (!is_host() || peer == 0 || !k.instance || !is_crt(k))
        return;
    auto& sent = g_own_sent[peer];
    if (!sent.insert(k.instance).second)
        return; // checked (and sent if ours) already
    if (!in_my_creations(k)) // a premade or a Sporepedia download: the same creation everywhere
        return;
    Bundle b;
    b.key = k;
    if (!records_from_game(k, b.recs)) {
        g_species_missing++;
        return;
    }
    // and its creaturedata (the game's cache of the species' abilities and stats, in Pollination):
    // a guest that has none computes it on first use, and a key first used while a world loaded
    // left the pack member of it without a species (0xD2C361: coop_creature_desync.py 2026-10-25)
    std::vector<dbpf::Record> cd;
    if (records_from_game({k.instance, kCreatureData, k.group}, cd))
        for (auto& r : cd)
            if (r.key.type == kCreatureData)
                b.recs.push_back(std::move(r));
    std::vector<uint8_t> data = pack(b);
    send_bundle(peer, 0, (Kind)kOwnSpecies, k.instance, data);
    g_own_out++;
    H->log(M, SK_INFO, "creations: own creation %08X of this world (%u records%s) to player %u", k.instance,
           (unsigned)b.recs.size(), cd.empty() ? ", no creaturedata" : "", peer);
}

void world_species_out(uint8_t peer, const std::vector<const std::vector<uint8_t>*>& files)
{
    if (!is_host())
        return;
    std::vector<uint32_t> keys;
    for (auto* f : files)
        dbpf::find_keys(*f, kInfo[kCreature].type, kInfo[kCreature].group, keys);
    for (uint32_t inst : keys)
        host_species_out(peer, {inst, kInfo[kCreature].type, kInfo[kCreature].group});
}

// guest: a creation of the host's own My Creations that its world uses: registered under a key of
// our own (from its content: the same creation gets the same key in every session, which keeps the
// game's caches by key right), not only under the host's key (our creation of that key would
// answer: the game's creaturedata and baked models are cached by key, ✔ 2026-10-25). The world's
// save keeps the host's key: a save rewritten to ours lost those herds and creatures at the load
// and the pack panel faulted on a pack member of them (docs/re/creature.md 15)
static void apply_own_species(const std::vector<uint8_t>& data)
{
    Bundle b;
    if (!unpack(data, b) || b.recs.empty() || !is_crt(b.key) || !find_paths())
        return;
    uint32_t h = 0x811C9DC5;
    for (auto& r : b.recs) // the creation itself (not the host's creaturedata cache)
        if (r.key.instance == b.key.instance && r.key.type != kCreatureData)
            h = fnv(r.data.data(), r.data.size(), fnv(&r.key.type, 4, h));
    uint32_t ours = 0x7D000000u | (h & 0x00FFFFFFu);
    auto it = g_remap.find(b.key.instance);
    if (it != g_remap.end() && it->second == ours)
        return;
    std::vector<dbpf::Record> recs = b.recs;
    for (auto& r : recs) {
        if (r.key.instance != b.key.instance)
            continue;
        r.key.instance = ours;
        // the metadata names its own key (big-endian type, group, instance at +0x0C): ours too
        uint8_t* d = r.data.data();
        if (r.key.type == kMeta && r.data.size() >= 0x18 &&
            ((uint32_t)d[0x14] << 24 | d[0x15] << 16 | d[0x16] << 8 | d[0x17]) == b.key.instance)
            for (int i = 0; i < 4; i++)
                d[0x14 + i] = (uint8_t)(ours >> (24 - 8 * i));
    }
    std::wstring path = g_dir + L"\\own_species_" + std::to_wstring(ours) + L".package";
    if (!package_for(path, recs, kPrioWorld)) {
        g_last_error = "cannot write a species package";
        return;
    }
    // a guest that has nothing under the host's key gets the host's records there too (as for an
    // unknown species, apply_species): whatever still names the species by that key finds it
    if (!key_exists(b.key) && !g_species_db.count(b.key.instance)) {
        std::wstring kp = g_dir + L"\\species_" + std::to_wstring(b.key.instance) + L".package";
        if (void* db = package_for(kp, b.recs, kPrioWorld))
            g_species_db[b.key.instance] = db;
    }
    g_remap[b.key.instance] = ours;
    g_unmap[ours] = b.key.instance;
    g_own_in++;
    // the species profile now (the game keeps profiles for the whole process): the first proxy of
    // it does not wait for its creaturedata
    void* prof = game::species_by_key({ours, b.key.type, b.key.group});
    std::string name, author;
    for (auto& r : b.recs)
        if (r.key.type == kMeta)
            dbpf::metadata_names(r.data, author, name);
    H->log(M, SK_INFO, "creations: the host's own creation %08X '%s' of its world is %08X here%s", b.key.instance,
           name.c_str(), ours, prof ? "" : " (no species profile yet)");
}

Key local_species(const Key& k)
{
    auto it = is_crt(k) ? g_remap.find(k.instance) : g_remap.end();
    return it == g_remap.end() ? k : Key{it->second, k.type, k.group};
}

Key host_species(const Key& k)
{
    auto it = is_crt(k) ? g_unmap.find(k.instance) : g_unmap.end();
    return it == g_unmap.end() ? k : Key{it->second, k.type, k.group};
}

bool species_pending(const Key& k) { return g_asked.count(k.instance) && !g_species_db.count(k.instance); }

static void send_bundle(int to, uint8_t owner, Kind k, uint32_t id, const std::vector<uint8_t>& data)
{
    for (size_t off = 0; off < data.size() || off == 0; off += kChunk) {
        net::Writer w;
        w.u8(owner);
        w.u8((uint8_t)k);
        w.u32(id);
        w.u32((uint32_t)data.size());
        w.u32((uint32_t)off);
        size_t n = std::min(kChunk, data.size() - off);
        w.u16((uint16_t)n);
        for (size_t i = 0; i < n; i++)
            w.u8(data[off + i]);
        if (to < 0)
            C.sess->broadcast_reliable(net::MSG_CREATION, w);
        else
            C.sess->send_reliable_to((uint8_t)to, net::MSG_CREATION, w);
        if (data.empty())
            break;
    }
}

// a creation of this player's own My Creations (the game finds it in the EditorSaves database),
// not a premade every machine has
static bool in_my_creations(const Key& k)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    void* es = a_GetDb ? cdecl_call<void*>(a_GetDb, kEditorSavesArea) : nullptr;
    uint32_t key[3] = {k.instance, k.type, k.group}, real[3] = {};
    return rm && es && vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) == es;
}

static void share_own(double now)
{
    if (!C.sess || !C.sess->connected())
        return;
    refresh_options(now);
    uint8_t me = C.sess->my_id();
    for (int k = 0; k < kKinds; k++) {
        Option o = mine((Kind)k);
        if (!o.key.instance)
            continue;
        Bundle b;
        Key hk = host_species(o.key);
        bool world_own = g_pre_editor_fix && !is_host() && k != kUfo && g_chosen[k] && !g_choice[k].instance;
        if (world_own) // "the world's own": the host's creature, by its key (BUG-0278)
            b.key = o.key;
        else if (!(hk == o.key)) // a guest that plays the host world's species, a creation of the host: the
            b.key = hk;     // host knows it under its own key (our records of that key are another one)
        else if (!make_bundle((Kind)k, o.key, b))
            continue;
        // A creation the editor has just saved is in the game's EditorSaves database before our
        // copy of the file has it (read every 10 s). Sent without its records it was a key nobody
        // else knows: the guest's game faulted building the host's new cell from it
        // (CreateCellObject, 0xE83EC8) and showed the old creature until the next bundle.
        if (!world_own && b.recs.empty() && in_my_creations(o.key)) {
            g_opts_read = -100;
            refresh_options(now);
            b = Bundle{};
            if (!make_bundle((Kind)k, o.key, b) || b.recs.empty()) {
                g_waits_on_disk++;
                continue; // not on disk yet: next round
            }
        }
        std::vector<uint8_t> data = pack(b);
        uint32_t id = fnv(data.data(), data.size());
        if (g_sent_id[k] == id)
            continue;
        g_sent_id[k] = id;
        apply(me, (Kind)k, id, data); // our own, under the same key everybody uses
        if (is_host()) {
            g_store[{me, (uint8_t)k}] = data;
            send_bundle(-1, me, (Kind)k, id, data);
        } else
            send_bundle(0, me, (Kind)k, id, data);
        g_bundles_out++;
    }
}

// ---- the own avatar takes the own creature ----
static void* g_swapped_avatar;
static Key g_swapped_key;
static void* g_seen_avatar; // a new avatar (a world was loaded): wait until the world has settled
static double g_seen_since;
// BUG-0119: the world counter with the avatar (the game reuses creature objects: the avatar of a
// reloaded world may have the old address). Tests: `coop.creations sameaddr` makes the next new
// avatar look like the old one (a reused address).
static uint32_t g_seen_epoch, g_swapped_epoch;
static bool g_sim_same_addr;

// the key this machine uses for its own creation of a kind
static Key own_key_here(Kind kind)
{
    // the own key itself (My Creations has it, and a saved game keeps a key every launch knows);
    // a guest whose own key is also the host's (another creation with the same id: ids are per
    // profile) uses the shared remapped one: the host's world package shadows the own one
    uint8_t me = C.sess->my_id();
    Key k = mine(kind).key;
    auto host = g_applied.find({0, (uint8_t)kind});
    if (me != 0 && host != g_applied.end() && host->second.orig == k)
        k = player_key(me, kind);
    // BUG-0278: an own creation under a key of the host's world (the own editor's "Save New" keeps
    // the key of the host's creation it edited): the host's package answers for that key here, ours
    // is registered under the shared remapped key
    else if (g_pre_editor_fix && me != 0 && host_world_key(kind, k.instance) && own_option(kind, k))
        if (Key r = player_key(me, kind); r.instance)
            k = r;
    return k;
}

// the own cell: the player's cell model, then the level is rebuilt (the cell hatches again)
static Key g_cell_set;
static bool g_cell_rebuilding;
static void apply_own_cell()
{
    uint8_t* s = cell_S();
    uint8_t* g = modes::cell_game();
    if (!s || !g)
        return;
    if (g_cell_rebuilding) { // wait for the new avatar with the new model, then join the partner
        uint8_t* av = modes::cell_at(*(uint32_t*)(g + 0x411C));
        if (av && *(uint32_t*)(av + 0xFC) == g_cell_set.instance && *(uint8_t*)(g + 0x51D8) == 0) {
            g_cell_rebuilding = false;
            if (C.cell_rebuilt)
                C.cell_rebuilt();
        }
        return;
    }
    Key k = own_key_here(kCell);
    if (!k.instance || k == cell_key() || k == g_cell_set || !key_exists(k))
        return;
    g_cell_set = k;
    auto* d = (uint32_t*)(s + 0x10);
    d[0] = k.instance; d[1] = k.type; d[2] = k.group;
    if (a_CellPreload)
        cdecl_call<void>(a_CellPreload, k.instance, k.type, k.group); // preload set (as the editor does)
    *(uint8_t*)(g + 0x51D8) = 1; // RebuildLevel on the next cell frame
    g_cell_rebuilding = true;
    g_swaps++;
    H->log(M, SK_INFO, "creations: own cell is now %08X (level rebuilt)", k.instance);
}

static void apply_own_avatar()
{
    if (game::mode_id() == modes::kCell && C.sess && C.sess->connected()) {
        apply_own_cell();
        return;
    }
    if (game::mode_id() != modes::kCreature || !C.sess || !C.sess->connected() || !a_SetSpecies)
        return;
    void* av = game::avatar();
    Key k = own_key_here(kCreature);
    if (!av || !k.instance)
        return;
    // SetSpecies while a world is still loading crashed (a null call inside the game): only on an
    // avatar that has lived for a few seconds
    double now = H->time_seconds();
    uint32_t epoch = game::world_epoch();
    if (g_sim_same_addr && g_seen_avatar && av != g_seen_avatar) { // tests: as if the address came back
        g_sim_same_addr = false;
        if (g_swapped_avatar == g_seen_avatar)
            g_swapped_avatar = av;
        g_seen_avatar = av;
    }
    if (av != g_seen_avatar || epoch != g_seen_epoch) {
        g_seen_avatar = av;
        g_seen_epoch = epoch;
        g_seen_since = now;
    }
    if (now - g_seen_since < 3.0)
        return;
    if (av == g_swapped_avatar && epoch == g_swapped_epoch && k == g_swapped_key)
        return;
    Key cur = avatar_species_key();
    if (!(cur == k)) {
        void* prof = game::species_by_key({k.instance, k.type, k.group});
        if (!prof)
            return; // not resolvable (yet)
        int age = *(int*)((uint8_t*)av + 0xB34);
        thiscall<bool>(a_SetSpecies, av, prof, age, true);
        g_swaps++;
        H->log(M, SK_INFO, "creations: own avatar is now %08X", k.instance);
    }
    g_swapped_avatar = av;
    g_swapped_epoch = epoch;
    g_swapped_key = k;
}

// ---- api ----
bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetDb = H->sym("Resource::GetDatabaseByID");
    a_OpNew = H->sym("Memory::operator_new_named");
    a_DbCtor = H->sym("Resource::DatabasePackedFile::ctor");
    a_RmPtr = H->sym("Resource::ResourceManager_ptr");
    a_SetSpecies = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    a_GetEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_CellPreload = H->sym("Simulator::Cell::PreloadModel");
    return a_GetDb && a_OpNew && a_DbCtor && a_RmPtr;
}

// back from an editor: the edited creature (a new creation in My Creations) is the own one now
// (the avatar appears a moment after the mode changes: wait for it). The same for a cell: until
// 2026-10-24 only creatures were taken over, and a guest with other cells in My Creations got its
// newest older cell back (and a level rebuild) right after its own cell editor.
static uint32_t g_prev_mode;
static double g_adopt_until, g_adopt_read;
static Kind g_adopt_kind;
static std::set<uint32_t> g_before_editor[kKinds]; // own creations when the editor opened (BUG-0278)
static unsigned g_adopt_skipped; // tests: returns from an editor that took over nothing of the world's
static bool adopt_after_editor(double now)
{
    uint32_t m = game::mode_id();
    bool editor = m == modes::kEdit || m == modes::kEditor;
    if (editor && !(g_prev_mode == modes::kEdit || g_prev_mode == modes::kEditor) && g_pre_editor_fix) {
        g_opts_read = -100; // what was there before: a new creation after it is the editor's
        refresh_options(now);
        for (int k = 0; k < kKinds; k++) {
            g_before_editor[k].clear();
            for (auto& o : g_opts[k])
                g_before_editor[k].insert(o.key.instance);
        }
    }
    if ((g_prev_mode == modes::kEdit || g_prev_mode == modes::kEditor) && (m == modes::kCreature || m == modes::kCell)) {
        g_adopt_until = now + 30;
        g_adopt_kind = m == modes::kCell ? kCell : kCreature;
        g_adopt_read = 0;
    }
    g_prev_mode = m;
    if (now > g_adopt_until)
        return false;
    Key a = g_adopt_kind == kCell ? cell_key() : avatar_species_key();
    if (!a.instance)
        return true; // not yet: do not apply the old one meanwhile
    if (g_pre_editor_fix && C.sess->role() == net::Role::Client) {
        // BUG-0278: a guest takes over only what the editor saved: the newest own creation that was
        // not in My Creations when the editor opened (a guest's own editor leaves the avatar on the
        // world's species, and right after the host's evolution editor the avatar still has the
        // old key). The file lags the editor by seconds: read it again every 2 s for 30 s. The
        // host keeps the old way (its default is the avatar's key anyway).
        if (now - g_adopt_read < 2.0)
            return false;
        g_adopt_read = now;
        g_opts_read = -100;
        refresh_options(now);
        Key pick;
        for (auto& o : g_opts[g_adopt_kind]) // newest first
            if (!g_before_editor[g_adopt_kind].count(o.key.instance)) {
                pick = o.key;
                break;
            }
        if (!pick.instance) {
            if (now + 2.0 > g_adopt_until) {
                g_adopt_skipped++;
                H->log(M, SK_INFO, "creations: back from the editor, no new own %s (the avatar keeps %08X)",
                       kInfo[g_adopt_kind].name, a.instance);
            }
            return false;
        }
        g_adopt_until = 0;
        if (!(pick == mine(g_adopt_kind).key) || !g_chosen[g_adopt_kind]) {
            choose(g_adopt_kind, pick);
            H->log(M, SK_INFO, "creations: back from the editor, own %s %08X", kInfo[g_adopt_kind].name, pick.instance);
        }
        return false;
    }
    g_adopt_until = 0;
    if (!(a == mine(g_adopt_kind).key)) {
        g_opts_read = -100;
        refresh_options(now);
        choose(g_adopt_kind, a);
        H->log(M, SK_INFO, "creations: back from the editor, own %s %08X", kInfo[g_adopt_kind].name, a.instance);
    }
    return false;
}

void tick(double now)
{
    if (!C.sess || !C.sess->connected())
        return;
    if (adopt_after_editor(now))
        return;
    if (now - g_last_check > 2.0) {
        g_last_check = now;
        share_own(now);
    }
    apply_own_avatar();
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_CREATION)
        return;
    uint8_t owner = r.u8(), kind = r.u8();
    uint32_t id = r.u32(), total = r.u32(), off = r.u32();
    uint16_t n = r.u16();
    if (r.ok() && kind == kAskSpecies) {
        if (is_host() && owner == from)
            serve_species(from, id);
        return;
    }
    if ((kind == kWorldSpecies || kind == kOwnSpecies) && (is_host() || from != 0))
        return;
    if (!r.ok() || (kind >= kKinds && kind != kWorldSpecies && kind != kOwnSpecies) || total > (8u << 20) ||
        (uint64_t)off + n > total)
        return;
    if (!is_host() && from != 0)
        return;
    if (is_host() && owner != from)
        return; // a guest only sends its own
    if (r.left() < n)
        return; // a cut piece: not a part of the creation
    auto& rx = g_rx[{owner, kind}];
    net::Chunk got = net::chunk_add(rx, id, total, off, r.cur(), n, kChunk, 8u << 20);
    if (got == net::Chunk::Drop) {
        g_rx_dropped++;
        return;
    }
    if (got == net::Chunk::Partial)
        return;
    std::vector<uint8_t> data = std::move(rx.data);
    g_rx.erase({owner, kind});
    if (kind == kWorldSpecies) {
        apply_species(data);
        return;
    }
    if (kind == kOwnSpecies) {
        if (g_own_species_fix)
            apply_own_species(data);
        return;
    }
    if (is_host()) { // keep it for players joining later and pass it on
        g_store[{owner, kind}] = data;
        for (auto& p : C.sess->players())
            if (p.id != 0 && p.id != owner)
                send_bundle(p.id, owner, (Kind)kind, id, data);
    }
    apply(owner, (Kind)kind, id, data);
}

void on_player_join(uint8_t id)
{
    if (!is_host())
        return;
    g_own_sent.erase(id); // a new guest under this id: it has none of our creations yet
    for (auto& [ok, data] : g_store)
        if (ok.first != id)
            send_bundle(id, ok.first, (Kind)ok.second, fnv(data.data(), data.size()), data);
}

bool g_leave_fix = true;
COOP_FIX("core.creations_leave", "BUG-0076", g_leave_fix, "creations: a player's creations go with the player, a new one does not inherit them");

void on_player_leave(uint8_t id)
{
    if (!g_leave_fix || id == 0)
        return; // (the host's leave ends the session: on_session_end)
    auto owned = [id](auto& m) {
        for (auto it = m.begin(); it != m.end();)
            it = it->first.first == id ? m.erase(it) : std::next(it);
    };
    owned(g_store);
    owned(g_rx);
    // its packages stay registered (cheap, as on_session_end): its puppet is gone with it, and a
    // package of the same content is found again by path if that player comes back
    owned(g_applied);
    g_own_sent.erase(id);
}

void on_session_end()
{
    // the host's creations registered under their original keys (priority 2000, above the guest's
    // own My Creations): after the session a guest's own creation of the same id (ids are per
    // profile and sequential) resolved to the host's records in its own games (BUG-0055). They go;
    // the remapped packages (0x7C... / 0x7D...) and the species packages of keys the guest did not
    // have stay (nothing of the guest's own answers for those keys)
    if (g_world_drop) {
        for (auto& [k, wp] : g_world_pkgs)
            if (wp.db) {
                drop_package(wp.db);
                g_world_dropped++;
            }
        g_world_pkgs.clear();
    }
    g_asked.clear(); // (registered species packages stay: the same world may come back)
    g_own_sent.clear();
    g_remap.clear(); // (their packages stay registered: the same world may come back)
    g_unmap.clear();
    g_store.clear();
    g_rx.clear();
    for (auto& s : g_sent_id)
        s = 0;
    // registered packages stay (cheap); keys of other players are forgotten
    for (auto it = g_applied.begin(); it != g_applied.end();)
        it = g_applied.erase(it);
    g_swapped_avatar = nullptr;
    g_cell_set = {};
}

Key player_key(uint8_t player, Kind k)
{
    auto it = g_applied.find({player, (uint8_t)k});
    return it == g_applied.end() ? Key{} : it->second.key;
}

void* player_species(uint8_t player)
{
    Key k = player_key(player, kCreature);
    return k.instance ? game::species_by_key({k.instance, k.type, k.group}) : nullptr;
}

std::vector<Option> options(Kind k)
{
    refresh_options(H->time_seconds());
    return g_opts[k];
}

void choose(Kind k, const Key& key)
{
    g_chosen[k] = true;
    g_choice[k] = key;
    if (find_paths()) {
        std::wstring v = key.instance ? std::wstring(L"0x") + std::to_wstring(0) : L"0";
        wchar_t hx[16];
        swprintf(hx, 16, L"%08X", key.instance);
        WritePrivateProfileStringW(L"creations", std::wstring(kInfo[k].name, kInfo[k].name + strlen(kInfo[k].name)).c_str(),
                                   hx, (g_dir + L"\\choices.ini").c_str());
    }
    g_last_check = 0; // share now
}

void cycle(Kind k, int dir)
{
    auto opts = options(k);
    // BUG-0278: "the world's own" (the host's creature) is one of the choices: a guest can go back
    // to it from a creature of its own
    if (g_pre_editor_fix && C.sess && C.sess->role() == net::Role::Client && k != kUfo && !opts.empty()) {
        std::vector<Key> ring{Key{}};
        for (auto& o : opts)
            ring.push_back(o.key);
        int i = 0;
        bool world = g_chosen[k] && !g_choice[k].instance;
        Key cur = mine(k).key;
        for (int j = 1; j < (int)ring.size(); j++)
            if (!world && ring[j] == cur)
                i = j;
        i = ((i + dir) % (int)ring.size() + (int)ring.size()) % (int)ring.size();
        choose(k, ring[i]);
        return;
    }
    if (opts.empty())
        return;
    Option cur = mine(k);
    int i = 0;
    for (int j = 0; j < (int)opts.size(); j++)
        if (opts[j].key == cur.key)
            i = j;
    i = ((i + dir) % (int)opts.size() + (int)opts.size()) % (int)opts.size();
    choose(k, opts[i].key);
}

std::string status()
{
    std::string s;
    char b[300];
    snprintf(b, sizeof b,
             "creations: in=%u out=%u registered=%u avatar_swaps=%u waits_on_disk=%u unusable=%u world_swaps=%u "
             "species asked=%u got=%u sent=%u missing=%u own_out=%u own_in=%u rx_dropped=%u adopt_skipped=%u%s%s\n",
             g_bundles_in, g_bundles_out, g_registered, g_swaps, g_waits_on_disk, g_unusable, g_world_swaps,
             g_species_asked, g_species_got, g_species_sent, g_species_missing, g_own_out, g_own_in, g_rx_dropped, g_adopt_skipped, g_last_error.empty() ? "" : " error=", g_last_error.c_str());
    s += b;
    if (uint8_t* g = modes::cell_game(); g && game::mode_id() == modes::kCell) {
        uint8_t* av = modes::cell_at(*(uint32_t*)(g + 0x411C));
        Key ck = cell_key();
        snprintf(b, sizeof b, "  cell: player model %08X, avatar model %08X\n", ck.instance,
                 av ? *(uint32_t*)(av + 0xFC) : 0u);
        s += b;
    }
    for (int k = 0; k < kKinds; k++) {
        Option o = mine((Kind)k);
        snprintf(b, sizeof b, "  mine %s: %08X '%s' (%u own)\n", kInfo[k].name, o.key.instance, o.name.c_str(),
                 (unsigned)g_opts[k].size());
        s += b;
    }
    for (auto& [hk, ours] : g_remap) {
        snprintf(b, sizeof b, "  host species %08X = %08X here\n", hk, ours);
        s += b;
    }
    for (auto& [ok, ap] : g_applied) {
        snprintf(b, sizeof b, "  player %u %s: %08X/%08X/%08X '%s'\n", ok.first, kInfo[ok.second].name, ap.key.instance,
                 ap.key.type, ap.key.group, ap.name.c_str());
        s += b;
    }
    return s;
}

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && (a[0] == "next" || a[0] == "prev")) {
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        cycle((Kind)k, a[0] == "next" ? 1 : -1);
        return "chosen: " + mine((Kind)k).name;
    }
    if (a.size() >= 2 && a[0] == "choose") { // choose <kind> <hex instance|none>
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        uint32_t inst = a.size() > 2 && a[2] != "none" ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : 0;
        choose((Kind)k, inst ? Key{inst, kInfo[k].type, kInfo[k].group} : Key{});
        return "chosen";
    }
    if (!a.empty() && a[0] == "sameaddr") { // tests (BUG-0119): the next new avatar looks like the old one
        g_sim_same_addr = true;
        return "the next avatar is taken for the old one (a reused address)";
    }
    if (a.size() >= 2 && a[0] == "worlddrop") { // worlddrop on|off (tests, BUG-0055)
        g_world_drop = a[1] != "off";
        return std::string("the host's world creations are dropped at the session end: ") + (g_world_drop ? "on" : "off") +
               " (dropped " + std::to_string(g_world_dropped) + ", registered " + std::to_string(g_world_pkgs.size()) + ")";
    }
    if (a.size() >= 2 && a[0] == "bem") { // bem <hex crt instance>: the body the game finds for that key (tests)
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group};
        std::vector<dbpf::Record> recs;
        if (!records_from_game(k, recs))
            return "bem " + hex(k.instance) + " none\n";
        uint32_t h = 0;
        for (auto& r : recs)
            if (r.key.type == kBem)
                h = fnv(r.data.data(), r.data.size());
        return "bem " + hex(k.instance) + " " + hex(h) + "\n";
    }
    if (!a.empty() && a[0] == "list") {
        std::string s;
        for (int k = 0; k < kKinds; k++)
            for (auto& o : options((Kind)k))
                s += std::string(kInfo[k].name) + " " + hex(o.key.instance) + " " + o.name + "\n";
        return s;
    }
    return status();
}

} // namespace creations
