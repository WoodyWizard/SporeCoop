#include "creations_internal.h"
#include "founders.h"

namespace creations {

using skg::cdecl_call;
using skg::thiscall;

const SkHost* H;
static SkMod* M;
Ctx C;

const KindInfo kInfo[kKinds] = {
    {"creature", 0x2B978C46, 0x40626200}, // crt
    {"cell", 0x3D97A8E4, 0x40616200},     // cll
    {"ufo", 0x476A98C7, 0x40656200},      // ufo
};
constexpr uint32_t kEditorSavesArea = 0x11AC199;
constexpr int kPrioPlayer = 1000, kPrioWorld = 2000; // EditorSaves is 1000; the list is searched highest first
// MSG_CREATION kinds after the player creations (older versions drop kind >= kKinds): a species of
// the host's world for a guest that lacks it (owner 0, id = the crt instance), and a guest's request
// and a species of the host's world that is one of the host's own creations (owner 0, id = the crt
// instance): the guest uses it under a key of its own (see creations.h)
constexpr uint8_t kWorldSpecies = 3, kAskSpecies = 4, kOwnSpecies = 5;
// BUG-0647: a design of the host's own My Creations (a civ vehicle): the guest asks (id = the instance,
// total = the model kind), the host sends the records (kOwnDesign) or says it is not one of its own
// (kDesignNotOwn: a premade, the same everywhere)
constexpr uint8_t kAskDesign = 6, kOwnDesign = 7, kDesignNotOwn = 8;
// BUG-0017: a guest registers the host's own creations of its world (nest species)
// BUG-0010: a guest asks the host for a species of its world it does not have

// BUG-0278: a guest's own editor (the vote's, docs/re/creations.md#creations-own-editor-save) saves
// its creature into My Creations but leaves the avatar on the world's species: the host's creature
// from before the host's editor (the legless one at the start of the Creature stage). The return
// from that editor took the avatar's key as the guest's own creature (choices.ini), and "Save New"
// keeps the edited creation's key, the host's: the host's package answers for it (priority 2000).
// Either way the guest played the host's legless creature from then on, over every world the host
// sent, and its next editor started from it again. Now only an own creation is taken over, a key
// the host's world owns is never the own one (a pinned one is ignored, an own creation under such
// a key is used under its shared remapped key), and the co-op window offers "the world's own".

uintptr_t a_GetDb, a_OpNew, a_DbCtor, a_RmPtr, a_SetSpecies, a_GetEmpire, a_CellPreload;

// ---- own creations (My Creations = EditorSaves.package) ----
std::wstring g_es_path, g_dir; // EditorSaves.package and our folder next to it
static std::vector<Option> g_opts[kKinds];
static dbpf::Package g_es;  // My Creations, read every few seconds
static bool g_es_ok;
static double g_opts_read = -100;
Key g_choice[kKinds];  // explicit choice ({} = default)
// BUG-0344: on the host a creature or cell choice holds only in the stage world it was made in (a
// choice from choices.ini of another world swapped the host's just evolved species for an old
// creature, and the shared save then named it: BUG-0341), and an editor's result is the host's own
constexpr uint32_t kNoEpoch = 0xFFFFFFFFu;
static uint32_t g_choice_epoch[kKinds] = {kNoEpoch, kNoEpoch, kNoEpoch}; // stage world of the choice (host)
static uint32_t choice_epoch_now(int k)
{
    return game::stage_epoch(k == 1 ? modes::kCell : modes::kCreature);
}
bool g_chosen[kKinds]; // the player chose (even "none")
// BUG-0345: a guest without an explicit choice played the newest creation of its My Creations in any
// world, also in a new world of the host's ("the guest spawns with its previous cell creature"). Now a
// guest's default is its own creation only in a world it already played it in (SporeCoop\worlds.ini:
// [creature] / [cell] <world crc> = <creation>; an own editor's result is recorded there, not as a
// choice); else the world's species, as the host since BUG-0344. A choice in the co-op window holds
// everywhere. Off: the newest own creation in any world (0.7.4).
bool g_guest_world_fix = true;
COOP_FIX("creations.guest_world_species", "BUG-0345", g_guest_world_fix,
         "creations: a guest plays its own creation by default only in a world it played it in (or chose it); else the world's species");
// BUG-0638: a guest's explicit choice (choices.ini) held in every world: a guest that missed the host's
// Cell -> Creature editor (it was in its own Cell editor) played a creature it had chosen in another
// world two days before. Now a guest's chosen creation holds only in a world it is recorded for
// (worlds.ini: the choice records the world it is made in, as playing it does); in another world the
// guest plays its default (this world's record, the host's evolved species, the world's species). An
// explicit "the world's own" still holds everywhere. Off: a choice holds in every world (0.7.9).
static bool g_choice_world_fix = true;
COOP_FIX("creations.choice_world", "BUG-0638", g_choice_world_fix,
         "creations: a guest's chosen own creation holds only in a world it chose or played it in");
static std::map<std::pair<int, uint32_t>, uint32_t> g_world_rec; // (kind, world) -> creation instance (cache)
static std::set<std::pair<int, uint32_t>> g_world_read;           // read from the file already
std::set<std::tuple<int, uint32_t, uint32_t>> g_own_told;  // notices given (kind, world, creation)
unsigned g_world_records;
bool find_paths();
std::wstring wide(const char* s) { return std::wstring(s, s + strlen(s)); }
uint32_t world_record(int k, uint32_t world)
{
    if (!world || !find_paths())
        return 0;
    auto id = std::make_pair(k, world);
    if (!g_world_read.count(id)) {
        g_world_read.insert(id);
        wchar_t key[16], buf[32] = {};
        swprintf(key, 16, L"%08X", world);
        GetPrivateProfileStringW(wide(kInfo[k].name).c_str(), key, L"", buf, 32, (g_dir + L"\\worlds.ini").c_str());
        if (buf[0])
            g_world_rec[id] = (uint32_t)wcstoul(buf, nullptr, 16);
    }
    auto it = g_world_rec.find(id);
    return it == g_world_rec.end() ? 0 : it->second;
}
void world_record_set(int k, uint32_t world, uint32_t inst)
{
    if (!world || !find_paths() || world_record(k, world) == inst)
        return;
    g_world_rec[{k, world}] = inst;
    wchar_t key[16], val[16];
    swprintf(key, 16, L"%08X", world);
    swprintf(val, 16, L"%08X", inst);
    WritePrivateProfileStringW(wide(kInfo[k].name).c_str(), key, inst ? val : nullptr, (g_dir + L"\\worlds.ini").c_str());
    g_world_records++;
    H->log(M, SK_INFO, "creations: own %s %08X recorded for world %08X (BUG-0345)", kInfo[k].name, inst, world);
}
// BUG-9262 (Lineages): a free note of the mod per planet in worlds.ini ([section] <world crc> = value; ASCII)
std::string world_note(const char* section, uint32_t world)
{
    if (!world || !section || !find_paths())
        return {};
    wchar_t key[16], buf[1024] = {};
    swprintf(key, 16, L"%08X", world);
    GetPrivateProfileStringW(wide(section).c_str(), key, L"", buf, 1024, (g_dir + L"\\worlds.ini").c_str());
    std::string out;
    for (const wchar_t* p = buf; *p; ++p)
        out += (char)(*p < 128 ? *p : '?');
    return out;
}
void world_note_set(const char* section, uint32_t world, const std::string& value)
{
    if (!world || !section || !find_paths())
        return;
    wchar_t key[16];
    swprintf(key, 16, L"%08X", world);
    const std::wstring v(value.begin(), value.end());
    WritePrivateProfileStringW(wide(section).c_str(), key, value.empty() ? nullptr : v.c_str(),
                               (g_dir + L"\\worlds.ini").c_str());
}
// BUG-0508: a guest that plays the host world's species (no creation of its
// own, no choice) takes the host's new creature in place after the host's editor, as the reloaded
// world gave it before: `g_follow` is the host's new creation, valid in the stage world it came in
// (a load gives the avatar the world's species again). g_seen_own: the own key last seen in a stage
// (in the vote's editor the avatar is not there when the host's bundle comes).
// The host's side (no world after the editor) is BUG-0508 (coop.cpp). Off: the guest's
// avatar keeps the species it had until a world load (0.7.3.6).
static bool g_follow_fix = true;
COOP_FIX("creations.follow_evolution", "BUG-0508", g_follow_fix,
         "creations: a guest that plays the world's species takes the host's evolved one in place");
static Key g_follow[kKinds], g_seen_own[kKinds];
static uint32_t g_follow_epoch[kKinds];
static unsigned g_follows;
// The world's player species (cSpeciesManager +0xA4, a ResourceKey read by PlayerSpecies 0x4DF490 on
// every call): a reloaded world had the host's evolved species there. In place the guest's game gets
// it too: from the host's previous creature to the new one (g_ps_from -> g_ps_to), written in a stage
// frame (not in the vote's editor). Every guest, also one playing its own creature (its world's player
// species is the host's then as well).
static uintptr_t a_SpeciesMgr;
static Key g_ps_from, g_ps_to;
static unsigned g_ps_set;
static uint32_t* player_species_slot()
{
    void* mgr = a_SpeciesMgr ? cdecl_call<void*>(a_SpeciesMgr) : nullptr;
    auto* k = mgr ? (uint32_t*)((uint8_t*)mgr + 0xA4) : nullptr;
    return k && skg::readable(k, 12) ? k : nullptr;
}
// BUG-1057: the request was cleared before the species was looked up: a species this game did not know yet in that
// frame (species_by_key null) lost the write for good and the world kept the host's previous species. Now the
// request stays while the species is unknown, for kPsRetry s. `coop.fix creations.species_retry off`: one try (0.7.13)
static bool g_ps_retry_fix = true;
COOP_FIX("creations.species_retry", "BUG-1057", g_ps_retry_fix, "creations: the world's player species follows the host's evolved one also when that species is known a few frames later");
constexpr double kPsRetry = 10.0;
static double g_ps_since;
unsigned g_ps_retries, g_ps_lost, g_ps_test_null; // (tests: frames species_by_key is taken as null)
static void player_species_tick()
{
    if (!g_ps_to.instance || game::mode_id() != modes::kCreature || !game::avatar())
        return;
    uint32_t* k = player_species_slot();
    Key to = g_ps_to;
    if (!k || k[0] != g_ps_from.instance || k[1] != to.type || k[2] != to.group) {
        g_ps_to = {};
        return; // a load or another species since
    }
    bool known = game::species_by_key({to.instance, to.type, to.group}) != nullptr;
    if (g_ps_test_null) { // tests (BUG-1057): the species is not known to this game yet
        g_ps_test_null--;
        known = false;
    }
    if (!known) {
        double now = H->time_seconds();
        if (!g_ps_since)
            g_ps_since = now;
        if (g_ps_retry_fix && now - g_ps_since < kPsRetry) {
            g_ps_retries++;
            return; // (kept: tried again next frame)
        }
        g_ps_to = {};
        g_ps_since = 0;
        g_ps_lost++;
        H->log(M, SK_WARN, "creations: the host's species %08X is not known here: the world's player species stays %08X (BUG-1057)",
               to.instance, g_ps_from.instance);
        return;
    }
    g_ps_to = {};
    g_ps_since = 0;
    k[0] = to.instance;
    g_ps_set++;
    H->log(M, SK_INFO, "creations: the world's player species %08X -> %08X (BUG-0508)", g_ps_from.instance, to.instance);
}

// ---- bundles: a creation with its records ----
static std::map<std::pair<uint8_t, uint8_t>, std::vector<uint8_t>> g_store; // host: (owner, kind) -> bundle
static uint32_t g_sent_id[kKinds];                                             // what we sent of our own
double g_last_check;
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
std::map<uint8_t, std::set<uint32_t>> g_own_sent; // host: guest -> crt instances checked / sent
static std::map<uint32_t, uint32_t> g_remap, g_unmap;   // guest: host instance -> ours, and back
static unsigned g_own_out, g_own_in;

// ---- helpers ----
uint32_t fnv(const void* p, size_t n, uint32_t h)
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

std::string hex(uint32_t v)
{
    char b[16];
    snprintf(b, sizeof b, "%08X", v);
    return b;
}

// where My Creations live: the game's EditorSaves database location
bool find_paths()
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

// BUG-9258 (Lineages): this profile's SporeCoop folder next to EditorSaves.package, "" while not known
std::wstring coop_dir() { return find_paths() ? g_dir : std::wstring(); }

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

bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }

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

bool key_known(const Key& k) { return key_exists(k); }

// ---- BUG-0341: creatures with parts of a pack this game does not have ----
// A Steam player without the "Creepy & Cute" parts pack crashed at 0x441F3B (a model bake with a
// null property list) on every load of a host world whose creature had one of its parts, and on the
// host's puppet built from it. Such a creation is not registered here (its puppet falls back as for
// an unknown creation), and a world that needs one is not loaded (world.cpp asks
// world_missing_parts): the player gets a message instead of a crash.
// BUG-0452: the same for every part id of every creature, cell and UFO model that reaches this game,
// not only the hard-coded Creepy & Cute list. A species of the host's world with a part of another
// parts pack (or of a mod) crashed a guest at the same 0x41F3B: the bake found no property list for
// the id. Each such id is logged; a part this game lacks is shown as a base game part (Creepy &
// Cute's table first) or, when no substitute of the right length is known, the creation is refused
// and the game's fallback shows instead.
// BUG-0469: the same check for every model the mod makes this game build from a key it did not make
// itself (a civ mirror vehicle, a building of a city, a host ship): the key AND every part / paint it
// names, with a substitute in a copy of the model's records or a refusal (docs/re/creations.md
// #creations-content-all-kinds). Off (`coop.fix content.all_kinds off`): 0.7.3.5, these call sites
// pass the key as they get it.
static bool g_all_kinds_fix = true;
COOP_FIX("content.all_kinds", "BUG-0469", g_all_kinds_fix, "civ, space: every model the mod makes the game build (a mirror vehicle, a building, a host ship) is checked with its parts and paints, not only its key");
constexpr uint32_t kCellPartGroup = 0x40616000, kUfoPartGroup = 0x40646000; // parts of a cell / UFO model
constexpr uint32_t kEffectProp = 0x497767B9, kEffectGroup = 0xEA5118B1; // a base game skin effect
// a part entry's slot and parent are small indexes (-1: none); an entry whose fields are not is not
// a part reference (the group bytes occur in other data too: a base game model has such entries, and
// the fixture Kirgle names 05D58995 that no package holds and builds fine, docs/re/creations.md)
constexpr uint32_t kMaxPartSlot = 0x10000;
static int g_have_cc = -1; // this game has the Creepy & Cute parts (-1: not asked yet)
unsigned g_parts_refused, g_parts_fallback, g_paints_fallback, g_unknown_logged;
static std::map<uint32_t, std::string> g_world_missing; // the host's world creatures this game cannot build
// key_exists is an RM lookup: the packs are mounted for the session, so an id's answer does not change
std::map<std::pair<uint32_t, uint32_t>, bool> g_key_cache; // part id + group -> this game has it
static std::map<uint32_t, bool> g_effect_cache;                  // paint effect id -> this game has it

const ModelInfo kModelInfo[kMkKinds] = {
    {kMkCreature, "creature", 0x2B978C46, 0x40626200, kPartGroup, true},
    {kMkCell, "cell", 0x3D97A8E4, 0x40616200, kCellPartGroup, false},
    {kMkUfo, "ufo", 0x476A98C7, 0x40656200, kUfoPartGroup, true},
    {kMkVehicle, "vehicle", 0x24682294, 0x40646200, kUfoPartGroup, true},
    {kMkBuilding, "building", 0x2399BE55, 0x40636200, 0x40636000, true},
};

static ModelKind mk_of(Kind k) { return k == kCell ? kMkCell : k == kUfo ? kMkUfo : kMkCreature; }

// the part property group of a model kind
uint32_t part_group(ModelKind k) { return kModelInfo[k].parts; }
// whether the kind's model header names the three paint effect dwords (creature and the kinds whose
// models are built from the same bem template)
static bool kind_paints(ModelKind k) { return kModelInfo[k].paints; }

// ---- part ids of a model (all kinds: the same group+instance key layout) ----

// decodes n bytes of the model's byte-run stream from `j` (0xFE n: the byte before, n more times;
// FE 00: the byte 0xFE); `last` is the byte before j
static bool decode_run(const std::vector<uint8_t>& m, size_t& j, uint8_t& last, uint8_t* out, int n)
{
    int got = 0;
    while (got < n) {
        if (j >= m.size())
            return false;
        uint8_t b = m[j];
        if (b != 0xFE) {
            out[got++] = b;
            last = b;
            j++;
            continue;
        }
        if (j + 1 >= m.size())
            return false;
        unsigned run = m[j + 1];
        if (run == 0) {
            out[got++] = 0xFE;
            last = 0xFE;
            j += 2;
            continue;
        }
        j += 2;
        for (unsigned r = 0; r < run && got < n; r++)
            out[got++] = last;
    }
    return true;
}

// the model's part references of `group`. A part key is written as its group (little endian) and
// the instance right after it; byte runs are packed (0xFE n = the byte before, n more times; a 0xFE
// byte is written 0xFE 0x00) and an id with a run inside it is skipped. Only entries whose slot and
// parent are plausible count: docs/re/creations.md#creations-unknown-parts
// The group's least significant byte (0x00 for the part groups) is often encoded as a run (the byte
// before, repeated), so the scan matches the group's last three bytes and reads the id after them:
// a model has 33 parts, and looking for the literal 4 bytes finds only the one whose 0x00 is literal.
std::vector<PartRef> model_part_refs(const std::vector<uint8_t>& m, uint32_t group)
{
    std::vector<PartRef> out;
    uint8_t g[3] = {(uint8_t)(group >> 8), (uint8_t)(group >> 16), (uint8_t)(group >> 24)};
    for (size_t i = 0; i + 15 <= m.size(); i++) {
        if (m[i] != g[0] || m[i + 1] != g[1] || m[i + 2] != g[2])
            continue;
        uint8_t id[4];
        int got = 0;
        size_t j = i + 3;
        bool ok = true;
        while (got < 4 && j < m.size()) {
            if (m[j] == 0xFE) {
                if (j + 1 >= m.size() || m[j + 1] != 0) { // a run inside the id: not a part id we can read
                    ok = false;
                    break;
                }
                id[got++] = 0xFE;
                j += 2;
            } else
                id[got++] = m[j++];
        }
        if (!ok || got != 4)
            continue;
        uint32_t v = id[0] | id[1] << 8 | id[2] << 16 | (uint32_t)id[3] << 24;
        size_t at = i + 3, len = j - at;
        // the slot and parent follow the id in the same byte-run stream
        uint8_t f[8];
        uint8_t last = id[3];
        bool safe = !(j + 1 < m.size() && m[j] == 0xFE && m[j + 1] != 0); // a run repeats the id's last byte
        if (!v || !decode_run(m, j, last, f, 8))
            continue;
        uint32_t slot = f[0] | f[1] << 8 | f[2] << 16 | (uint32_t)f[3] << 24;
        uint32_t parent = f[4] | f[5] << 8 | f[6] << 16 | (uint32_t)f[7] << 24;
        if (!(slot == 0xFFFFFFFFu || slot < kMaxPartSlot) || !(parent == 0xFFFFFFFFu || parent < kMaxPartSlot))
            continue; // not a part entry (the group bytes occur in other data)
        out.push_back({v, at, len, safe});
    }
    return out;
}

bool have_creepy_cute()
{
    if (g_have_cc < 0 && a_RmPtr && *(void**)a_RmPtr) {
        g_have_cc = key_exists({kCreepyCuteParts[0], kPartProp, kPartGroup}) ? 1 : 0;
        H->log(M, SK_INFO, "creations: content: Creepy & Cute parts pack %s", g_have_cc ? "present" : "NOT present");
    }
    return g_have_cc != 0;
}

// the part ids a creature model (bem record) names, for the tests and the support trace
std::vector<uint32_t> model_parts(const std::vector<uint8_t>& m)
{
    std::vector<uint32_t> out;
    for (auto& ref : model_part_refs(m, kPartGroup))
        out.push_back(ref.id);
    return out;
}

bool is_cc_part(uint32_t id)
{
    return std::binary_search(std::begin(kCreepyCuteParts), std::end(kCreepyCuteParts), id);
}

// whether this game has the part (cached: key_exists is an RM lookup)
// tests (K2b, BUG-0725 / BUG-0726 / BUG-0728): `coop.creations lack <hex id>` makes the mod treat a part this game has
// as one it lacks (the checks, the substitutes and the lookalike search), so one lab lane can play a guest without a
// pack: both games of a lane share one install. Never set outside the tests.
std::set<uint32_t> g_lacked;
bool g_objmodels_scan; // tests: objmodels scan
bool part_known(uint32_t id, uint32_t group)
{
    if (!g_lacked.empty() && g_lacked.count(id))
        return false;
    auto k = std::make_pair(id, group);
    auto it = g_key_cache.find(k);
    if (it != g_key_cache.end())
        return it->second;
    bool have = key_exists({id, kPartProp, group});
    g_key_cache[k] = have;
    return have;
}

// the base game part written in place of a part this game does not have and whose kind is not known
// here (content_packs.h kBasePartFallbacks), for an id of `fe` 0xFE bytes; 0: none
static uint32_t part_fallback(ModelKind k, int fe)
{
    uint32_t group = part_group(k);
    for (auto& f : kBasePartFallbacks)
        if (f.kind == (uint8_t)k && f.fe == fe && part_known(f.base, group))
            return f.base;
    return 0;
}

// ---- BUG-0559: a part this game lacks shown as the most similar part it has ----
// The fixed tables above were made from one install (GOG 2024) for one pack, and a part of another pack
// became one fixed detail whatever it was (a mouth, a foot). Now the sending game adds the description
// of each part its creation uses (the part's own property list: role, abilities, cost, size; the group
// kPartDescGroup, which no game resource has) and the receiving game picks, among the parts IT has
// (every part prop of the packages registered with its resource manager), the one of the same role
// with the nearest abilities and size (net/partprops). docs/re/creations.md#creations-lookalike.
constexpr uint32_t kPartDescGroup = 0x7C0DE5C1; // a part's description in a bundle (never registered)
constexpr size_t kMaxDescBytes = 256u << 10;      // per bundle
// BUG-0725 (class BUG-0424): the lookalike above ran only for the bundles of a player's creature, cell and UFO: the
// host's vehicle, building and ship designs (BUG-0647 / BUG-0670 / BUG-0724) went out without the descriptions of
// their parts (add_part_descs knew the three creation types only), the guest dropped what came (apply_own_design),
// and ensure_model substituted without any: a part the guest lacked became one fixed base game part of that kind
// (kBasePartFallbacks), whatever it was. Now every bundle carries its parts' descriptions, every received
// description is kept for the session (g_rx_descs, by part id) and ensure_model, the one check of every model the
// mod makes this game build, picks the most similar part from them. `coop.fix content.design_lookalike off`: the
// fixed fallback for the designs (0.7.10).
static bool g_design_descs_fix = true;
COOP_FIX("content.design_lookalike", "BUG-0725", g_design_descs_fix, "creations: a part a host's vehicle, building or ship design names and this game lacks is shown as the most similar part this game has (the design's part descriptions go with it), not as one fixed part");
std::map<uint32_t, std::vector<uint8_t>> g_rx_descs; // part id -> its sender's description (this session)
// BUG-0726: a model with a part this game lacks was refused for a caller that cannot pass another key to the game
// (a building: cCity::SetBuildingModel reads the civ's design key itself) and the host's building was left out of
// the guest's city. Since BUG-0469 (review) the copy with the substitutes answers the model's OWN key (above every
// package of the game), so such a caller builds the copy too. `coop.fix content.building_copy off`: refused (0.7.10).
static bool g_building_copy_fix = true;
COOP_FIX("content.building_copy", "BUG-0726", g_building_copy_fix, "creations: a building whose design names a part this game lacks is built from the copy with substitutes (under the design's own key), not left out");

// the part prop entries (type kPartProp, the 4 part groups) of one package file of this game, read once
struct PartFile {
    std::wstring path;
    std::vector<dbpf::Entry> parts;
    bool ok = false;
};
static std::map<std::wstring, PartFile> g_part_files;
static std::map<uint32_t, std::vector<partprops::Part>> g_catalogue; // part group -> the parts this game has
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_lookalike; // (group, id) -> chosen (0: none)
static unsigned g_lookalikes, g_desc_out, g_desc_in;
static double g_catalogue_ms;

static bool is_part_group(uint32_t g)
{
    return g == kPartGroup || g == kCellPartGroup || g == kUfoPartGroup || g == 0x40636000;
}

static bool read_file_at(HANDLE f, uint64_t off, uint32_t n, std::vector<uint8_t>& buf)
{
    buf.resize(n);
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    DWORD rd = 0;
    return SetFilePointerEx(f, li, nullptr, FILE_BEGIN) && (n == 0 || (ReadFile(f, buf.data(), n, &rd, nullptr) && rd == n));
}

static const PartFile& part_file(const std::wstring& path)
{
    auto it = g_part_files.find(path);
    if (it != g_part_files.end())
        return it->second;
    PartFile& pf = g_part_files[path];
    pf.path = path;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return pf;
    std::vector<uint8_t> hdr, idx;
    dbpf::IndexInfo info;
    std::vector<dbpf::Entry> all;
    pf.ok = read_file_at(f, 0, 96, hdr) && dbpf::index_info(hdr.data(), hdr.size(), info) && info.size < (64u << 20) &&
            read_file_at(f, info.offset, info.size, idx) && dbpf::parse_index(idx.data(), idx.size(), info, all);
    CloseHandle(f);
    for (auto& e : all)
        if (e.key.type == kPartProp && is_part_group(e.key.group) && e.size <= (1u << 20))
            pf.parts.push_back(e);
    return pf;
}

static bool read_entry(const std::wstring& path, const dbpf::Entry& e, std::vector<uint8_t>& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    std::vector<uint8_t> raw;
    bool ok = read_file_at(f, e.offset, e.size, raw) && dbpf::decode(e, raw.data(), out);
    CloseHandle(f);
    return ok;
}

bool file_location(void* db, std::wstring& out)
{
    const wchar_t* loc = db ? vcall<const wchar_t*>(db, 0x28 / 4) : nullptr; // GetLocation
    if (!loc || IsBadReadPtr(loc, 8) || !(loc[1] == L':' || loc[0] == L'\\'))
        return false; // not a package file (a folder database)
    out = loc;
    return true;
}

// this game's property list of a part (the package that answers for it, as the game reads it)
bool part_prop_bytes(uint32_t id, uint32_t group, std::vector<uint8_t>& out)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    uint32_t key[3] = {id, kPartProp, group}, real[3] = {};
    void* db = rm ? vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) : nullptr; // FindRecord
    std::wstring path;
    if (!file_location(db, path))
        return false;
    for (auto& e : part_file(path).parts)
        if (e.key.instance == id && e.key.group == group)
            return read_entry(path, e, out);
    return false;
}

// every part of `group` this game has: the part props of the packages the resource manager searches
// (its list RM+0x2C, highest priority first, as FindDatabase walks it: node {next, Database*}), the
// first package with an id answers for it. Built once per session (the packages do not change).
static const std::vector<partprops::Part>& catalogue(uint32_t group)
{
    auto it = g_catalogue.find(group);
    if (it != g_catalogue.end())
        return it->second;
    std::vector<partprops::Part>& out = g_catalogue[group];
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    if (!rm)
        return out;
    LARGE_INTEGER f0, f1, fq;
    QueryPerformanceCounter(&f0);
    std::set<uint32_t> seen;
    unsigned files = 0, bad = 0;
    void** head = *(void***)((uint8_t*)rm + 0x2C);
    for (void** node = head ? (void**)*head : nullptr; node && files < 512; node = (void**)node[0]) {
        if (IsBadReadPtr(node, 8))
            break;
        std::wstring path;
        if (!file_location(node[1], path))
            continue;
        if (path.find(L"\\SporeCoop\\") != std::wstring::npos)
            continue; // our own packages (received creations, model copies): no parts
        files++;
        const PartFile& pf = part_file(path);
        std::vector<const dbpf::Entry*> want;
        for (auto& e : pf.parts)
            if (e.key.group == group && !seen.count(e.key.instance))
                want.push_back(&e);
        if (want.empty())
            continue;
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            continue;
        std::vector<uint8_t> raw, data;
        for (auto* e : want) {
            seen.insert(e->key.instance);
            partprops::Part p;
            if (read_file_at(f, e->offset, e->size, raw) && dbpf::decode(*e, raw.data(), data) &&
                partprops::parse(data.data(), data.size(), e->key.instance, p))
                out.push_back(std::move(p));
            else
                bad++;
        }
        CloseHandle(f);
    }
    QueryPerformanceCounter(&f1);
    QueryPerformanceFrequency(&fq);
    g_catalogue_ms += 1000.0 * (double)(f1.QuadPart - f0.QuadPart) / (double)fq.QuadPart;
    H->log(M, SK_INFO, "creations: content: this game has %u parts of group %08X (%u packages, %u unreadable, %.0f ms)",
           (unsigned)out.size(), group, files, bad, g_catalogue_ms);
    return out;
}

// the descriptions of the parts the bundle's models use, from this game's part props (the sender has them)
static void add_part_descs(const Key& key, std::vector<dbpf::Record>& recs)
{
    uint32_t group = key.type == kInfo[kCell].type ? kCellPartGroup : key.type == kInfo[kUfo].type ? kUfoPartGroup
                     : key.type == kInfo[kCreature].type                                          ? kPartGroup
                                                                                                  : 0;
    if (!group && g_design_descs_fix) // BUG-0725: the designs (vehicle, building) too
        for (auto& mi : kModelInfo)
            if (mi.type == key.type)
                group = mi.parts;
    if (!group)
        return;
    std::set<uint32_t> ids;
    for (auto& r : recs)
        if (r.key.type == kBem)
            for (auto& ref : model_part_refs(r.data, group))
                ids.insert(ref.id);
    static std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> cache; // (group, id) -> prop bytes
    size_t total = 0;
    for (uint32_t id : ids) {
        auto k = std::make_pair(group, id);
        auto it = cache.find(k);
        if (it == cache.end()) {
            std::vector<uint8_t> d;
            if (!part_prop_bytes(id, group, d) || d.size() > 16384)
                d.clear();
            it = cache.emplace(k, std::move(d)).first;
        }
        if (it->second.empty() || total + it->second.size() > kMaxDescBytes)
            continue;
        total += it->second.size();
        dbpf::Record r;
        r.key = {id, kPartProp, kPartDescGroup};
        r.data = it->second;
        recs.push_back(std::move(r));
        g_desc_out++;
    }
}

// the part descriptions out of received records (never registered: the game must not take a part for one
// it has when only its description came)
static std::map<uint32_t, std::vector<uint8_t>> take_part_descs(std::vector<dbpf::Record>& recs)
{
    std::map<uint32_t, std::vector<uint8_t>> out;
    for (auto it = recs.begin(); it != recs.end();) {
        if (it->key.group == kPartDescGroup) {
            if (it->key.type == kPartProp && g_rx_descs.size() < 4096)
                g_rx_descs[it->key.instance] = it->data; // BUG-0725: for every model of the session
            if (it->key.type == kPartProp && out.size() < 512)
                out[it->key.instance] = std::move(it->data);
            g_desc_in++;
            it = recs.erase(it);
        } else
            ++it;
    }
    return out;
}

// the most similar part this game has to the part `id` described by `desc` (0: none of its role)
static uint32_t lookalike_of(uint32_t id, uint32_t group, const std::vector<uint8_t>& desc)
{
    auto k = std::make_pair(group, id);
    auto it = g_lookalike.find(k);
    if (it != g_lookalike.end())
        return it->second;
    partprops::Part want;
    const std::vector<partprops::Part>* cat = &catalogue(group);
    std::vector<partprops::Part> kept; // tests: without the parts `lack` names
    if (!g_lacked.empty()) {
        for (auto& c : *cat)
            if (!g_lacked.count(c.id))
                kept.push_back(c);
        cat = &kept;
    }
    uint32_t sub = partprops::parse(desc.data(), desc.size(), id, want) ? partprops::nearest(want, *cat) : 0;
    g_lookalike[k] = sub;
    H->log(M, SK_INFO, "creations: content: part %08X (%s, %u abilities) is shown as %08X, the most similar part this game has",
           id, want.detail() ? "detail" : "role part", (unsigned)want.abil.size(), sub);
    return sub;
}

// the unbuildable part ids of these records, each once (BUG-0341, BUG-0452: every part id this game lacks)
std::vector<uint32_t> unknown_part_ids(const std::vector<dbpf::Record>& recs, ModelKind k)
{
    std::vector<uint32_t> out;
    uint32_t group = part_group(k);
    for (auto& r : recs) {
        if (r.key.type != kBem)
            continue;
        for (auto& ref : model_part_refs(r.data, group)) {
            bool unknown = !part_known(ref.id, group);
            if (unknown && std::find(out.begin(), out.end(), ref.id) == out.end())
                out.push_back(ref.id);
        }
    }
    return out;
}

// the unknown ids in the log, one line each (BUG-0452)
static void log_unknown_parts(const std::vector<uint32_t>& ids, ModelKind k, const std::string& what)
{
    for (uint32_t id : ids) {
        H->log(M, SK_INFO, "creations: %s: part %08X unknown here: this game does not have it (%s%08X/%08X%s)",
               what.c_str(), id, k == kMkCreature ? "crt" : k == kMkCell ? "cll" : k == kMkUfo ? "ufo" : "vcl",
               kPartProp, part_group(k), k == kMkCreature && is_cc_part(id) ? ", Creepy & Cute" : "");
        g_unknown_logged++;
    }
}

// BUG-0341, the owner's choice: on a game without the pack each pack part of a creature is shown as a
// similar base game part (content_packs.h kCreepyCuteSubstitutes: the same kind, the nearest ability
// levels), rewritten in this game's copy of the creature's model records before they are registered;
// nothing of the pack is sent. A part with no substitute (or whose substitute this game lacks) keeps
// the refusal above.
unsigned g_parts_substituted;
static bool g_subst_told;

static uint32_t substitute_of(uint32_t id)
{
    auto it = std::lower_bound(std::begin(kCreepyCuteSubstitutes), std::end(kCreepyCuteSubstitutes), id,
                               [](const PartSubstitute& s, uint32_t v) { return s.pack < v; });
    return it != std::end(kCreepyCuteSubstitutes) && it->pack == id ? it->base : 0;
}

// the encoded bytes of an id in a model: 0xFE is written FE 00
static void encode_id(uint32_t id, std::vector<uint8_t>& out)
{
    for (int k = 0; k < 4; k++) {
        uint8_t b = (uint8_t)(id >> (8 * k));
        out.push_back(b);
        if (b == 0xFE)
            out.push_back(0);
    }
}

// rewrites the parts of the model records (bem) this game does not have to their substitutes in
// place; returns how many were rewritten. The Creepy & Cute table first (the same kind), then a base
// game detail of the same group and length (kBasePartFallbacks: the kind of another pack's part is not
// known here). A part with neither is left in place: unknown_part_ids refuses the creation.
static unsigned substitute_parts(std::vector<dbpf::Record>& recs, ModelKind k, const std::string& whose,
                                 const std::map<uint32_t, std::vector<uint8_t>>* descs = nullptr)
{
    uint32_t group = part_group(k);
    unsigned n = 0, fb = 0;
    for (auto& r : recs) {
        if (r.key.type != kBem)
            continue;
        auto& m = r.data;
        for (auto& ref : model_part_refs(m, group)) {
            if (!ref.safe)
                continue; // a run after the id repeats its last byte: rewriting it would change the slot
            bool unknown = !part_known(ref.id, group);
            if (!unknown)
                continue;
            uint32_t sub = 0;
            if (descs) { // BUG-0559: the sender's description of the part
                auto d = descs->find(ref.id);
                if (d != descs->end()) {
                    uint32_t s = lookalike_of(ref.id, group, d->second);
                    if (s && part_known(s, group)) {
                        sub = s;
                        g_lookalikes++;
                    }
                }
            }
            if (!sub && k == kMkCreature) { // the Creepy & Cute table first (creature parts)
                uint32_t s = substitute_of(ref.id);
                if (s && part_known(s, kPartGroup))
                    sub = s;
            }
            int fe = 0;
            for (int i = 0; i < 4; i++)
                fe += (((ref.id >> (8 * i)) & 0xFF) == 0xFE) ? 1 : 0;
            if (!sub) {
                sub = part_fallback(k, fe);
                if (sub)
                    fb++;
            }
            if (!sub)
                continue;
            std::vector<uint8_t> enc;
            encode_id(sub, enc);
            if (enc.size() != ref.len) // not the same length: left as it is (refused below)
                continue;
            std::copy(enc.begin(), enc.end(), m.begin() + (long)ref.at);
            n++;
        }
    }
    g_parts_substituted += n;
    g_parts_fallback += fb;
    return n;
}

// BUG-0369: a creature model names its three paint layers (base, coat, detail) by their skin effect
// ids, three dwords at byte 26 of the model's (unpacked) header. The pack's paints use effects only the
// pack has: a game without it painted the host's creature without those layers (other colours). On a
// game without the pack they are rewritten in this game's copy of the model to the base game effect of
// the same layer (content_packs.h kCreepyCutePaintSubstitutes), as the parts above.
unsigned g_paints_substituted;

static uint32_t paint_substitute_of(uint32_t id)
{
    auto it = std::lower_bound(std::begin(kCreepyCutePaintSubstitutes), std::end(kCreepyCutePaintSubstitutes), id,
                               [](const PartSubstitute& s, uint32_t v) { return s.pack < v; });
    return it != std::end(kCreepyCutePaintSubstitutes) && it->pack == id ? it->base : 0;
}

// the three skin effect ids of a model record and where each is written (packed offset and length);
// false when the header is too short. BUG-0852: an id inside a byte run (a model whose coat and detail are 0, `00 FE
// 07`) is read too: patch[k] tells whether layer k can be rewritten in place. Before, such a model was skipped whole
// (its base paint never checked or substituted). `coop.fix creations.paint_runs off`: the strict reader (0.7.12).
static bool g_paint_runs_fix = true;
COOP_FIX("creations.paint_runs", "BUG-0852", g_paint_runs_fix, "creations: the paints of a model whose paint bytes are run-packed are checked and substituted too");
unsigned g_paints_in_run; // unknown paints inside a run: not patched in place (the creation is refused instead)
bool model_effects(const std::vector<uint8_t>& m, uint32_t eff[3], size_t at[3], size_t len[3], bool patch[3])
{
    bool p3[3];
    return model_paint::effects(m, eff, at, len, patch ? patch : p3, g_paint_runs_fix);
}

bool is_cc_paint(uint32_t id) { return paint_substitute_of(id) != 0; }

// the 0xFE bytes of an id as a model writes it (FE 00 each): the encoded length is 4 + this
static int fe_count(uint32_t id)
{
    int n = 0;
    for (int i = 0; i < 4; i++)
        n += (((id >> (8 * i)) & 0xFF) == 0xFE) ? 1 : 0;
    return n;
}

// BUG-0452: an effect this game has. key_exists finds the base game's effect records; 14 of the base
// game's 111 skin paints have no record (the game defines them itself), so the generated list counts
// too. Creepy & Cute's effects are only present with that pack (its table names them).
// BUG-0559: whether this game's effect directories have the effect: the lookup the game's effect creator
// makes (Swarm::cEffectsManager::GetDirectoryAndEffectIndex(id, 0) < 0: EffectWorld::CreateEffect gives up).
// A skin paint is a compiled effect of an effect directory (type 0xEA5118B0), not always a record of its
// own: the base game's cell paints (skinpaint_cell_stripes 0xE4C02CCE, from the Cell stage) have none, the
// lists below did not know them and a creature that kept its cell paint was painted another colour on
// every other machine. -1: no effect manager (the lists decide).
static int effect_in_directories(uint32_t id)
{
    static uintptr_t get = H->sym("Swarm::IEffectsManager::Get"),
                     find = H->sym("Swarm::cEffectsManager::GetDirectoryAndEffectIndex");
    void* mgr = get && find ? cdecl_call<void*>(get) : nullptr;
    if (!mgr)
        return -1;
    return (int)thiscall<uint32_t>(find, mgr, id, (uint32_t)0) >= 0 ? 1 : 0;
}

static std::map<uint32_t, bool> g_effect_cache_dir; // BUG-0559: id -> this game has it
bool paint_known(uint32_t id)
{
    auto d = g_effect_cache_dir.find(id);
    if (d != g_effect_cache_dir.end())
        return d->second;
    int dir = effect_in_directories(id);
    if (dir >= 0) {
        g_effect_cache_dir[id] = dir == 1;
        return dir == 1;
    }
    auto it = g_effect_cache.find(id);
    if (it != g_effect_cache.end())
        return it->second;
    bool known = key_exists({id, kEffectProp, kEffectGroup}) ||
                 std::binary_search(std::begin(kBasePaintEffects), std::end(kBasePaintEffects), id) ||
                 (is_cc_paint(id) && have_creepy_cute());
    g_effect_cache[id] = known;
    return known;
}

// the base game effect written in place of an unknown paint (content_packs.h kBasePaintFallbacks)
static uint32_t paint_fallback(int layer, int fe)
{
    for (auto& f : kBasePaintFallbacks)
        if (f.layer == layer && f.fe == fe && paint_known(f.base))
            return f.base;
    return 0;
}

// rewrites the skin effects of the creature's model records this game does not have in place; returns
// how many. Creepy & Cute's table first (BUG-0369), then a base game paint of the same layer and
// length; an effect with neither stays (the game leaves that layer unpainted, no crash).
static unsigned substitute_paints(std::vector<dbpf::Record>& recs, const std::string& whose)
{
    unsigned n = 0, fb = 0;
    for (auto& r : recs) {
        uint32_t eff[3];
        size_t at[3], len[3];
        bool patch[3];
        if (r.key.type != kBem || !model_effects(r.data, eff, at, len, patch))
            continue;
        for (int k = 0; k < 3; k++) {
            if (!eff[k])
                continue;
            bool unknown = !paint_known(eff[k]);
            if (!unknown)
                continue;
            if (!patch[k]) { // BUG-0852: inside a run: no byte to rewrite (unknown_paint_ids refuses the creation)
                g_paints_in_run++;
                H->log(M, SK_INFO, "creations: %s: paint layer %d (%08X) is run-packed: not substituted in place", whose.c_str(), k,
                       eff[k]);
                continue;
            }
            uint32_t sub = paint_substitute_of(eff[k]);
            if (sub && !paint_known(sub))
                sub = 0;
            if (!sub) {
                sub = paint_fallback(k, fe_count(eff[k]));
                if (sub)
                    fb++;
            }
            if (!sub)
                continue; // unknown_paint_ids refuses the creation
            std::vector<uint8_t> enc;
            encode_id(sub, enc);
            if (enc.size() != len[k])
                continue;
            std::copy(enc.begin(), enc.end(), r.data.begin() + (long)at[k]);
            n++;
        }
    }
    if (n) {
        g_paints_substituted += n;
        g_paints_fallback += fb;
        H->log(M, SK_INFO, "creations: %s: %u paint layer(s) this game does not have shown as base game paints%s",
               whose.c_str(), n, fb ? " (a base game paint: the pack's kind is not known here, BUG-0452)" : " (the pack's, BUG-0369)");
    }
    return n;
}

// the unknown paint effects of these records, each once (BUG-0452)
static std::vector<uint32_t> unknown_paint_ids(const std::vector<dbpf::Record>& recs)
{
    std::vector<uint32_t> out;
    for (auto& r : recs) {
        uint32_t eff[3];
        size_t at[3], len[3];
        if (r.key.type != kBem || !model_effects(r.data, eff, at, len))
            continue;
        for (int i = 0; i < 3; i++) {
            if (!eff[i])
                continue;
            bool unknown = !paint_known(eff[i]);
            if (unknown && std::find(out.begin(), out.end(), eff[i]) == out.end())
                out.push_back(eff[i]);
        }
    }
    return out;
}

// a log line and, once per session, a notice for the player
static void told_substituted(unsigned n, unsigned fallbacks, const std::string& whose)
{
    if (!n)
        return;
    H->log(M, SK_INFO, "creations: %s: %u part(s) this game does not have shown as base game parts%s", whose.c_str(), n,
           fallbacks ? " (a base game detail: the pack part's kind is not known here, BUG-0452)"
                     : " (similar base game parts, BUG-0341)");
    if (!g_subst_told && C.feed) {
        g_subst_told = true;
        C.feed("Some parts a player uses are shown as similar base game parts (this game does not have that pack)");
    }
}

// BUG-0452: checks and fixes every part and paint id of these model records; returns how many ids
// remain unknown (the creation must not be built then, the game's fallback shows instead)
// BUG-0559, the blocks: a species' creaturedata (0x0F43029A, the game's cache of what it derived from
// the model; the host sends its own with the host's own species, BUG-0017 / 0xD2C361) is a second copy of
// the model's part list (each part as `00 60 62 40` + its id, not byte-run packed, with the part's box)
// and of its three paint effects. A guest's game that lacks a part builds the species from it and draws
// that part as its box: the "big blocks, different from part to part" of the player report, while the bem
// had the substitutes. The ids substituted in the bem are now substituted in it as well; one this game
// still lacks drops the record (the game computes a new one when it is missing, docs/re/creations.md 1.1).
static unsigned g_species_data_fixed, g_species_data_dropped;

static void align_species_data(std::vector<dbpf::Record>& recs, ModelKind k, const std::vector<dbpf::Record>& before,
                               const std::vector<uint32_t>& unknown, const std::string& whose)
{
    if (k != kMkCreature)
        return;
    std::map<uint32_t, uint32_t> parts;       // the bem's part ids: before -> after
    std::vector<std::pair<std::array<uint8_t, 12>, std::array<uint8_t, 12>>> paints; // the three effects
    for (size_t i = 0; i < recs.size() && i < before.size(); i++) {
        if (recs[i].key.type != kBem || before[i].key.type != kBem || recs[i].data.size() != before[i].data.size())
            continue;
        auto a = model_part_refs(before[i].data, kPartGroup), b = model_part_refs(recs[i].data, kPartGroup);
        for (size_t j = 0; j < a.size() && j < b.size(); j++)
            if (a[j].at == b[j].at && a[j].id != b[j].id)
                parts[a[j].id] = b[j].id;
        uint32_t e0[3], e1[3];
        size_t at[3], len[3];
        if (model_effects(before[i].data, e0, at, len) && model_effects(recs[i].data, e1, at, len) &&
            memcmp(e0, e1, sizeof e0)) {
            std::array<uint8_t, 12> x, y;
            memcpy(x.data(), e0, 12);
            memcpy(y.data(), e1, 12);
            paints.push_back({x, y});
        }
    }
    for (auto it = recs.begin(); it != recs.end();) {
        if (it->key.type != kCreatureData) {
            ++it;
            continue;
        }
        auto& d = it->data;
        unsigned n = 0;
        for (size_t i = 0; i + 7 <= d.size(); i++) {
            if (d[i] != 0x60 || d[i + 1] != 0x62 || d[i + 2] != 0x40)
                continue;
            uint32_t id = d[i + 3] | d[i + 4] << 8 | d[i + 5] << 16 | (uint32_t)d[i + 6] << 24;
            auto p = parts.find(id);
            if (p != parts.end()) {
                for (int b = 0; b < 4; b++)
                    d[i + 3 + b] = (uint8_t)(p->second >> (8 * b));
                n++;
            }
        }
        for (auto& [x, y] : paints) {
            auto f = std::search(d.begin(), d.end(), x.begin(), x.end());
            if (f != d.end()) {
                std::copy(y.begin(), y.end(), f);
                n++;
            }
        }
        // a part id it names that this game still lacks: the record goes (the game makes its own)
        uint32_t missing = 0;
        for (size_t i = 0; i + 8 <= d.size() && !missing; i++)
            if (d[i] == 0x00 && d[i + 1] == 0x60 && d[i + 2] == 0x62 && d[i + 3] == 0x40) {
                uint32_t id = d[i + 4] | d[i + 5] << 8 | d[i + 6] << 16 | (uint32_t)d[i + 7] << 24;
                if (id && std::find(unknown.begin(), unknown.end(), id) != unknown.end() && !part_known(id, kPartGroup))
                    missing = id;
            }
        if (missing) {
            g_species_data_dropped++;
            H->log(M, SK_INFO, "creations: %s: its species data names part %08X this game does not have: left out "
                   "(the game computes its own, BUG-0559)", whose.c_str(), missing);
            it = recs.erase(it);
            continue;
        }
        if (n) {
            g_species_data_fixed++;
            H->log(M, SK_INFO, "creations: %s: its species data names the same substitutes as its model (%u ids, BUG-0559)",
                   whose.c_str(), n);
        }
        ++it;
    }
}

static unsigned ensure_content(std::vector<dbpf::Record>& recs, ModelKind k, const std::string& whose)
{
    // BUG-0559: the sender's part descriptions are never registered
    std::map<uint32_t, std::vector<uint8_t>> descs = take_part_descs(recs);
    // BUG-0452: log every id this game does not have, before substituting it
    std::vector<uint32_t> unknown = unknown_part_ids(recs, k);
    log_unknown_parts(unknown, k, whose);
    if (kind_paints(k)) {
        std::vector<uint32_t> paints = unknown_paint_ids(recs);
        for (uint32_t id : paints)
            H->log(M, SK_WARN, "creations: %s: paint %08X unknown here: this game does not have it (BUG-0452)",
                   whose.c_str(), id);
        unknown.insert(unknown.end(), paints.begin(), paints.end());
    }
    // substitute what this game can
    std::vector<dbpf::Record> before = recs;
    unsigned fb0 = g_parts_fallback;
    unsigned n = substitute_parts(recs, k, whose, &descs);
    if (kind_paints(k))
        n += substitute_paints(recs, whose);
    align_species_data(recs, k, before, unknown, whose); // BUG-0559: its creaturedata the same
    if (n)
        told_substituted(n, g_parts_fallback - fb0, whose);
    // what is still unknown after substitution is refused
    std::vector<uint32_t> remaining = unknown_part_ids(recs, k);
    if (kind_paints(k)) {
        std::vector<uint32_t> paints = unknown_paint_ids(recs);
        remaining.insert(remaining.end(), paints.begin(), paints.end());
    }
    return (unsigned)remaining.size();
}

std::string world_missing_parts()
{
    if (g_world_missing.empty())
        return {};
    std::string s;
    for (auto& [k, name] : g_world_missing)
        s += (s.empty() ? "'" : ", '") + name + "'";
    return s;
}

static Key avatar_species_key()
{
    void* av = game::mode_id() == modes::kCreature ? game::avatar() : nullptr;
    if (!av)
        return {};
    auto* k = (uint32_t*)((uint8_t*)av + 0xB28);
    return {k[0], k[1], k[2]};
}

// BUG-0638: the guest's explicit choice was made (or played) in this world; outside a world it holds
static bool choice_here(int k)
{
    uint32_t w = modes::current_world();
    return !w || world_record(k, w) == g_choice[k].instance;
}

static bool host_world_key(Kind k, uint32_t inst); // a creation of the host's world (BUG-0278)
bool records_from_game(const Key& k, std::vector<dbpf::Record>& out); // its records, any registered package
static bool own_option(Kind k, const Key& key)
{
    for (auto& o : g_opts[k])
        if (o.key == key)
            return true;
    return false;
}

// F5 (BUG-0769): creature.mode host (the host's session setting): every guest plays the host's cell and creature
bool one_creature() { return settings::get(settings::kCreatureMode) == 1; }
static unsigned g_one_mine; // mine() answered with the host's creation for that reason (coop.creations)

Option mine(Kind k)
{
    bool guest = C.sess && C.sess->role() == net::Role::Client;
    if (guest && k != kUfo && one_creature()) { // no own choice, no world record: the host's creation, also evolved
        g_one_mine++;
        auto h = g_applied.find({0, (uint8_t)k});
        Key a = h != g_applied.end() && h->second.orig.instance ? h->second.orig
                : k == kCreature                                ? avatar_species_key()
                                                                : cell_key();
        for (auto& o : g_opts[k])
            if (o.key.instance == a.instance)
                return o;
        return {a, ""};
    }
    // BUG-0278: a choice of a creation of the host's world that is not one of ours (taken over from
    // an editor by 0.7.3.0) is not a choice; "the world's own" follows the host's creature
    bool chosen = g_chosen[k];
    if (chosen && !guest && k != kUfo && g_choice[k].instance && g_choice_epoch[k] != choice_epoch_now(k))
        chosen = false; // BUG-0344: a choice of another world (or before this stage): the world's own
    if (chosen && guest && k != kUfo && g_choice[k].instance &&
        host_world_key(k, g_choice[k].instance) && !own_option(k, g_choice[k]))
        chosen = false;
    if (g_choice_world_fix && chosen && guest && k != kUfo && g_choice[k].instance && !choice_here(k))
        chosen = false; // BUG-0638: chosen in another world: this world's default
    if (chosen && guest && k != kUfo && !g_choice[k].instance) {
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
    // BUG-0345: a guest's default is its own creation only in a world it played it in
    if (guest && g_guest_world_fix && !g_opts[k].empty()) {
        if (uint32_t rec = world_record(k, modes::current_world()))
            for (auto& o : g_opts[k])
                if (o.key.instance == rec)
                    return o;
        if (g_follow_fix && g_follow[k].instance && g_follow_epoch[k] == choice_epoch_now(k))
            return named(g_follow[k]); // BUG-0508: the host's evolved species, in place
        auto h = g_applied.find({0, (uint8_t)k});
        if (h != g_applied.end() && h->second.orig.instance)
            return named(h->second.orig); // the host's creation: the world's species
        Key a = k == kCreature ? avatar_species_key() : cell_key();
        if (a.instance && !own_option(k, a))
            return named(a);
        return {};
    }
    // default: a guest plays its newest creation; the host keeps the one of its world
    if (guest && !g_opts[k].empty())
        return g_opts[k].front();
    if (guest && g_follow_fix && g_follow[k].instance && g_follow_epoch[k] == choice_epoch_now(k))
        return named(g_follow[k]); // BUG-0508: the host's evolved species, in place
    Key a = k == kCreature ? avatar_species_key() : cell_key();
    if (a.instance)
        return named(a);
    return {};
}

// ---- bundle serialization ----
std::vector<uint8_t> pack(const Bundle& b)
{
    // BUG-0559: the description of each part the models use goes with them (the receiver picks a part
    // like it when its game lacks one); a receiver of 0.7.6 registers them in a group nobody asks for
    std::vector<dbpf::Record> recs = b.recs;
    bool has_desc = false;
    for (auto& r : recs)
        has_desc |= r.key.group == kPartDescGroup;
    if (!has_desc)
        add_part_descs(b.key, recs);
    net::Writer w;
    w.u32(b.key.instance); w.u32(b.key.type); w.u32(b.key.group);
    w.u16((uint16_t)recs.size());
    for (auto& r : recs) {
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

// BUG-0921: a player's creation bundle carries its pictures (png in the model group and the thumbnail group +1).
// The guest registers the host's bundle under the host's own key above every package (world_*.package): that copy
// answers for the species of the host's world, and without a picture in it the guest's Tribe roster drew a white
// square for every member (the host's own species picture of BUG-0643 is only registered under the host's key when
// nothing answers for that key yet, and the bundle always came first). Off: the model and the metadata only (0.7.11)
static bool g_bundle_png_fix = true;
COOP_FIX("creations.bundle_png", "BUG-0921", g_bundle_png_fix, "creations: a player's creation bundle carries its pictures (the Tribe roster's portraits of the host's species made in the session)");
// BUG-0802: a rename of a creation whose model is not in our EditorSaves (a partner's or an imported cell played as our
// own: its model is in the package it came from) saves only a new metadata record there; that bundle went out as it
// was and the host built a cell of a key without a model (fault at 0xE83EC8). Now a bundle without its model takes the
// model from the game's own resources (the package that has it), else from the records kept of an applied creation
// (BUG-0370), else it has no records (the next round tries again; never sent), and a received bundle with records but
// no model is refused (the player keeps the look it had). Off: 0.7.12.
static bool g_meta_only_fix = true;
COOP_FIX("creations.meta_only", "BUG-0802", g_meta_only_fix, "creations: a renamed creation whose model came from elsewhere is sent with its model, and a creation without its model is not built (the host crashed at 0xE83EC8)");
static unsigned g_meta_filled, g_meta_waits, g_meta_refused; // tests: models added from the game / kept, bundles held, refused
static const std::vector<dbpf::Record>* old_records(uint32_t inst);
bool make_bundle(Kind k, const Key& key, Bundle& b)
{
    b.key = key;
    if (!key.instance || !g_es_ok)
        return key.instance != 0; // a premade: no records (everybody has it)
    const dbpf::Package& pk = g_es;
    for (auto& e : pk.entries()) {
        bool png = g_bundle_png_fix && e.key.type == kPng && e.size <= (1u << 20) &&
                   (e.key.group == kInfo[k].group || e.key.group == kInfo[k].group + 1);
        if (e.key.instance != key.instance || (e.key.type != kBem && e.key.type != kMeta && !png))
            continue;
        if (e.key.group != kInfo[k].group && !png)
            continue;
        dbpf::Record r;
        r.key = e.key;
        if (pk.read(e, r.data))
            b.recs.push_back(std::move(r));
    }
    bool model = false; // pictures alone are no creation (callers take "no records" as "not on disk yet")
    for (auto& r : b.recs)
        model |= r.key.type == kBem;
    if (!model && g_meta_only_fix && !b.recs.empty()) { // BUG-0802: metadata without its model
        std::vector<dbpf::Record> found;
        if (records_from_game({key.instance, kBem, kInfo[k].group}, found))
            for (auto& r : found)
                if (r.key.type == kBem && r.key.instance == key.instance && !r.data.empty()) {
                    b.recs.push_back(std::move(r));
                    model = true;
                }
        if (!model)
            if (auto* old = old_records(key.instance))
                for (auto& r : *old)
                    if (r.key.type == kBem && !r.data.empty()) {
                        b.recs.push_back(r);
                        model = true;
                    }
        if (model)
            g_meta_filled++;
        else
            g_meta_waits++;
    }
    if (!model)
        std::erase_if(b.recs, [](const dbpf::Record& r) { return r.key.type == kPng; });
    if (!model && g_meta_only_fix)
        b.recs.clear(); // never a creation without its model
    return true;
}

// ---- registering a package with the game's resource manager (docs/re/creations.md §3) ----
// BUG-0996 (docs/re/creations.md#creations-package-close): a package that failed to open or register, or that was
// dropped (a newer version, the session end's world packages), stayed allocated with its file open for the process:
// the next write of the same name failed and package_for fell back to "<name>_2.package" ... ("_49", then "cannot
// write the package"). Now our reference is given back once nobody else holds one: GetRefCount (vt 0x10) == 1 (ours,
// after RegisterDatabase(false) released the manager's), Close (vt 0x1C) and Release ((db+4) vt[1]: at 0 the
// scalar deleting dtor, whose Shutdown closes the file too). Dropped packages wait kCloseAfter s and are released
// outside the frame. `coop.fix creations.package_close off`: kept open (0.7.13).
static bool g_pkg_close_fix = true;
COOP_FIX("creations.package_close", "BUG-0996", g_pkg_close_fix, "creations: a dropped or failed creation package is closed and released (its file can be written again)");
constexpr double kCloseAfter = 2.0;
struct Closing {
    void* db;
    double at;
    int tries = 0;
};
static std::vector<Closing> g_closing;
static bool g_close_queued;
static unsigned g_pkgs_closed, g_pkgs_close_kept, g_pkg_renames;
static bool package_held(void* db); // (below: g_pkgs, g_applied, g_world_pkgs)
// our last reference to a package nobody uses: closed and released (true), or left as it is (false)
static bool release_package(void* db)
{
    if (!db || package_held(db) || vcall<int>(db, 0x10 / 4) != 1) // GetRefCount: someone else still holds it
        return false;
    vcall<bool>(db, 0x1C / 4);            // Close
    vcall<int>((uint8_t*)db + 4, 1);      // Release -> 0: the deleting dtor
    g_pkgs_closed++;
    return true;
}
static void close_run(void*)
{
    g_close_queued = false;
    double now = H->time_seconds();
    for (auto it = g_closing.begin(); it != g_closing.end();) {
        if (now < it->at) {
            ++it;
            continue;
        }
        if (release_package(it->db) || ++it->tries >= 10) {
            if (it->tries >= 10) {
                g_pkgs_close_kept++;
                H->log(M, SK_WARN, "creations: a dropped package %p is still held by the game: kept open (BUG-0996)", it->db);
            }
            it = g_closing.erase(it);
        } else {
            it->at = now + kCloseAfter;
            ++it;
        }
    }
}
static void close_tick(double now)
{
    if (g_close_queued || g_closing.empty())
        return;
    for (auto& c : g_closing)
        if (now >= c.at) {
            g_close_queued = true;
            H->run_outside_frame(close_run, nullptr);
            return;
        }
}
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
        if (g_pkg_close_fix)
            release_package(db); // BUG-0996 (only ours: nobody else saw it)
        return nullptr;
    }
    if (!vcall<bool>(rm, 0x50 / 4, true, db, priority)) { // RegisterDatabase
        g_last_error = "RegisterDatabase failed";
        if (g_pkg_close_fix)
            release_package(db); // BUG-0996
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
std::map<std::pair<uint8_t, uint32_t>, WorldPkg> g_world_pkgs; // (kind, original instance)

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
bool g_world_drop = true;
COOP_FIX("creations.world_drop", "BUG-0055", g_world_drop, "creations: the host's world creations are dropped at the session end (coop.creations worlddrop)");
unsigned g_world_dropped;

// BUG-0370: a player's creature replaced by a newer one (its editor, the co-op window's arrows) is
// unregistered at once on every machine, while creatures of it can still be in the host's world (a
// puppet's leftover a world share or a save brought back): the host sent such an NPC, the guest
// asked for its species and the host answered "not found here", so the NPC was missing on the guest
// until the next world share. The records of every player creature applied here are kept by their
// instance (the newest kKeepOld) and the host answers from them when the game no longer finds the
// key.
constexpr size_t kKeepOld = 24;
static std::map<uint32_t, std::vector<dbpf::Record>> g_old_recs; // instance -> its records (renamed)
static std::deque<uint32_t> g_old_order;
static unsigned g_old_served;
static const std::vector<dbpf::Record>* old_records(uint32_t inst)
{
    auto it = g_old_recs.find(inst);
    return it != g_old_recs.end() ? &it->second : nullptr;
}
static void keep_records(uint32_t inst, const std::vector<dbpf::Record>& recs)
{
    if (!inst || recs.empty() || g_old_recs.count(inst))
        return;
    g_old_recs[inst] = recs;
    g_old_order.push_back(inst);
    while (g_old_order.size() > kKeepOld) {
        g_old_recs.erase(g_old_order.front());
        g_old_order.pop_front();
    }
}

void* package_for(std::wstring path, const std::vector<dbpf::Record>& recs, int priority,
                         std::wstring* used)
{
    auto it = g_pkgs.find(path);
    if (it != g_pkgs.end()) {
        if (used)
            *used = path;
        return it->second;
    }
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
        g_pkg_renames++; // (BUG-0996: the old file of that name is still open)
    }
    void* db = register_package(path, priority);
    if (db)
        g_pkgs[path] = db;
    if (db && used)
        *used = path;
    return db;
}

static void drop_package(void* db)
{
    if (!db)
        return;
    unregister_package(db);
    for (auto it = g_pkgs.begin(); it != g_pkgs.end();)
        it = it->second == db ? g_pkgs.erase(it) : std::next(it);
    if (g_pkg_close_fix && H) // BUG-0996: released a little later, outside the frame, once nobody holds it
        g_closing.push_back({db, H->time_seconds() + kCloseAfter});
}
static bool package_held(void* db)
{
    for (auto& [p, d] : g_pkgs)
        if (d == db)
            return true;
    for (auto& [k, a] : g_applied)
        if (a.db == db)
            return true;
    for (auto& [k, w] : g_world_pkgs)
        if (w.db == db)
            return true;
    return false;
}

// BUG-0469: every model the mod makes this game build from a key it did not create itself (a civ
// mirror vehicle, a building of a city, a host ship) gets this check: the key is in this game AND
// every part / paint id the model names is here. Ids this game lacks are shown as substitutes in a
// copy of the model's records, which is registered at once under the model's OWN key, above every
// package the game has (kPrioModel), so no key of ours is passed to the game or can reach a save; a
// model with an id that has no substitute (or whose key cannot be shadowed) is refused, the caller
// uses the game's own default or waits. Each unknown id is logged once per session.
static bool same_path(const wchar_t* a, const std::wstring& b);

// the database that answers `k` is the package at `path` (our copy of the model's records): what the
// game builds for that key is our copy, not the record the original package holds (BUG-0469 review).
// FindDatabase walks the priority-sorted list highest first and probes OpenRecord (no global key
// index, docs/re/creations.md §3), so a package registered last at the highest priority answers.
static bool model_shadow_answers(const std::wstring& path, const Key& k)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    uint32_t key[3] = {k.instance, k.type, k.group}, real[3] = {};
    void* db = rm ? vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) : nullptr; // FindRecord
    if (!db)
        return false;
    return same_path(vcall<const wchar_t*>(db, 0x28 / 4), path); // GetLocation (as find_paths)
}

static bool same_path(const wchar_t* a, const std::wstring& b)
{
    if (!a || !a[0])
        return false;
    size_t i = 0;
    for (; a[i] && i < b.size(); i++) {
        wchar_t x = a[i], y = b[i];
        if (x >= L'A' && x <= L'Z')
            x = (wchar_t)(x - L'A' + L'a'); // Windows paths: case is not part of a path
        if (y >= L'A' && y <= L'Z')
            y = (wchar_t)(y - L'A' + L'a');
        if (x != y)
            return false;
    }
    return a[i] == 0 && i == b.size();
}
std::map<std::pair<uint8_t, uint32_t>, Model> g_model_use; // (kind, instance) -> the verdict
static std::set<std::pair<uint64_t, uint32_t>> g_model_logged;    // (kind+model, id) already logged
static unsigned g_models_checked, g_models_same, g_models_substituted, g_models_refused, g_models_unreadable;

Model ensure_model(const Key& model, ModelKind kind, bool allow_copy)
{
    if (!g_all_kinds_fix || !model.instance)
        return {Model::kSame, model}; // 0.7.3.5 (the key as the caller got it), or the game's own design
    auto id = std::make_pair((uint8_t)kind, model.instance);
    auto it = g_model_use.find(id);
    if (it != g_model_use.end())
        return it->second;
    const char* name = kModelInfo[kind].name;
    auto remember = [&](const Model& m) {
        g_models_checked++;
        g_model_use[id] = m;
        return m;
    };
    auto log_unknown = [&](const std::vector<uint32_t>& ids, const char* what) {
        for (uint32_t uid : ids) {
            if (!g_model_logged.insert({((uint64_t)kind << 32) | model.instance, uid}).second)
                continue; // once per session
            H->log(M, SK_INFO, "creations: content: %s model %08X %s %08X unknown here: this game does not have it",
                   name, model.instance, what, uid);
        }
    };
    auto refuse = [&]() {
        g_models_refused++;
        return remember(Model{}); // kRefused, {}
    };
    if (!key_exists(model)) {
        H->log(M, SK_INFO, "creations: content: %s model %08X is not in this game", name, model.instance);
        return refuse();
    }
    std::vector<dbpf::Record> recs;
    bool have_model = records_from_game(model, recs);
    if (have_model) {
        have_model = false;
        for (auto& r : recs)
            have_model |= r.key.type == kBem;
    }
    if (!have_model) {
        // the records of this key are not in a package we can read (a baked model, a database format
        // we do not parse): the key is built as it is, as 0.7.3.5 did (not verified, BUG-0469)
        if (!g_models_unreadable++)
            H->log(M, SK_WARN, "creations: content: %s model %08X: its records cannot be read here: built as it is",
                   name, model.instance);
        Model m{Model::kSame, model};
        g_models_same++;
        return remember(m);
    }
    std::vector<uint32_t> bad = unknown_part_ids(recs, kind), bad_paints;
    log_unknown(bad, "part");
    if (kind_paints(kind)) {
        bad_paints = unknown_paint_ids(recs);
        log_unknown(bad_paints, "paint");
    }
    if (bad.empty() && bad_paints.empty()) {
        Model m{Model::kSame, model};
        g_models_same++;
        return remember(m);
    }
    if (!allow_copy && !g_building_copy_fix) { // the caller cannot pass another key to the game (a building)
        H->log(M, SK_INFO, "creations: content: %s model %08X has %u id(s) this game does not have: not built here",
               name, model.instance, (unsigned)(bad.size() + bad_paints.size()));
        return refuse();
    }
    std::string whose = std::string("model ") + hex(model.instance);
    unsigned n = substitute_parts(recs, kind, whose, g_design_descs_fix ? &g_rx_descs : nullptr);
    if (kind_paints(kind))
        n += substitute_paints(recs, whose);
    std::vector<uint32_t> left = unknown_part_ids(recs, kind);
    if (kind_paints(kind)) {
        std::vector<uint32_t> paints = unknown_paint_ids(recs);
        left.insert(left.end(), paints.begin(), paints.end());
    }
    if (!left.empty() || !find_paths()) {
        H->log(M, SK_WARN, "creations: content: %s model %08X: %u id(s) this game does not have cannot be shown here"
               ": not built here", name, model.instance, (unsigned)left.size());
        return refuse();
    }
    // our copy of the model records, under the model's own key: the game builds our copy instead of
    // the record it finds for that key (BUG-0469 review: a key of our own, 0x7D......, would reach the
    // guest's save and no package would answer it after a restart).
    std::vector<dbpf::Record> copy = recs;
    std::wstring path = g_dir + L"\\model_" + std::to_wstring((int)kind) + L"_" + std::to_wstring(model.instance) +
                        L".package", used = path;
    bool shadowed = package_for(path, copy, kPrioModel, &used) && model_shadow_answers(used, model);
    if (!shadowed) {
        g_last_error = "cannot put the model's copy above the game's own record";
        H->log(M, SK_WARN, "creations: content: %s model %08X: %s (%s): not built here", name, model.instance,
               g_last_error.c_str(), slotfs::narrow(used).c_str());
        return refuse();
    }
    Model m{Model::kSubstituted, model}; // the model's own key: the game builds our copy of it
    g_models_substituted++;
    H->log(M, SK_INFO, "creations: content: %s model %08X: %u id(s) shown as substitutes, our copy answers its key",
           name, model.instance, n);
    return remember(m);
}

bool content_all_kinds() { return g_all_kinds_fix; }

std::string content_status()
{
    char b[360];
    snprintf(b, sizeof b, "content checks=%u same=%u substituted=%u refused=%u unreadable=%u fix=%d\n"
             "lookalike parts=%u descriptions out=%u in=%u catalogue=%.0f ms species_data fixed=%u dropped=%u\n",
             g_models_checked, g_models_same, g_models_substituted, g_models_refused, g_models_unreadable,
             (int)g_all_kinds_fix, g_lookalikes, g_desc_out, g_desc_in, g_catalogue_ms, g_species_data_fixed,
             g_species_data_dropped);
    return b;
}

// BUG-9261 (Lineages BUG-9180): the key a player's creation has on every game: 0x7C + a hash. Before, the hash took the
// owner's network id as well (salted_instance): the key changed with every rejoin under another id (in Lineages a new
// nest each session), and the owner itself played its own My Creations key, another species there. Now the hash is
// the bundle's alone (the creation's own key and records), the same on every game and across rejoins; two players
// with the very same creation (the same file) share its species. A partner's creature archived with the host's planet
// (archive_guest_model) is found again under this key. `coop.fix creations.one_key off`: the old key, and the own My
// Creations key on the owner's game (0.7.15.2).
static bool g_one_key = true;
COOP_FIX("creations.one_key", "BUG-9261", g_one_key,
         "creations: a player's creation has one key on every game, its own included, the same across rejoins");
static uint32_t salted_instance(uint8_t owner, uint32_t id)
{
    uint8_t seed[5] = {owner, (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16), (uint8_t)(id >> 24)};
    return 0x7C000000u | (fnv(seed, sizeof seed) & 0x00FFFFFFu);
}
static uint32_t shared_instance(uint8_t owner, uint32_t id)
{
    if (!g_one_key)
        return salted_instance(owner, id);
    uint8_t seed[5] = {0xC1, (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16), (uint8_t)(id >> 24)};
    return 0x7C000000u | (fnv(seed, sizeof seed) & 0x00FFFFFFu);
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
    if (g_meta_only_fix && !b.recs.empty() &&
        std::none_of(b.recs.begin(), b.recs.end(), [&](const dbpf::Record& r) {
            return r.key.type == kBem && r.key.instance == b.key.instance && !r.data.empty();
        })) { // BUG-0802: records without the model: nothing the game can build (0xE83EC8); the old look stays
        g_meta_refused++;
        g_last_error = "received a creation without its model";
        H->log(M, SK_WARN, "creations: player %u's %s %08X came without its model (%u record(s)): not built here (BUG-0802)",
               owner, kInfo[k].name, b.key.instance, (unsigned)b.recs.size());
        return;
    }
    std::string name;
    for (auto& r : b.recs)
        if (r.key.type == kMeta) {
            std::string author;
            dbpf::metadata_names(r.data, author, name);
        }
    // BUG-0508: a guest that played the host's previous creation as the world's species follows it
    bool follow = false;
    if (g_follow_fix && owner == 0 && k != kUfo && C.sess && C.sess->role() == net::Role::Client && ap.orig.instance &&
        b.key.instance && !(b.key == ap.orig) && g_opts[k].empty() && !g_chosen[k]) {
        Key before = mine(k).key;
        if (!before.instance)
            before = g_seen_own[k];
        follow = before == ap.orig || (g_follow[k] == ap.orig && g_follow_epoch[k] == choice_epoch_now(k));
    }
    const uint32_t orig_before = ap.orig.instance;
    Key use = b.key;
    void* old = ap.db;
    void* db = nullptr;
    bool unusable = false;
    const char* noun = k == kCreature ? "creature" : k == kCell ? "cell" : "UFO";
    unsigned miss = ensure_content(b.recs, mk_of(k), "player " + std::to_string(owner) + "'s " + noun + " '" + name + "'");
    if (miss) { // BUG-0452: its puppet falls back as for an unknown creation
        use = {};
        unusable = true;
        g_unusable++;
        g_parts_refused += miss;
        H->log(M, SK_WARN, "creations: player %u's %s '%s' has %u part(s) this game does not have: "
               "not built here (BUG-0452)", owner, noun, name.c_str(), miss);
        if (C.feed)
            C.feed((C.name_of ? C.name_of(owner) : "a player") + std::string("'s ") + noun + " uses parts this game does "
                   "not have: shown as another " + noun + " here");
    } else if (!b.recs.empty() && find_paths()) {
        // the same id everywhere: the content hash (a new version after an editor = a new key); BUG-9261: not the
        // owner's network id (creations.one_key)
        uint32_t inst = shared_instance(owner, id);
        std::vector<dbpf::Record> recs = b.recs;
        for (auto& r : recs)
            if (r.key.instance == b.key.instance)
                r.key.instance = inst;
        keep_records(inst, recs); // BUG-0370
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
    if (g_follow_fix && owner == 0 && k == kCreature && !unusable && orig_before && b.key.instance &&
        C.sess && C.sess->role() == net::Role::Client && orig_before != b.key.instance) {
        g_ps_from = {orig_before, b.key.type, b.key.group};
        g_ps_to = b.key;
    }
    if (follow && !unusable) {
        g_follow[k] = b.key;
        g_follow_epoch[k] = choice_epoch_now(k);
        g_follows++;
        H->log(M, SK_INFO, "creations: the host's %s changed %08X -> %08X: this game's world species follows it in place (BUG-0508)",
               kInfo[k].name, orig_before, b.key.instance);
    }
    if (C.appearance_changed)
        C.appearance_changed(owner);
}

// ---- sending ----
constexpr size_t kChunk = 900;
void send_bundle(int to, uint8_t owner, Kind k, uint32_t id, const std::vector<uint8_t>& data);

// The records of a creation from the package file the game reads it from (any registered package:
// My Creations (DBPF), the Sporepedia cache Pollination.package (DBBF), one of ours). Read piece by
// piece: Pollination grows to hundreds of MB and the game has little address space left.
// BUG-0643: a creation's records go with its pictures (png); off: the model and the metadata only (0.7.8)
static bool g_png_fix = true;
COOP_FIX("creations.png", "BUG-0643", g_png_fix, "creations: the pictures of a creation go with it (the Tribe roster's member portraits on the guest)");
bool records_from_game(const Key& k, std::vector<dbpf::Record>& out)
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
        // BUG-0643: and its pictures (png in the model group and the thumbnail group +1): the Tribe roster's member
        // portraits are the species' picture, a guest without them showed white squares
        bool png = g_png_fix && e.key.type == kPng && (e.key.group == k.group || e.key.group == k.group + 1);
        if (!ok || e.key.instance != k.instance || (e.key.group != k.group && !png) ||
            (e.key.type != kBem && e.key.type != kMeta && e.key.type != k.type && !png) || e.size > (4u << 20))
            continue;
        dbpf::Record r;
        r.key = e.key;
        if (read_at(e.offset, e.size, raw) && dbpf::decode(e, raw.data(), r.data))
            out.push_back(std::move(r));
    }
    CloseHandle(f);
    bool model = false;
    for (auto& r : out)
        model |= r.key.type != kMeta && r.key.type != kPng;
    return model;
}

// host: a guest asks for a species of our world
static void serve_species(uint8_t guest, uint32_t instance)
{
    Bundle b;
    b.key = {instance, kInfo[kCreature].type, kInfo[kCreature].group};
    if (!records_from_game(b.key, b.recs)) {
        auto old = g_old_recs.find(instance);
        if (old == g_old_recs.end()) {
            g_species_missing++;
            H->log(M, SK_WARN, "creations: player %u asks for species %08X: not found here", guest, instance);
            return;
        }
        b.recs = old->second; // BUG-0370: a player's replaced creature
        g_old_served++;
        H->log(M, SK_INFO, "creations: species %08X is a player's earlier creature (no longer registered): sent from "
               "its kept records", instance);
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
    unsigned miss = ensure_content(b.recs, kMkCreature, "species " + trace::hex(b.key.instance) + " of the host's world");
    if (miss) { // BUG-0452: an NPC species this game cannot build
        g_parts_refused += miss;
        H->log(M, SK_WARN, "creations: species %08X of the host's world has %u part(s) this game does "
               "not have: not registered (BUG-0452)", b.key.instance, miss);
        return;
    }
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
    // a premade or a Sporepedia download: the same creation wherever a game has that key (BUG-0724: not in every game,
    // a Sporepedia cache is per profile; a guest without it asks for it, want_species / serve_species)
    if (!in_my_creations(k))
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
// BUG-0200: the save names the host's own creations by the host's keys, and a guest whose own My Creations
// hold another creation under such a key (ids are per profile and sequential) got ITS creation from its
// game's species manager for that key: the save's herds and nests of the host's creature, and the cards the
// game makes from them (the nest's "The <name> are your allies!"), were the guest's creation (its name, its
// look). The species manager keeps one profile per key for the whole process (docs/re/creature.md 15), so
// the guest's manager answers for the host's key with the profile of the key of ours the host's records are
// registered under: everything of the game that looks the species up by the host's key gets the host's
// creation. The profile it had before goes back at the session end (the guest's own creation, in its own
// games). The guest's own creation never plays under such a key in the host's world (own_key_here remaps a
// colliding own creation); a guest playing the world's species of a host creation gets the host's creation too.
// `coop.fix creations.host_key_alias off`: the guest's own creation of that key (0.7.10).
static bool g_host_alias_fix = true;
COOP_FIX("creations.host_key_alias", "BUG-0200", g_host_alias_fix,
         "creature: a guest's game gives the host's creation (not the guest's own of the same id) for the species keys of the host's world: its nests and their cards");
std::map<uint32_t, Alias> g_alias; // host's instance -> alias
unsigned g_alias_set, g_alias_restored;

static Key own_key_here(Kind kind);
static Key avatar_species_key();
static void alias_undo(uint32_t hi, Alias& a)
{
    void** slot = a.prof ? game::species_slot({hi, kInfo[kCreature].type, kInfo[kCreature].group}) : nullptr;
    if (slot && *slot == a.prof) {
        *slot = a.orig; // (null: the game builds the guest's own profile again on the next lookup)
        g_alias_restored++;
    }
    a = Alias{};
}

// the keys a player plays (the guest's avatar, the world's player species, the host's creature): not aliased.
// SetSpecies with an aliased profile writes the profile's own key (+0x504, ours) into the avatar (+0xB28), and a
// key of ours must never reach a save (BUG-0469 review); the players' bodies follow the bundles as before
static bool alias_skip(uint32_t hi)
{
    if (avatar_species_key().instance == hi || (game::mode_id() == modes::kCreature && own_key_here(kCreature).instance == hi))
        return true;
    auto h = g_applied.find({0, (uint8_t)kCreature});
    if (h != g_applied.end() && (h->second.orig.instance == hi || h->second.key.instance == hi))
        return true;
    if (a_SpeciesMgr) {
        auto* mgr = cdecl_call<uint8_t*>(a_SpeciesMgr);
        if (mgr && skg::readable(mgr + 0xA4, 12) && skg::field<uint32_t>(mgr, 0xA4) == hi)
            return true; // the world's player species
    }
    return false;
}

// BUG-0819: the alias is the host's world's: while this guest plays a world of its own (own_world::elsewhere, or a
// Space save of its own) its game gives its own creation for its own keys again, as at the session end (its own
// nests showed the host's creature, and a herd member spawned meanwhile could carry our key into that save); it comes
// back when the guest plays the host's world again. Off: aliased wherever the guest plays (0.7.12).
static bool g_alias_own_world_fix = true;
COOP_FIX("creations.alias_own_world", "BUG-0819", g_alias_own_world_fix,
         "creature: a guest's own world gives the guest's own creations for its keys, not the host's");
unsigned g_alias_own_undone;
static void alias_tick()
{
    if (!g_host_alias_fix || !C.sess || C.sess->my_id() == 0)
        return;
    if (g_alias_own_world_fix && (own_world::elsewhere() || own_world::space_other())) {
        for (auto& [hi, a] : g_alias)
            if (a.prof) {
                alias_undo(hi, a);
                g_alias_own_undone++;
            }
        return;
    }
    for (auto& [hi, ours] : g_remap) {
        Alias& a = g_alias[hi];
        if (alias_skip(hi)) {
            if (a.prof)
                alias_undo(hi, a);
            continue;
        }
        if (a.ours == ours && a.prof)
            continue;
        Key hk{hi, kInfo[kCreature].type, kInfo[kCreature].group};
        void* prof = game::species_by_key({ours, hk.type, hk.group});
        if (!prof)
            continue; // (not built yet: next time)
        game::Key gk{hk.instance, hk.type, hk.group};
        if (!game::species_slot(gk))
            (void)game::species_by_key(gk); // the node of the host's key (the game makes it on the first lookup)
        void** slot = game::species_slot(gk);
        if (!slot)
            continue;
        if (!a.prof)
            a.orig = *slot;
        *slot = prof;
        a.ours = ours;
        a.prof = prof;
        g_alias_set++;
        H->log(M, SK_INFO, "creations: species %08X here is the host's creation (the profile of %08X; was %p)", hi, ours,
               a.orig);
    }
}

static void alias_restore()
{
    for (auto& [hi, a] : g_alias)
        alias_undo(hi, a);
    g_alias.clear();
}

static void apply_own_species(const std::vector<uint8_t>& data)
{
    Bundle b;
    if (!unpack(data, b) || b.recs.empty() || !is_crt(b.key) || !find_paths())
        return;
    unsigned miss = ensure_content(b.recs, kMkCreature, "the host's own creation " + trace::hex(b.key.instance));
    if (miss) { // BUG-0452: not registered, the world is not loaded
        std::string name, author;
        for (auto& r : b.recs)
            if (r.key.type == kMeta)
                dbpf::metadata_names(r.data, author, name);
        g_world_missing[b.key.instance] = name.empty() ? trace::hex(b.key.instance) : name;
        g_parts_refused += miss;
        H->log(M, SK_WARN, "creations: the host's own creation %08X '%s' of its world has %u part(s) this "
               "game does not have: not registered, the world cannot be loaded here (BUG-0452)", b.key.instance,
               name.c_str(), miss);
        return;
    }
    g_world_missing.erase(b.key.instance);
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

// ---- BUG-0647: the host's own designs (civ vehicles) on a guest ----
// The host's civilization names its vehicle designs by the keys of the host's own editor saves (My
// Creations, ids sequential per profile). A guest's copy of that civilization names the same keys, which
// its game resolves to ITS OWN creation of that id (or to nothing: the game's generated design): the guest
// saw "nightmarish monsters" where the host had its vehicles (player report 2026-11-06). The guest asks
// the host for each design key a mirror needs; the host answers with the records when the key is one of
// its own My Creations, else "not mine"; the guest registers the records under a key of its own (0x7D......
// from the content, as the host's own species: the game caches baked models by key) and builds the
// mirror from that key. Mirrors never go into a save (BUG-0470), so the key of ours never reaches one.
static bool g_host_designs_fix = true;
COOP_FIX("creations.host_designs", "BUG-0647", g_host_designs_fix, "civ: a guest's mirror vehicles are built from the host's own vehicle designs (sent by the host), not from the guest's creation of the same id");
struct DesignState {
    double asked = -100;    // the last request
    double first = -1;      // the first request (an old host never answers: the key as it is)
    bool not_own = false;   // the host said: not its own (a premade)
    uint32_t ours = 0;      // the key of ours its records are registered under
};
static std::map<std::pair<uint8_t, uint32_t>, DesignState> g_designs; // guest: (model kind, instance)
static std::map<uint8_t, std::set<std::pair<uint8_t, uint32_t>>> g_design_sent; // host: guest -> answered
// BUG-0422: designs answered to a departed guest were not sent again to a newcomer under its id
PLAYER_STATE("creations.designs", [](uint8_t id) { g_design_sent.erase(id); },
             [](uint8_t id) -> size_t { auto it = g_design_sent.find(id); return it == g_design_sent.end() ? 0 : it->second.size(); });
static unsigned g_design_asks, g_design_in, g_design_not_own, g_design_out, g_design_timeouts;

Key host_design(const Key& k, ModelKind kind, bool* pending)
{
    if (pending)
        *pending = false;
    if (!g_host_designs_fix || !k.instance || kind >= kMkKinds || !C.sess || C.sess->role() != net::Role::Client ||
        !C.sess->connected())
        return k;
    auto& d = g_designs[{(uint8_t)kind, k.instance}];
    if (d.ours)
        return {d.ours, k.type, k.group};
    if (d.not_own)
        return k;
    double now = H->time_seconds();
    if (d.first < 0)
        d.first = now;
    if (now - d.first > 20.0) { // no answer (a host of an older version): as before
        g_design_timeouts++;
        H->log(M, SK_WARN, "creations: the host did not answer for the %s design %08X: built as it is",
               kModelInfo[kind].name, k.instance);
        d.not_own = true;
        return k;
    }
    if (now - d.asked >= 5.0) {
        d.asked = now;
        net::Writer w;
        w.u8(C.sess->my_id());
        w.u8(kAskDesign);
        w.u32(k.instance);
        w.u32((uint32_t)kind);
        w.u32(0);
        w.u16(0);
        C.sess->send_reliable_to(0, net::MSG_CREATION, w);
        g_design_asks++;
        H->log(M, SK_INFO, "creations: asked the host for the %s design %08X", kModelInfo[kind].name, k.instance);
    }
    if (pending)
        *pending = true;
    return k;
}

// host: a guest asks for a design key its mirrors need
static void serve_design(uint8_t guest, uint32_t inst, uint32_t kind)
{
    if (kind >= kMkKinds || !inst)
        return;
    Key k{inst, kModelInfo[kind].type, kModelInfo[kind].group};
    g_design_sent[guest].insert({(uint8_t)kind, inst});
    Bundle b;
    b.key = k;
    if (!in_my_creations(k) || !records_from_game(k, b.recs) || b.recs.empty()) {
        net::Writer w;
        w.u8(0);
        w.u8(kDesignNotOwn);
        w.u32(inst);
        w.u32(kind);
        w.u32(0);
        w.u16(0);
        C.sess->send_reliable_to(guest, net::MSG_CREATION, w);
        return;
    }
    std::vector<uint8_t> data = pack(b);
    send_bundle(guest, (uint8_t)kind, (Kind)kOwnDesign, inst, data);
    g_design_out++;
    H->log(M, SK_INFO, "creations: own %s design %08X (%u records, %u bytes) to player %u", kModelInfo[kind].name, inst,
           (unsigned)b.recs.size(), (unsigned)data.size(), guest);
}

// guest: the records of a host's own design, registered under a key of ours
static void apply_own_design(uint8_t kind, const std::vector<uint8_t>& data)
{
    Bundle b;
    if (kind >= kMkKinds || !unpack(data, b) || b.recs.empty() || !find_paths())
        return;
    auto& d = g_designs[{kind, b.key.instance}];
    if (g_design_descs_fix)
        take_part_descs(b.recs); // BUG-0725: the descriptions of the design's parts, kept for ensure_model
    uint32_t h = 0x811C9DC5;
    for (auto& r : b.recs)
        if (r.key.instance == b.key.instance)
            h = fnv(r.data.data(), r.data.size(), fnv(&r.key.type, 4, h));
    uint32_t ours = 0x7D000000u | (h & 0x00FFFFFFu);
    std::vector<dbpf::Record> recs;
    for (auto& r : b.recs) {
        if (r.key.instance != b.key.instance || r.key.group == kPartDescGroup)
            continue; // (the part descriptions of a bundle are never registered)
        recs.push_back(r);
        recs.back().key.instance = ours;
    }
    std::wstring path = g_dir + L"\\own_design_" + std::to_wstring(ours) + L".package";
    if (!package_for(path, recs, kPrioWorld)) {
        g_last_error = "cannot write a design package";
        d.not_own = true; // built as it is
        return;
    }
    d.ours = ours;
    g_design_in++;
    H->log(M, SK_INFO, "creations: the host's own %s design %08X is %08X here (%u records)", kModelInfo[kind].name,
           b.key.instance, ours, (unsigned)recs.size());
}

std::string design_status()
{
    char b[200];
    unsigned have = 0;
    for (auto& [k, d] : g_designs)
        have += d.ours != 0;
    snprintf(b, sizeof b, "designs fix=%d asks=%u in=%u not_own=%u out=%u timeouts=%u registered=%u\n",
             (int)g_host_designs_fix, g_design_asks, g_design_in, g_design_not_own, g_design_out, g_design_timeouts, have);
    return b;
}

// the hash of the model record (bem) the game answers for a key (tests, BUG-0647: which model a mirror shows)
std::string model_hash(const Key& k)
{
    std::vector<dbpf::Record> recs;
    if (!records_from_game(k, recs))
        return "none";
    for (auto& r : recs)
        if (r.key.type == kBem)
            return hex(fnv(r.data.data(), r.data.size()));
    return "nobem";
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

void send_bundle(int to, uint8_t owner, Kind k, uint32_t id, const std::vector<uint8_t>& data)
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
        g_seen_own[k] = o.key; // BUG-0508
        Bundle b;
        Key hk = host_species(o.key);
        bool world_own = !is_host() && k != kUfo && g_chosen[k] && !g_choice[k].instance;
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
        {
            unsigned models = 0;
            for (auto& r : b.recs)
                models += r.key.type == kBem;
            H->log(M, SK_INFO, "creations: sending our %s %08X: %u record(s), %u model(s)", kInfo[k].name, b.key.instance,
                   (unsigned)b.recs.size(), models); // tests (BUG-0802)
        }
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
bool g_sim_same_addr;

// the key this machine uses for its own creation of a kind
static Key own_key_here(Kind kind)
{
    // the own key itself (My Creations has it, and a saved game keeps a key every launch knows);
    // a guest whose own key is also the host's (another creation with the same id: ids are per
    // profile) uses the shared remapped one: the host's world package shadows the own one
    uint8_t me = C.sess->my_id();
    Key k = mine(kind).key;
    auto host = g_applied.find({0, (uint8_t)kind});
    // BUG-9261: a guest plays its creature under the key every game has for it (applied by share_own as everybody's):
    // the host's nest of it (home_nests.cpp) is a nest of the guest's own species on the guest's game too
    if (g_one_key && kind == kCreature && me != 0 && k.instance) {
        auto self = g_applied.find({me, (uint8_t)kind});
        if (self != g_applied.end() && self->second.orig == k && !self->second.unusable && self->second.key.instance)
            return self->second.key;
    }
    if (me != 0 && host != g_applied.end() && host->second.orig == k)
        k = player_key(me, kind);
    // BUG-0278: an own creation under a key of the host's world (the own editor's "Save New" keeps
    // the key of the host's creation it edited): the host's package answers for that key here, ours
    // is registered under the shared remapped key
    else if (me != 0 && host_world_key(kind, k.instance) && own_option(kind, k))
        if (Key r = player_key(me, kind); r.instance)
            k = r;
    return k;
}

// the own cell: the player's cell model, then the level is rebuilt (the cell hatches again)
static Key g_cell_set;
static bool g_cell_rebuilding;
// BUG-0349: the own cell is applied again after every load of a Cell world: g_cell_set (the key
// already applied) lived until the session end, so after a reload the guest kept the world's cell
// while the others drew its own one (and its next cell editor edited the host's species)
// BUG-0342: a rebuild asked for on the first frames of a freshly loaded Cell world left the guest's
// cell unable to eat for minutes (players; lanes 5/6: 0 of 8 plants it swam into, at the first
// load and after a reload); 5 s later it eats. The own cell waits until the loaded Cell world has
// run 5 s and 150 frames.
static double g_cell_since;
static uint64_t g_cell_since_frame;
static uint32_t g_cell_epoch_seen = 0xFFFFFFFFu;
static void apply_own_cell()
{
    uint8_t* s = cell_S();
    uint8_t* g = modes::cell_game();
    if (!s || !g)
        return;
    if (uint32_t e = game::stage_epoch(modes::kCell); e != g_cell_epoch_seen) {
        g_cell_epoch_seen = e;
        g_cell_since = H->time_seconds();
        g_cell_since_frame = H->frame_index();
        // BUG-0349: a new Cell world: its avatar has the world's model again
        g_cell_set = {};
        g_cell_rebuilding = false;
    }
    if (g_cell_rebuilding) { // wait for the new avatar with the new model, then join the partner
        uint8_t* av = modes::cell_at(*(uint32_t*)(g + 0x411C));
        if (av && *(uint32_t*)(av + 0xFC) == g_cell_set.instance && *(uint8_t*)(g + 0x51D8) == 0) {
            g_cell_rebuilding = false;
            if (C.cell_rebuilt)
                C.cell_rebuilt();
        }
        return;
    }
    if (!g_cell_rebuilding && (H->time_seconds() - g_cell_since < 5.0 || H->frame_index() - g_cell_since_frame < 150))
        return;
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

// BUG-0318: an editor that opens while the avatar's new body (SetSpecies) is still being built stayed
// black for good and could not be left (guest at 6.7 fps, request 0.3 s after "own avatar is now":
// 2 of 2; 60 s later: fine; at 60 fps the build takes a few frames). Editor requests wait until the
// last swap is 5 s and 300 frames old (coop.cpp h_Submit, submit_tick, own_editor_tick).
// The same holds for a new avatar (a world just loaded, a rejoin without a swap: lab 20261030-162204,
// the editor 1.7 s after the load stayed black): its body is built over the first frames as well.
static double g_swap_at = -100;
static uint64_t g_swap_frame = 0;
static double g_new_avatar_at = -100;
static uint64_t g_new_avatar_frame = 0;
bool avatar_swap_settling(double now)
{
    uint64_t f = H->frame_index();
    bool swap = g_swap_at >= 0 && (now - g_swap_at < 5.0 || f - g_swap_frame < 300);
    bool fresh = g_new_avatar_at >= 0 && (now - g_new_avatar_at < 5.0 || f - g_new_avatar_frame < 300);
    return swap || fresh;
}

// BUG-0928: SetSpecies gives the avatar the new species' health maximum and leaves its health as it was: a guest
// that went from its own creature (16 of 16) to the host's (10 at most) had 16 of 10 (a bar of "16" over a body
// with 10, regenerating on top), and the way back left a full creature at 10 of 16. On: the avatar keeps the share
// of its health over the swap (full stays full, half stays half). Off: the old number stays.
static bool g_swap_health_fix = true;
COOP_FIX("creations.swap_health", "BUG-0928", g_swap_health_fix, "creations: the avatar keeps its share of health when its creature is swapped");
static void swap_health(void* av, float hp0, float max0)
{
    float max1 = game::max_health(av);
    if (!g_swap_health_fix || game::is_dead(av) || !(hp0 > 0) || !(max0 > 0) || !(max1 > 0) || max1 == max0)
        return;
    float share = hp0 / max0 > 1.f ? 1.f : hp0 / max0;
    float hp1 = share * max1 < 1.f ? 1.f : share * max1;
    game::set_health(av, hp1);
    H->log(M, SK_INFO, "creations: own avatar's health %.1f/%.1f -> %.1f/%.1f over the swap (BUG-0928)", hp0, max0, hp1, max1);
}

// BUG-1173: SetSpecies on the avatar while a cinematic of the game plays (CRG_BrainUpgradeN: the DNA shared at the
// join crossed a brain level in the 3 s before the swap) left that cinematic playing for good: its camera part ran
// on, its caption never came (no pause, no OK), so Enter did nothing and the game's input lock held the guest until
// the next world load (no clicks, its own editor after the evolution vote never opened, lane 10 probe 2026-11-15:
// F6A4B1E1 for 40 s, 8 Enters; the same DNA after the swap: caption at 14 s, gone on the first Enter). The swap
// now waits until no cinematic plays. Off (`coop.fix creations.swap_after_cinematic off`): it swaps at once (0.7.14).
static bool g_swap_cine_fix = true;
COOP_FIX("creations.swap_after_cinematic", "BUG-1173", g_swap_cine_fix,
         "creations: the guest's own creature is applied only when no cinematic of the game plays (a brain upgrade's caption stayed away)");
static unsigned g_swap_cine_waits; // tests: ticks the swap waited for a cinematic
static bool g_swap_cine_waiting;
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
        g_new_avatar_at = now; // BUG-0318: a new avatar's body is still being built
        g_new_avatar_frame = H->frame_index();
    }
    if (now - g_seen_since < 3.0)
        return;
    if (av == g_swapped_avatar && epoch == g_swapped_epoch && k == g_swapped_key)
        return;
    Key cur = avatar_species_key();
    if (!(cur == k) && g_swap_cine_fix && world::cinematic_playing()) {
        if (!g_swap_cine_waiting)
            H->log(M, SK_INFO, "creations: own avatar %08X waits: cinematic %08X plays (BUG-1173)", k.instance,
                   world::cinematic_id());
        g_swap_cine_waiting = true;
        g_swap_cine_waits++;
        return;
    }
    g_swap_cine_waiting = false;
    if (!(cur == k)) {
        void* prof = game::species_by_key({k.instance, k.type, k.group});
        if (!prof) {
            trace::ev("body", "own unresolved", "own creature %08X is not resolvable here (yet): the avatar stays %08X",
                      k.instance, cur.instance);
            return; // not resolvable (yet)
        }
        int age = *(int*)((uint8_t*)av + 0xB34);
        float hp0 = game::health(av), max0 = game::max_health(av);
        thiscall<bool>(a_SetSpecies, av, prof, age, true);
        swap_health(av, hp0, max0);
        g_swap_at = now;
        g_swap_frame = H->frame_index();
        g_swaps++;
        H->log(M, SK_INFO, "creations: own avatar is now %08X", k.instance);
    }
    g_swapped_avatar = av;
    g_swapped_epoch = epoch;
    g_swapped_key = k;
}

// ---- BUG-0358: the ability bar after the own creature swap ----
// The creature HUD's ability bar (SporeCoop::AbilityBar_ptr, 5 rows x 5 slots of {ability index,
// flag}) is filled by the game (SporeCoop::AbilityBar_Rebuild) when the stage's HUD starts, from the
// avatar it has then: the loaded world's species. SetSpecies gives the avatar our own creature and
// its abilities but leaves the bar as it was. A slot whose index is past the new ability count made
// the game's slot lookup (SporeCoop::AbilityBar_Ability) read past the species' ability array, and
// every key press of the creature stage (CreatureMode OnKeyDown -> AbilityBar_FindByKey) faulted on
// that garbage at 0xD4A397 (lab 20261031-185843 campaign#2: 203 caught faults on one guest; the
// world's species had more abilities than the guest's own creature). The bar is built again
// whenever the avatar's abilities change under the same avatar (our swap), and whenever a slot
// points past the avatar's abilities.
static uintptr_t a_BarPtr, a_BarRebuild;
static void* g_bar_avatar;
static uint32_t g_bar_sig;
bool g_bar_queued;
unsigned g_bar_rebuilds;


uint8_t* bar_table()
{
    if (!a_BarPtr || game::mode_id() != modes::kCreature)
        return nullptr;
    uint8_t* bar = *(uint8_t**)a_BarPtr;
    if (!bar || !skg::readable(bar, 0xD8))
        return nullptr;
    int rows = *(int*)(bar + 0xC8), cols = *(int*)(bar + 0xCC);
    return rows > 0 && rows <= 5 && cols > 0 && cols <= 5 ? bar : nullptr;
}

BarState bar_state(void* av)
{
    BarState st;
    uint8_t* bar = bar_table();
    if (!bar || !av)
        return st;
    st.abilities = vcall<int>(av, 0xB0 / 4);
    int rows = *(int*)(bar + 0xC8), cols = *(int*)(bar + 0xCC);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            int idx = *(int*)(bar + r * 0x28 + c * 8);
            if (idx == -1)
                continue;
            st.used++;
            st.max_index = std::max(st.max_index, idx);
            if (idx < 0 || idx >= st.abilities)
                st.stale++;
        }
    return st;
}

// the avatar's abilities: their count and objects (vt 0xB0 count, vt 0xB4 GetAbility(i))
static uint32_t ability_sig(void* av)
{
    int n = vcall<int>(av, 0xB0 / 4);
    uint32_t h = 2166136261u ^ (uint32_t)n;
    for (int i = 0; i < n && i < 64; i++)
        h = (h ^ (uint32_t)(uintptr_t)vcall<void*>(av, 0xB4 / 4, i)) * 16777619u;
    return h;
}

void bar_rebuild_task(void*)
{
    g_bar_queued = false;
    void* av = game::avatar();
    if (!a_BarRebuild || !av || !bar_table())
        return;
    cdecl_call<void>(a_BarRebuild);
    g_bar_rebuilds++;
    BarState st = bar_state(av);
    H->log(M, SK_INFO, "creations: ability bar built again for the avatar's %d abilities (%d slots, %d past them)",
           st.abilities, st.used, st.stale);
}

static void bar_tick()
{
    if (g_bar_queued || !a_BarRebuild)
        return;
    void* av = game::avatar();
    if (!av || !bar_table())
        return;
    uint32_t sig = ability_sig(av);
    bool rebuild = false;
    if (av != g_bar_avatar) { // a new avatar: the game builds its bar itself
        g_bar_avatar = av;
        g_bar_sig = sig;
    } else if (sig != g_bar_sig) { // the same avatar with other abilities (SetSpecies)
        g_bar_sig = sig;
        rebuild = true;
    }
    if (!rebuild && bar_state(av).stale > 0)
        rebuild = true;
    if (rebuild) {
        g_bar_queued = true;
        H->run_outside_frame(bar_rebuild_task, nullptr);
    }
}

// ---- BUG-9261 (Lineages port phase 3): a partner's creature archived with the host's planet ----
// A partner's own creature lives in the host's save (its nest now, its village and nation later) under the key every
// game has for it (shared_instance). Its records exist only in the package the host registered when the partner sent
// it: after the host's game restarts, a load of that planet while the partner is offline had a species the game could
// not build. archive_guest_model writes the partner's bundle next to the planet (founders::store_guest_model); the hook
// on LoadGameByName registers every archive of the planet before the game reads it. Archive layout: u32 'DGA1', u32 the
// shared key, u8 the owner's network id when archived (only the key check uses it), the bundle as pack() wrote it.
constexpr uint32_t kArchiveMagic = 0x31414744; // DGA1
using LoadPlanet = void(SK_THISCALL*)(void*, const char16_t*);
static LoadPlanet o_LoadPlanet;
static unsigned g_archived, g_restored, g_restore_failed;
static std::string g_archive_last;

// the archive's bundle with its records under the archive's key; false: not a valid archive of a creature this game
// can build (err says why)
static bool decode_founder(const std::vector<uint8_t>& archive, Bundle& b, std::string& err)
{
    net::Reader header(archive.data(), archive.size());
    uint32_t magic = header.u32(), key = header.u32();
    uint8_t owner = header.u8();
    if (!header.ok() || magic != kArchiveMagic || !owner || owner >= 0xFE || header.left() < 14) {
        err = "invalid creature archive header";
        return false;
    }
    std::vector<uint8_t> payload(header.cur(), header.cur() + header.left());
    net::Reader record_header(payload.data(), payload.size());
    record_header.u32();
    record_header.u32();
    record_header.u32();
    uint16_t count = record_header.u16();
    const uint32_t content = fnv(payload.data(), payload.size());
    // (an archive written with creations.one_key off has the owner's salted key)
    if (!count || count > 2u + 512u + 8u || !unpack(payload, b) || !b.key.instance || !is_crt(b.key) ||
        (key != shared_instance(owner, content) && key != salted_instance(owner, content))) {
        err = "invalid or mismatched creature archive";
        H->log(M, SK_WARN, "creations: creature archive of player %u refused: %u record(s), key %08X/%08X/%08X, archive key %08X, "
               "expected %08X", owner, count, b.key.instance, b.key.type, b.key.group, key, shared_instance(owner, content));
        return false;
    }
    bool model = false, meta = false;
    size_t used = 14; // the original key, the record count
    std::vector<dbpf::Record> kept;
    for (auto& rec : b.recs) {
        used += 16 + rec.data.size();
        if (rec.key.group == kPartDescGroup) {
            // a part's description (BUG-0559): counted, not registered (the receiver's own parts answer)
            if (rec.data.empty() || rec.data.size() > (64u << 10)) {
                err = "unexpected part description in the creature archive";
                return false;
            }
            continue;
        }
        // its pictures (BUG-0643: png in the model group and the thumbnail group +1) go with it
        const bool png = rec.key.type == kPng && rec.key.instance == b.key.instance &&
                         (rec.key.group == b.key.group || rec.key.group == b.key.group + 1);
        if (rec.data.empty() || rec.key.instance != b.key.instance || (!png && rec.key.group != b.key.group) ||
            (!png && rec.key.type != kBem && rec.key.type != kMeta) || (rec.key.type == kBem ? model : rec.key.type == kMeta && meta)) {
            err = "unexpected record in the creature archive";
            return false;
        }
        if (rec.key.type == kBem) {
            uint32_t eff[3];
            size_t at[3], len[3];
            if (!model_effects(rec.data, eff, at, len)) {
                err = "invalid creature model header";
                return false;
            }
            for (const auto& ref : model_part_refs(rec.data, kPartGroup))
                if (!part_known(ref.id, kPartGroup)) {
                    err = "the archived creature needs part " + hex(ref.id) + ", which this game does not have";
                    return false;
                }
            for (uint32_t fx : eff)
                if (fx && !paint_known(fx)) {
                    err = "the archived creature needs paint " + hex(fx) + ", which this game does not have";
                    return false;
                }
            model = true;
        } else if (rec.key.type == kMeta)
            meta = true;
        rec.key.instance = key;
        kept.push_back(rec);
    }
    b.recs = std::move(kept);
    if (used != payload.size()) {
        err = "unexpected data after the archived creature's records";
        return false;
    }
    if (!model) {
        err = "the archived creature has no model";
        return false;
    }
    b.key.instance = key;
    return true;
}

bool archive_guest_model(uint8_t guest, const std::string& planet, std::string& err)
{
    auto id = std::make_pair(guest, (uint8_t)kCreature);
    auto a = g_applied.find(id);
    if (a == g_applied.end() || !a->second.key.instance || a->second.unusable) {
        err = "the partner's creature has not arrived yet";
        return false;
    }
    if (a->second.key == a->second.orig)
        return true; // a premade: every game has its records
    if (!o_LoadPlanet) {
        err = "no hook on the planet load: an archive could not be restored";
        return false;
    }
    auto source = g_store.find(id);
    if (source == g_store.end()) {
        err = "the partner creature's records are not here";
        return false;
    }
    net::Writer w;
    w.u32(kArchiveMagic);
    w.u32(a->second.key.instance);
    w.u8(guest);
    w.raw(source->second.data(), source->second.size());
    Bundle b;
    if (!decode_founder(w.data, b, err) || !founders::store_guest_model(planet, b.key.instance, w.data, err))
        return false;
    if (g_archived++ < 50 || g_archive_last != hex(b.key.instance))
        H->log(M, SK_INFO, "creations: player %u's creature %08X archived with the planet (BUG-9261)", guest, b.key.instance);
    g_archive_last = hex(b.key.instance);
    return true;
}

static bool restore_model(const std::string& planet, uint32_t id, std::string& err)
{
    std::vector<uint8_t> archive;
    if (!founders::load_guest_model(planet, id, archive, err))
        return false;
    Bundle b;
    if (!decode_founder(archive, b, err))
        return false;
    if (id != b.key.instance) {
        err = "the archived creature does not match its file name";
        return false;
    }
    std::vector<dbpf::Record> current;
    if (records_from_game(b.key, current)) {
        for (auto& rec : b.recs) {
            auto it = std::find_if(current.begin(), current.end(), [&](const dbpf::Record& r) { return r.key == rec.key; });
            if (it == current.end() || it->data != rec.data) {
                err = "another creature has the archived creature's key here";
                return false;
            }
        }
        H->log(M, SK_INFO, "creations: archived creature %08X is registered already", b.key.instance);
        return true;
    }
    if (!find_paths() ||
        !package_for(g_dir + L"\\founder_" + slotfs::widen(hex(b.key.instance) + "_" + hex(fnv(archive.data(), archive.size()))) +
                         L".package",
                     b.recs, kPrioModel)) {
        err = "could not register the archived creature";
        return false;
    }
    H->log(M, SK_INFO, "creations: archived creature %08X registered before the planet loads (BUG-9261)", b.key.instance);
    return true;
}

bool restore_guest_model(const std::string& planet, std::string& err)
{
    std::vector<uint32_t> ids;
    if (!founders::model_ids(planet, ids, err))
        return false;
    bool all = true;
    for (uint32_t id : ids) {
        std::string one;
        if (restore_model(planet, id, one)) {
            g_restored++;
            continue;
        }
        // one archive that cannot be used does not keep the planet from loading (its nest's creatures are missing)
        g_restore_failed++;
        all = false;
        err += (err.empty() ? "" : "; ") + hex(id) + ": " + one;
    }
    return all;
}

// the game loads a planet by name (the galaxy menu, coop.load, a received world): its archived creatures first
static void SK_THISCALL h_LoadPlanet(void* manager, const char16_t* name)
{
    if (name) {
        std::wstring planet;
        for (size_t i = 0; i < MAX_PATH && name[i]; ++i)
            planet.push_back((wchar_t)name[i]);
        std::string key = world::game_key_for_planet(slotfs::narrow(planet)), err;
        if (!key.empty() && !restore_guest_model(key, err)) {
            H->log(M, SK_WARN, "creations: archived creature(s) of this planet not restored: %s", err.c_str());
            if (C.feed)
                C.feed("A partner's creature archived with this planet could not be restored: " + err);
        }
    }
    o_LoadPlanet(manager, name);
}

std::string archive_status()
{
    char b[300];
    snprintf(b, sizeof b, "archive: hook=%d one_key=%d archived=%u restored=%u failed=%u last=%s\n", (int)(o_LoadPlanet != nullptr),
             (int)g_one_key, g_archived, g_restored, g_restore_failed, g_archive_last.empty() ? "-" : g_archive_last.c_str());
    return b;
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
    a_SpeciesMgr = H->sym("Editors::cSpeciesManager::Get");
    a_GetEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_CellPreload = H->sym("Simulator::Cell::PreloadModel");
    a_BarPtr = H->sym("SporeCoop::AbilityBar_ptr");
    a_BarRebuild = H->sym("SporeCoop::AbilityBar_Rebuild");
    if (uintptr_t load = H->sym("Simulator::cGamePersistenceManager::LoadGameByName")) // BUG-9261
        H->hook_create(M, (void*)load, (void*)h_LoadPlanet, (void**)&o_LoadPlanet);
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
    if (editor && !(g_prev_mode == modes::kEdit || g_prev_mode == modes::kEditor)) {
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
    if (C.sess->role() == net::Role::Client) {
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
        if (g_guest_world_fix && !(g_chosen[g_adopt_kind] && g_choice[g_adopt_kind].instance)) {
            // BUG-0345: the editor's result is the own one in this world (not a choice for every world)
            uint32_t world = modes::current_world();
            if (world && !g_chosen[g_adopt_kind]) {
                world_record_set(g_adopt_kind, world, pick.instance);
                g_last_check = 0; // share now
                H->log(M, SK_INFO, "creations: back from the editor, own %s %08X in this world", kInfo[g_adopt_kind].name, pick.instance);
                return false;
            }
        }
        if (!(pick == mine(g_adopt_kind).key) || !g_chosen[g_adopt_kind]) {
            choose(g_adopt_kind, pick);
            H->log(M, SK_INFO, "creations: back from the editor, own %s %08X", kInfo[g_adopt_kind].name, pick.instance);
        }
        return false;
    }
    g_adopt_until = 0;
    if (is_host()) { // BUG-0344: the editor's result (the avatar's species) is the own one
        g_choice_epoch[g_adopt_kind] = kNoEpoch;
        return false;
    }
    if (!(a == mine(g_adopt_kind).key)) {
        g_opts_read = -100;
        refresh_options(now);
        choose(g_adopt_kind, a);
        H->log(M, SK_INFO, "creations: back from the editor, own %s %08X", kInfo[g_adopt_kind].name, a.instance);
    }
    return false;
}

// BUG-0345: a guest playing its own creation records the world it plays it in and says so, once
static void own_world_tick()
{
    if (!g_guest_world_fix || !C.sess || C.sess->role() != net::Role::Client)
        return;
    int m = game::mode_id();
    Kind k = m == modes::kCell ? kCell : m == modes::kCreature ? kCreature : kKinds;
    if (k == kKinds || (k == kCreature && !game::avatar()) || (k == kCell && !modes::cell_game()))
        return;
    Option o = mine(k);
    uint32_t world = modes::current_world();
    if (!o.key.instance || !world || !own_option(k, o.key))
        return;
    Key here = own_key_here(k), now = k == kCreature ? avatar_species_key() : cell_key();
    if (!here.instance || !(here == now))
        return; // not (yet) applied
    world_record_set(k, world, o.key.instance);
    if (g_own_told.insert({(int)k, world, o.key.instance}).second && C.notice) {
        std::string who = C.name_of ? C.name_of(C.sess->my_id()) : std::string("A guest");
        C.notice(who + " plays its own " + kInfo[k].name + (o.name.empty() ? "" : " '" + o.name + "'"));
    }
}

void tick(double now)
{
    close_tick(now); // BUG-0996 (also after the session: its end drops the world packages)
    if (!C.sess || !C.sess->connected())
        return;
    if (adopt_after_editor(now))
        return;
    own_world_tick();
    if (now - g_last_check > 2.0) {
        g_last_check = now;
        share_own(now);
    }
    alias_tick(); // BUG-0200 (first: a key the avatar is about to play loses its alias before SetSpecies)
    apply_own_avatar();
    player_species_tick(); // BUG-0508
    bar_tick(); // BUG-0358
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
    if (r.ok() && kind == kAskDesign) { // BUG-0647
        if (is_host() && owner == from)
            serve_design(from, id, total);
        return;
    }
    if (r.ok() && kind == kDesignNotOwn) {
        if (!is_host() && from == 0 && total < kMkKinds) {
            g_designs[{(uint8_t)total, id}].not_own = true;
            g_design_not_own++;
        }
        return;
    }
    if ((kind == kWorldSpecies || kind == kOwnSpecies || kind == kOwnDesign) && (is_host() || from != 0))
        return;
    if (!r.ok() || (kind >= kKinds && kind != kWorldSpecies && kind != kOwnSpecies && kind != kOwnDesign) ||
        total > (8u << 20) ||
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
        apply_own_species(data);
        return;
    }
    if (kind == kOwnDesign) { // BUG-0647 (owner = the model kind)
        apply_own_design(owner, data);
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
    g_design_sent.erase(id);
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
    g_own_told.clear(); // BUG-0345: the notice again in the next session
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
    g_world_missing.clear();
    g_own_sent.clear();
    alias_restore(); // BUG-0200 (before the remap goes)
    g_remap.clear(); // (their packages stay registered: the same world may come back)
    g_designs.clear(); // BUG-0647 (the next host's designs are other creations under the same ids)
    g_design_sent.clear();
    g_unmap.clear();
    g_store.clear();
    g_rx.clear();
    for (int k = 0; k < kKinds; k++) // BUG-0508: the session's host and its species
        g_follow[k] = g_seen_own[k] = Key{};
    g_ps_from = g_ps_to = Key{};
    for (auto& s : g_sent_id)
        s = 0;
    // registered packages stay (cheap); keys of other players are forgotten
    for (auto it = g_applied.begin(); it != g_applied.end();)
        it = g_applied.erase(it);
    g_swapped_avatar = nullptr;
    g_cell_set = {};
}
LIFECYCLE_HOOK("creations.session", lifecycle::kSessionEnd, -46, [](const lifecycle::Ctx&) { on_session_end(); }, true); // C2 (BUG-1141): end_session

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
    g_choice_epoch[k] = choice_epoch_now(k);
    if (g_choice_world_fix && k != kUfo && key.instance && C.sess && C.sess->role() == net::Role::Client)
        world_record_set(k, modes::current_world(), key.instance); // BUG-0638: the world it is chosen in
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
    if (k != kUfo && one_creature() && C.sess && C.sess->role() == net::Role::Client) {
        if (C.feed) // F5 (BUG-0769): the guest's creation arrows are hidden; a click that comes anyway changes nothing
            C.feed("everybody plays the host's creature (session setting)");
        return;
    }
    auto opts = options(k);
    // BUG-0278: "the world's own" (the host's creature) is one of the choices: a guest can go back
    // to it from a creature of its own
    if (C.sess && C.sess->role() == net::Role::Client && k != kUfo && !opts.empty()) {
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
    char b[400];
    snprintf(b, sizeof b,
             "creations: in=%u out=%u registered=%u avatar_swaps=%u waits_on_disk=%u unusable=%u world_swaps=%u "
             "species asked=%u got=%u sent=%u missing=%u old_sent=%u own_out=%u own_in=%u rx_dropped=%u adopt_skipped=%u parts_refused=%u world_missing=%u paints_fb=%u unknown_logged=%u cc=%d%s%s\n",
             g_bundles_in, g_bundles_out, g_registered, g_swaps, g_waits_on_disk, g_unusable, g_world_swaps,
             g_species_asked, g_species_got, g_species_sent, g_species_missing, g_old_served, g_own_out, g_own_in, g_rx_dropped, g_adopt_skipped, g_parts_refused, (unsigned)g_world_missing.size(), g_paints_fallback, g_unknown_logged, g_have_cc, g_last_error.empty() ? "" : " error=", g_last_error.c_str());
    s += b;
    snprintf(b, sizeof b, "  swap waits for a cinematic: %u ticks (BUG-1173)\n", g_swap_cine_waits);
    s += b;
    snprintf(b, sizeof b, "  packages: open=%zu closing=%zu closed=%u kept=%u renamed=%u close_fix=%d\n", g_pkgs.size(),
             g_closing.size(), g_pkgs_closed, g_pkgs_close_kept, g_pkg_renames, (int)g_pkg_close_fix); // BUG-0996
    s += b;
    snprintf(b, sizeof b, "  meta_only: fix=%d filled=%u waits=%u refused=%u\n", (int)g_meta_only_fix, g_meta_filled, g_meta_waits,
             g_meta_refused); // BUG-0802
    s += b;
    snprintf(b, sizeof b, "  creature mode: %s (host's creation answers=%u)\n", one_creature() ? "host" : "own", g_one_mine);
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
    if (uint32_t* ps = player_species_slot()) { // BUG-0508
        snprintf(b, sizeof b, "  world player species %08X (set in place %u, retries %u, lost %u)\n", ps[0], g_ps_set, g_ps_retries,
                 g_ps_lost);
        s += b;
    }
    for (int k = 0; k < kKinds; k++)
        if (g_follow[k].instance) { // BUG-0508
            snprintf(b, sizeof b, "  follow %s: %08X%s (follows %u)\n", kInfo[k].name, g_follow[k].instance,
                     g_follow_epoch[k] == choice_epoch_now(k) ? "" : " (another world)", g_follows);
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


// support trace (trace.h): the own creation, where the choice came from, what the game has now
std::string trace_line(Kind k)
{
    bool guest = C.sess && C.sess->role() == net::Role::Client;
    Option o = mine(k);
    bool other_world = guest && g_choice_world_fix && g_chosen[k] && k != kUfo && g_choice[k].instance && !choice_here(k);
    const char* origin = other_world ? "the world's default (the choices.ini creation is of another world, BUG-0638)"
                         : g_chosen[k] && k != kUfo ? (g_choice[k].instance ? "choices.ini" : "choices.ini: the world's own")
                         : k == kUfo                ? "own empire"
                         : guest && !g_opts[k].empty() && g_guest_world_fix ? (world_record(k, modes::current_world()) ? "this world's record (worlds.ini)" : "the world's species (no own one played in this world)")
                         : guest && !g_opts[k].empty() ? "newest own creation"
                                                       : "the world's (host's) own";
    Key here = C.sess && C.sess->connected() ? own_key_here(k) : o.key;
    Key now = k == kCreature ? avatar_species_key() : k == kCell ? cell_key() : ufo_key();
    auto host = g_applied.find({0, (uint8_t)k});
    char b[320];
    snprintf(b, sizeof b, "%s: own=%s '%s' from %s, used here as %s, the game has %s%s, host's %s, %u own creations, swaps=%u",
             kInfo[k].name, trace::hex(o.key.instance).c_str(), o.name.c_str(), origin, trace::hex(here.instance).c_str(),
             trace::hex(now.instance).c_str(), here.instance && now.instance && !(here == now) ? " (DIFFERENT)" : "",
             trace::hex(host != g_applied.end() ? host->second.key.instance : 0u).c_str(), (unsigned)g_opts[k].size(), g_swaps);
    return b;
}

} // namespace creations
