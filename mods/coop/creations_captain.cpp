// The captains between the games (WP-K1 of docs/plan-0.7.17.md, creations 1.2; docs/re/creations.md#creations-captain-0717):
//  - BUG-1279 (MSG_CREATION kind 10, host -> guest): the property list of the host's Space captain, so a guest's
//    captain screen (it plays the host's empire) shows the captain's items
//  - BUG-1262 (MSG_CREATION kind 11, any -> host -> all): each player's adventure captain, so the partner's puppet in an
//    adventure is the partner's captain, not this game's own (BUG-1254's stand-in)
//  - the save guard of SporeCoop::CaptainProps_Save: no property list of ours reaches a player's profile
// The starting point was the outside agent's commit 4e7490e6 (fix/agy-open-bugs) and its review
// (out/research/agy-review-creations.md): ported as ideas, the review's findings fixed. The rules are captain_logic.h.
#include "creations_internal.h"

#include "captain_logic.h"
#include "net/compat.h"

namespace creations {

extern SkMod* M;
constexpr int kPrioHostList = 500; // below EditorSaves (1000): never over anything of the game's

// ---- who reads kinds 10 and 11 (creations 1.2, BUG-1282's rule: an older peer gets nothing new) ----
static bool reads(const std::vector<net::FamilyRev>& v, bool has)
{
    const net::FamilyInfo* mine = net::family_info((uint8_t)net::Family::creations);
    return mine && net::compat::has_minor(v, has, (uint8_t)net::Family::creations, mine->rev, captain::kMinor);
}
static bool peer_reads(uint8_t id)
{
    bool has = false;
    std::vector<net::FamilyRev> v;
    return C.sess && C.sess->peer_families(id, has, v) && reads(v, has);
}
static bool host_reads()
{
    bool has = false;
    std::vector<net::FamilyRev> v;
    return C.sess && C.sess->host_families(has, v) && reads(v, has);
}
static bool guest_connected() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

static uintptr_t a_Empire, a_PropsSave;
static Key empire_captain()
{
    void* e = game::mode_id() == modes::kSpace && a_Empire ? skg::cdecl_call<void*>(a_Empire) : nullptr;
    if (!e || !skg::readable((uint8_t*)e + 0xC4, 12))
        return {};
    auto* k = (uint32_t*)((uint8_t*)e + 0xC4); // cEmpire mCaptainKey (SporeCoop::cEmpire_GetCaptainKey 0xC310F0)
    return {k[0], k[1], k[2]};
}

// the database that answers a key here (RM FindRecord) and whether it is a package of ours (SporeCoop folder)
static void* db_of(const Key& k)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    uint32_t key[3] = {k.instance, k.type, k.group}, real[3] = {};
    return rm ? vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) : nullptr;
}
// 0 none, 1 a database of the game's, 2 a package of ours, 3 this player's own My Creations (EditorSaves.package)
static int whose(const Key& k)
{
    void* db = db_of(k);
    if (!db)
        return 0;
    std::wstring p;
    if (!file_location(db, p))
        return 1;
    if (p.find(L"\\SporeCoop\\") != std::wstring::npos)
        return 2;
    return p.size() >= 19 && _wcsicmp(p.c_str() + p.size() - 19, L"EditorSaves.package") == 0 ? 3 : 1;
}

// one property list record of a creation from the game's own package file that answers it (EditorSaves.package or
// Pollination.package; the GA captain copies and "Name Your Captain"'s captains are in EditorSaves: their records are
// written by 0x6462E0, docs/re/creations.md#creations-captain-0717); false: none, ours, or not a file
static bool prop_from_game(const Key& pk, std::vector<uint8_t>& out)
{
    out.clear();
    std::wstring path;
    if (!file_location(db_of(pk), path) || path.find(L"\\SporeCoop\\") != std::wstring::npos)
        return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    std::vector<uint8_t> hdr, idx, raw;
    dbpf::IndexInfo info;
    std::vector<dbpf::Entry> all;
    bool ok = read_file_at(f, 0, 96, hdr) && dbpf::index_info(hdr.data(), hdr.size(), info) && info.size < (64u << 20) &&
              read_file_at(f, info.offset, info.size, idx) && dbpf::parse_index(idx.data(), idx.size(), info, all);
    bool found = false;
    for (auto& e : all)
        if (ok && e.key.instance == pk.instance && e.key.type == pk.type && e.key.group == pk.group && e.size <= (256u << 10) &&
            read_file_at(f, e.offset, e.size, raw) && dbpf::decode(e, raw.data(), out) && !out.empty()) {
            found = true;
            break;
        }
    CloseHandle(f);
    return found;
}

// App::cPropManager::RemovePropertyLists (vt 0x3C, 0x6A5BB0, thiscall(count, const ResourceKey* keys) ret 8; Ghidra
// 2026-11-23: the vtable 0x1409210 of App::IPropManager::Get's object): the manager's cached list of a key goes, so a
// list registered (or dropped) later is read again
static void prop_evict(const Key& pk)
{
    static uintptr_t get = H->sym("App::IPropManager::Get");
    void* pm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!pm || !skg::readable(pm, 4) || !pk.instance)
        return;
    uint32_t rk[3] = {pk.instance, pk.type, pk.group};
    vcall<void>(pm, 0x3C / 4, 1, (const uint32_t*)rk);
}

// =============================================================== BUG-1279: the host captain's property list
// A guest in the host's Space world plays the host's empire: its captain (cEmpire +0xC4) is the host's creation, whose
// property list {captain, 0x00B1B104, group | 0xE500} (item config 0xC339D5C7, the unlocked item arrays) the guest's
// game never had: the captain screen showed no item limits (BUG-0304's guard). The host sends that list (only a
// captain's: it has the item config; without the newer-version link 0x0D8A4BC9, which cEmpire_UpdateCaptainKey would
// follow on the guest onto an unrelated creation) to each guest of creations 1.2 when it plays Space; the guest
// registers it under the host's key in a package of ours at priority 500, only when it has nothing of its own under
// the list's key or the captain's creature key (captain::host_prop), and drops the game's cached list of that key.
// `coop.fix creations.captain_props off`: nothing sent, nothing taken, every list saved as the game does (0.7.16.1).
bool g_captain_props_fix = true;
COOP_FIX("creations.captain_props", "BUG-1279", g_captain_props_fix,
         "space: a guest gets the host captain's property list (its items), and no list of ours is saved into a profile");
struct HostList {
    uint32_t hash = 0;
    void* db = nullptr;
    Key prop;
};
static std::map<std::pair<uint32_t, uint32_t>, HostList> g_lists;  // guest: (instance, list group) -> registered
static std::map<uint8_t, uint32_t> g_list_sent;                     // host: guest -> the hash it has
static Key g_list_captain;
static std::vector<uint8_t> g_list_bundle;
static uint32_t g_list_hash, g_list_mode;
static double g_list_next;
static unsigned g_list_out, g_list_in, g_list_own, g_list_refused, g_list_none, g_save_refused;
PLAYER_STATE("creations.captain_lists", [](uint8_t id) { g_list_sent.erase(id); },
             [](uint8_t id) -> size_t { return g_list_sent.count(id); });

// host: the captain's list as it goes out (bundle key = the creature, its one record = the list); false: none to send
static bool list_bundle(const Key& cap, std::vector<uint8_t>& out, uint32_t& hash)
{
    Key pk{cap.instance, captain::kPropType, captain::prop_group(cap.group)};
    std::vector<uint8_t> raw, clean;
    captain::PropScan s;
    if (!prop_from_game(pk, raw) || !captain::strip_prop(raw, captain::kPropNewer, clean, &s) || !s.items)
        return false;
    Bundle b;
    b.key = cap;
    b.recs.push_back({pk, std::move(clean)});
    out = pack(b);
    hash = fnv(out.data(), out.size());
    hash = hash ? hash : 1;
    return true;
}

static void lists_tick(double now)
{
    if (!g_captain_props_fix || !is_host())
        return;
    uint32_t m = game::mode_id();
    bool back = m == modes::kSpace && g_list_mode != modes::kSpace; // (an outfitter or an adventure may have changed it)
    g_list_mode = m;
    if (m != modes::kSpace)
        return;
    Key cap = empire_captain();
    if (!cap.instance || !captain::is_creature(cap.type, cap.group))
        return;
    if (back || !(cap == g_list_captain) || now >= g_list_next) {
        g_list_next = now + 30.0;
        g_list_captain = cap;
        g_list_bundle.clear();
        g_list_hash = 0;
        if (!list_bundle(cap, g_list_bundle, g_list_hash)) {
            g_list_none++;
            g_list_bundle.clear();
        }
    }
    if (g_list_bundle.empty())
        return;
    for (auto& p : C.sess->players()) {
        if (p.id == 0 || !peer_reads(p.id))
            continue;
        auto it = g_list_sent.find(p.id);
        if (it != g_list_sent.end() && it->second == g_list_hash)
            continue;
        g_list_sent[p.id] = g_list_hash;
        send_bundle(p.id, 0, (Kind)kKindHostProps, g_list_hash, g_list_bundle);
        g_list_out++;
        H->log(M, SK_INFO, "creations: the captain %08X's property list (%u bytes) to player %u (BUG-1279)", cap.instance,
               (unsigned)g_list_bundle.size(), p.id);
    }
}

static void take_list(const std::vector<uint8_t>& data)
{
    Bundle b;
    if (!g_captain_props_fix || is_host() || !find_paths() || !unpack(data, b) || b.recs.size() != 1 ||
        !captain::is_creature(b.key.type, b.key.group)) {
        g_list_refused++;
        return;
    }
    Key pk{b.key.instance, captain::kPropType, captain::prop_group(b.key.group)};
    dbpf::Record& r = b.recs[0];
    std::vector<uint8_t> clean;
    captain::PropScan s;
    if (!(r.key == pk) || r.data.size() > (256u << 10) || !captain::strip_prop(r.data, captain::kPropNewer, clean, &s)) {
        g_list_refused++;
        H->log(M, SK_WARN, "creations: refused a captain list for %08X (%u bytes, BUG-1279)", b.key.instance,
               (unsigned)r.data.size());
        return;
    }
    uint32_t h = fnv(clean.data(), clean.size());
    h = h ? h : 1;
    auto at = std::make_pair(pk.instance, pk.group);
    auto it = g_lists.find(at);
    bool reg = it != g_lists.end();
    auto act = captain::host_prop(s.items, h, reg, reg ? it->second.hash : 0, !reg && key_known(pk),
                                  !reg && whose(b.key) == 3);
    if (act == captain::HostProp::kSame)
        return;
    if (act == captain::HostProp::kSkipOwn) {
        g_list_own++;
        H->log(M, SK_INFO, "creations: the host captain %08X's list is not taken: this game has its own list or creation "
               "of that key (BUG-1279)", pk.instance);
        return;
    }
    if (act != captain::HostProp::kRegister) {
        g_list_refused++;
        return;
    }
    std::wstring path = g_dir + L"\\hostcap_" + std::to_wstring(pk.instance) + L"_" + std::to_wstring(h) + L".package";
    void* db = package_for(path, {{pk, std::move(clean)}}, kPrioHostList);
    if (!db)
        return;
    void* old = reg ? it->second.db : nullptr;
    g_lists[at] = HostList{h, db, pk};
    if (old && old != db)
        drop_package(old);
    prop_evict(pk);
    g_list_in++;
    H->log(M, SK_INFO, "creations: the host captain %08X's property list is registered here (%u properties, BUG-1279)",
           pk.instance, s.count);
}

// SporeCoop::CaptainProps_Save 0x5BF220 (verified 2026-11-23: cdecl bool(const ResourceKey* creation, PropertyList*,
// bool async, IResourceDatabase* db), plain ret; every caller but CaptainProps_Copy drops the result): see
// captain::refuse_props_save. A refused save returns false as the game's own "no database" path does.
using PropsSaveFn = bool(__cdecl*)(const uint32_t*, void*, uint32_t, void*);
static PropsSaveFn g_props_save_orig;
static bool __cdecl h_props_save(const uint32_t* c, void* props, uint32_t async, void* db)
{
    if (g_captain_props_fix && game::on_main() && c && skg::readable(c, 12)) {
        Key ck{c[0], c[1], c[2]};
        bool host_list = g_lists.count({c[0], captain::prop_group(c[2])}) != 0;
        int w = whose(ck);
        bool ours = captain::is_copy(c[0]) || w == 2;
        if (captain::refuse_props_save(true, guest_connected(), host_list, ours, host_list && w == 3)) {
            g_save_refused++;
            if (g_save_refused <= 8 || g_save_refused % 100 == 0)
                H->log(M, SK_INFO, "creations: the property list of %08X is not saved: %s (BUG-1279, %u)", c[0],
                       host_list ? "the host captain's list" : "a creation of ours", g_save_refused);
            return false;
        }
    }
    return g_props_save_orig(c, props, async, db);
}

// =============================================================== BUG-1262: the partners' adventure captains
// In an adventure each game plays its own captain: the galaxy menu's adventures copy the picked captain into a new
// creation of this profile (0x6462E0: a key from cIDGenerator, so the keys are per game and the same key can be two
// captains), the Space missions take the empire's captain. A partner's key names nothing here, so the puppet was this
// game's own captain (BUG-1254's stand-in). Each player in an adventure sends its captain (the model records from the
// game's package, its creaturedata, its property list without the newer-version link) as kind 11; every game registers
// a partner's under a key of its own (0x7A......, captain::copy_instance) and asks the game to bake its model; the
// puppet (modes.cpp scenario_spawn) is made from it only once it is baked (BUG-1321: never on a time-out), rebuilt in
// the frame. Sent only in an adventure (no captain traffic in Space), only to games of creations 1.2.
// `coop.fix adventure.partner_captain off`: nothing sent, received, relayed or registered (0.7.16.1: the stand-in).
bool g_partner_captain_fix = true;
COOP_FIX("adventure.partner_captain", "BUG-1262", g_partner_captain_fix,
         "adventure: a partner's puppet is the partner's own captain (its creation sent), not this game's captain");
struct Copy {
    uint32_t id = 0;
    Key key, orig;
    void* db = nullptr;
    std::string name;
    bool unusable = false;
    int bake = -2;
    bool shown = false;
};
static std::map<uint8_t, Copy> g_copies;                        // the partners' captains here
static std::map<uint8_t, std::vector<uint8_t>> g_cap_store;     // host: owner -> its captain's bundle
static Key g_cap_sent;
static uint32_t g_cap_sent_id;
static double g_cap_next, g_bake_at;
static unsigned g_cap_tries, g_cap_out, g_cap_in, g_cap_waits, g_cap_baked, g_cap_shown;
static bool g_bake_queued;
static uint32_t g_look_due; // owners (bits) whose puppet is rebuilt in the next tick
PLAYER_STATE("creations.captains",
             [](uint8_t id) {
                 if (id) {
                     g_cap_store.erase(id);
                     g_copies.erase(id);
                 }
             },
             [](uint8_t id) -> size_t { return id ? g_cap_store.count(id) + g_copies.count(id) : 0; });

static bool has_model(const Bundle& b)
{
    for (auto& r : b.recs)
        if (r.key.instance == b.key.instance && (r.key.type == kBem || r.key.type == b.key.type) && !r.data.empty())
            return true;
    return false;
}

static void forget_copy(uint8_t owner)
{
    auto it = g_copies.find(owner);
    if (it == g_copies.end())
        return;
    if (it->second.db)
        drop_package(it->second.db);
    g_copies.erase(it);
}

static void take_captain(uint8_t owner, uint32_t id, const std::vector<uint8_t>& data)
{
    if (!g_partner_captain_fix || (C.sess && owner == C.sess->my_id()))
        return;
    Bundle b;
    if (!unpack(data, b) || !is_crt(b.key) || !has_model(b) || !find_paths()) {
        H->log(M, SK_WARN, "creations: player %u's captain bundle is not usable (BUG-1262)", owner);
        return;
    }
    auto& cp = g_copies[owner];
    if (cp.id == id && (cp.key.instance || cp.unusable))
        return;
    std::string name;
    for (auto& r : b.recs)
        if (r.key.type == kMeta) {
            std::string author;
            dbpf::metadata_names(r.data, author, name);
        }
    unsigned miss = ensure_content(b.recs, kMkCreature, "player " + std::to_string(owner) + "'s captain '" + name + "'");
    void* old = cp.db;
    if (miss) {
        if (old)
            drop_package(old);
        cp = Copy{};
        cp.id = id, cp.orig = b.key, cp.name = name, cp.unusable = true;
        H->log(M, SK_WARN, "creations: player %u's captain '%s' has %u part(s) this game does not have: its puppet stays "
               "this game's captain (BUG-1262)", owner, name.c_str(), miss);
        return;
    }
    const uint32_t inst = captain::copy_instance(owner, id);
    std::vector<dbpf::Record> recs;
    for (auto& r : b.recs) {
        if (r.key.instance != b.key.instance || r.key.group == kPartDescGroup)
            continue;
        dbpf::Record nr = r;
        nr.key.instance = inst;
        if (nr.key.type == captain::kPropType) { // the captain's list under the copy's key (CaptainProps_Get readers)
            std::vector<uint8_t> clean;
            if (!captain::is_prop_key(nr.key.type, nr.key.group) || !captain::strip_prop(r.data, captain::kPropNewer, clean))
                continue;
            nr.data.swap(clean);
        }
        uint8_t* d = nr.data.data();
        if (nr.key.type == kMeta && nr.data.size() >= 0x18 &&
            ((uint32_t)d[0x14] << 24 | (uint32_t)d[0x15] << 16 | (uint32_t)d[0x16] << 8 | d[0x17]) == b.key.instance)
            for (int i = 0; i < 4; i++)
                d[0x14 + i] = (uint8_t)(inst >> (24 - 8 * i)); // (its own key in the metadata)
        recs.push_back(std::move(nr));
    }
    std::wstring path = g_dir + L"\\cap_" + std::to_wstring(owner) + L"_" + std::to_wstring(id) + L".package";
    void* db = package_for(path, recs, 1000);
    if (!db)
        return;
    if (old && old != db)
        drop_package(old);
    cp = Copy{};
    cp.id = id, cp.key = {inst, b.key.type, b.key.group}, cp.orig = b.key, cp.db = db, cp.name = name;
    g_cap_in++;
    H->log(M, SK_INFO, "creations: player %u's captain '%s' (%08X there) is %08X here (%u records, BUG-1262)", owner,
           name.c_str(), b.key.instance, inst, (unsigned)recs.size());
}

// outside the frame (as civ_species asks): the game's bake of each partner captain not shown yet
static void bake_task(void*)
{
    g_bake_queued = false;
    if (!g_partner_captain_fix || game::mode_id() != modes::kScenario)
        return;
    for (auto& [owner, cp] : g_copies) {
        if (cp.unusable || !cp.key.instance || cp.bake == 1 || !key_known(cp.key))
            continue;
        cp.bake = game::model_baked({cp.key.instance, cp.key.type, cp.key.group}, true);
        if (cp.bake == 1) {
            g_cap_baked++;
            g_look_due |= 1u << (owner & 31);
            H->log(M, SK_INFO, "creations: player %u's captain %08X is baked here (BUG-1262)", owner, cp.key.instance);
        }
    }
}

static void send_captain(double now)
{
    if (!g_partner_captain_fix || game::mode_id() != modes::kScenario || now < g_cap_next)
        return;
    bool host = is_host();
    if (!host && !host_reads())
        return;
    void* av = game::avatar();
    game::Key gk = av ? game::species_key(av) : game::Key{0, 0, 0};
    Key ck{gk.instance, gk.type, gk.group};
    if (!ck.instance || !is_crt(ck) || (ck == g_cap_sent && g_cap_sent_id))
        return;
    Bundle b;
    b.key = ck;
    std::vector<dbpf::Record> cd;
    std::vector<uint8_t> list;
    if (!records_from_game(ck, b.recs) || !has_model(b)) { // (a new captain: its package is written a moment later)
        g_cap_waits++;
        g_cap_next = now + captain::captain_retry(g_cap_tries++);
        b.recs.clear();
        return;
    }
    if (records_from_game({ck.instance, kCreatureData, ck.group}, cd))
        for (auto& r : cd)
            if (r.key.type == kCreatureData)
                b.recs.push_back(std::move(r));
    Key pk{ck.instance, captain::kPropType, captain::prop_group(ck.group)};
    if (prop_from_game(pk, list))
        b.recs.push_back({pk, std::move(list)});
    std::vector<uint8_t> data = pack(b);
    uint32_t id = fnv(data.data(), data.size());
    id = id ? id : 1;
    g_cap_sent = ck, g_cap_sent_id = id, g_cap_tries = 0;
    uint8_t me = C.sess->my_id();
    if (host) {
        g_cap_store[me] = data;
        for (auto& p : C.sess->players())
            if (p.id != 0 && peer_reads(p.id))
                send_bundle(p.id, me, (Kind)kKindCaptain, id, data);
    } else
        send_bundle(0, me, (Kind)kKindCaptain, id, data);
    g_cap_out++;
    H->log(M, SK_INFO, "creations: our captain %08X goes to the partners (%u records, %u bytes, BUG-1262)", ck.instance,
           (unsigned)b.recs.size(), (unsigned)data.size());
}

// ---- the module's entry points (creations.cpp) ----
void captain_message(uint8_t kind, uint8_t owner, uint32_t id, const std::vector<uint8_t>& data)
{
    if (kind == kKindHostProps) {
        take_list(data);
        return;
    }
    if (kind != kKindCaptain || !g_partner_captain_fix)
        return;
    if (is_host()) {
        g_cap_store[owner] = data;
        for (auto& p : C.sess->players())
            if (p.id != 0 && p.id != owner && peer_reads(p.id))
                send_bundle(p.id, owner, (Kind)kKindCaptain, id, data);
    }
    take_captain(owner, id, data);
}
bool captain_kind_on(uint8_t kind)
{
    return (kind == kKindHostProps && g_captain_props_fix) || (kind == kKindCaptain && g_partner_captain_fix);
}

void captain_tick(double now)
{
    lists_tick(now);
    send_captain(now);
    if (game::mode_id() != modes::kScenario || !g_partner_captain_fix)
        return;
    if (g_look_due && C.appearance_changed) { // (in the frame: the puppet is made again by the adapter, BUG-1254)
        for (auto& [owner, cp] : g_copies)
            if (g_look_due & (1u << (owner & 31)) && cp.bake == 1 && !cp.shown) {
                cp.shown = true;
                g_cap_shown++;
                C.appearance_changed(owner);
            }
        g_look_due = 0;
    }
    if (g_bake_queued || now - g_bake_at < captain::kAskEvery)
        return;
    for (auto& [owner, cp] : g_copies)
        if (!cp.unusable && cp.key.instance && cp.bake != 1) {
            g_bake_at = now;
            g_bake_queued = true;
            H->run_outside_frame(bake_task, nullptr);
            return;
        }
}

void captain_list_recheck() { g_list_next = 0; }

void captain_join(uint8_t id)
{
    g_list_sent.erase(id);
    if (!g_partner_captain_fix || !peer_reads(id))
        return;
    for (auto& [owner, data] : g_cap_store)
        if (owner != id)
            send_bundle(id, owner, (Kind)kKindCaptain, fnv(data.data(), data.size()) ? fnv(data.data(), data.size()) : 1,
                        data);
}

void captain_leave(uint8_t id)
{
    g_list_sent.erase(id);
    g_cap_store.erase(id);
    forget_copy(id);
}

void captain_end()
{
    for (auto& [k, l] : g_lists) {
        if (l.db)
            drop_package(l.db);
        prop_evict(l.prop);
    }
    g_lists.clear();
    g_list_sent.clear();
    g_list_captain = {};
    g_list_bundle.clear();
    g_list_hash = g_list_mode = 0;
    g_list_next = 0;
    for (auto& [owner, cp] : g_copies)
        if (cp.db)
            drop_package(cp.db);
    g_copies.clear();
    g_cap_store.clear();
    g_cap_sent = {};
    g_cap_sent_id = 0;
    g_cap_next = g_bake_at = 0;
    g_cap_tries = 0;
    g_look_due = 0;
}

void captain_init()
{
    a_Empire = H->sym("Simulator::GetPlayerEmpire");
    a_PropsSave = H->sym("SporeCoop::CaptainProps_Save");
    if (a_PropsSave && !g_props_save_orig &&
        H->hook_create(M, (void*)a_PropsSave, (void*)&h_props_save, (void**)&g_props_save_orig) != 0)
        g_props_save_orig = nullptr;
    H->log(M, SK_INFO, "creations: the captain property list save guard is %s (BUG-1279)", g_props_save_orig ? "on" : "off");
}

std::string captain_status()
{
    char b[512];
    std::string s;
    snprintf(b, sizeof b,
             "  captain lists: fix=%d out=%u in=%u own=%u refused=%u none=%u save_refused=%u here=%zu (BUG-1279)\n"
             "  captains: fix=%d out=%u in=%u waits=%u baked=%u shown=%u (BUG-1262)\n",
             (int)g_captain_props_fix, g_list_out, g_list_in, g_list_own, g_list_refused, g_list_none, g_save_refused,
             g_lists.size(), (int)g_partner_captain_fix, g_cap_out, g_cap_in, g_cap_waits, g_cap_baked, g_cap_shown);
    s += b;
    for (auto& [k, l] : g_lists) {
        snprintf(b, sizeof b, "  host captain list %08X/%08X/%08X\n", l.prop.instance, l.prop.type, l.prop.group);
        s += b;
    }
    for (auto& [pid, cp] : g_copies) {
        snprintf(b, sizeof b, "  player %u captain: %08X (orig %08X) '%s' bake=%d shown=%d%s\n", pid, cp.key.instance,
                 cp.orig.instance, cp.name.c_str(), cp.bake, (int)cp.shown, cp.unusable ? " unusable" : "");
        s += b;
    }
    return s;
}

Key player_captain_key(uint8_t player)
{
    auto it = g_copies.find(player);
    if (it == g_copies.end() || it->second.unusable ||
        !captain::puppet_shows_copy(g_partner_captain_fix, it->second.key.instance != 0, it->second.bake))
        return {};
    return it->second.key;
}
void* player_captain_species(uint8_t player)
{
    Key k = player_captain_key(player);
    return k.instance ? game::species_by_key({k.instance, k.type, k.group}) : nullptr;
}

} // namespace creations
