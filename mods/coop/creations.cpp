#include "creations.h"
#include "cellsync.h"
#include "creature_hp.h"
#include "squadron.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <set>

#include "game.h"
#include "founders.h"
#include "world.h"
#include "tribe.h"
#include "../../updater/sha256.h"
#include "modes.h"
#include "slotfs.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "content_packs.h"
#include "trace.h"

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
// BUG-0469 review: our copy of a model the game cannot build is registered under the model's OWN key,
// above every package of the game (and above the host's world packages at 2000), so no key of ours
// ever reaches the game or a save
constexpr int kPrioModel = 3000;
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
static Key g_land_result;
static Key g_campaign_creature;
static uint32_t g_campaign_place;
static std::wstring g_campaign_entry;
// BUG-0344: on the host a creature or cell choice holds only in the stage world it was made in (a
// choice from choices.ini of another world swapped the host's just evolved species for an old
// creature, and the shared save then named it: BUG-0341), and an editor's result is the host's own
static bool g_host_world_fix = true;
COOP_FIX("creations.host_world_species", "BUG-0344", g_host_world_fix, "creations: the host plays its world's species unless it chose another creation in this stage world; an editor's result is its own");
constexpr uint32_t kNoEpoch = 0xFFFFFFFFu;
static uint32_t g_choice_epoch[kKinds] = {kNoEpoch, kNoEpoch, kNoEpoch}; // stage world of the choice (host)
static uint32_t choice_epoch_now(int k)
{
    return game::stage_epoch(k == 1 ? modes::kCell : modes::kCreature);
}
static bool g_chosen[kKinds]; // the player chose (even "none")
static bool g_outfit_ack = true;
COOP_FIX("creations.outfit_ack", "BUG-9044", g_outfit_ack,
         "creations: retain the previous Tribal choice until the host accepts an outfit");

// One local editor can be pending. Journal its previous choice before changing
// choices.ini, so a process exit has the same rollback as a clean disconnect.
// C:hhhhhhhh-wwwwwwww:kkkkkkkk is a campaign choice; E is explicit, U unchosen.
static bool restore_pending_outfit()
{
    if (g_dir.empty()) return true;
    auto path = g_dir + L"\\choices.ini";
    wchar_t raw[64] = {};
    GetPrivateProfileStringW(L"outfit_pending", L"restore", L"", raw, 64, path.c_str());
    std::wstring value(raw);
    if (value.empty()) return true;
    bool campaign = value.size() == 28 && value[0] == L'C' && value[1] == L':' &&
                    value[10] == L'-' && value[19] == L':';
    bool explicit_choice = value.size() == 10 && (value[0] == L'E' || value[0] == L'U') && value[1] == L':';
    if (!campaign && !explicit_choice) return false;
    for (size_t i = 2; i < value.size(); ++i) {
        if (campaign && (i == 10 || i == 19)) continue;
        if (!((value[i] >= L'0' && value[i] <= L'9') || (value[i] >= L'A' && value[i] <= L'F'))) return false;
    }
    auto entry = campaign ? value.substr(2, 17) : L"creature";
    const wchar_t* key = raw + (campaign ? 20 : 2);
    if (!WritePrivateProfileStringW(campaign ? L"landfall" : L"creations", entry.c_str(), value[0] == L'U' ? nullptr : key, path.c_str())) return false;
    // The documented flush return is zero, including a successful cache flush.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    uint32_t id = (uint32_t)wcstoul(key, nullptr, 16);
    Key previous{id, kInfo[kCreature].type, kInfo[kCreature].group};
    if (campaign && entry == g_campaign_entry) g_campaign_creature = previous;
    if (!campaign) { g_chosen[kCreature] = value[0] != L'U'; g_choice[kCreature] = previous; }
    return WritePrivateProfileStringW(L"outfit_pending", L"restore", nullptr, path.c_str()) != FALSE;
}

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
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0 || sz.QuadPart >= (256 << 20)) {
        CloseHandle(f); return false;
    }
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
    if (!restore_pending_outfit()) {
        H->log(M, SK_ERROR, "creations: pending outfit choice could not be restored");
        g_es_path.clear();
        return false;
    }
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

bool key_known(const Key& k) { return key_exists(k); }

// ---- BUG-0341: creatures with parts of a pack this game does not have ----
// A Steam player without the "Creepy & Cute" parts pack crashed at 0x441F3B (a model bake with a
// null property list) on every load of a host world whose creature had one of its parts, and on the
// host's puppet built from it. Such a creation is not registered here (its puppet falls back as for
// an unknown creation), and a world that needs one is not loaded (world.cpp asks
// world_missing_parts): the player gets a message instead of a crash.
static bool g_parts_fix = true;
COOP_FIX("creations.missing_parts", "BUG-0341", g_parts_fix, "creations: a creature with Creepy & Cute parts is not built on a game without that pack, and a world that needs one is not loaded (0x441F3B)");
// BUG-0452: the same for every part id of every creature, cell and UFO model that reaches this game,
// not only the hard-coded Creepy & Cute list. A species of the host's world with a part of another
// parts pack (or of a mod) crashed a guest at the same 0x41F3B: the bake found no property list for
// the id. Each such id is logged; a part this game lacks is shown as a base game part (Creepy &
// Cute's table first) or, when no substitute of the right length is known, the creation is refused
// and the game's fallback shows instead. Off (`coop.fix creations.unknown_parts off`): 0.7.3.4, only
// the Creepy & Cute list is checked.
static bool g_unknown_fix = true;
COOP_FIX("creations.unknown_parts", "BUG-0452", g_unknown_fix, "creations: every part id of a received creature, cell and UFO model is checked against this game, not only Creepy & Cute's (0x41F3B)");
// BUG-0469: the same check for every model the mod makes this game build from a key it did not make
// itself (a civ mirror vehicle, a building of a city, a host ship): the key AND every part / paint it
// names, with a substitute in a copy of the model's records or a refusal (docs/re/creations.md
// #creations-content-all-kinds). Off (`coop.fix content.all_kinds off`): 0.7.3.5, these call sites
// pass the key as they get it.
static bool g_all_kinds_fix = true;
COOP_FIX("content.all_kinds", "BUG-0469", g_all_kinds_fix, "civ, space: every model the mod makes the game build (a mirror vehicle, a building, a host ship) is checked with its parts and paints, not only its key");
// BUG-0469 review: the substitute copy of a model was registered under an instance of our own
// (0x7D......, the fix's first version). Such a key reaches the guest's save (mirror vehicles are not
// stripped) and no package answers it after a restart (or without the mod): the game then cannot
// build the saved object. The copy now goes under the model's OWN key, in a package above every
// package of the game, so no key of ours reaches the game at all (kPrioModel, model_shadow_answers).
// Off (`coop.fix content.model_key off`): the first version, an instance of our own (the lab's run
// that shows the saved key).
static bool g_model_key_fix = true;
COOP_FIX("content.model_key", "BUG-0469", g_model_key_fix, "creations: the copy of a model this game cannot build is registered under that model's own key, never a key of the mod's (a save must not name a key only this mod has)");
constexpr uint32_t kPartProp = 0x00B1B104, kPartGroup = 0x40626000;
constexpr uint32_t kCellPartGroup = 0x40616000, kUfoPartGroup = 0x40646000; // parts of a cell / UFO model
constexpr uint32_t kEffectProp = 0x497767B9, kEffectGroup = 0xEA5118B1; // a base game skin effect
// a part entry's slot and parent are small indexes (-1: none); an entry whose fields are not is not
// a part reference (the group bytes occur in other data too: a base game model has such entries, and
// the fixture Kirgle names 05D58995 that no package holds and builds fine, docs/re/creations.md)
constexpr uint32_t kMaxPartSlot = 0x10000;
static int g_have_cc = -1; // this game has the Creepy & Cute parts (-1: not asked yet)
static unsigned g_parts_refused, g_parts_fallback, g_paints_fallback, g_unknown_logged;
static std::map<uint32_t, std::string> g_world_missing; // the host's world creatures this game cannot build
// key_exists is an RM lookup: the packs are mounted for the session, so an id's answer does not change
static std::map<std::pair<uint32_t, uint32_t>, bool> g_key_cache; // part id + group -> this game has it
static std::map<uint32_t, bool> g_effect_cache;                  // paint effect id -> this game has it

// BUG-0469: the models the mod builds by key: type and group of the creation key
// (docs/re/creations.md table), the part group its bem record names, and whether the three paint
// effect dwords after byte 26 of the bem header are skin effects (checked in Data/Spore_Content.package:
// creature, UFO, vehicle and building models name base game paint effects there; a cell model names
// cell paint ids that no package holds as an effect record, its header is not the creature one:
// docs/re/creations.md#creations-content-all-kinds)
struct ModelInfo {
    uint8_t kind;
    const char* name;
    uint32_t type, group, parts;
    bool paints;
};
static const ModelInfo kModelInfo[kMkKinds] = {
    {kMkCreature, "creature", 0x2B978C46, 0x40626200, kPartGroup, true},
    {kMkCell, "cell", 0x3D97A8E4, 0x40616200, kCellPartGroup, false},
    {kMkUfo, "ufo", 0x476A98C7, 0x40656200, kUfoPartGroup, true},
    {kMkVehicle, "vehicle", 0x24682294, 0x40646200, kUfoPartGroup, true},
    {kMkBuilding, "building", 0x2399BE55, 0x40636200, 0x40636000, true},
};

static ModelKind mk_of(Kind k) { return k == kCell ? kMkCell : k == kUfo ? kMkUfo : kMkCreature; }

// the part property group of a model kind
static uint32_t part_group(ModelKind k) { return kModelInfo[k].parts; }
// whether the kind's model header names the three paint effect dwords (creature and the kinds whose
// models are built from the same bem template)
static bool kind_paints(ModelKind k) { return kModelInfo[k].paints; }

// ---- part ids of a model (all kinds: the same group+instance key layout) ----
struct PartRef {
    uint32_t id;
    size_t at;    // raw offset of the first id byte in the model
    size_t len;   // raw length of the id (0xFE is written FE 00)
    bool safe;    // rewriting the id cannot change the slot that follows it
};

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
static std::vector<PartRef> model_part_refs(const std::vector<uint8_t>& m, uint32_t group)
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

bool campaign_content(const char* version, std::string& profile, std::string& error)
{
    if (!a_RmPtr || !*(void**)a_RmPtr) {
        error = "Spore's resources are still loading. Try again from the galaxy menu.";
        return false;
    }
    size_t present = 0;
    for (uint32_t part : kCreepyCuteParts)
        present += key_exists({part, kPartProp, kPartGroup});
    if (present && present != std::size(kCreepyCuteParts)) {
        error = "Only some Creepy & Cute parts loaded. Verify the parts pack and restart Spore.";
        return false;
    }
    profile = std::string(version) + (present ? " / Creepy & Cute" : " / base parts");
    g_have_cc = present ? 1 : 0;
    H->log(M, SK_INFO, "campaign profile: %s (%u pack parts loaded)", profile.c_str(), (unsigned)present);
    return true;
}

// the part ids a creature model (bem record) names, for the tests and the support trace
std::vector<uint32_t> model_parts(const std::vector<uint8_t>& m)
{
    std::vector<uint32_t> out;
    for (auto& ref : model_part_refs(m, kPartGroup))
        out.push_back(ref.id);
    return out;
}

static bool is_cc_part(uint32_t id)
{
    return std::binary_search(std::begin(kCreepyCuteParts), std::end(kCreepyCuteParts), id);
}

// whether this game has the part (cached: key_exists is an RM lookup)
static bool part_known(uint32_t id, uint32_t group)
{
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

// the unbuildable part ids of these records, each once (with the generic check off only the Creepy &
// Cute ids count, as 0.7.3.4); 0 with the fixes off
static std::vector<uint32_t> unknown_part_ids(const std::vector<dbpf::Record>& recs, ModelKind k)
{
    std::vector<uint32_t> out;
    if (!g_parts_fix)
        return out;
    uint32_t group = part_group(k);
    bool cc = k == kMkCreature && have_creepy_cute();
    for (auto& r : recs) {
        if (r.key.type != kBem)
            continue;
        for (auto& ref : model_part_refs(r.data, group)) {
            bool unknown = g_unknown_fix ? !part_known(ref.id, group)
                                         : (k == kMkCreature && is_cc_part(ref.id) && !cc);
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
// the refusal above. Old (`coop.fix creations.part_substitute off`): the refusal for every pack part.
static bool g_subst_fix = true;
COOP_FIX("creations.part_substitute", "BUG-0341", g_subst_fix, "creations: on a game without Creepy & Cute a creature's pack parts are shown as similar base game parts instead of the creature being refused");
static unsigned g_parts_substituted;
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
static unsigned substitute_parts(std::vector<dbpf::Record>& recs, ModelKind k, const std::string& whose)
{
    if (!g_parts_fix || !g_subst_fix)
        return 0;
    uint32_t group = part_group(k);
    bool cc = k == kMkCreature && have_creepy_cute();
    unsigned n = 0, fb = 0;
    for (auto& r : recs) {
        if (r.key.type != kBem)
            continue;
        auto& m = r.data;
        for (auto& ref : model_part_refs(m, group)) {
            if (!ref.safe)
                continue; // a run after the id repeats its last byte: rewriting it would change the slot
            bool unknown = g_unknown_fix ? !part_known(ref.id, group)
                                         : (k == kMkCreature && is_cc_part(ref.id) && !cc);
            if (!unknown)
                continue;
            uint32_t sub = 0;
            if (k == kMkCreature) { // the Creepy & Cute table first (creature parts)
                uint32_t s = substitute_of(ref.id);
                if (s && part_known(s, kPartGroup))
                    sub = s;
            }
            int fe = 0;
            for (int i = 0; i < 4; i++)
                fe += (((ref.id >> (8 * i)) & 0xFF) == 0xFE) ? 1 : 0;
            if (!sub && g_unknown_fix) {
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
// Old (`coop.fix creations.paint_substitute off`): the pack's effect ids stay (0.7.3.3).
static bool g_paint_fix = true;
COOP_FIX("creations.paint_substitute", "BUG-0369", g_paint_fix, "creations: on a game without Creepy & Cute a creature's pack paints are shown as similar base game paints");
static unsigned g_paints_substituted;
constexpr size_t kBemEffects = 26; // the unpacked header: base, coat, detail effect ids from here

static uint32_t paint_substitute_of(uint32_t id)
{
    auto it = std::lower_bound(std::begin(kCreepyCutePaintSubstitutes), std::end(kCreepyCutePaintSubstitutes), id,
                               [](const PartSubstitute& s, uint32_t v) { return s.pack < v; });
    return it != std::end(kCreepyCutePaintSubstitutes) && it->pack == id ? it->base : 0;
}

// the three skin effect ids of a model record and where each is written (packed offset and length);
// false when the header is too short or an id is not written byte by byte (a run)
static bool model_effects(const std::vector<uint8_t>& m, uint32_t eff[3], size_t at[3], size_t len[3])
{
    size_t dec = 0, p = 0;
    size_t first[12] = {}, last[12] = {};
    uint8_t val[12] = {};
    while (p < m.size() && dec < kBemEffects + 12) {
        size_t start = p;
        uint8_t b = m[p];
        if (b == 0xFE) {
            if (p + 1 >= m.size())
                return false;
            if (m[p + 1] != 0) { // a run: n more of the byte before
                size_t n = m[p + 1];
                if (dec + n > kBemEffects && dec < kBemEffects + 12)
                    return false;
                dec += n;
                p += 2;
                continue;
            }
            p += 2;
        } else
            p++;
        if (dec >= kBemEffects) {
            first[dec - kBemEffects] = start;
            last[dec - kBemEffects] = p;
            val[dec - kBemEffects] = b;
        }
        dec++;
    }
    if (dec < kBemEffects + 12)
        return false;
    for (int k = 0; k < 3; k++) {
        eff[k] = val[4 * k] | val[4 * k + 1] << 8 | val[4 * k + 2] << 16 | (uint32_t)val[4 * k + 3] << 24;
        at[k] = first[4 * k];
        len[k] = last[4 * k + 3] - first[4 * k];
    }
    return true;
}

static bool is_cc_paint(uint32_t id) { return paint_substitute_of(id) != 0; }

static bool g_archive_runs = true;
static bool g_archive_cell_paints = true;
COOP_FIX("creations.archive_cell_paints", "BUG-9042", g_archive_cell_paints,
         "creations: archive native landfall creatures retaining a stock Cell base paint");
COOP_FIX("creations.archive_runs", "BUG-9041", g_archive_runs,
         "creations: validate saved founder paint IDs even when their header uses byte runs");
static bool archive_model_effects(const std::vector<uint8_t>& model, uint32_t effects[3])
{
    if (!g_archive_runs) {
        size_t at[3], lengths[3];
        return model_effects(model, effects, at, lengths);
    }
    // Archive validation only reads paint IDs. The substitution parser must
    // still refuse compressed IDs because its offsets are used for rewriting.
    uint8_t header[kBemEffects + 12], last = 0;
    size_t cursor = 0;
    if (!decode_run(model, cursor, last, header, sizeof header) ||
        header[0] != 0xD3 || header[1] != 0x7E || header[2] != 0x65 || header[3] != 0x48) return false;
    for (int i = 0; i < 3; ++i) {
        const auto* p = header + kBemEffects + 4*i;
        effects[i] = uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
    }
    return true;
}

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
static bool paint_known(uint32_t id)
{
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
    if (!g_parts_fix || !g_paint_fix)
        return 0;
    bool cc = have_creepy_cute();
    if (cc && !g_unknown_fix)
        return 0; // 0.7.3.3: nothing to substitute when the pack is here
    unsigned n = 0, fb = 0;
    for (auto& r : recs) {
        uint32_t eff[3];
        size_t at[3], len[3];
        if (r.key.type != kBem || !model_effects(r.data, eff, at, len))
            continue;
        for (int k = 0; k < 3; k++) {
            if (!eff[k])
                continue;
            bool unknown = g_unknown_fix ? !paint_known(eff[k]) : (is_cc_paint(eff[k]) && !cc);
            if (!unknown)
                continue;
            uint32_t sub = paint_substitute_of(eff[k]);
            if (sub && !paint_known(sub))
                sub = 0;
            if (!sub && g_unknown_fix) {
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
    if (!g_parts_fix)
        return out;
    bool cc = have_creepy_cute();
    for (auto& r : recs) {
        uint32_t eff[3];
        size_t at[3], len[3];
        if (r.key.type != kBem || !model_effects(r.data, eff, at, len))
            continue;
        for (int i = 0; i < 3; i++) {
            if (!eff[i])
                continue;
            bool unknown = g_unknown_fix ? !paint_known(eff[i]) : (is_cc_paint(eff[i]) && !cc);
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
static unsigned ensure_content(std::vector<dbpf::Record>& recs, ModelKind k, const std::string& whose)
{
    if (!g_parts_fix)
        return 0;
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
    unsigned fb0 = g_parts_fallback;
    unsigned n = substitute_parts(recs, k, whose);
    if (kind_paints(k))
        n += substitute_paints(recs, whose);
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
    if (!g_parts_fix || g_world_missing.empty())
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

static bool host_world_key(Kind k, uint32_t inst); // a creation of the host's world (BUG-0278)
static bool records_from_game(const Key& k, std::vector<dbpf::Record>& out); // its records, any registered package
static bool own_option(Kind k, const Key& key)
{
    for (auto& o : g_opts[k])
        if (o.key == key)
            return true;
    return false;
}

// Automatic founders belong to this host's planet. A player-picked F9 default
// retains its usual meaning, including an explicit choice to use the world's body.
static void refresh_campaign_creature()
{
    uint32_t mode = game::mode_id();
    if (C.sess->role() != net::Role::Client ||
        (mode != modes::kCell && mode != modes::kCreature && mode != modes::kTribe &&
         mode != modes::kCiv && mode != modes::kSpace)) return;
    uint32_t place = modes::current_world();
    if (!place || place == g_campaign_place || !modes::current_world_name().ends_with(".spo") || !find_paths()) return;
    g_campaign_place = place;
    auto host = C.sess->name_of(0);
    wchar_t entry[24];
    swprintf(entry, 24, L"%08X-%08X", fnv(host.data(), host.size()), place);
    g_campaign_entry = entry;
    wchar_t value[16];
    GetPrivateProfileStringW(L"landfall", entry, L"", value, 16, (g_dir + L"\\choices.ini").c_str());
    uint32_t id = (uint32_t)wcstoul(value, nullptr, 16);
    Key bem{id, kBem, kInfo[kCreature].group};
    g_campaign_creature = id && key_exists(bem) ? Key{id, kInfo[kCreature].type, bem.group} : Key{};
}

static void choose_campaign_creature(const Key& key)
{
    g_campaign_creature = key;
    wchar_t value[16];
    swprintf(value, 16, L"%08X", key.instance);
    if (g_campaign_entry.empty() || !WritePrivateProfileStringW(L"landfall", g_campaign_entry.c_str(), value,
                                                               (g_dir + L"\\choices.ini").c_str())) {
        if (C.feed) C.feed("Your land creature is active. Its campaign choice could not be saved.");
    }
    g_last_check = 0;
}

Option mine(Kind k)
{
    bool guest = C.sess && C.sess->role() == net::Role::Client;
    // BUG-0278: a choice of a creation of the host's world that is not one of ours (taken over from
    // an editor by 0.7.3.0) is not a choice; "the world's own" follows the host's creature
    bool chosen = g_chosen[k];
    if (g_host_world_fix && chosen && !guest && k != kUfo && g_choice[k].instance && g_choice_epoch[k] != choice_epoch_now(k))
        chosen = false; // BUG-0344: a choice of another world (or before this stage): the world's own
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
    if (guest && k == kCreature && g_campaign_creature.instance)
        return named(g_campaign_creature);
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

static uint32_t shared_instance(uint8_t owner, uint32_t id)
{
    uint8_t seed[5] = {owner, (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16), (uint8_t)(id >> 24)};
    return 0x7C000000u | (fnv(seed, sizeof seed) & 0x00FFFFFFu);
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

static bool g_personal_ship = true;
COOP_FIX("creations.personal_ship", "BUG-9071", g_personal_ship,
         "Space: keep a founder's own launch design when local spacecraft IDs collide");
static std::wstring g_launch_entry, g_pending_launch;
static std::string g_launch_path;
static uint32_t g_launch_model, g_launch_captain;
static bool g_space_restore_due, g_space_restore_queued;
static uint64_t g_space_restore_epoch;
static bool g_editing_captain;
static bool g_captain_outfit_fix = true;
COOP_FIX("creations.captain_outfit", "BUG-9071", g_captain_outfit_fix,
         "Space: accept the personal captain's inherited body in its native outfitter");
static Key g_captain_source;
static uintptr_t a_CaptainSourceReturn;
static bool g_captain_flush_pending, g_captain_flush_queued;
static double g_captain_flush_after;
static unsigned g_captain_flush_tries;
using SpeciesManagerKey = const Key*(SK_THISCALL*)(void*);
static SpeciesManagerKey o_SpeciesManagerKey;
static SpeciesManagerKey o_EmpireCaptainKey;
static uintptr_t a_FirstFlightCaptainCheck;
static std::string g_restart_path;
static Key g_restart_captain;

// Native first flight checks for an existing captain immediately before opening
// its naming dialog. Restore the already owned captain at that call only.
static const Key* SK_THISCALL h_EmpireCaptainKey(void* empire)
{
    if (g_restart_captain.instance && (uintptr_t)__builtin_return_address(0) == a_FirstFlightCaptainCheck &&
        C.sess && C.sess->role() == net::Role::Client && game::mode_id() == modes::kSpace &&
        world::current_game_key() == g_restart_path) {
        auto set = H->sym("SporeCoop::Empire_SetCaptainKey");
        if (set) {
            thiscall<void>(set,empire,&g_restart_captain);
            H->log(M,SK_INFO,"creations: restored captain %08X before first flight",g_restart_captain.instance);
            g_restart_captain = {};
            g_restart_path.clear();
        }
    }
    return o_EmpireCaptainKey(empire);
}

static bool g_founder_captain = true;
COOP_FIX("creations.founder_captain", "BUG-9071", g_founder_captain,
         "Space: create the guest captain from its own founding species");

static const Key* SK_THISCALL h_SpeciesManagerKey(void* manager)
{
    if (!g_founder_captain || (uintptr_t)__builtin_return_address(0) != a_CaptainSourceReturn ||
        !C.sess || !C.sess->connected() || game::mode_id() != modes::kSpace)
        return o_SpeciesManagerKey(manager);
    g_captain_flush_pending = true;
    g_captain_flush_tries = 0;
    g_captain_flush_after = H->time_seconds() + .5;
    if (g_captain_source.instance && !is_host() && world::current_game_key() == g_launch_path) {
        H->log(M, SK_INFO, "creations: native first captain uses founder model %08X", g_captain_source.instance);
        return &g_captain_source;
    }
    return o_SpeciesManagerKey(manager);
}

static std::wstring launch_entry(const std::string& planet, const founders::Roster& pair)
{
    wchar_t key[64];
    swprintf(key,64,L"%08X-%08X-%08X-%08X-%08X",fnv(planet.data(),planet.size()),
             pair.home().species,pair.guest().species,pair.home().faction,pair.guest().faction);
    return key;
}

bool needs_launch_checkpoint(const std::string& planet, const founders::Roster& pair)
{
    if (!find_paths()) return true; // an unreadable preference cannot establish that training is finished
    const auto key = launch_entry(planet, pair);
    const auto ini = g_dir + L"\\choices.ini";
    auto state = GetPrivateProfileIntW(L"space-training",key.c_str(),0,ini.c_str());
    if (state == 2) return false;
    if (!state) {
        // A profile from before training was tracked can already own its ship.
        wchar_t model[16]{};
        GetPrivateProfileStringW(L"space-launch",key.c_str(),L"",model,16,ini.c_str());
        if (wcstoul(model,nullptr,16)) return false;
    }
    return true;
}

// A campaign's saved founders identify it even when the received slot changes.
// Re-read the binding when the slot changes, not on each resource lookup.
static void load_launch_entry()
{
    auto path = world::current_game_key();
    if (path == g_launch_path && !g_launch_entry.empty()) return;
    g_launch_path = path;
    g_launch_entry.clear();
    g_launch_model = 0;
    g_launch_captain = 0;
    founders::Roster pair;
    if (path.empty() || !find_paths() || !founders::read(path,pair,founders::Stage::Civilization)) return;
    g_launch_entry = launch_entry(modes::current_world_name(), pair);
    const auto* key = g_launch_entry.c_str();
    g_space_restore_due = true;
    wchar_t value[16]{};
    GetPrivateProfileStringW(L"space-launch",key,L"",value,16,(g_dir+L"\\choices.ini").c_str());
    g_launch_model = (uint32_t)wcstoul(value,nullptr,16);
    GetPrivateProfileStringW(L"space-captain",key,L"",value,16,(g_dir+L"\\choices.ini").c_str());
    g_launch_captain = (uint32_t)wcstoul(value,nullptr,16);
}

bool personal_captain_outfit(uint32_t instance)
{
    return g_captain_outfit_fix && instance && instance == g_launch_captain &&
           C.sess && C.sess->connected() && !g_launch_entry.empty();
}

void begin_captain_outfit()
{
    g_editing_captain = true;
}

void begin_personal_launch()
{
    load_launch_entry();
    g_captain_source = {};
    if (g_launch_entry.empty()) return;
    founders::Outfits outfits;
    std::string error;
    if (founders::read_outfits(g_launch_path,outfits,error)) {
        const auto key = Key{outfits.current(1),kInfo[kCreature].type,kInfo[kCreature].group};
        auto it = g_applied.find({C.sess->my_id(), kCreature});
        // The host may know the founder by a remapped network key. The native
        // captain creator needs the editable creation in this player's library.
        if (it != g_applied.end() && (it->second.key == key || it->second.orig == key) &&
            own_option(kCreature,it->second.orig)) g_captain_source = it->second.orig;
        else if (own_option(kCreature,key)) g_captain_source = key;
    }
    g_pending_launch = g_launch_entry;
    WritePrivateProfileStringW(L"space-training",g_launch_entry.c_str(),L"1",
                               (g_dir+L"\\choices.ini").c_str());
    world::begin_local_space_intro();
}

bool needs_local_space_intro(const std::string& received_planet, bool civilization)
{
    if (!C.sess || C.sess->role() != net::Role::Client) return false;
    load_launch_entry();
    if (g_launch_entry.empty()) return false;
    founders::Roster local, incoming;
    if (!founders::read(g_launch_path,local,founders::Stage::Civilization) ||
        !founders::read(received_planet,incoming,founders::Stage::Civilization) || !(incoming == local)) return false;
    const auto state = GetPrivateProfileIntW(L"space-training",g_launch_entry.c_str(),0,
                                             (g_dir+L"\\choices.ini").c_str());
    return state == 1 || (civilization && state == 0);
}

bool schedule_training_restart()
{
    if (!C.sess || C.sess->role() != net::Role::Client || game::mode_id() != modes::kCiv) return false;
    load_launch_entry();
    if (g_launch_entry.empty() || !g_launch_model || !g_launch_captain || !o_EmpireCaptainKey) return false;
    if (GetPrivateProfileIntW(L"space-training",g_launch_entry.c_str(),0,(g_dir+L"\\choices.ini").c_str()) != 1)
        return false;
    g_restart_path = g_launch_path;
    return true;
}

void finish_local_space_intro()
{
    if (!C.sess || C.sess->role() != net::Role::Client) return;
    load_launch_entry();
    if (!g_launch_entry.empty())
        WritePrivateProfileStringW(L"space-training",g_launch_entry.c_str(),L"2",
                                   (g_dir+L"\\choices.ini").c_str());
}

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

// BUG-0370: a player's creature replaced by a newer one (its editor, the co-op window's arrows) is
// unregistered at once on every machine, while creatures of it can still be in the host's world (a
// puppet's leftover a world share or a save brought back): the host sent such an NPC, the guest
// asked for its species and the host answered "not found here", so the NPC was missing on the guest
// until the next world share. The records of every player creature applied here are kept by their
// instance (the newest kKeepOld) and the host answers from them when the game no longer finds the
// key. `coop.fix creations.old_species off`: not kept, not answered (0.7.3.3).
static bool g_old_species_fix = true;
COOP_FIX("creations.old_species", "BUG-0370", g_old_species_fix, "creations: the host answers a guest's request for a player's replaced creature (an NPC of it is still in the world)");
constexpr size_t kKeepOld = 24;
static std::map<uint32_t, std::vector<dbpf::Record>> g_old_recs; // instance -> its records (renamed)
static std::deque<uint32_t> g_old_order;
static unsigned g_old_served;
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

static void* package_for(std::wstring path, const std::vector<dbpf::Record>& recs, int priority,
                         std::wstring* used = nullptr)
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
}

static void protect_launch_model(const Bundle& local)
{
    if (!g_personal_ship || is_host() || game::mode_id() != modes::kSpace ||
        !local.key.instance || local.recs.empty()) return;
    load_launch_entry();
    if (g_launch_entry.empty()) return;
    if (g_pending_launch == g_launch_entry) {
        g_launch_model = local.key.instance;
        wchar_t value[12];
        swprintf(value,12,L"%08X",g_launch_model);
        if (WritePrivateProfileStringW(L"space-launch",g_launch_entry.c_str(),value,
                                       (g_dir+L"\\choices.ini").c_str()))
            g_pending_launch.clear();
    }
    if (g_launch_model != local.key.instance) return;
    auto it = g_world_pkgs.find({kUfo,g_launch_model});
    if (it != g_world_pkgs.end()) {
        drop_package(it->second.db);
        g_world_pkgs.erase(it);
    }
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
static std::map<std::pair<uint8_t, uint32_t>, Model> g_model_use; // (kind, instance) -> the verdict
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
    if (!allow_copy) { // the caller cannot pass another key to the game (a building)
        H->log(M, SK_INFO, "creations: content: %s model %08X has %u id(s) this game does not have: not built here",
               name, model.instance, (unsigned)(bad.size() + bad_paints.size()));
        return refuse();
    }
    std::string whose = std::string("model ") + hex(model.instance);
    unsigned n = substitute_parts(recs, kind, whose);
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
    // guest's save and no package would answer it after a restart). The old scheme stays behind
    // `coop.fix content.model_key off` for the lab.
    std::vector<dbpf::Record> copy = recs;
    Key use = model;
    if (!g_model_key_fix) { // the first version: a key of our own (0x7D......, as apply() registers)
        uint8_t seed[5] = {(uint8_t)kind, (uint8_t)model.instance, (uint8_t)(model.instance >> 8),
                           (uint8_t)(model.instance >> 16), (uint8_t)(model.instance >> 24)};
        use.instance = 0x7D000000u | (fnv(seed, sizeof seed) & 0x00FFFFFFu);
        for (auto& r : copy)
            if (r.key.instance == model.instance)
                r.key.instance = use.instance;
    }
    std::wstring path = g_dir + L"\\model_" + std::to_wstring((int)kind) + L"_" + std::to_wstring(model.instance) +
                        L".package", used = path;
    bool shadowed = package_for(path, copy, g_model_key_fix ? kPrioModel : kPrioWorld, &used) &&
                    (!g_model_key_fix || model_shadow_answers(used, model));
    if (!shadowed) {
        g_last_error = g_model_key_fix ? "cannot put the model's copy above the game's own record"
                                      : "cannot write the model's copy";
        H->log(M, SK_WARN, "creations: content: %s model %08X: %s (%s): not built here", name, model.instance,
               g_last_error.c_str(), slotfs::narrow(used).c_str());
        return refuse();
    }
    Model m{Model::kSubstituted, use}; // the model's own key: the game builds our copy of it
    g_models_substituted++;
    H->log(M, SK_INFO, "creations: content: %s model %08X: %u id(s) shown as substitutes%s", name, model.instance, n,
           g_model_key_fix ? ", our copy answers its key" : ", built as a key of ours");
    return remember(m);
}

bool content_all_kinds() { return g_all_kinds_fix; }

std::string content_status()
{
    char b[160];
    snprintf(b, sizeof b, "content checks=%u same=%u substituted=%u refused=%u unreadable=%u fix=%d\n", g_models_checked,
             g_models_same, g_models_substituted, g_models_refused, g_models_unreadable, (int)g_all_kinds_fix);
    return b;
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
        // the same id everywhere: owner + content hash (a new version after an editor = a new key)
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
        const bool personal_key = (g_personal_ship && k == kUfo && g_launch_model == b.key.instance) ||
                                  (g_founder_captain && k == kCreature && g_launch_captain == b.key.instance);
        const bool personal = personal_key &&
                              game::mode_id() == modes::kSpace && g_launch_path == world::current_game_key() &&
                              own_option(k,b.key);
        if (owner == 0 && C.sess && C.sess->role() == net::Role::Client && !personal) {
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
        auto old = g_old_species_fix ? g_old_recs.find(instance) : g_old_recs.end();
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

static bool g_world_designs = true;
COOP_FIX("creations.world_designs", "BUG-9053", g_world_designs,
         "creations: preserve custom Civilization designs with a shared save");
static constexpr size_t kDesignArchiveLimit = 32u << 20;
static constexpr const char* kDesignSuffix = ".duo-designs";
static std::map<std::pair<uint32_t, uint32_t>, Key> g_archived_designs;
static std::set<void*> g_design_packages;

static ModelKind design_kind(uint32_t group)
{
    return group == kModelInfo[kMkBuilding].group ? kMkBuilding :
           group == kModelInfo[kMkVehicle].group ? kMkVehicle : kMkKinds;
}

// Only creation records are accepted, never arbitrary resource overrides from a save.
static bool decode_designs(const std::vector<uint8_t>& data, std::vector<dbpf::Record>& records,
                           std::string& err)
{
    dbpf::Package package;
    if (data.size() > kDesignArchiveLimit || !package.load(data) || package.entries().empty() ||
        package.entries().size() > 1024) {
        err = "Invalid saved Civilization design archive."; return false;
    }
    std::set<std::pair<uint32_t, uint32_t>> models, designs;
    std::set<std::pair<uint64_t, uint32_t>> seen;
    size_t bytes = 0;
    for (const auto& e : package.entries()) {
        auto kind = design_kind(e.key.group);
        if (kind == kMkKinds || !e.key.instance || !e.usize || e.usize > (4u << 20) ||
            (e.key.type != kBem && e.key.type != kMeta && e.key.type != kModelInfo[kind].type) ||
            !seen.insert({((uint64_t)e.key.group << 32) | e.key.instance, e.key.type}).second ||
            (bytes += e.usize) > kDesignArchiveLimit) {
            err = "Unexpected resource in saved Civilization designs."; return false;
        }
        dbpf::Record record{e.key, {}};
        if (!package.read(e, record.data)) { err = "Unreadable saved Civilization design."; return false; }
        designs.insert({e.key.group, e.key.instance});
        if (e.key.type == kBem) models.insert({e.key.group, e.key.instance});
        records.push_back(std::move(record));
    }
    if (models != designs) { err = "A saved Civilization design has no model."; return false; }
    for (auto kind : {kMkBuilding, kMkVehicle}) {
        std::vector<dbpf::Record> subset;
        for (const auto& r : records) if (r.key.group == kModelInfo[kind].group) subset.push_back(r);
        if (!unknown_part_ids(subset, kind).empty() || !unknown_paint_ids(subset).empty()) {
            err = "Saved Civilization designs require parts or paints missing from this installation.";
            return false;
        }
    }
    return true;
}

bool archive_world_designs(std::vector<slotfs::File>& files, std::string& err)
{
    if (!g_world_designs) return true;
    std::vector<slotfs::File> added;
    for (const auto& file : files) {
        if (file.rel.size() < 4 || file.rel.substr(file.rel.size() - 4) != ".spo") continue;
        std::vector<dbpf::Record> records;
        for (auto kind : {kMkBuilding, kMkVehicle}) {
            const auto& info = kModelInfo[kind];
            std::vector<uint32_t> keys;
            dbpf::find_keys(file.data, info.type, info.group, keys);
            for (uint32_t id : keys) {
                Key key{id, info.type, info.group};
                if (!in_my_creations(key) && !g_archived_designs.count({info.group, id})) continue;
                if (!records_from_game(key, records)) {
                    err = "Cannot archive custom Civilization design " + hex(id); return false;
                }
            }
        }
        if (records.empty()) continue;
        auto archive = dbpf::write(records);
        std::vector<dbpf::Record> checked;
        if (!decode_designs(archive, checked, err)) return false;
        added.push_back({file.rel + kDesignSuffix, std::move(archive)});
    }
    for (auto& file : added) {
        auto it = std::find_if(files.begin(), files.end(), [&](const slotfs::File& f) { return f.rel == file.rel; });
        if (it == files.end()) files.push_back(std::move(file)); else *it = std::move(file);
    }
    return true;
}

static bool restore_world_designs(const std::string& planet, std::string& err)
{
    if (!g_world_designs) return true;
    auto source = slotfs::widen(planet + kDesignSuffix);
    DWORD attributes = GetFileAttributesW(source.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    std::vector<uint8_t> archive;
    std::vector<dbpf::Record> records;
    if (!read_file(source, archive)) { err = "Cannot read saved Civilization designs."; return false; }
    if (!decode_designs(archive, records, err)) return false;
    if (!find_paths()) { err = "Cannot locate the creation cache."; return false; }
    auto path = g_dir + L"\\designs_" + slotfs::widen(upd::sha256_hex(archive)) + L".package";
    void* db = package_for(path, records, kPrioWorld);
    if (!db) { err = "Cannot register saved Civilization designs."; return false; }
    g_design_packages.insert(db);
    for (const auto& record : records) if (record.key.type == kBem) {
        auto kind = design_kind(record.key.group);
        g_archived_designs[{record.key.group, record.key.instance}] =
            {record.key.instance, kModelInfo[kind].type, record.key.group};
        g_model_use.erase({(uint8_t)kind, record.key.instance});
    }
    H->log(M, SK_INFO, "creations: restored %u Civilization design records before planet load", (unsigned)records.size());
    return true;
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
        if (k == kUfo) protect_launch_model(b);
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
// BUG-0349: the own cell is applied again after every load of a Cell world: g_cell_set (the key
// already applied) lived until the session end, so after a reload the guest kept the world's cell
// while the others drew its own one (and its next cell editor edited the host's species)
// BUG-0342: a rebuild asked for on the first frames of a freshly loaded Cell world left the guest's
// cell unable to eat for minutes (players; lanes 5/6: 0 of 8 plants it swam into, at the first
// load and after a reload); 5 s later it eats. The own cell waits until the loaded Cell world has
// run 5 s and 150 frames.
static bool g_own_rebuild_fix = true;
COOP_FIX("cell.own_rebuild", "BUG-0342", g_own_rebuild_fix, "creations: the own cell is applied (level rebuild) only after the loaded Cell world has run 5 s");
static double g_cell_since;
static uint64_t g_cell_since_frame;
static bool g_cell_reapply_fix = true;
COOP_FIX("creations.cell_reapply", "BUG-0349", g_cell_reapply_fix, "creations: the own cell is applied again after every load of a Cell world");
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
        if (g_cell_reapply_fix) { // a new Cell world: its avatar has the world's model again
            g_cell_set = {};
            g_cell_rebuilding = false;
        }
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
    if (g_own_rebuild_fix && !g_cell_rebuilding &&
        (H->time_seconds() - g_cell_since < 5.0 || H->frame_index() - g_cell_since_frame < 150))
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
// `coop.fix creations.editor_after_swap off`: no wait (0.7.3.1).
static bool g_editor_after_swap_fix = true;
COOP_FIX("creations.editor_after_swap", "BUG-0318", g_editor_after_swap_fix, "creations: an editor waits until the avatar's new body is built");
// The same holds for a new avatar (a world just loaded, a rejoin without a swap: lab 20261030-162204,
// the editor 1.7 s after the load stayed black): its body is built over the first frames as well.
static double g_swap_at = -100;
static uint64_t g_swap_frame = 0;
static double g_new_avatar_at = -100;
static uint64_t g_new_avatar_frame = 0;
bool avatar_swap_settling(double now)
{
    if (g_land_result.instance) return true;
    if (!g_editor_after_swap_fix)
        return false;
    uint64_t f = H->frame_index();
    bool swap = g_swap_at >= 0 && (now - g_swap_at < 5.0 || f - g_swap_frame < 300);
    bool fresh = g_new_avatar_at >= 0 && (now - g_new_avatar_at < 5.0 || f - g_new_avatar_frame < 300);
    return swap || fresh;
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
        g_new_avatar_at = now; // BUG-0318: a new avatar's body is still being built
        g_new_avatar_frame = H->frame_index();
    }
    if (now - g_seen_since < 3.0)
        return;
    if (av == g_swapped_avatar && epoch == g_swapped_epoch && k == g_swapped_key)
        return;
    Key cur = avatar_species_key();
    if (!(cur == k)) {
        void* prof = game::species_by_key({k.instance, k.type, k.group});
        if (!prof) {
            trace::ev("body", "own unresolved", "own creature %08X is not resolvable here (yet): the avatar stays %08X",
                      k.instance, cur.instance);
            return; // not resolvable (yet)
        }
        int age = *(int*)((uint8_t*)av + 0xB34);
        thiscall<bool>(a_SetSpecies, av, prof, age, true);
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
// world's species had more abilities than the guest's own creature). With the fix the bar is built
// again whenever the avatar's abilities change under the same avatar (our swap), and whenever a
// slot points past the avatar's abilities. `coop.fix creations.ability_bar off`: the old behaviour.
static uintptr_t a_BarPtr, a_BarRebuild;
static bool g_bar_fix = true;
COOP_FIX("creations.ability_bar", "BUG-0358", g_bar_fix, "creations: the creature ability bar is built again after the own creature swap (no slot past the avatar's abilities)");
static void* g_bar_avatar;
static uint32_t g_bar_sig;
static bool g_bar_queued;
static unsigned g_bar_rebuilds;

struct BarState {
    int abilities = -1; // the avatar's ability count (vt 0xB0)
    int used = 0;       // slots with an ability index
    int stale = 0;      // slots whose index is not one of the avatar's abilities
    int max_index = -1;
};

static uint8_t* bar_table()
{
    if (!a_BarPtr || game::mode_id() != modes::kCreature)
        return nullptr;
    uint8_t* bar = *(uint8_t**)a_BarPtr;
    if (!bar || !skg::readable(bar, 0xD8))
        return nullptr;
    int rows = *(int*)(bar + 0xC8), cols = *(int*)(bar + 0xCC);
    return rows > 0 && rows <= 5 && cols > 0 && cols <= 5 ? bar : nullptr;
}

static BarState bar_state(void* av)
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

static void bar_rebuild_task(void*)
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
    if (!g_bar_fix || g_bar_queued || !a_BarRebuild)
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

// ---- api ----
using LoadPlanet = void(SK_THISCALL*)(void*, const char16_t*);
static LoadPlanet o_LoadPlanet;
static void SK_THISCALL h_LoadPlanet(void* manager, const char16_t* name)
{
    std::string campaign;
    if (name) {
        std::wstring planet;
        for (size_t i = 0; i < MAX_PATH && name[i]; ++i) planet.push_back((wchar_t)name[i]);
        std::string key = world::game_key_for_planet(slotfs::narrow(planet)), err;
        campaign = key;
        founders::CampaignRules rules;
        if (!key.empty() && founders::read_rules(key,rules) == founders::RulesFile::Unreadable) {
            if (C.feed) C.feed("Planet load stopped because the reserved founder settings are unreadable.");
            return;
        }
        if (!key.empty() && !restore_guest_model(key, err)) {
            H->log(M, SK_ERROR, "creations: stopped planet load: %s", err.c_str());
            if (C.feed) C.feed("Planet load stopped. " + err);
            return;
        }
    }
    world::native_campaign_loaded(campaign);
    cellsync::clear_cell_credit();
    creature_hp::clear_enemy_health();
    squadron::clear_enemy_stats();
    o_LoadPlanet(manager, name);
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    auto captain_key = H->sym("SporeCoop::Empire_GetCaptainKey");
    a_FirstFlightCaptainCheck = H->sym("SporeCoop::FirstFlight_CaptainCheckReturn");
    if (captain_key && a_FirstFlightCaptainCheck)
        H->hook_create(M,(void*)captain_key,(void*)h_EmpireCaptainKey,(void**)&o_EmpireCaptainKey);
    auto species_key = H->sym("SporeCoop::SpeciesManager_CurrentCreationKey");
    a_CaptainSourceReturn = H->sym("SporeCoop::CaptainName_SourceKeyReturn");
    if (species_key && a_CaptainSourceReturn)
        H->hook_create(M,(void*)species_key,(void*)h_SpeciesManagerKey,(void**)&o_SpeciesManagerKey);
    if (uintptr_t load = H->sym("Simulator::cGamePersistenceManager::LoadGameByName"))
        H->hook_create(M, (void*)load, (void*)h_LoadPlanet, (void**)&o_LoadPlanet);
    a_GetDb = H->sym("Resource::GetDatabaseByID");
    a_OpNew = H->sym("Memory::operator_new_named");
    a_DbCtor = H->sym("Resource::DatabasePackedFile::ctor");
    a_RmPtr = H->sym("Resource::ResourceManager_ptr");
    a_SetSpecies = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    a_GetEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_CellPreload = H->sym("Simulator::Cell::PreloadModel");
    a_BarPtr = H->sym("SporeCoop::AbilityBar_ptr");
    a_BarRebuild = H->sym("SporeCoop::AbilityBar_Rebuild");
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
static uint32_t g_tribal_outfit_world;
static Key g_tribal_outfit_previous;
void begin_tribal_outfit()
{
    g_tribal_outfit_world = modes::current_world();
    g_tribal_outfit_previous = mine(kCreature).key;
    g_adopt_until = 0;
    // The campaign's EditorSaves record may not have existed at the first load
    // check. Pin the actual pre-editor body before newest-creation fallback changes.
    if (!g_chosen[kCreature] && !g_campaign_entry.empty() && !g_campaign_creature.instance)
        g_campaign_creature = g_tribal_outfit_previous;
}
static bool journal_tribal_choice()
{
    if (!g_outfit_ack) return true;
    if (!find_paths()) return false;
    auto path = g_dir + L"\\choices.ini";
    wchar_t pending[64] = {};
    GetPrivateProfileStringW(L"outfit_pending", L"restore", L"", pending, 64, path.c_str());
    if (pending[0]) return false; // never overwrite an unacknowledged editor
    bool campaign = g_campaign_creature.instance && !g_chosen[kCreature];
    if (campaign && g_campaign_entry.size() != 17) return false;
    wchar_t previous[16];
    // An unselected newest-creation fallback must also return to the old body,
    // not to the newly saved catalog entry after a crash.
    swprintf(previous, 16, L"%08X", campaign ? g_campaign_creature.instance :
             g_chosen[kCreature] ? g_choice[kCreature].instance : g_tribal_outfit_previous.instance);
    std::wstring value = campaign ? L"C:" + g_campaign_entry + L":" + previous :
                                   L"E:" + std::wstring(previous);
    if (!WritePrivateProfileStringW(L"outfit_pending", L"restore", value.c_str(), path.c_str())) return false;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return true;
}

void confirm_tribal_outfit()
{
    if (!g_dir.empty() && !WritePrivateProfileStringW(L"outfit_pending", L"restore", nullptr,
                                                    (g_dir + L"\\choices.ini").c_str()) && C.feed)
        C.feed("Outfit accepted. Its local confirmation could not be saved; reconnect may restore the previous outfit.");
}
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
    bool tribal = g_tribal_outfit_world && g_tribal_outfit_world == modes::current_world() && m == modes::kTribe;
    if ((g_prev_mode == modes::kEdit || g_prev_mode == modes::kEditor) && (m == modes::kCreature || m == modes::kCell || tribal)) {
        g_adopt_until = now + 30;
        g_adopt_kind = m == modes::kCell ? kCell : kCreature;
        g_adopt_read = 0;
    }
    g_prev_mode = m;
    if (now > g_adopt_until) {
        if (tribal && g_adopt_until) { tribe::finish_own_outfit(false); g_tribal_outfit_world = 0; }
        return false;
    }
    Key a = tribal ? mine(kCreature).key : g_adopt_kind == kCell ? cell_key() : avatar_species_key();
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
        if (tribal && !journal_tribal_choice()) {
            tribe::finish_own_outfit(false);
            g_tribal_outfit_world = 0;
            if (C.feed) C.feed("Outfit kept in My Creations. The campaign choice could not be protected, so it was not changed.");
            return false;
        }
        if (!(pick == mine(g_adopt_kind).key) || !g_chosen[g_adopt_kind]) {
            if (g_adopt_kind == kCreature && g_campaign_creature.instance && !g_chosen[kCreature])
                choose_campaign_creature(pick);
            else
                choose(g_adopt_kind, pick);
            H->log(M, SK_INFO, "creations: back from the editor, own %s %08X", kInfo[g_adopt_kind].name, pick.instance);
        }
        if (tribal) { tribe::finish_own_outfit(true); g_tribal_outfit_world = 0; }
        return false;
    }
    g_adopt_until = 0;
    if (g_host_world_fix && is_host()) { // BUG-0344: the editor's result (the avatar's species) is the own one
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

// BUG-9006: retain the guest's actual cell while the host moves the group to land.
// A direct Creature start, another planet or an explicit F9 land body has no conversion.
static Key g_land_cell;
static uint32_t g_land_world;
static bool g_land_queued;
static double g_land_started, g_land_retry;
bool landfall_pending() { return g_land_cell.instance != 0; }

// The early editor asks the asset catalog for this creation. A BEM alone works
// on the live avatar, but the editor can start an empty body in its place.
static bool landfall_catalog(void* rm, void* db, const Key& cell, const Key& land)
{
    constexpr uint32_t thumb = 0x2F7D0004;
    uintptr_t raw_write = H->sym("SporeCoop::WriteRawCreationRecord");
    uintptr_t alloc = H->sym("GeneralAllocator::new_");
    uintptr_t ctor = H->sym("SporeCoop::AssetMetadataCtor");
    uintptr_t name = H->sym("SporeCoop::AssetMetadataName");
    uintptr_t description = H->sym("SporeCoop::AssetMetadataDescription");
    uintptr_t set = H->sym("Pollinator::cAssetMetadata::Set");
    if (!alloc || !raw_write || !ctor || !name || !description || !set) return false;
    Key thumbnail{cell.instance, thumb, cell.group};
    std::vector<dbpf::Record> records;
    if (!records_from_game(thumbnail, records)) return false;
    auto png = std::find_if(records.begin(), records.end(), [&](const auto& r) { return r.key == thumbnail; });
    if (png == records.end() || png->data.empty()) return false;
    Key new_thumb{land.instance, thumb, land.group};
    if (!cdecl_call<bool>(raw_write, db, &new_thumb, png->data.data(), (uint32_t)png->data.size(), 1)) return false;

    void* source = nullptr;
    Key meta_key{cell.instance, kMeta, cell.group};
    bool loaded = vcall<bool>(rm, 0x0C / 4, &meta_key, &source, nullptr, nullptr, nullptr, nullptr);
    void* meta = loaded && source ? vcall<void*>(source, 3, kMeta) : nullptr;
    bool ok = false;
    if (meta) {
        void* storage = cdecl_call<void*>(alloc, 0xD8, "Duo landfall metadata", 0, 0, 0, 0);
        void* created = storage ? thiscall<void*>(ctor, storage) : nullptr;
        if (created) {
            vcall<int>(created, 0);
            const char16_t* title = thiscall<const char16_t*>(name, meta);
            const char16_t* desc = thiscall<const char16_t*>(description, meta);
            ok = thiscall<bool>(set, created, &land, title ? title : u"", desc ? desc : u"", u"", &cell, false) &&
                 vcall<bool>(rm, 0x20 / 4, created, nullptr, db, nullptr, nullptr);
            vcall<int>(created, 1);
        }
    }
    if (source) vcall<int>(source, 1);
    return ok;
}

static bool make_land_creature(const Key& cell, Key& result)
{
    uintptr_t convert = H->sym("SporeCoop::CellModelToEarlyCreature");
    uintptr_t write = H->sym("SporeCoop::WriteCreationResource");
    uintptr_t publish = H->sym("SporeCoop::RegisterConvertedModel");
    uintptr_t cache_get = H->sym("Resource::IResourceManager::Get");
    uintptr_t generator = H->sym("App::cIDGenerator::Get");
    void* ids = generator ? cdecl_call<void*>(generator) : nullptr;
    void* db = a_GetDb ? cdecl_call<void*>(a_GetDb, kEditorSavesArea) : nullptr;
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    void* cache = cache_get ? cdecl_call<void*>(cache_get) : nullptr;
    if (!convert || !write || !publish || !cache || !ids || !db || !rm) return false;

    void* source = nullptr;
    void* model = nullptr;
    bool ok = vcall<bool>(rm, 0x0C / 4, &cell, &source, nullptr, nullptr, nullptr, nullptr) && source &&
              thiscall<bool>(convert, source, &model) && model;
    if (ok) {
        vcall<void>(ids, 1, &result, kInfo[kCreature].type, 0x62, 0, 0x62, 0);
        Key bem{result.instance, kBem, result.group};
        ok = result.instance && result.type == kInfo[kCreature].type && result.group == kInfo[kCreature].group &&
             !key_exists(bem);
        if (ok) {
            *(Key*)((uint8_t*)model + 8) = result;
            cdecl_call<void>(publish, model);
            vcall<bool>(cache, 0x6C / 4, model, true);
            ok = cdecl_call<bool>(write, model, db, nullptr, &bem) && landfall_catalog(rm, db, cell, result);
        }
    }
    if (model) vcall<int>(model, 1);
    if (source) vcall<int>(source, 1);
    return ok;
}

static bool landfall_tick(double now)
{
    if (C.sess->role() != net::Role::Client || C.sess->players().size() != 2 || g_chosen[kCreature]) {
        g_land_cell = {};
        g_land_result = {};
        return false;
    }
    uint32_t m = game::mode_id();
    if (m == modes::kCell) {
        Key cell = cell_key();
        auto host = g_applied.find({0, (uint8_t)kCell});
        if (cell.instance && host != g_applied.end()) {
            g_land_cell = (cell == host->second.orig || cell == host->second.key) ? Key{} : cell;
            g_land_world = modes::current_world();
        }
        return false;
    }
    if (m != modes::kCreature) {
        if (m == modes::kTribe || m == modes::kCiv || m == modes::kSpace) {
            g_land_cell = {};
            g_land_result = {};
        }
        return false;
    }
    if (!g_land_cell.instance || !game::avatar()) return false;
    if (!g_land_world || modes::current_world() != g_land_world) {
        g_land_cell = {};
        g_land_result = {};
        return false;
    }
    if (g_land_queued || (g_land_result.instance && now < g_land_retry)) return true;
    g_land_queued = true;
    H->run_outside_frame([](void*) {
        g_land_queued = false;
        if (!g_land_cell.instance || !C.sess->connected() || C.sess->role() != net::Role::Client ||
            C.sess->players().size() != 2 || g_chosen[kCreature] || game::mode_id() != modes::kCreature ||
            modes::current_world() != g_land_world || !game::avatar()) return;
        double t = H->time_seconds();
        if (!g_land_result.instance) {
            Key result{};
            if (make_land_creature(g_land_cell, result)) {
                g_land_result = result;
                g_land_started = t;
                g_land_retry = t + 1;
                return; // the game completes its packed-file write asynchronously
            }
        } else {
            void* db = cdecl_call<void*>(a_GetDb, kEditorSavesArea);
            std::vector<dbpf::Record> records;
            if (db && vcall<bool>(db, 0x24 / 4) && records_from_game(g_land_result, records) &&
                std::any_of(records.begin(), records.end(), [](const auto& rec) { return rec.key.type == kBem && !rec.data.empty(); })) {
                g_opts_read = -100;
                refresh_options(t);
                choose_campaign_creature(g_land_result);
                H->log(M, SK_INFO, "landfall: guest cell %08X -> own Creature %08X", g_land_cell.instance, g_land_result.instance);
                if (C.feed) C.feed("Your land creature grew from your own cell.");
            } else if (t - g_land_started < 30) {
                g_land_retry = t + 1;
                return;
            } else {
                if (C.feed) C.feed("Your land creature could not be saved. Choose a creature in F9.");
            }
        }
        if (!g_land_result.instance) {
            if (C.feed) C.feed("Your cell could not be converted. Choose a land creature in F9.");
        }
        g_land_result = {};
        g_land_cell = {};
    }, nullptr);
    return true;
}

// History handles the stage transition. Supply the ship already owned by
// this founder when the player confirms their unfinished flight lesson.
bool prepare_training_ship(void* request, bool& restoring)
{
    restoring = !g_restart_path.empty() && world::current_game_key() == g_restart_path;
    if (!restoring) return true;
    refresh_options(H->time_seconds());
    const Key ship{g_launch_model,kInfo[kUfo].type,kInfo[kUfo].group};
    const Key captain{g_launch_captain,kInfo[kCreature].type,kInfo[kCreature].group};
    for (const auto& [kind,key] : {std::pair{kUfo,ship},std::pair{kCreature,captain}}) {
        if (!own_option(kind,key)) {
            C.feed("Your saved ship or captain is missing from My Creations. Restore it before restarting training.");
            return false;
        }
        auto shadow = g_world_pkgs.find({kind,key.instance});
        if (shadow != g_world_pkgs.end()) {
            drop_package(shadow->second.db);
            g_world_pkgs.erase(shadow);
        }
        Bundle model;
        if (!make_bundle(kind,key,model) || model.recs.empty() ||
            !unknown_part_ids(model.recs,mk_of(kind)).empty()) {
            C.feed("Your saved ship or captain could not be loaded. Check the required creation parts.");
            return false;
        }
    }
    skg::field<Key>(request,0x10) = ship;
    skg::field<uint8_t>(request,0x20) = 0; // load the named creation
    g_restart_captain = captain;
    H->log(M,SK_INFO,"creations: first flight reuses saved ship %08X and captain %08X",ship.instance,captain.instance);
    return true;
}

// Restore only on entry to a received save. Later ordinary ship and outfit
// edits belong to the player, and must not be undone on the next tick.
static void restore_space_identity(double now)
{
    const auto* space = modes::for_mode(modes::kSpace);
    if (is_host() || game::mode_id() != modes::kSpace || !g_personal_ship || !space ||
        world::load_queued() || world::save_busy()) return;
    load_launch_entry();
    if (!g_space_restore_due || g_space_restore_queued || g_captain_flush_pending ||
        !g_pending_launch.empty() || !space->local() || g_launch_entry.empty()) return;
    if (!g_launch_model && !g_launch_captain) { g_space_restore_due = false; return; }
    refresh_options(now);
    if (!g_es_ok) return;
    g_space_restore_queued = true;
    H->run_outside_frame([](void* token) {
        const auto* space = modes::for_mode(modes::kSpace);
        const auto epoch = *static_cast<uint64_t*>(token);
        delete static_cast<uint64_t*>(token);
        if (epoch != g_space_restore_epoch) return;
        g_space_restore_queued = false;
        if (!C.sess || !C.sess->connected() || is_host() || game::mode_id() != modes::kSpace ||
            g_captain_flush_pending || world::load_queued() || world::save_busy() ||
            world::current_game_key() != g_launch_path || !space || !space->local()) return;
        void* empire = a_GetEmpire ? cdecl_call<void*>(a_GetEmpire) : nullptr;
        if (!empire) return;
        g_space_restore_due = false;
        for (Kind kind : {kUfo,kCreature}) {
            const Key own{kind == kUfo ? g_launch_model : g_launch_captain,kInfo[kind].type,kInfo[kind].group};
            if (!own.instance || !own_option(kind,own) || (kind == kCreature && !g_founder_captain)) continue;
            auto shadow = g_world_pkgs.find({kind,own.instance});
            if (shadow != g_world_pkgs.end()) {
                drop_package(shadow->second.db);
                g_world_pkgs.erase(shadow);
            }
            if (skg::field<Key>(empire,kind == kUfo ? 0xB8 : 0xC4) == own) continue;
            Bundle local;
            if (!make_bundle(kind,own,local) || local.recs.empty() ||
                !unknown_part_ids(local.recs,mk_of(kind)).empty()) continue;
            const auto set = kind == kUfo ? H->sym("SporeCoop::Empire_SetSpacecraftKey")
                                          : H->sym("SporeCoop::Empire_SetCaptainKey");
            if (!set) continue;
            thiscall<void>(set,empire,&own);
            // The native ship setter also updates other ships of this empire.
            // Rebuild actual remote puppets with their separate network models.
            if (kind == kUfo && C.appearance_changed)
                for (const auto& player : C.sess->players())
                    if (player.id != C.sess->my_id()) C.appearance_changed(player.id);
        }
        g_last_check = 0;
    },new uint64_t(g_space_restore_epoch));
}

static void commit_native_captain(double now)
{
    if (!g_captain_flush_pending || g_captain_flush_queued || now < g_captain_flush_after ||
        game::mode_id() != modes::kSpace) return;
    g_captain_flush_queued = true;
    H->run_outside_frame([](void*) {
        g_captain_flush_queued = false;
        if (!g_captain_flush_pending || !C.sess || !C.sess->connected() ||
            game::mode_id() != modes::kSpace) return;
        if (++g_captain_flush_tries > 5) {
            g_captain_flush_pending = false;
            H->log(M,SK_WARN,"creations: saved captain model could not be confirmed in the creation library");
            return;
        }
        g_captain_flush_after = H->time_seconds() + 2;
        void* empire = a_GetEmpire ? cdecl_call<void*>(a_GetEmpire) : nullptr;
        if (!empire) return;
        const Key key = skg::field<Key>(empire,0xC4);
        if (!is_crt(key) || !key.instance) return;
        void* editor_db = a_GetDb ? cdecl_call<void*>(a_GetDb,kEditorSavesArea) : nullptr;
        std::vector<dbpf::Record> records;
        if (!editor_db || !vcall<bool>(editor_db,0x24 / 4) ||
            !records_from_game({key.instance,kBem,key.group},records)) return;
        load_launch_entry();
        if (g_launch_entry.empty()) { g_captain_flush_pending = false; return; }
        wchar_t value[12];
        swprintf(value,12,L"%08X",key.instance);
        if (!WritePrivateProfileStringW(L"space-captain",g_launch_entry.c_str(),value,
                                        (g_dir+L"\\choices.ini").c_str())) return;
        g_launch_captain = key.instance;
        H->log(M,SK_INFO,"creations: native captain %08X committed to the creation library",key.instance);
        g_captain_flush_pending = false;
    },nullptr);
}

void tick(double now)
{
    if (!C.sess || !C.sess->connected())
        return;
    if (g_editing_captain && (g_prev_mode == modes::kEdit || g_prev_mode == modes::kEditor) &&
        game::mode_id() == modes::kSpace) {
        g_editing_captain = false;
        g_captain_flush_pending = true;
        g_captain_flush_tries = 0;
        g_captain_flush_after = now + .5;
    }
    commit_native_captain(now);
    refresh_campaign_creature();
    if (landfall_tick(now)) return;
    if (adopt_after_editor(now))
        return;
    if (now - g_last_check > 2.0) {
        g_last_check = now;
        restore_space_identity(now);
        share_own(now);
    }
    apply_own_avatar();
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
    g_launch_entry.clear();
    g_launch_path.clear();
    g_pending_launch.clear();
    g_restart_path.clear();
    g_restart_captain = {};
    g_launch_model = 0;
    g_captain_source = {};
    g_launch_captain = 0;
    g_editing_captain = false;
    ++g_space_restore_epoch;
    g_space_restore_due = g_space_restore_queued = false;
    g_captain_flush_pending = false;
    g_captain_flush_after = 0;
    if (!restore_pending_outfit() && C.feed) C.feed("The previous outfit choice could not be restored. Rejoin after restarting Spore.");
    g_last_check = 0;
    g_tribal_outfit_world = 0;
    g_tribal_outfit_previous = {};
    g_land_cell = {};
    g_land_world = 0;
    g_land_result = {};
    g_campaign_place = 0;
    g_campaign_creature = {};
    g_campaign_entry.clear();
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
    for (void* db : g_design_packages) drop_package(db);
    g_design_packages.clear();
    for (const auto& [id, key] : g_archived_designs)
        g_model_use.erase({(uint8_t)design_kind(key.group), key.instance});
    g_archived_designs.clear();
    g_world_missing.clear();
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

// Preserve only the two creation record types used by make_bundle(). Never mount a
// received save attachment as an unrestricted Spore package.
static bool decode_founder(const std::vector<uint8_t>& archive, Bundle& b, std::string& err)
{
    net::Reader header(archive.data(), archive.size());
    uint32_t magic = header.u32(), key = header.u32();
    uint8_t owner = header.u8();
    if (!header.ok() || magic != 0x31414744 || !owner || owner >= 0xFE || header.left() < 14) {
        err = "Invalid guest creature archive header.";
        return false;
    }
    std::vector<uint8_t> payload(header.cur(), header.cur() + header.left());
    net::Reader record_header(payload.data(), payload.size());
    record_header.u32(); record_header.u32(); record_header.u32();
    uint16_t count = record_header.u16();
    if (!count || count > 2 || !unpack(payload, b) || !b.key.instance || !is_crt(b.key) ||
        key != shared_instance(owner, fnv(payload.data(), payload.size()))) {
        err = "Invalid or mismatched guest creature data.";
        return false;
    }
    bool model = false, meta = false;
    size_t used = 14; // original key, record count
    for (auto& rec : b.recs) {
        if (rec.data.empty() || rec.key.instance != b.key.instance || rec.key.group != b.key.group ||
            (rec.key.type != kBem && rec.key.type != kMeta) || (rec.key.type == kBem ? model : meta)) {
            err = "Unexpected record in guest creature archive.";
            return false;
        }
        if (rec.key.type == kBem) {
            uint32_t effects[3];
            if (!archive_model_effects(rec.data, effects)) { err = "Invalid creature model header."; return false; }
            for (const auto& ref : model_part_refs(rec.data, kPartGroup)) {
                if (!part_known(ref.id, kPartGroup)) { err = "Saved guest creature needs missing part " + hex(ref.id); return false; }
            }
            for (int layer = 0; layer < 3; ++layer) {
                uint32_t fx = effects[layer];
                bool inherited = g_archive_cell_paints && layer == 0 && effects[1] == 0 && effects[2] == 0 &&
                    std::binary_search(std::begin(kBaseCellPaints), std::end(kBaseCellPaints), fx);
                if (fx && !paint_known(fx) && !inherited) { err = "Saved guest creature needs missing paint " + hex(fx); return false; }
            }
            model = true;
        } else meta = true;
        used += 16 + rec.data.size();
        rec.key.instance = key;
    }
    if (used != payload.size()) { err = "Unexpected data after the guest creature records."; return false; }
    if (!model) {
        err = "Missing saved guest creature model.";
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
        err = "Waiting for the guest creature to finish loading.";
        return false;
    }
    if (a->second.key == a->second.orig) return true; // a premade uses the game's own records
    if (!o_LoadPlanet) { err = "This game build cannot restore archived founder models."; return false; }
    auto source = g_store.find(id);
    if (source == g_store.end()) { err = "The guest creature's original records are unavailable."; return false; }
    net::Writer w;
    w.u32(0x31414744); w.u32(a->second.key.instance); w.u8(guest);
    w.raw(source->second.data(), source->second.size());
    Bundle b;
    return decode_founder(w.data, b, err) && founders::store_guest_model(planet, b.key.instance, w.data, err);
}

static bool restore_model(const std::string& planet, uint32_t id, std::string& err)
{
    std::vector<uint8_t> archive;
    if (!founders::load_guest_model(planet, id, archive, err)) return false;
    Bundle b;
    if (!decode_founder(archive, b, err)) return false;
    if (id != b.key.instance) { err = "The archived creature does not match its filename."; return false; }
    std::vector<dbpf::Record> current;
    if (records_from_game(b.key, current)) {
        for (auto& rec : b.recs) {
            auto it = std::find_if(current.begin(), current.end(), [&](const dbpf::Record& r) { return r.key == rec.key; });
            if (it == current.end() || it->data != rec.data) { err = "A different creature occupies the saved founder's key."; return false; }
        }
        return true;
    }
    if (!find_paths() || !package_for(g_dir + L"\\founder_" + slotfs::widen(upd::sha256_hex(archive)) + L".package", b.recs, kPrioModel)) {
        err = "Could not register the saved guest creature.";
        return false;
    }
    H->log(M, SK_INFO, "creations: restored saved founder %08X before planet load", b.key.instance);
    return true;
}

bool restore_guest_model(const std::string& planet, std::string& err)
{
    if (!restore_world_designs(planet, err)) return false;
    std::vector<uint32_t> ids;
    if (!founders::model_ids(planet, ids, err)) return false;
    for (uint32_t id : ids)
        if (!restore_model(planet, id, err)) return false;
    return true;
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
    char b[400];
    snprintf(b, sizeof b,
             "creations: in=%u out=%u registered=%u avatar_swaps=%u waits_on_disk=%u unusable=%u world_swaps=%u "
             "species asked=%u got=%u sent=%u missing=%u old_sent=%u own_out=%u own_in=%u rx_dropped=%u adopt_skipped=%u parts_refused=%u world_missing=%u paints_fb=%u unknown_logged=%u cc=%d%s%s\n",
             g_bundles_in, g_bundles_out, g_registered, g_swaps, g_waits_on_disk, g_unusable, g_world_swaps,
             g_species_asked, g_species_got, g_species_sent, g_species_missing, g_old_served, g_own_out, g_own_in, g_rx_dropped, g_adopt_skipped, g_parts_refused, (unsigned)g_world_missing.size(), g_paints_fallback, g_unknown_logged, g_have_cc, g_last_error.empty() ? "" : " error=", g_last_error.c_str());
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
    if ((!a.empty() && a.size() <= 2 && a[0] == "ship_parts") ||
        (a.size() == 2 && (a[0] == "body_parts" || a[0] == "body_hash"))) {
        const bool ship = a[0] == "ship_parts";
        const auto& info = kInfo[ship ? kUfo : kCreature];
        const Key key = a.size() == 1 ? ufo_key() : Key{(uint32_t)strtoul(a[1].c_str(),nullptr,16),
                                                        info.type,info.group};
        std::vector<dbpf::Record> records;
        if (!key.instance || !records_from_game({key.instance,kBem,key.group}, records)) return "No readable creation.";
        std::string parts;
        for (const auto& record : records) {
            if (record.key.type != kBem) continue;
            if (a[0] == "body_hash") return upd::sha256_hex(record.data);
            for (const auto& part : model_part_refs(record.data, ship ? kUfoPartGroup : kPartGroup))
                parts += " " + hex(part.id);
        }
        return parts.empty() ? "No readable parts." : (ship ? "ship " : "body ") + hex(key.instance) + " parts:" + parts;
    }
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
    if (!a.empty() && a[0] == "content") { // BUG-0469: content [<kind> <hex instance>]: counters, or one model
        if (a.size() < 3)
            return content_status();
        int kind = a[1] == "cell" ? kMkCell : a[1] == "ufo" ? kMkUfo : a[1] == "vehicle" ? kMkVehicle
                   : a[1] == "building" ? kMkBuilding : kMkCreature;
        uint32_t inst = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        Model m = ensure_model({inst, kModelInfo[kind].type, kModelInfo[kind].group}, (ModelKind)kind);
        char b[200];
        snprintf(b, sizeof b, "content %s %08X status=%s key=%08X/%08X/%08X\n", kModelInfo[kind].name, inst,
                 m.status == Model::kSame ? "same" : m.status == Model::kSubstituted ? "substituted" : "refused",
                 m.key.instance, m.key.type, m.key.group);
        return b;
    }
    if (a.size() >= 2 && a[0] == "parts") { // parts <hex crt instance>: the parts of the model the game reads for that key (tests, BUG-0452)
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group};
        std::vector<dbpf::Record> recs;
        if (!records_from_game(k, recs))
            return "parts " + hex(k.instance) + " none\n";
        std::vector<uint32_t> unknown = unknown_part_ids(recs, kMkCreature);
        unsigned n = 0, cc = 0;
        for (auto& r : recs)
            if (r.key.type == kBem)
                for (auto& ref : model_part_refs(r.data, kPartGroup)) {
                    n++;
                    cc += is_cc_part(ref.id);
                }
        // the game's own species profile (it builds the model): only when it can (a part it does
        // not have faults in the bake, 0x41F3B)
        int species = unknown.empty() ? game::species_by_key({k.instance, k.type, k.group}) != nullptr : -1;
        char b[300];
        snprintf(b, sizeof b, "parts %s n=%u cc=%u unknown=%u species=%d have_pack=%d substituted=%u refused=%u",
                 hex(k.instance).c_str(), n, cc, (unsigned)unknown.size(), species, (int)have_creepy_cute(),
                 g_parts_substituted, g_parts_refused);
        for (uint32_t id : unknown)
            snprintf(b + strlen(b), sizeof b - strlen(b), " %08X", id);
        snprintf(b + strlen(b), sizeof b - strlen(b), "\n");
        return b;
    }
    if (a.size() >= 2 && a[0] == "paints") { // paints <hex crt instance>: the paint layers of the model the game reads (tests, BUG-0452)
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group};
        std::vector<dbpf::Record> recs;
        if (!records_from_game(k, recs))
            return "paints " + hex(k.instance) + " none\n";
        std::string out = "paints " + hex(k.instance);
        unsigned cc = 0, unknown = 0;
        for (auto& r : recs) {
            uint32_t eff[3];
            size_t at[3], len[3];
            if (r.key.type != kBem || !model_effects(r.data, eff, at, len))
                continue;
            for (int i = 0; i < 3; i++) {
                out += " " + hex(eff[i]);
                cc += is_cc_paint(eff[i]);
                unknown += g_unknown_fix ? !paint_known(eff[i]) : (is_cc_paint(eff[i]) && !have_creepy_cute());
            }
        }
        char b[120];
        snprintf(b, sizeof b, " cc=%u unknown=%u have_pack=%d substituted=%u\n", cc, unknown, (int)have_creepy_cute(),
                 g_paints_substituted);
        return out + b;
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
    if (a.size() >= 2 && a[0] == "bar" && a[1] == "rebuild") { // tests: the game builds the bar for the avatar it has now
        if (!g_bar_queued) {
            g_bar_queued = true;
            H->run_outside_frame(bar_rebuild_task, nullptr);
        }
        return "the ability bar is built again on the next frame\n";
    }
    if (!a.empty() && a[0] == "bar") { // the creature ability bar against the avatar's abilities (tests, BUG-0358)
        BarState st = bar_state(game::avatar());
        char b[200];
        snprintf(b, sizeof b, "bar abilities=%d used=%d stale=%d max_index=%d rebuilds=%u fix=%d slots=", st.abilities, st.used,
                 st.stale, st.max_index, g_bar_rebuilds, (int)g_bar_fix);
        std::string out = b;
        if (uint8_t* bar = bar_table(); bar && game::avatar())
            for (int r = 0; r < *(int*)(bar + 0xC8); r++)
                for (int c = 0; c < *(int*)(bar + 0xCC); c++)
                    if (int idx = *(int*)(bar + r * 0x28 + c * 8); idx != -1)
                        out += std::to_string(r) + "." + std::to_string(c) + ":" + std::to_string(idx) + ",";
        return out + "\n";
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

// support trace (trace.h): the own creation, where the choice came from, what the game has now
std::string trace_line(Kind k)
{
    bool guest = C.sess && C.sess->role() == net::Role::Client;
    Option o = mine(k);
    const char* origin = g_chosen[k] && k != kUfo ? (g_choice[k].instance ? "choices.ini" : "choices.ini: the world's own")
                         : k == kUfo                ? "own empire"
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
