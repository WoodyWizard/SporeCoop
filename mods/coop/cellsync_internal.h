// The internals of the Cell module (cellsync.cpp) that its other files share: cellsync_combat.cpp (what the cells
// do to each other), cellsync_physics.cpp (the contact solver, the soft-body particles and the broadphase guards),
// cellsync_tiles.cpp (the population around every player), cellsync_host.cpp, cellsync_guest.cpp (the two sides of
// the pool's state) and cellsync_debug.cpp (the coop.pool console command). Included only by mods/coop/cellsync*.cpp.
// C8 of 0.7.15 (BUG-1183): moved out of cellsync.cpp without edits; docs/01-architecture.md#arch-cell-files.
#pragma once

#include "cellsync.h"
#include "call_guard.h"
#include "lifecycle.h"
#include "census.h"
#include "coop_ui.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "cell_logic.h"
#include "dna_pace.h"
#include "settings.h"
#include "view_centres.h"
#include "game.h"
#include "modes.h"
#include "net/interp.h"
#include "net/utf8.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "trace.h"
#include "own_world.h"
#include "find.h"
#include "space_ext.h"

void coop_module_of(uintptr_t a, char* out, size_t cap); // debug.cpp (RE, BUG-0355)

namespace cellsync {

// ---- cCellObjectData fields (docs/re/cell.md) ----
constexpr uintptr_t kTarget = 0x08, kFlags = 0x48, kCounter = 0x4A, kPos = 0x4C, kSize = 0x58, kRot = 0x5C,
                    kPrevPos = 0x84, kVel = 0x90, kTargetSize = 0xB4, kModelKey = 0xFC, kRes = 0x108,
                    kInvuln = 0x111, kDead = 0x112, kDying = 0x113, kAiLock = 0x16C, kHp = 0x244,
                    kKeepAlive = 0x354, kLevel = 0x358, kQuery = 0x35C, kFlee = 0x220, kChase = 0x22C;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

// the diagnostic counters (coop.pool and the Cell status lines print them, the tests read them; never reset by a
// session or world change, as before C8 of 0.7.15)
struct Counters {
    uint32_t own_bites = 0, own_hits = 0; // DamageCell by our avatar / that took hp
    uint32_t own_chunks = 0; // chunks our avatar bit off the host's cells (EatChunk)
    uint32_t hits_in = 0, eats_in = 0, hits_aggro = 0, bitten_in = 0, puppet_hits_dropped = 0, kills_in = 0, eats_late = 0;
    uint32_t kills_sent = 0, death_replays = 0, loot_skipped = 0;
    uint32_t hurt_local = 0, hurt_fwd = 0, soft_moves = 0, bites_taken = 0, damage_taken = 0;
    uint32_t scale_conv = 0; // samples converted (host and guest scale differed)
    uint32_t ko_bites_blocked = 0, ko_hits_refused = 0;
    uint32_t ko_eats_blocked = 0; // BUG-0364: meals and chunk bites of our knocked-out cell left out
    uint32_t ko_swallows_blocked = 0;
    uint32_t statehold_dropped = 0;
    uint32_t hits_spared = 0;
    uint32_t solo_times = 0, solo_released = 0, solo_spawns = 0, solo_sent = 0, solo_dropped = 0;
    uint32_t send_held = 0; // host: sends held back for a guest not in the host's Cell world (tests)
    uint32_t kills_resent = 0; // tests: a guest's kill sent again after the host left the creature alive
    uint32_t kills_dup_sent = 0, kills_dup_refused = 0;
    uint32_t eaten_skipped = 0, gone_again = 0;
    uint32_t late_chunks = 0, rounds_partial = 0; // guest: state chunks of an older round, rounds with a lost chunk
    uint32_t created = 0, destroyed = 0, hits_sent = 0, eats_sent = 0, purged = 0, bitten_sent = 0, soft_turns = 0, chunks_sent = 0, chunks_taken = 0, breaks_sent = 0;
    uint32_t revives = 0, respawns = 0;
    uint32_t camera_snaps = 0;
    uint32_t camera_heals = 0;
    uint32_t puppet_cell_refused = 0, puppet_cell_own = 0;
    uint32_t ko_deaths = 0; // timed-out knock-outs that ended in the game's own death (cell.respawn_small off; coop.cell status: ko_deaths)
    uint32_t chunk_kills = 0;
    uint32_t product_hits = 0, foreign_mirror = 0, foreign_puppet = 0, foreign_other = 0, product_pairs = 0, products_live = 0; // tests
    uint32_t goldie_kept = 0; // BUG-1248: purges that kept our own tutorial's cells
    uint32_t puppet_poison_steps = 0;
    uint32_t ko_clouds_blocked = 0; // tests: clouds a knocked-out cell did not make (own avatar's and partners' steps)
    uint32_t partner_poison_refused = 0, partner_poison_taken = 0; // tests: refused / taken (switch off, ff, Rivals)
    uint32_t clouds_made = 0, cloud_last = 0xFFFFFFFF, puppet_clouds = 0;
    uint32_t own_clouds_held = 0;
    uint32_t zaps = 0, zap_hits = 0; // tests
    uint32_t puppet_zaps = 0; // tests (BUG-1243)
    uint32_t bite_rings = 0; // tests
    uint32_t mirror_kills = 0, npc_deaths = 0; // npc_deaths: NPCs of this pool killed / broken by the game
    uint32_t wipe_kept = 0, wipe_destroyed = 0, wipe_near_lost = 0, wipe_calls = 0; // host: per session (tests)
    uint32_t dbg_destroys = 0, dbg_expect = 0, dbg_freed = 0, dbg_nogfx = 0, dbg_alive = 0; // RE (BUG-0355)
    uint32_t swallows_marked = 0;
    uint32_t kept = 0, puppet_saves = 0, puppet_new = 0, own_eats = 0, own_food = 0;
    uint32_t quick_destroys = 0;
    uint32_t products_kept = 0;
    uint32_t own_nf_events = 0;   // finite -> not finite transitions seen
    uint32_t stack_logs = 0, stack_pairs = 0; // tests: `coop.pool stack`
    uint32_t stack2_pairs = 0;
    uint32_t pairs_skipped = 0;
    uint32_t creature_pairs = 0, bite_events = 0;
    uint32_t own_pairs = 0; // tests (BUG-0342): collision pairs of our own cell with any cell
    uint32_t guest_attacks = 0, guest_attacks_spared = 0, guest_attack_kills = 0; // tests
    uint32_t contacts_bad = 0, contacts_bad_own = 0, contact_logs = 0, contact_solves = 0, contacts_unit = 0;
    uint32_t wipes = 0;
    uint32_t nonfinite_sent_skipped = 0, nonfinite_dropped = 0;
    uint32_t nan_pop_skips = 0, nan_heals = 0;
    uint32_t tilecap_skips = 0;
    uint32_t tiles_core = 0, tiles_starved = 0, tiles_fed = 0, send_capped = 0, mirror_capped = 0, cells_peak = 0;
    uint32_t tiles_unshared = 0, tiles_flush = 0, tiles_full = 0, tiles_partner = 0, tiles_shared = 0, tiles_bg = 0, tile_passes = 0, tiles_kept = 0, tile_resets = 0, tiles_drained = 0;
    uint32_t views_empty_skips = 0;
    uint32_t handed_flushed = 0;                         // hand-overs into a list flushed in the same pass
    uint32_t tile_check_warns_between = 0;               // "the frame between" warnings
    uint32_t tiles_handed = 0;
    uint32_t tiles_scale_flush = 0;
    uint32_t tile_checks_bad = 0;
    uint32_t stale_targets = 0;
    uint32_t loot_calls = 0, loot_cells = 0;
    uint32_t part_bad_frees = 0, particles_peak = 0;
    uint32_t soft_refused = 0, soft_made = 0, gfx_peak = 0, soft_count0 = 0;
    uint32_t part_room_runs = 0, part_stripped = 0, part_spare_freed = 0, bare_moves = 0;
    uint32_t mirror_nan_heals = 0, mirror_nan_turns = 0;
    uint32_t sap_over = 0, sap_peak = 0, sap_sweeps = 0, sap_left = 0;
    uint32_t sap_pairs = 0, sap_pair_cap = 0, sap_pairs_full = 0; // tests (BUG-0342): pairs of the last sweep, the pair list size, sweeps that filled it
    uint32_t merge_ours = 0, merge_peak = 0, merge_clipped = 0; // tests: merges of ours, the largest array merged, entries cut at the array's size
    uint32_t joins_load = 0;     // tests: guest moved next to the host after a new avatar
    uint32_t bg_culled = 0;
    uint32_t leave_writes = 0, leave_dropped = 0; // BUG-0308: our pool work while the stage was left (off) / skipped (on)
};

struct Mirror {
    uint32_t local = 0;
    net::Vec3 pos, vel;
    net::Quat rot;
    float size = 1;
    int hp = 6;
    double seen = 0;
    float scale = 0; // the host's world scale G+0x514C of the sample (0: an older host without it)
    bool product = false; // BUG-0958: a player's product on the host (+0x110: a poison cloud), state flag 4
    double soft_at = 0;   // tests (BUG-1247): when h_Soft last turned it to the host's heading
    float soft_d = 0;     // ... and by how much (rad)
    float step_head = 0;  // tests (BUG-1247): its heading (+0x5C) right after the cell step, before ours is written back
    double step_at = 0;
};

struct FoodTx {
    int32_t food = -1, plant = -1;
    uint32_t ack = 0;
    double at = 0;
};

constexpr float kSmall = 0.5f;

enum : uint8_t { KO_UP = 0, KO_OUT = 1, KO_REVIVED = 2, KO_RESPAWNED = 3 };
struct Ko {
    uint8_t state = KO_UP;
    float left = 0, hold = 0;
    double since = 0, fx_at = 0;
    double refreshed = 0; // the last MSG_CELL_KO of its owner (KO_OUT comes every 0.25 s)
    bool expired = false; // ended here by ko_expire, not by its owner (BUG-0238: no effect replay)
    uint32_t fx = 0;      // death effects played on its puppet (tests)
};

constexpr uintptr_t kOpacity = 0xA4;

// MSG_CELL_EAT: u32 host index, u8 kind, u8 arg, i32 food, u8 plant (older guests send the index only)
enum : uint8_t { ACT_GONE = 0, ACT_KILL = 1, ACT_BREAK = 2 };

using NoAttack_t = bool(SK_CDECL*)(uint8_t*, uint8_t*);
using DamageCell_t = bool(SK_CDECL*)(uint8_t*, uint8_t*, const float*, int, int, float);
using Cloud_t = void(SK_CDECL*)(uint8_t*, int, void*);
using Zap_t = void(SK_CDECL*)(uint8_t*, uint32_t, uint32_t*);
using Chunk_t = void(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, float);
using KillCell_t = bool(SK_CDECL*)(uint8_t*, uint32_t, void*, bool, int, float);
using Break_t = void(SK_CDECL*)(uint8_t*, uint8_t*, float);

// tests (BUG-1064): `coop.pool areas mark` marks the cells in the host's visible box and in the same box round each
// partner's cell (area 0: the host, k: partner k); `coop.pool areas` tells per area how many are gone and which
// DestroyCell caller removed them (h_Destroy, by return address)
struct AreaMark {
    uint8_t area;
    uint8_t parent; // at the mark: 1 when the cell's parent cell +0x36C is alive (ShouldCullCell never culls it then)
    uint8_t ring;   // at the mark: its offset from the area's centre in sixths of the box half (max of x, y), 0..6
    int8_t lvl;     // at the mark: its level +0x358 - the food level
    uint32_t by; // DestroyCell's return address (rebased), 0 while not destroyed
};

// tests (BUG-1064): per area, the cull 0xE7C3E6 of a marked cell: how old the partner view boxes h_TileActive read were
// (sim frames since g_view_boxes was built), and how far the area's box centre was from the area's cell then
struct AreaCull {
    uint32_t n, stale, maxage, fresh_ok, lvl[8], layer1, box_ok, union_ok; // lvl: the culled tile's level - the food level, -2 .. +5
    float maxoff, maxrel;                        // maxrel: the cell's farthest offset from its area's cell in box halves
};

using Destroy_t = void(SK_CDECL*)(uint32_t, bool, float, int);

// tests (BUG-0480): when each host index was last sent, and where it was then (`coop.pool cell <key>` on the
// host names it for a cell it does not have now); pruned after 20 s
struct SentCell {
    double first, at;
    net::Vec3 pos;
    int level;
    uint32_t sends;
};

using EatFood_t = void(SK_CDECL*)(uint32_t, uint32_t, void*, float);
using ExecInter_t = void(SK_CDECL*)(uint32_t*, float);
using Swallow_t = void(SK_CDECL*)(uint8_t*, uint8_t*, int);
using AddFood_t = void(SK_CDECL*)(int, bool);

enum : unsigned { NF_POS = 1, NF_PREV = 2, NF_VEL = 4, NF_ACC = 8, NF_ROT = 16, NF_SHAPE = 32, NF_SIZE = 64,
                  NF_PPOS = 128, NF_PVEL = 256, NF_PGOAL = 512, NF_MASS = 1024 };

using Pair_t = void(SK_CDECL*)(uint8_t*, uint8_t*, float);
using Bite_t = bool(SK_CDECL*)(uint32_t, uint32_t, void*, void*, int, float);
using GetDamage_t = int(SK_CDECL*)(uint8_t*, uint8_t*);

struct Contact {
    uint32_t handle, a, b;
    float point[3], normal[3], depth, accum, unk2c, mass, mass2, target;
    uint32_t frame;
};
using ContactPass_t = void(SK_CDECL*)();
using ContactSolve_t = int(SK_CDECL*)(Contact*);
using Wipe_t = void(SK_CDECL*)();
using Populate_t = int(SK_CDECL*)(float, int, int, void*, void*, void*, int);

constexpr uint32_t kSendCap = 900, kMirrorCap = 1500;

struct TileList {             // the game's fixed_vector layout (PopulateTiles reads +0..+8, +0x10, +0x1018)
    int32_t* begin;
    int32_t* end;
    int32_t* cap;
    uint32_t pad;
    int32_t* fixed;
    uint8_t rest[0x1020 - 0x14];
};
struct PartnerTiles {
    TileList* list = nullptr;
    int32_t* buf = nullptr;
    double seen = 0;
};

using PopTiles_t = void(SK_CDECL*)(int, TileList*, int);
using PopTile_t = void(SK_CDECL*)(uint32_t*, int);
using TileActive_t = bool(SK_CDECL*)(const int32_t*);
using WorldScale_t = void(SK_CDECL*)(float);
using PickTargets_t = bool(SK_CDECL*)(uint8_t*, void*);
using SpawnLoot_t = void(SK_CDECL*)(void*, void*, void*, void*, float, int, void*, float, uint32_t*, int*);

// The cells' soft-body particles come from one pool of 1500 (G+0x4108, ParticleAlloc 0xE4DBA0).
// A full pool returns -1 and the game keeps it in the cell's list (CreateSoftBody 0xE6D8F0, the
// re-tier 0xE51A20 when a cell grows or shrinks; the re-tier even frees the -1 of a cell without
// particles). The game then reads and writes particle -1, 8 bytes before the pool's block (its heap
// header; a cell's centre from it jumped thousands of metres), and ParticleFree 0xE4EC10 (no bound
// check) puts -1 on the free list: the next allocations read garbage and fault every frame at
// 0xE4DBD9, and the count fell below what is used (-25 seen). The fix: the pool never gets full;
// a full pool's allocation gets the spare slot 1499 (the game never hands it out),
// set to the asking cell's position, and freeing it, or anything outside the pool, does nothing.
// The game itself never hands out slot 1499 (it refuses at 1499 used, its last slot is 1498).
constexpr int kParticles = 1500, kSpare = kParticles - 1;

using Soft_t = void(SK_CDECL*)(uint8_t*, void*, float);

// cellsync.cpp
extern const SkHost* H;
extern SkMod* M;
extern Ctx C;
extern uintptr_t a_ModelData;
extern uintptr_t a_IsPrey;
extern uintptr_t a_Rollover;
extern uintptr_t a_Bite;
extern uintptr_t a_RefCreate;
extern uintptr_t a_Cine;
extern uintptr_t a_CellUI;
extern uintptr_t a_TargetParams;
extern uintptr_t a_EatFood;
extern uintptr_t a_IsThreat;
extern uintptr_t a_GetData;
extern uintptr_t a_ScaleDiff;
extern uintptr_t a_Serializer;
extern uintptr_t a_GetDamage;
extern Counters g_n;
bool is_host();
bool is_guest();
// BUG-9275 (cellsync_lineage.cpp): PopulateMarker's depth on this game (h_Populate), the chooser's registration, the
// stage's preloads forgotten, a guest's key of a host cell model, a loot drop's part tokens
extern int g_marker_depth;
void lineage_init();
void lineage_forget();
void lineage_mirror_key(const uint32_t key[3], uint32_t use[3]);
void lineage_loot(void* res, const uint32_t* cells, int n);
// BUG-9285 (cellsync_tiles.cpp): whose area the running PopulateTiles pass fills: a partner's player id, -1 the
// host's own pass or none
int tile_pass_player();
const char* kind_of(uint8_t* c);
bool in_cell();
uint8_t* G();
uint32_t avatar_idx();
uint8_t* avatar();
uint32_t mate_idx();
extern uint32_t g_matings, g_egg_lays; // tests (BUG-1245)
// BUG-1246 (cellsync_mate.cpp): a guest's mate in the host's game
void mate_in(int from, net::Reader& r);
void mate_tick(double now);
void mate_forget();
void mate_player_left(int id);
void mate_session_end();
std::string mate_status();
uint32_t mate_copy_of(int id);
bool is_mate_copy(uint32_t idx);
extern bool g_fix_goldie_keep; // BUG-1248
void mirrors_after_step(); // tests (BUG-1247): right after each cell step (modes' SimUpdate hook)
bool tutorial_cell(uint8_t* c, uint32_t idx); // BUG-1248: one of our own Goldie tutorial's two cells while it runs
bool tutorial_cells(uint32_t* goldie, uint32_t* hunter, char* why, size_t n); // BUG-1248: the two cells of the running tutorial's event
void* fg_query();
uint8_t* S();
net::Vec3 pos_of(uint8_t* c);
void set_pos(uint8_t* c, const net::Vec3& p);
extern double g_last_send;
extern double g_last_food;
extern uint16_t g_cycle;
extern int32_t g_food_sent;
extern int32_t g_plant_sent;
extern std::unordered_map<uint32_t, Mirror> g_mirrors;
extern std::unordered_map<uint32_t, uint32_t> g_host_of;
extern std::unordered_set<uint32_t> g_cycle_seen;
extern bool g_food_guard;
extern bool g_replay;
extern bool g_our_destroy;
extern int g_pend_food;
extern bool g_pend_plant;
extern bool g_mirror_fixes;
extern bool g_leave_fix;
extern bool g_fix_purge;
extern bool g_fix_hostof;
extern bool g_fix_foodseq;
extern bool g_fix_resend;
extern bool g_fix_kostale;
extern bool g_fix_koreset;
extern bool g_fix_bodylevel;
extern bool g_fix_guestbite;
float world_scale();
extern bool g_fix_ko_no_swallow;
extern double g_kodrop_until;
extern double g_statehold_until;
extern bool g_test_away;
extern bool g_solo;
extern int g_solo_food[2];
extern bool g_solo_owed;
extern bool g_fix_solo_elsewhere;
extern bool g_fix_send_here;
extern cell_logic::Rounds g_rounds;
bool kill_send_first(uint32_t hidx, double now);
extern bool g_in_guest_attack;
extern bool g_fix_eat_once;
extern cell_logic::Once g_eaten;
extern std::unordered_map<uint32_t, uint32_t> g_gone_sends;
extern cell_logic::Meals g_meals;
extern std::unordered_map<int, uint32_t> g_food_ack;
extern std::unordered_map<int, FoodTx> g_food_tx;
extern double g_invuln_until;
extern bool g_downed;
extern float g_body;
extern float g_body_size;
extern bool g_fix_session_reset;
float revive_dist(uint8_t* a);
void camera_snap(const net::Vec3& p);
void stop_at(uint8_t* c, const net::Vec3& p);
void set_body(uint8_t* a, float factor);
extern std::unordered_map<int, Ko> g_ko;
std::string nearest_partner(float max_dist, net::Vec3* where = nullptr, float* dist = nullptr, bool helper = false);
extern bool g_fix_spawn_slots;
bool spawn_spot(net::Vec3* at, float* dist, bool helper, float old_dx, float old_dy);
bool crowded(uint8_t* a, float r);
void effect(uint8_t* c, const char* name);
uint8_t* puppet_cell(int id);
bool go_down(uint8_t* c);
bool res_types(uint8_t* c, int* cell_type, int* ai_type);
int ai_type(uint8_t* c);
void hurt_avatar(int dmg, int type);
std::string away_status();

// cellsync_combat.cpp
void send_act(uint32_t hidx, uint8_t kind, uint8_t arg, int food, bool plant);
void flush_food();
extern DamageCell_t o_DamageCell;
extern bool g_fix_chunk_killer;
void send_hit(uint32_t hidx, int dmg, int type);
extern uintptr_t a_PoisonStep;
void puppet_poison(float dt);
extern bool g_fix_partner_poison;
extern std::unordered_set<uint32_t> g_partner_clouds;
extern char g_own_hit_last[160];
extern uint32_t g_own_hits_n; // tests (BUG-1243)
bool players_hurt_each_other();
extern Cloud_t o_Cloud;
extern bool g_test_no_own_cloud;
void SK_CDECL h_Cloud(uint8_t* maker, int size, void* at);
extern Zap_t o_Zap;
extern uintptr_t a_Zap;
void SK_CDECL h_Zap(uint8_t* att, uint32_t a2, uint32_t* first);
int on_screen_dbg(uint8_t* c);
float rollover_of(uint32_t idx);
extern const char* g_bite_ring_skip;
bool SK_CDECL h_DamageCell(uint8_t* victim, uint8_t* attacker, const float* dir, int type, int x, float dt);
extern Chunk_t o_ChunkB;
extern Chunk_t o_ChunkA;
void SK_CDECL h_ChunkA(uint32_t e, uint32_t v, uint32_t a, uint32_t b, float dt);
void SK_CDECL h_ChunkB(uint32_t e, uint32_t v, uint32_t a, uint32_t b, float dt);
extern KillCell_t o_KillCell;
extern std::unordered_map<uintptr_t, uint32_t> g_destroy_callers;
bool SK_CDECL h_KillCell(uint8_t* c, uint32_t killer, void* res, bool by_player, int type, float dt);
extern Break_t o_Break;
void SK_CDECL h_Break(uint8_t* victim, uint8_t* attacker, float f);
extern bool g_in_wipe;
extern std::unordered_map<uint32_t, AreaMark> g_area_mark;
extern float g_centre_err_max;
extern float g_centre_err_moving;
extern AreaCull g_area_cull[4];
extern uint32_t g_boxes_frame;
extern uint32_t g_area_skips0;
extern uint32_t g_sim_frame;
extern uintptr_t a_CullRet;
extern Destroy_t o_Destroy;
extern std::map<uintptr_t, uint32_t> g_dbg_destroy_by;
extern uint32_t g_mate_gone_by; // tests (BUG-1186): the return address (rebased) of the DestroyCell of our mate G+0x51D4
extern double g_mate_gone_at;
extern std::unordered_map<uint32_t, double> g_touched;
extern std::unordered_map<uint32_t, double> g_swallowed;
extern std::unordered_map<int, void*> g_puppet_seen;
extern std::unordered_map<uint32_t, SentCell> g_sent_at;
extern double g_sent_pruned;
extern std::map<uintptr_t, uint32_t> g_quick_destroy;
extern std::map<uintptr_t, uint32_t> g_product_destroys;
extern uintptr_t a_PoisonCullRet;
void SK_CDECL h_Destroy(uint32_t idx, bool loot, float f, int x);
extern EatFood_t o_EatFood;
void SK_CDECL h_EatFood(uint32_t eater, uint32_t food, void* ref, float dt);
extern ExecInter_t o_ExecInter;
extern uintptr_t a_ExecInter;
void SK_CDECL h_ExecInter(uint32_t* in, float dt);
extern Swallow_t o_Swallow;
void SK_CDECL h_Swallow(uint8_t* eater, uint8_t* food, int type);
extern AddFood_t o_AddFood;
extern std::vector<std::pair<double, std::function<void()>>> g_food_q;
void food_in_step(std::function<void()> f);
void food_drain(bool late_only);
void SK_CDECL h_AddFood(int amount, bool plant);
bool fin(const float* v, int n);
unsigned cell_nonfinite(uint8_t* c);
extern uint32_t g_shared_parts;
extern float g_last_dt;
void own_probe(const char* where, uint8_t* other = nullptr);
void shared_parts_scan(double now);
extern uint32_t g_stack_idx;
extern uint32_t g_stack2_nf;
extern uint32_t g_stack2_a;
extern std::vector<uint32_t> g_stack2;
extern float g_stack2_vmax;
extern int g_stack2_frames;
extern int g_stack_frames;
extern Pair_t o_Pair;
extern Bite_t o_Bite;
bool SK_CDECL h_Bite(uint32_t att, uint32_t vic, void* p, void* d, int x, float dt);
extern bool g_fix_guest_hit_game;
extern uint32_t g_guest_attack_types[8];
extern GetDamage_t o_GetDamage;
int SK_CDECL h_GetDamage(uint8_t* att, uint8_t* vic);
bool guest_attack(uint32_t from, uint8_t* c, uint8_t* pc, int dmg, int t, bool lethal);
bool guest_knocked_out(uint32_t from);
void SK_CDECL h_Pair(uint8_t* a, uint8_t* b, float dt);

// cellsync_physics.cpp
extern ContactPass_t o_ContactPass;
extern ContactSolve_t o_ContactSolve;
extern uintptr_t a_ContactSolve;
extern uintptr_t a_ContactPass;
int SK_CDECL h_ContactSolve(Contact* k);
void SK_CDECL h_ContactPass();
extern uintptr_t a_PartAlloc;
extern uintptr_t a_PartFree;
extern int(SK_CDECL* o_PartAlloc)(uint8_t*, const float*, const float*, float, int);
extern void(SK_CDECL* o_PartFree)(uint8_t*, int);
extern uint32_t g_part_spare;
extern uintptr_t g_part_site[1500];
int particles_used();
void particle_state(int* minus1_cells, int* head_bad);
extern uintptr_t a_GfxPtr;
extern uintptr_t a_CreateSoft;
extern void(SK_CDECL* o_CreateSoft)(uint32_t, int);
uint32_t* gfx_pool();
void SK_CDECL h_CreateSoft(uint32_t idx, int arg);
void make_particle_room(uint8_t* parts, int asking);
void spare_tick();
int SK_CDECL h_PartAlloc(uint8_t* parts, const float* pos, const float* vel, float w, int cell);
void SK_CDECL h_PartFree(uint8_t* parts, int i);
extern int g_nan_vel_frames;
extern bool g_fix_mirror_nan_heal;
extern Soft_t o_Soft;
void SK_CDECL h_Soft(uint8_t* c, void* res, float dt);
extern uintptr_t a_UpdatePairs;
extern uintptr_t a_MergeAxis;
extern uintptr_t a_Overlap;
extern void(__cdecl* o_UpdatePairs)(uint8_t*);
extern bool g_fix_sap_merge;
void __cdecl h_UpdatePairs(uint8_t* s);

// cellsync_tiles.cpp
extern Wipe_t o_Wipe;
void SK_CDECL h_Wipe();
extern Populate_t o_Populate;
int SK_CDECL h_Populate(float prob, int layer, int level, void* query, void* bbox, void* marker, int x);
extern bool g_fix_finite;
extern double g_tilecap_max;
extern uintptr_t a_TileSizes;
bool pose_finite(const net::Vec3& p, float vx, float vy, float size);
extern uint32_t kPopCap;
extern uint32_t kPopCapScreen;
extern int g_tile_level;
extern std::unordered_map<uint32_t, uint8_t> g_starved;
uint32_t cells_live();
extern uintptr_t a_TileActive;
extern uintptr_t a_PopTiles;
extern uintptr_t a_PopTile;
extern uintptr_t a_ViewBoxes;
extern std::unordered_map<int, PartnerTiles> g_ptiles;
extern views::Set g_views;
extern int g_test_view_edge;
extern PopTiles_t o_PopTiles;
extern PopTile_t o_PopTile;
extern TileActive_t o_TileActive;
float* view_boxes();
void SK_CDECL h_PopTile(uint32_t* t, int all);
bool SK_CDECL h_TileActive(const int32_t* t);
void area_cull_note(uint8_t area, uint8_t* c);
extern WorldScale_t o_WorldScale;
extern uintptr_t a_WorldScale;
bool scale_moving();
net::Vec3 first_sphere(uint8_t* c);
void stack_pair(uint8_t* av, uint8_t* other);
void SK_CDECL h_WorldScale(float dt);
void partner_tiles_reset(bool drain);
void SK_CDECL h_PopTiles(int level, TileList* list, int all);
extern NoAttack_t o_NoAttack;
NoAttack_t& o_NoAttack_ptr();
bool SK_CDECL h_NoAttack(uint8_t* a, uint8_t* b);
extern PickTargets_t o_PickTargets;
extern uintptr_t a_PickTargets;
bool SK_CDECL h_PickTargets(uint8_t* c, void* res);
extern SpawnLoot_t o_SpawnLoot;
extern uint32_t g_loot_big_n;
extern int g_loot_listed_last;
extern int g_loot_listed_big;
extern std::string g_loot_seen;
extern std::deque<std::pair<uint32_t, double>> g_loot_recent;
uint32_t pool_live();
void SK_CDECL h_SpawnLoot(void* res, void* key, void* query, void* pos, float size, int level, void* table, float dt, uint32_t* out, int* nout);

// cellsync_host.cpp
extern bool g_fix_send_centres;
extern float g_send_centre_lag;
extern float g_send_centre_lag_max;
void host_tick(double now);

// cellsync_guest.cpp
void destroy_local(uint32_t idx);
void guest_state(net::Reader& r);
extern bool g_own_good_set;
void heal_own(uint8_t* a, const net::Vec3& me, bool bad);
extern uint32_t g_join_avatar;
extern double g_join_since;
extern double g_join_check;
extern int g_join_tries;
void guest_tick(double now);
void solo_reset();
void solo_send();
void solo_tick();

// cellsync_debug.cpp
const char* kind_of(uint8_t* c);
std::string see(int max);
std::string prey_report(float r, int mode);
std::string sap_report();

// every live foreground cell (gameplay layer), avatar excluded
template <typename Fn>
static void for_each_cell(Fn fn)
{
    uint8_t* g = G();
    if (!g)
        return;
    uint8_t* data = F<uint8_t*>(g, 0x1C);
    uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
    if (!data || size != 0x398)
        return;
    void* fq = fg_query();
    uint32_t av = avatar_idx();
    for (uint32_t slot = 0; slot < used && slot < 4096; slot++) {
        uint8_t* c = data + (size_t)slot * size;
        uint32_t idx = F<uint32_t>(c, 0);
        if ((idx & 0xFFFF) != slot || idx == av || F<void*>(c, kQuery) != fq)
            continue;
        fn(c, idx);
    }
}

} // namespace cellsync
