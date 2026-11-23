// The internals of the creations module (creations.cpp) that creations_debug.cpp (the coop.creations console
// command) reads. Included only by mods/coop/creations*.cpp. C8 of 0.7.15 (BUG-1183): moved out of creations.cpp
// without edits; docs/01-architecture.md#arch-cell-files.
#pragma once

#include "lifecycle.h"
#include "creations.h"
#include "player_life.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <tuple>

#include "game.h"
#include "modes.h"
#include "slotfs.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "own_world.h"
#include "content_packs.h"
#include "model_paint.h"
#include "net/partprops.h"
#include "trace.h"
#include "settings.h"
#include "world.h"

namespace creations {

using skg::vcall;

struct KindInfo {
    const char* name;
    uint32_t type, group;
};

constexpr uint32_t kBem = 0x1A99B06B, kMeta = 0x030BDEE3, kCreatureData = 0x0F43029A, kPng = 0x2F7D0004;

// BUG-0469 review: our copy of a model the game cannot build is registered under the model's OWN key,
// above every package of the game (and above the host's world packages at 2000), so no key of ours
// ever reaches the game or a save
constexpr int kPrioModel = 3000;

struct Bundle {
    Key key;                         // the creation (crt/cll/ufo key)
    std::vector<dbpf::Record> recs;  // bem + metadata, empty for a premade (everybody has it)
};

// BUG-0469 review: the substitute copy of a model was registered under an instance of our own
// (0x7D......, the fix's first version). Such a key reaches the guest's save (mirror vehicles are not
// stripped) and no package answers it after a restart (or without the mod): the game then cannot
// build the saved object. The copy now goes under the model's OWN key, in a package above every
// package of the game, so no key of ours reaches the game at all (kPrioModel, model_shadow_answers).
constexpr uint32_t kPartProp = 0x00B1B104, kPartGroup = 0x40626000;

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

struct PartRef {
    uint32_t id;
    size_t at;    // raw offset of the first id byte in the model
    size_t len;   // raw length of the id (0xFE is written FE 00)
    bool safe;    // rewriting the id cannot change the slot that follows it
};

// the host's creations registered under their original key (the world's species), by that key
struct WorldPkg {
    uint32_t id = 0;
    void* db = nullptr;
};

struct Alias {
    uint32_t ours = 0;
    void* orig = nullptr;  // the slot's profile before (restored at the session end)
    void* prof = nullptr;  // ours
};

struct BarState {
    int abilities = -1; // the avatar's ability count (vt 0xB0)
    int used = 0;       // slots with an ability index
    int stale = 0;      // slots whose index is not one of the avatar's abilities
    int max_index = -1;
};

// creations.cpp
extern const SkHost* H;
extern SkMod* M;
extern Ctx C;
extern const KindInfo kInfo[3];
extern uintptr_t a_RmPtr;
extern std::wstring g_dir;
extern Key g_choice[3];
extern bool g_chosen[3];
extern bool g_guest_world_fix;
extern std::set<std::tuple<int, uint32_t, uint32_t>> g_own_told;
extern unsigned int g_world_records;
std::wstring wide(const char* s);
uint32_t world_record(int k, uint32_t world);
void world_record_set(int k, uint32_t world, uint32_t inst);
extern unsigned int g_ps_test_null;
extern double g_last_check;
void test_hide_own(int k, bool on); // tests (BUG-1476): hideown
extern std::map<uint8_t, std::set<uint32_t>> g_own_sent;
uint32_t fnv(const void* p, size_t n, uint32_t h = 0x811C9DC5);
std::string hex(uint32_t v);
bool find_paths();
bool is_host();
extern unsigned int g_parts_refused;
extern std::map<std::pair<uint32_t, uint32_t>, bool> g_key_cache;
extern const ModelInfo kModelInfo[5];
uint32_t part_group(ModelKind k);
std::vector<PartRef> model_part_refs(const std::vector<uint8_t>& m, uint32_t group);
bool is_cc_part(uint32_t id);
extern std::set<uint32_t> g_lacked;
std::string test_break_remap(uint32_t host_inst); // tests (BUG-1293)
extern bool g_objmodels_scan;
bool part_known(uint32_t id, uint32_t group);
extern std::map<uint32_t, std::vector<uint8_t>> g_rx_descs;
extern std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_lookalike;
bool file_location(void* db, std::wstring& out);
bool part_prop_bytes(uint32_t id, uint32_t group, std::vector<uint8_t>& out);
std::vector<uint32_t> unknown_part_ids(const std::vector<dbpf::Record>& recs, ModelKind k);
extern unsigned int g_parts_substituted;
extern unsigned int g_paints_substituted;
extern unsigned int g_paints_in_run;
bool model_effects(const std::vector<uint8_t>& m, uint32_t eff[3], size_t at[3], size_t len[3], bool patch[3] = nullptr);
bool is_cc_paint(uint32_t id);
bool paint_known(uint32_t id);
std::vector<uint8_t> pack(const Bundle& b);
bool make_bundle(Kind k, const Key& key, Bundle& b);
extern std::map<std::pair<uint8_t, uint32_t>, WorldPkg> g_world_pkgs;
extern bool g_world_drop;
extern unsigned int g_world_dropped;
void* package_for(std::wstring path, const std::vector<dbpf::Record>& recs, int priority, std::wstring* used = nullptr);
extern std::map<std::pair<uint8_t, uint32_t>, Model> g_model_use;
bool records_from_game(const Key& k, std::vector<dbpf::Record>& out);
extern std::map<uint32_t, Alias> g_alias;
extern unsigned int g_alias_set;
extern unsigned int g_alias_restored;
extern unsigned int g_alias_own_undone;
void send_bundle(int to, uint8_t owner, Kind k, uint32_t id, const std::vector<uint8_t>& data);
extern bool g_sim_same_addr;
extern bool g_bar_queued;
extern unsigned int g_bar_rebuilds;
uint8_t* bar_table();
BarState bar_state(void* av);
void bar_rebuild_task(void*);
// WP-K1 (creations_captain.cpp, creations 1.2): the captains between the games (BUG-1279, BUG-1262)
bool key_exists(const Key& k);
bool read_file_at(HANDLE f, uint64_t off, uint32_t n, std::vector<uint8_t>& buf);
unsigned ensure_content(std::vector<dbpf::Record>& recs, ModelKind k, const std::string& whose);
bool unpack(const std::vector<uint8_t>& d, Bundle& b);
void drop_package(void* db);
bool is_crt(const Key& k);
constexpr uint32_t kPartDescGroup = 0x7C0DE5C1; // a part's description in a bundle (never registered)
bool captain_kind_on(uint8_t kind); // MSG_CREATION kind 10 / 11 with its switch on
void captain_message(uint8_t kind, uint8_t owner, uint32_t id, const std::vector<uint8_t>& data);
void captain_tick(double now);
void captain_join(uint8_t id);
void captain_leave(uint8_t id);
void captain_end();
void captain_init();
std::string captain_status();


} // namespace creations
