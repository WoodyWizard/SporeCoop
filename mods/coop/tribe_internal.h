// tribe.cpp's state and helpers its split files share (C8 of 0.7.15, BUG-1162): the types, constants and macros
// more than one of them uses, and the declarations of the variables and functions one file defines and another
// uses. Not an interface for other modules (that is tribe.h). docs/01-architecture.md#arch-tribe-files.
#pragma once

#include "tribe.h"
#include "tribe_logic.h"
#include "call_guard.h"
#include "lifecycle.h"
#include "mirror_registry.h"
#include "player_life.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "civ.h"
#include "coop_ui.h"
#include "creations.h"
#include "driver.h"
#include "effects.h"
#include "game.h"
#include "modes.h"
#include "npc.h"
#include "savestrip.h"
#include "space_ext.h"
#include "stage_epoch.h"
#include "world.h"
#include "owner_claims.h"
#include "autosave_glue.h"
#include "store.h"
#include "wsync.h"
#include "wire/tribe_anim.h"
#include "net/held_cmds.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/snapshot_rx.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "settings.h"
#include "fields.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {

extern const SkHost* H;
extern SkMod* M;
extern Ctx C;
extern bool g_ok;

// ---- layout (docs/re/tribe.md) ----
constexpr uint32_t kTribeNoun = 0x18C6D19;
constexpr uintptr_t kMembersOff = 0x340;   // cTribe: vector<cCreatureCitizen*>

constexpr uintptr_t kOwnerTribeOff = 0x1014; // cCreatureCitizen::mpOwnerTribe
constexpr uintptr_t kAgeOff = 0xB34;       // 0 baby, 1 adult
constexpr uintptr_t kSelectedOff = 0x12C;  // spatial+0x6C IsSelected

extern uintptr_t a_AddMember;
extern uintptr_t a_CreateCitizen;
extern uintptr_t a_DoAction;
extern uintptr_t a_DoActionForObjects;
extern uintptr_t a_MaxPop;
extern uintptr_t a_SetValueVector3;
extern uintptr_t a_SpawnNpcTribe;
extern uintptr_t a_WalkSelected;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

bool is_host();
bool is_guest();
extern bool g_fix_away;
bool local_play();
void anim_send(uint8_t to, const net::Writer& w, bool reliable);
void* player_tribe();
bool is_citizen(void* c);
std::vector<void*> members(void* t);
std::vector<void*> all_tribes();
net::Vec3 tribe_pos(void* t);
bool is_tree(void* t);

template <typename Fn>
static void for_each_tree(Fn fn)
{
    static uintptr_t get = H->sym("SporeCoop::Flora_Get");
    uint8_t* fl = get ? skg::cdecl_call<uint8_t*>(get) : nullptr;
    if (!fl || IsBadReadPtr(fl + 0x1E5F0, 8))
        return;
    uint8_t* b = F<uint8_t*>(fl, 0x1E5F0);
    uint8_t* e = F<uint8_t*>(fl, 0x1E5F4);
    constexpr size_t kPatch = 0x19A8;
    if (!b || e < b || (size_t)(e - b) % kPatch || (size_t)(e - b) / kPatch > 20000)
        return;
    for (uint8_t* el = b; el < e; el += kPatch) {
        uint8_t* rec = el + 8;
        for (uintptr_t vo : {0x44u, 0x9Cu, 0xF4u}) {
            void** tb = F<void**>(rec, vo);
            void** te = F<void**>(rec, vo + 4);
            if (!tb || te < tb || te - tb > 64 || IsBadReadPtr(tb, (te - tb) * 4))
                continue;
            for (void** it = tb; it < te; ++it)
                if (is_tree(*it))
                    fn(*it, net::Vec3{F<float>(*it, 0x10), F<float>(*it, 0x14), F<float>(*it, 0x18)});
        }
    }
}

void* find_tree(const net::Vec3& pos, float max_dist);

// tribe_graduation.cpp (BUG-1325): the end of Tribe's TribeStage_Graduate hook and its test command
void graduation_init();
std::string graduation_cmd(const std::vector<std::string>& a);

// ---------------- host ----------------
// C1 (BUG-1140): one entry of the host's member table (tribe.hm); its id, object and serial are the registry entry's
struct HMember {
    uint8_t owner = 0;
    uint32_t bhv_anim = 0; // BUG-0569: the last animation its behaviour tree started
    uint32_t idle_anim = 0; // BUG-0936: the last animation its idle refresh played (its own idle / walk choice)
    uint32_t hold = 0;      // BUG-0914: the hand's started hold modifier last seen (handle), 0 none
    void* tribe = nullptr; // BUG-0620: the tribe whose member list held it last
    double body_since = 0; // BUG-0620: when it left that list dead (its body), 0: in a list
    uint32_t bt_ticks = 0; // BUG-0531 (diagnostics, `coop.tribe ai`): its behaviour tree's ticks in the host's game
};

extern bool g_kept_refs_fix;
extern uint32_t g_kept_stale;
bool kept_alive(void* o, uint32_t serial, uint32_t noun);
extern bool g_join_body_fix;
extern uint32_t g_join_bodies;

constexpr double kBodyMax = 60.0;
constexpr uint8_t kBodyBit = 0x80; // in a snapshot entry's age byte: a body (the guest takes it out of its tribe too)

extern std::unordered_map<void*, void*> g_detached;
extern uintptr_t a_BTTerminate;
extern uint32_t g_teardown_restored;
void detach_body(void* c);

// C1 of 0.7.15 (BUG-1140, docs/01-architecture.md#arch-mirror-registry): the host's member table (g_hm / g_hm_by_id,
// keyed both ways by hand) and the guest's mirrors (g_gm / g_gid_of) are mirror::Registry tables: one entry per net
// id with its object (pointer, serial, noun), the reverse index kept by the registry. The host adopts every member of
// the tribes' lists (another serial at a known address is a new member: BUG-0951) and sweeps the entries its pass did
// not see (a body is kept, BUG-0620); the guest binds a mirror to its object and sweeps after a complete snapshot (C4).
// Liveness is the registry's injected rule: the object listed now with its serial (game::LiveRef), or its own memory
// with the old switches off (tribe.kept_refs on the host, tribe.mirror_member_lifetime on the guest).
constexpr uint32_t kMemberNoun = 0x18EB4B7; // (kCitizenNoun, below)

bool hm_alive(const mirror::Ref& r);

using HReg = mirror::Registry<uint16_t, HMember>;
using HEntry = HReg::Entry;
// C8 (BUG-1162): the host's member table and how its members are dealt to the players
struct HostTable {
    HReg members{"tribe.hm", hm_alive};
    uint16_t next_id = 1;
    uint32_t dealt = 0;                        // the player tribe's members dealt in turns since the last split
    std::vector<uint8_t> split_for;            // player ids the player tribe was split for
    std::unordered_map<void*, uint8_t> born_for; // BUG-0671: a new member made for a guest -> its owner
    uint32_t bodies_sent = 0;                  // BUG-0620 (diagnostics)
};

extern HostTable g_host;
HEntry* hm_of(const void* c);
void* hm_obj(uint16_t id);
bool hm_ok(const HEntry& e);

// net::alloc_id's view of the ids in use
struct HIds {
    size_t count(uint16_t id) const { return g_host.members.has(id) ? 1 : 0; }
};

extern double g_last_send;
extern uint16_t g_cycle;
extern uint32_t g_cmds_done;
extern uint32_t g_cmds_rejected;
extern std::string g_last_cmd;
extern uint32_t g_gifts;
extern float g_test_lift;

// one guest mirror of a host member (tribe.gm): its id (the host's member id), object and serial are the registry entry's
struct GMember {
    uint8_t tribe = 0, owner = 0xFE, age = 1;
    bool body = false; // BUG-0620: the host's member is a body out of its tribe's list
    net::Vec3 pos, vel;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    drive::State ds;
    net::SnapshotBuffer buf; // BUG-0365: the host's states by its clock
    net::Vec3 aim;           // coop.track: where the driving aimed this frame
    float aim_speed = 0;     // and the host's speed of that state
    float walk_sp = 0;       // BUG-0619: the speed the smoothed target's mirror is walked at (eased in)
    double walk_t = 0;
    bool jittery = false;    // BUG-0619 (diagnostics)
    float lead_sp = -1;      // BUG-0619: the raw lead's speed while the smoothed target drives it (-1: none)
    double held_since = 0, held_said = -1e9; // BUG-0386 (diagnostics)
    double test_undo_until = 0; // tests (BUG-0386, `coop.tribe undo <id> <s>`): this game undoes the mirror's placing
    net::Vec3 test_undo_at{};   // and its walk: where it is held
    double anim_rx_at = 0, anim_fix_at = 0; // BUG-0569: the last relayed start, the last snapshot play
    const char* not_made = nullptr; // BUG-0685: why no mirror was made for it last time (logged when it changes)
    float air_agl = 0;              // BUG-0946: the host's member over the host's ground (m), when it was off it
    double air_at = -1e9;           // and when that came (MSG_NPC_ANIM kind 9)
    bool air = false;               // placed in the air last frame (drive::air_place)
    uint32_t hold_anim = 0;         // BUG-0914: the animation the host's member took its item in (kind 11), 0 none
    double hold_at = -1e9;          // when that came
    bool hold_bound = false;        // the mirror's hold modifier was started in that animation here
    uint32_t hold_anim_bound = 0;   // (that animation)
    int hold_item = 0;              // BUG-0971: the item the host's member holds in that hold (kind 12), 0 unknown
    double hold_play_at = -1e9;     // BUG-0968: when hold_try last started the host's hold animation on the mirror
    double hold_end_at = -1e9;      // the host's last end (reliable): an older start overtaking it is no new hold
    double hold_set_at = 0;         // BUG-1347: when hold_try (or its retry) attached the item for a bound hold, 0 none
    uint8_t hold_retries = 0;       // BUG-1347: the attaches tried again for that bind
    uint32_t relayed_anim = 0;      // BUG-0936: the last animation the relay (kinds 2 / 3 / 10) played on it
};

extern bool g_member_lifetime_fix;
bool gm_alive(const mirror::Ref& r);

using GReg = mirror::Registry<uint16_t, GMember>;
using GEntry = GReg::Entry;
// C8 (BUG-1162): the guest's mirrors of the host's members, the host's snapshot cycles they come in (C4) and the
// host tribe index -> local tribe map of the last snapshot
struct GuestMirrors {
    GReg mirrors{"tribe.gm", gm_alive};
    // policy: 255 chunks, keep_order = net.cycle_order (read with every chunk), 0x8000 behind is older, sweep once,
    // the last chunk completes a cycle
    net::SnapshotReceiver<uint16_t> snap;
    std::vector<void*> local_tribe_of; // host tribe index -> local tribe
    uint32_t spawned = 0, removed = 0;   // mirrors spawned / removed by the mod
    uint32_t stale = 0, remove_refused = 0; // BUG-0844 (diagnostics)
};

extern GuestMirrors g_guest;
bool mirror_ok(const GEntry& e);

// BUG-0685: a guest's log says when a member mirror goes and why one is not made again (at most kMirrorLogMax lines;
// a mirror's "not made" line only when its reason changes)
constexpr uint32_t kMirrorLogMax = 60;

extern uint32_t g_mirror_logs;

#define MIRROR_LOG(...) \
    do { \
        if (g_mirror_logs < kMirrorLogMax) { \
            g_mirror_logs++; \
            H->log(M, SK_INFO, __VA_ARGS__); \
        } \
    } while (0)

extern uint32_t g_mirror_timed;
extern uint32_t g_mirror_walks;
extern uint32_t g_mirror_grounded;

constexpr double kMirrorDelay = 0.2;  // s: the interpolation delay (the host sends every 0.12 s or more)

extern double g_test_delay;
extern bool g_test_lag_fix;

constexpr float kMirrorWalkMax = 8.f; // farther off: drive::step (it teleports at 8 m)

extern bool g_mirror_keep_fix;

// BUG-0619: a member's lead jump up to this is smoothed (a start known late over a mobile network: 2.9-4 m at 9 m/s;
// the lead's cap kMaxLead x 9 m/s = 5.4 m)
constexpr float kMirrorSmoothMax = 6.f; // m
constexpr float kMirrorWalkAcc = 40.f; // m/s2 (BUG-0619)
constexpr float kMirrorWalkOver = 3.f; // m/s: walked at most this faster than the lead itself (BUG-0619)
constexpr double kPathDt = 0.1;       // s: the interpolated path's velocity is taken over this

extern bool g_cycle_order_fix;
extern uint32_t g_map_stale_skipped;
extern bool g_state_hold;
uint8_t owner_of(void* c);
extern std::unordered_set<void*> g_bt_local;

// tests (BUG-1084, `coop.tribe freezeat <id> <node> [state]`): a guest's mirror whose tree reaches that node (in that
// state or a later one) while the host is away is held there (skipped as if the host were back) until the host is back
struct FreezeAt {
    uint32_t node;
    int state; // the lowest state
    int ticks; // ticks in that node and state before it is held
};

extern std::unordered_map<void*, FreezeAt> g_bt_freeze_at;
extern std::unordered_set<void*> g_bt_frozen;

struct Target {
    uint8_t kind = 0; // 0 none, 1 citizen id, 2 object by noun + position, 3 tree by position
    uint16_t id = 0;
    uint32_t noun = 0;
    net::Vec3 pos;
};

void write_target(net::Writer& w, const Target& t);
Target read_target(net::Reader& r);

enum : uint8_t { CMD_ACTION = 0, CMD_WALK = 1, CMD_GIVE = 2, CMD_BABY = 3, CMD_CONQUER = 4 };

extern bool g_away_conquest_fix;
extern uint32_t g_conquests_done;
extern uint32_t g_conquests_refused;
extern uint32_t g_conquests_sent;

struct Ring {
    void* target = nullptr;
    std::vector<uint16_t> ids;
    double since = 0;
    uint32_t serial = 0; // BUG-0951
};

extern Ring g_ring;
extern uint32_t g_rings_drawn;
bool ring_shown(void* t);
void* member_mirror(uint16_t id);

// C8 (BUG-1162): the counters of the member animation relay (kinds 2 / 3), the holds (11 / 12), the air (9), the idle
// (10), the phase (14 / 15) and the tracks (16), the theme (4-6) and party music (7 / 8): diagnostics, `coop.tribe anims`
struct AnimStats {
    uint32_t hold_sent = 0, hold_rx = 0, hold_bound = 0, hold_item_sets = 0, hold_anim_plays = 0, hold_queued = 0;
    uint32_t hold_retried = 0; // BUG-1347
    uint32_t anim_relay_sent = 0, anim_relay_played = 0, anim_relay_failed = 0, anim_relay_rx = 0, anim_snap_played = 0;
    uint32_t air_sent = 0, air_rx = 0, air_placed = 0, air_landed = 0;
    uint32_t idle_sent = 0, idle_refreshed = 0, idle_played = 0, stale_cut = 0;
    uint32_t phase_sent = 0, phase_rx = 0, phase_set = 0, phase_unlooped = 0, tracks_sent = 0, tracks_rx = 0, tracks_set = 0;
    uint32_t theme_sent = 0, theme_rx = 0, theme_played = 0;
    uint32_t party_sent = 0, party_rx = 0, party_played = 0, party_stopped = 0;
};

extern AnimStats g_anim;

// ---- tools (BUG-0514, docs/re/tribe.md#tribe-tools-guest) ----
// A member's tool (+0x102C mSpecializedTool: 1..9 the racks' weapons and instruments, 9 heal, 0xB the chief's
// staff) is its own game's: the host's game runs the guest's members (a GrabTool order of a guest is done
// there), the guest's mirror never took the tool, so the guest saw its members walk to the rack and come
// back empty-handed (no weapon in hand, no heal order: InputAction 0x28 takes only members with tool 9).
// The held tool is a replicated field of the members (its own kind); the guest sets it on its mirror with the
// game's SetSpecializedTool.
constexpr uintptr_t kToolsOff = 0x36C;    // cTribe: vector<cTribeTool*> mTools
constexpr uintptr_t kHeldToolOff = 0x102C; // cCreatureCitizen: mSpecializedTool

extern uintptr_t a_SetChieftain;
uint16_t member_id(void* c);
extern uintptr_t a_SetSpecializedTool;
extern uintptr_t a_SetToolPurchased;
extern uintptr_t a_ToolLayout;
extern uintptr_t a_ToolModelReset;
extern uint32_t g_leader_tools_kept;
extern uint32_t g_tool_sets;
bool is_leader(void* c);
void apply_held_tool(void* c, float v);

// BUG-0906: what a member holds in its hand (+0xFFC mCurrentHandheldItem: 0x11 the chief's staff, 1..4 / 0xD..0x10 /
// 0x12 the racks' weapons, instruments and the healer's rod, 0x13.. what it carries) is put there by the game's
// behaviours (the grab 0xDAB4A0, the idle and gather nodes: 0xC26470 / 0xC250A0), never by SetSpecializedTool, which
// only writes the tool type. A guest's mirrors run no behaviours: the mirror of the host's chief stood without its
// staff (the tool read 0xB on both games) and the mirrors of members with a weapon or an instrument were empty-handed
// (seen in the same_view_tribe key frames, lab 20261110-201619). The held item is a replicated field of the members
// (its own kind and switch): the guest attaches the host member's item with the game's own call. On the wire the
// item + 1 (values under 0.001 are not sent). `coop.fix tribe.held_item off`: the mirrors' hands as before.
constexpr uintptr_t kHeldItemOff = 0xFFC; // cCreatureCitizen: mCurrentHandheldItem
constexpr uintptr_t kHandOff = 0xFC4;     // ... the hand (an attachment slot) the item goes to

extern uintptr_t a_SetHandheldItem;
extern uintptr_t a_ClearPending;
extern uint32_t g_item_sets;
bool has_body(void* c);
// BUG-1271 (tribe_species.cpp): the members of their owner's own species
void species_init();
void species_tick(double now);
std::string species_status();
// BUG-1299 (tribe.birth_species): a member the game makes now (a baby, a guest's mirror) whose owner is known is born
// in that owner's line species: between birth_begin(tribe, owner) and birth_end(the new members) the tribe's species
// slot answers the line's profile to the citizen's init (0xC27330 reads cTribe_SpeciesSlot); nothing is written into
// the tribe. birth_end notes the new members as changed by the mod (they go back to the slot like the others)
void birth_begin(void* t, uint8_t owner);
void birth_end(const std::vector<void*>& made);
std::string species_list();
extern uint32_t g_item_holds_ended;
uint32_t hold_modifier(void* c, bool end);
void set_hand(void* c, int item);
// BUG-1335 (creature.fx_bone): the member's body has its hand bone (an item attached there now cannot fault)
bool hand_ready(void* c);
extern uint32_t g_hand_waits, g_hand_repairs;
struct HandPlant { // tests (`coop.tribe hand_plant`): a hand bone planted past the body's bones, put back next tick
    void* c = nullptr;
    uint32_t serial = 0;
    int hand = -1;
};
extern HandPlant g_hand_plant;
extern bool g_mirror_hold_fix;
extern bool g_hold_item_fix;
extern bool g_hold_anim_fix;
extern bool g_hold_queued_fix;
void hold_try(GEntry& e, double now);
// BUG-0992: the pending tracks of animation `cur` ended with the game's own call (the attach binds the hold to them first)
bool clear_pending_same(void* c, uint32_t cur);

// tests (BUG-0968 / BUG-0970): the last hold events of the guest's mirrors (`coop.tribe holdtrace [id]`)
struct HoldEv {
    double t;
    uint16_t id;
    char what; // S host start rx, E host end rx, T bound by hold_try, t hold_try miss (cur), Q bound in the mirror's current animation, W pending tracks ended (BUG-0992), O own hold seen, C own cut, M 1.5 s end, R relay played, H hand set by `coop.tribe hand` (tests), B a hold of that mirror's hand begins this frame (tests, 3 s after H), A the attach of a bound hold tried again (BUG-1347)
    uint32_t a, b;
};

extern HoldEv g_hold_ev[4096];
extern uint32_t g_hold_ev_n;
void hold_ev(uint16_t id, char what, uint32_t a, uint32_t b);
extern uint16_t g_hand_probe_id;
extern uint32_t g_hand_probe_handle;
extern double g_hand_probe_until;
int hand_model(void* c);
extern bool g_item_scale_fix;
float read_item_scale(void* c);
void apply_item_scale(void* c, float v);
extern bool g_hunger_fix;
extern uintptr_t a_SetHunger;
extern uint32_t g_hunger_sets;
extern uintptr_t a_ClockStop, a_ClockElapsed; // BUG-1187
extern bool g_hunger_full_fix;     // BUG-1187 (tribe.mirror_hunger_full)
extern uint32_t g_hunger_full_ended; // BUG-1187: mirrors' full clocks ended by the host's hunger
bool end_full_delay(void* c);      // BUG-1187: ends the member's full clock (true: it was running)

constexpr uintptr_t kHungerOff = 0xBBC;
constexpr uintptr_t kHungerClockOff = 0xB90; // BUG-1187: the full clock (a game clock: +0x18 running)
constexpr uintptr_t kHungerDelayOff = 0xB8C; // BUG-1187: how long a full member stays at 100 (ms)

int tool_type(void* tool);
void for_each_player_tool(void (*fn)(void* tool, int type, const net::Vec3& pos, void* user), void* user);
net::Quat upright(const net::Vec3& p);
void* make_player_tool(int type, const net::Vec3& pos);

// ---- every tribe's tools (BUG-0523 / BUG-0524, docs/re/tribe.md#tribe-tools-all) ----
constexpr uintptr_t kToolDragOff = 0x34 + 0x72; // the tool's spatial (+0x34) +0x72: the palette is dragging it

std::vector<void*> tribe_tools(void* t);
extern void* g_test_dragged;
void* tribe_of_index(int i);
void for_each_tool(bool all, ToolFn fn, void* user);
void* make_tool_in(void* t, int type, const net::Vec3& pos);
extern uint32_t g_tools_bought;
std::string ai_line(void* c);
void* SK_THISCALL h_Form(void* self, int arg);
using Form_t = void*(SK_THISCALL*)(void*, int);
extern Form_t o_Form;
extern uintptr_t a_NpcCtrl;
void theme_hooks_install();
void party_hooks_install();
void party_forget();
void baby_tick(double now);
void on_idle_anim(void* c, uint32_t anim);
void arrival_place_due(double now);

constexpr size_t kEntry = 2 + 1 + 1 + 1 + 12 + 12 + 16 + 4; // one member in a tribe state chunk
constexpr uint32_t kStateTrailer = 0x54435354u; // BUG-0365: a chunk's trailer, magic + the host's ms
constexpr size_t kTrailerSize = 8;

// BUG-0381: an NPC tribe the host's game formed during play (cNpcTribeController, 0xCC6440: "a new
// tribe has appeared") is formed by the host's game only: the guests' games never formed it (2 of 5
// tribes on every guest for minutes in a 4-player campaign) and the host's members of it had no
// tribe to be mirrored into on a guest (sync-check "t: 27 missing on the guest"). The host appends
// each tribe's species and archetype to its state chunks (after the clock trailer); a guest makes a
// host NPC tribe it has no tribe for (Simulator::SpawnNpcTribe at the host's tribe position, no
// members: the mirrors are spawned into it as for any tribe).
constexpr uint32_t kTribeInfoTag = 0x49425254u; // "TRBI": the tribe table's species / archetype after the trailer
constexpr size_t kTribeInfo = 12 + 1;           // species key (instance, type, group) + archetype (0xFF: none)

struct TribeInfo {
    game::Key key{};
    uint8_t arch = 0xFF;
};

TribeInfo tribe_info(void* t);
extern bool g_mirror_air_fix;

constexpr double kMirrorAirFresh = 0.35;

extern bool g_mirror_hang_fix;

constexpr double kMirrorHangHold = 1.0;
constexpr float kMirrorHangMax = 2000.f;

void send_member_air();
extern bool g_mirror_idle_fix;
extern bool g_stale_anim_fix;
extern bool g_phase_any_fix;
extern bool g_hold_restart_fix;
extern bool g_mirror_phase_fix;
extern bool g_phase_tracks_fix;
extern double g_phase_last_off;
uint8_t* main_track(void* c);
bool track_loops(uint8_t* tr);

// BUG-1072: every playing track of a creature (the playing list +0xF0C, 16 indices; the main one is +0xF0C[+0xFA4]):
// fn(track, is_main); a track's first dword is 0 when it holds no animation
template <typename Fn>
static void playing_tracks(void* c, Fn fn)
{
    uint8_t* ac = has_body(c) ? F<uint8_t*>(c, 0xB54) : nullptr;
    uint8_t* st = ac && skg::readable(ac + 0x184, 4) ? *(uint8_t**)(ac + 0x184) : nullptr;
    if (!st || !skg::readable(st, 0xFAC))
        return;
    uint32_t mi = *(uint32_t*)(st + 0xFA4);
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t k = *(uint32_t*)(st + 0xF0C + i * 4);
        if (k > 0xF)
            continue;
        uint8_t* tr = st + 8 + k * 0xF0;
        if (*(uint32_t*)tr && *(uint32_t*)(tr + 0xD8))
            fn(tr, i == mi);
    }
}

void send_member_anims(double now);
void theme_forget();
void on_theme(uint8_t kind, net::Reader& r);
extern bool g_party_fix;
extern bool g_party_stop_fix;
extern uint32_t g_party_stops_outside;
void on_party(uint8_t kind, net::Reader& r);
bool player_tribe_out(void* t);
void gameover_host_tick(double now);

// tests (BUG-0759): `coop.tribe setvel <id> <vx> <vy> <vz> <s>` holds a host member's velocity field (cLocomotiveObject
// mVelocity) at that value for s seconds, as the game left the chief's in campaign_tc2 (119.7 m/s while it slept)
struct TestVel {
    net::Vec3 v;
    double until;
};

extern std::unordered_map<void*, TestVel> g_test_vel;
void test_vel_tick(double now);
extern uint32_t g_member_moves;
extern uint32_t g_member_moves_unowned;
extern bool g_owners_fix;
extern std::unordered_map<void*, std::string> g_member_claim;
extern std::unordered_map<void*, uint8_t> g_member_flags;
extern uint32_t g_mclaims_held;
extern uint32_t g_mclaims_moved;
extern uint32_t g_mclaims_read;
extern uint32_t g_mclaims_serial;
extern bool g_members_matched;
void member_claim(void* c, uint8_t owner, uint8_t flag);
void host_tick(double now);
extern uint32_t g_fruit_orders_moved;

static constexpr uint32_t kFruitNoun = 0x2C9CC91;

extern bool g_cmd_listed_fix;
extern uint32_t g_cmd_gone;
void give_on_host(uint8_t from, uint8_t to, const std::vector<uint16_t>& ids);
extern bool g_guest_baby_fix;
extern uint32_t g_babies_local;
extern uint32_t g_babies_made;
extern uint32_t g_babies_paired;
extern uint32_t g_babies_sent;
extern uintptr_t g_baby_from;
extern bool g_baby_pair_fix;

struct Buy {
    uint8_t from;
    net::Vec3 pos;
    double t;
    void* fx; // only to tell a second report of the same effect (never used as an object)
};

extern std::vector<Buy> g_buys;
extern uint32_t g_buys_none;
extern uint32_t g_buys_other;
extern uint32_t g_buys_own;
extern uint32_t g_buys_seen;
extern std::string g_baby_last;
void baby_on_host(uint8_t from, const net::Vec3& pos);
void conquer_on_host(uint8_t from, const net::Vec3& p);
extern uint32_t g_guest_missions;
void host_command(uint8_t from, net::Reader& r);
extern bool g_fix_gameover;
extern uint32_t g_spawns_blocked;
extern uint32_t g_mirror_out_passed;
extern uint32_t g_mirrors_grown;
void* spawn_member(void* t, uint8_t age, uint8_t owner = 0xFF); // owner: BUG-1299 (born in its line species)
void restore_body_tribe(void* c, void* t);
void* tribe_holding(void* c);
void remove_member(void* c);
extern std::unordered_map<void*, uint32_t> g_ours_c;
extern std::unordered_map<void*, uint32_t> g_ours_t;
void note_ours(std::unordered_map<void*, uint32_t>& m, void* o);
bool ours_in(const std::unordered_map<void*, uint32_t>& m, void* o);
extern uint32_t g_markers_used;

void make_npc_tribes(const uint8_t* p, size_t len, const std::vector<std::pair<net::Vec3, bool>>& htribes,
                            const std::vector<void*>& locals);

extern uint32_t g_forms_blocked;
// BUG-1233: what NpcTribeController_FormTribe reads before it forms (tribe.cpp, h_Form)
struct FormSpecies {
    int tribes = -1, slots = -1, species = -1; // tribe nouns, its property list's count, the species list's size
    bool past = false;                         // the next species would be read past the list's end
};
bool form_species_state(void* ctrl, FormSpecies& s);
void form_species_recheck();
extern bool g_form_species_fix;
extern uint32_t g_forms_past;
extern bool g_last_species_fix;
extern uint32_t g_forms_last; // BUG-1238: villages formed with the list's last species (tribe.form_last_species)
extern std::string g_form_species_last;
extern bool g_form_pace_fix;
extern uint32_t g_forms_paced;
extern std::string g_pace_last;
void unmapped_tick(const std::vector<void*>& locals, void* pt, double now);
void guest_state(net::Reader& r);
extern bool g_fix_ko;
extern uint32_t g_ko_replaced;
extern uint32_t g_ko_shown;
extern uint32_t g_away_trees_ended;
void away_tree_end_tick();
void guest_tick(double now);
extern std::unordered_map<uint8_t, void*> g_chief;
extern std::unordered_map<void*, float> g_chief_scale;
extern uint32_t g_duet_ticks;
extern void* g_epic;
extern void* g_epic_herd;
extern bool g_fix_epic_herd;
extern bool g_fix_epic_players;

constexpr uint32_t kEpicArchetype = 0xFC811218u;

uint16_t id_of(void* c);
extern bool g_fix_chiefs;

struct EpicBlows {
    uint32_t n = 0;
    float last = 0, max = 0;
};

extern std::unordered_map<void*, EpicBlows> g_epic_blows;
extern bool g_duet_fix;

struct Performer {
    uint16_t id;
    uint8_t owner;
    void* target;
    void* ui;
    int state;
};

std::vector<Performer> performers();
bool is_epic_herd_of_ours(void* o);
extern uint32_t g_epic_skipped;
bool spawn_epic();
bool epic_listed();
uint32_t bt_node(void* c);
uint32_t cur_anim(void* c);
extern bool g_grab_spot_fix;
extern uint32_t g_grab_spot_moves;
extern uint32_t g_race_msg;
extern bool g_race_before;
extern bool g_race_post;

// tests (BUG-0672): `coop.tribe frametool <tribe index> <type> <metres>`: the host's tool made in this game's tribe of
// that index the way the wobj tool kind makes it (make_tool), from the next frame's tick (inside SporeKit's frame, as the
// wobj snapshot runs), with a task for outside the frame queued just before (as one often is)
struct FrameTool {
    bool armed = false;
    int ix = 0, type = 0;
    float metres = 10.f;
};

extern FrameTool g_frame_tool;

// tests (BUG-0844, class BUG-0426): mirror <id>'s citizen is removed by the game (RemoveMember with destroy on its
// tribe, no bookkeeping of ours: the mirror still names it), then once a frame a member is made in the guest's own
// tribe (removed again unless it is at the freed address) until the game gives it that block
struct TribeHunt {
    uint8_t* target = nullptr;
    uint16_t id = 0;
    int left = 0, tries = 0;
    void* hit = nullptr;
    uint32_t hit_serial = 0;
};

extern TribeHunt g_th;
std::string tribe_hunt_status();

constexpr uintptr_t kTribeStampOff = 0x19D4, kTribeStampPropOff = 0x2D4;

extern uint32_t g_resets;
void before_save();
extern uint32_t g_world_checks_fired;
bool center(net::Vec3& pos);
std::string member_brief(void* c);
std::string list();

} // namespace tribe
