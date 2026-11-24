// The state the files of the coop core share (B2 of 0.7.15, docs/audit-0.7.15/core.md CORE-2): coop.cpp (the module
// entry, the frame, the world-share glue), coop_cmds.cpp (the console commands), dna.cpp (DNA routing), vote.cpp (the
// evolution vote and the own editor), puppets.cpp (the partners' puppets, the recorders) and session_glue.cpp (the
// session callbacks and the session end) were one file. Its anonymous namespace is the named namespace coop_core
// now, with a using-directive like the one an anonymous namespace has, so the code reads as before; what more than
// one of these files uses is declared here, everything else stays file-static. Only these files include this header (and civ_arrival.cpp, WP-X1 of
// 0.7.16: the Tribe -> Civ arrival's own editors, next to vote.cpp's own editor).
// The moves are proved by tools/refactor_dump.py (docs/10-dev-pipeline.md#pipeline-refactor-proof).
#pragma once

#include <winsock2.h>
#include "lifecycle.h"
#include "call_guard.h"
#include "player_life.h"
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <mutex>
#include <cstddef>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <csignal>
#include <cstdio>
#include <deque>
#include <map>
#include <string>

#include "adapters.h"
#include "adventure.h"
#include "coop_ui.h"
#include "driver.h"
#include "game.h"
#include "modes.h"
#include "stage_epoch.h"
#include "mouse.h"
#include "shared.h"
#include "sui.h"
#include "teamplay.h"
#include "tribe.h"
#include "drama.h"
#include "cellsync.h"
#include "audio.h"
#include "census.h"
#include "civ.h"
#include "autoupdate.h"
#include "gamefixes.h"
#include "spacecoop.h"
#include "squadron.h"
#include "npc.h"
#include "creature_hp.h"
#include "player_rules.h"
#include "mirror_fx.h"
#include "effects.h"
#include "nest.h"
#include "fields.h"
#include "wsync.h"
#include "worldobj.h"
#include "realign.h"
#include "parts.h"
#include "rejoin.h"
#include "savestrip.h"
#include "cell_logic.h"
#include "busy.h"
#include "social.h"
#include "world.h"
#include "find.h"
#include "net/discovery.h"
#include "net/fixes.h"
#include "net/give_target.h"
#include "net/interp.h"
#include "net/lobby.h"
#include "net/session.h"
#include "net/utf8.h"
#include "creations.h"
#include "notify.h"
#include "hud_portrait.h"
#include "notify_text.h"
#include "syncdbg.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include <shlobj.h>
#include "share_wait.h"
#include "dna_route.h"
#include "dna_ledger.h"
#include "dna_pace.h"
#include "dna_own.h"
#include "settings.h"
#include "sreport.h"
#include "space_ext.h"
#include "host_world.h"
#include "own_world.h"
#include "diag_coop.h"
#include "trace.h"
#include "view_centres.h"

extern const SkHost* H; // coop.cpp
extern SkMod* M;

#define LOG(...) H->log(M, SK_INFO, __VA_ARGS__)
#define SAY(...) H->out_printf(out, __VA_ARGS__)

namespace coop_core {

// ---- types and constants (moved from coop.cpp) ----
enum class EditKind { Own, Edit, Evolve };
constexpr uint32_t kTagMating = 0x12191CAu, kTagStageEnd = 0x64AA168u, kEditorLandfall = 0xFD4902BDu;
// BUG-0720 tests (coop.editreq): a request captured at Submit instead of being submitted
struct ReqImage {
    bool have = false;
    uint8_t bytes[0x9C] = {};
    std::vector<uint32_t> vec70; // the +0x70 vector's contents
};
typedef bool(SK_THISCALL* CreatureGameMsg_t)(void*, uint32_t, void*);
struct Remote {
    uint8_t id = 0;
    std::string name;
    net::SnapshotBuffer buf;
    void* puppet = nullptr;
    drive::State drv;
    const modes::Adapter* puppet_adapter = nullptr; // adapter that created `puppet`
    uint32_t puppet_world = 0; // the place `puppet` was made in
    bool echo = false; // local loopback test remote
    bool silent_said = false; // BUG-0513: "is not responding" was told
    uint64_t world_sent = 0; // host: (mode << 32 | save crc) last sent to this guest
    double other_mode_since = -1; // host: since when the guest plays another mode
    bool joined_phase = false; // host: the guest played the host's mode after the last world send
    // host, BUG-1435: the guest has yet to load the world sent last (world::share_serial changed): its pass through the
    // galaxy menu for that load is no "left to the menu", and its frames in the old copy before it are no "joined the
    // phase". load_acked: when the share was no longer in flight (0: still is); load_left: when the guest left the
    // host's mode after that (0: not yet); load_played: it played the host's mode since the share
    uint32_t load_serial = 0;
    bool load_wait = false, load_played = false;
    double load_acked = 0, load_left = 0;
    bool needs_world = false;  // host: joined while the host was not playing; send the world later
    double cine_wait_since = 0; // host: since when this guest's share waits for a cinematic (BUG-0232)
    // host, BUG-0508: after the host's editor this guest was in the same world and
    // stage, so it may take the new body without the whole world; evo_check_until: until then its avatar
    // must have left the host's pre-editor species (Creature), or it gets the world after all
    bool evo_inplace = false;
    double evo_check_until = 0;
    // host, BUG-0231: a failed share is retried; dropped with the entry at the
    // session end (g_remotes.clear())
    uint32_t join_serial = 0; // this guest's join (the share's tag: an id reused by a newcomer is not it)
    bool share_at_join = false; // the last share was the join's (a retry waits for needs_world)
    int share_tries = 0;        // retries in this phase (share_try_key)
    uint64_t share_try_key = 0;
    double retry_at = 0;        // no share for this guest before
    // the invariant monitor (docs/06-testing.md#testing-monitor): the puppet read back from the game at
    // the start of the mod's frame (before any driving: where the game drew it) on a frame that writes
    // the trace lines; drawn_at = that frame's time, -1: not read
    net::PlayerState drawn_st;
    double drawn_at = -1;
    // coop.track (BUG-0414): the partner's animations played on its puppet (MSG_ANIM), the last one's id
    uint32_t anims = 0, anim_last = 0;
    // BUG-0923 (notify.appeared_once): the shown puppet was taken away to be built again with the partner's new
    // look (creations' appearance_changed): the puppet made next is the same appearance, not told again
    bool relook = false;
};
// Creature puppets the game did not destroy when we forgot them: the creature stage world stays
// through the editor (the local avatar is gone meanwhile), so the old puppet stood on as an
// unregistered creature, an NPC with its own AI in the nestless herd made for it. Its behaviour
// tree then searched land around that herd's home, which could lie deep inside the planet, and
// read past the planet's cube map (host crash 0xB88BEA, 2026-10-25; docs/re/creature.md 10).
// Such puppets are removed once the player is back in the same world.
struct Stray {
    void* c;
    void* herd;
    uint32_t species;
    uint32_t world;
    double since;
};
static constexpr double kSoloAwait = 5.0;
constexpr double kOfferSeconds = 90.0;
constexpr uint8_t kVoteOffer = 1; // MSG_VOTE flags: an evolve offer, not a vote (sent with active = 0: older guests ignore it)
using BlockingErrors_t = uint32_t*(SK_THISCALL*)(void*, uint32_t*, void*, void*, int);
// BUG-0315: the Cell stage's switch to Creature (0xE62440: ends the cinematics, CreatureStart_FromCell, the mode
// switch to 0x1654C01, the fade effect), reached from cCellModeStrategy::HandleMessage 0x47C1D19 (History's check)
// and the cell event 0x1F (0xE7E130). cdecl void(void), globals only: safe to run later from outside the frame.
typedef void(SK_CDECL* CellToCreature_t)();
// BUG-1269: the Cell stage's end sequence after History's check (SporeCoop::CellStage_End 0xE72C80: G+0x5158 = 3, the
// letterbox, the avatar's animation 0x2E, then the event 0x1F that calls CellToCreature). cdecl void(void), globals only.
typedef void(SK_CDECL* CellStageEnd_t)();
// Selecting a creature in the creature stage: CreatureSelectionUI reads (+0xE84)->+0x32C only for
// an adult with +0xB58 mask 0x1 set and mask 0x2 clear (GOG 2024 0xB6C9C3..0xB6CA11: `test al,2`
// skips the read, `test al,1` gates it; then vt 0xD0 / 0xD4 false). Our puppets and proxies have no
// +0xE84 object (the game crashed there), so mask 0x1 is hidden for the call. Mask 0x1 is not the
// pack (that is 0x100); docs/re/creature.md#creature-selection-gate (BUG-0118).
typedef void(SK_CDECL* SelectUI_t)(void*, void*, void*, uint32_t, float);
// The quit message 0x4715068 (save and leave the stage for the galaxy menu; coop.load and received
// worlds use it, docs/re/space.md 8.2) saves the game itself. Handled while our save for a world
// share was still queued or being written (a guest joined as the host left), the two saves broke
// the host's slot: its next load faulted at 0xBA7F87 (data 0x1188, from 0xB27DA6) and stayed in
// LoadGame. Like the game does while a mode switch is pending, the message is posted again until
// our save is written.
using SimMsg_t = int(SK_THISCALL*)(void*, uint32_t, void*);
// The creature stage's avatar-event handler (a vtable method, 0xD47FC0) reads (+0xE84)->+0x32C of
// the creature an event is about (msg+4: the attacked creature of 0xD335362C, the impressed one of
// 0x60B4123) when it is an adult of another species, without a null check (0xD48634). Our puppets
// and NPC proxies have no +0xE84 object: the guest faulted there while it fought proxies
// (play_creature.py, 2026-10-23). The host of a player's report faulted there 3 times on a
// creature that was none of ours (a puppet brought back by a save, npc.cpp remove_saved_strays), so the
// creature of the message itself is checked too, whatever it is. During the handler these get a
// blank object whose +0x32C is -1 ("none"); a creature with tuning is never touched.
typedef bool(SK_THISCALL* StageMsg_t)(void*, uint32_t, void*);
using Submit_t = bool(SK_CDECL*)(void*);
using CollEnabled_t = uint8_t*(SK_THISCALL*)(void*, uint8_t*, uint8_t*, uint8_t*);
using HoverFilter_t = bool(SK_CDECL*)(void**);
using LocalAddr = adapters::Addr;
enum class EndPath { Leave, Unload, Restart, Disconnect };
// ---- evolution vote: the host's editor request (next stage or evolution inside a stage) waits
// until every player is ready (or 60 s / "Start now"), then the game goes on ----
struct Vote {
    bool active = false;
    void* request = nullptr; // Editors::EditorRequest*, one reference held
    double t0 = 0;
    std::string text;
    int left = 0;
    uint32_t editor = 0; // the editor the host opens (EditorRequest+0xC); guests open the same for their own creature
    uint32_t mode = 0;   // host: the stage and world the vote belongs to (BUG-0043)
    uint32_t world = 0;
    bool arrival = false; // host (BUG-0315): the first Creature editor after a passed switch vote: a guest that arrived is ready
};

// ---- defined in coop.cpp ----
std::string world_id_status(); // C2 (BUG-1141): coop.lifecycle world
char host_out_of_stage(); // 'm' menu, 'e' editor, 'l' load, 0: the host plays our stage (or unknown)
extern bool g_anim_log;
extern bool g_away_test;
extern bool g_disc_on;
extern bool g_echo;
extern bool g_give_pick_fix;
extern bool g_gopos_ground_fix;
extern bool g_host_away;
extern bool g_host_away_fix;
extern bool g_keepalive_ptr_fix;
extern bool g_lobby_dirty;
extern bool g_overlay;
extern bool g_place_fix;
extern bool g_player_forget_fix;
extern bool g_role_fix;
extern bool g_share_world;
extern bool g_status_block_fix;
extern bool g_tag_self;
extern bool g_vote_world_fix;
extern bool g_world_gate_fix;
extern double g_echo_delay;
extern double g_host_away_since;
extern double g_last_lobby;
extern double g_last_send;
extern float g_dbg_sc[3];
extern float g_dna_pending;
extern float g_echo_offset;
extern float g_solo_dna;
extern int g_lan_fake_n;
extern net::Discovery g_disc;
extern net::Session g_sess;
extern std::array<int, 4> g_status_block;
extern int g_status_drawn_right;
extern std::atomic<bool> g_menu_up;
extern std::atomic<bool> g_text_up;
extern std::deque<std::pair<double, net::PlayerState>> g_echo_q;
extern std::deque<std::string> g_chat;
extern std::deque<uint32_t> g_chat_col;
extern std::map<uint8_t, Remote> g_remotes;
extern std::map<uint8_t, bool> g_ready;
extern std::map<uint8_t, int> g_lobby_rtt;
extern std::map<uint8_t, uint8_t> g_roles;
extern std::string g_name;
extern std::thread* g_keepalive;
extern uint32_t g_cine_hidden;
extern uint32_t g_dna_owed_world;
extern uint32_t g_dna_spend_owed_world, g_spend_owed_dropped; // BUG-1506
extern bool g_spend_owed_world_fix;
extern uint32_t g_host_away_count;
extern uint32_t g_host_save;
extern uint32_t g_host_save_heard;
extern uint32_t g_lan_fake_until;
extern uint32_t g_menu_hidden;
extern uint32_t g_owed_dropped;
extern uint32_t g_place_dropped;
extern uint32_t g_solo_dropped;
extern uint32_t g_away_kept_in_editor; // BUG-1422 (tests)
extern uint32_t g_solo_mode;
extern uint32_t g_stage_world;
extern uint32_t g_tags_drawn;
extern uint32_t g_vote_elsewhere;
extern uint32_t g_world_announced;
bool adventure_sky_own(uint32_t mode);
bool gate_drops(uint8_t type);
bool guest_elsewhere();
bool guest_space_other();
bool host_elsewhere(bool unknown = false);
bool host_here();
bool stage_mode(uint32_t m);
float gopos_onto_surface(net::Vec3& p);
host_world::HostWorld host_world_now();
int give_selected_to(uint8_t to);
std::string join_apart_status();
std::string targets(int all);
std::vector<LocalAddr> local_addresses();
uint32_t build_id();
uint32_t host_save_known();
uint8_t give_target();
void add_mark(uint8_t from, const net::Vec3& p, uint32_t mode);
void chat_log(const std::string& s, uint32_t argb = 0);
void feed(const std::string& s);
void feed_kind(const std::string& s, const char* kind);
void give_target_cycle();
void keepalive_start();
void keepalive_stop();
void notice(const std::string& text, int from = -1);

// ---- defined in host_busy.cpp (C3, BUG-1142) ----
void host_busy_tick(double now);
// BUG-1305 (guest_mating.cpp): a guest's mating with a host copy as the mate ends in its editor
void guest_mating_tick(double now);
std::string guest_mating_status();
void host_busy_pause_menu(bool shown);
void host_busy_join(uint8_t id);
void host_busy_message(uint8_t from, net::Reader& r);
std::string host_busy_status();
void host_busy_long_wait(const char* what); // BUG-1419, host: an editor is asked for: the guests wait longer

// ---- defined in coop_cmds.cpp ----
void register_commands_cell();
void register_commands_civ();
void register_commands_core();
void register_commands_editor();
void register_commands_stages();
void register_commands_ui();

// ---- defined in dna.cpp ----
extern uint32_t g_own_totals_ignored, g_own_gains_kept, g_own_deeds_given; // BUG-1469 (coop.progress)
extern bool g_away_dna_fix;
extern bool g_dna_seen_ok;
extern bool g_solo_await_fix;
extern dna_ledger::Notices g_dna_notices;
extern dna_pace::Carry g_pace_carry;
extern double g_dna_at;
extern double g_solo_await_until;
extern float g_dna_owed;
extern float g_dna_seen;
extern float g_dna_sent;
extern float g_dna_spend;
extern float g_dna_spend_owed;
extern float g_paced_in;
extern float g_paced_out;
extern float g_solo_await;
extern int g_pace_heard;
extern std::vector<dna_ledger::Part> g_dna_out;
extern uint32_t g_dna_fwd;
extern uint32_t g_dna_noticed;
extern uint32_t g_dna_owed_paid;
extern uint32_t g_dna_own;
extern uint32_t g_dna_rx;
extern uint32_t g_dna_spends_got;
extern uint32_t g_dna_spends_sent;
extern uint32_t g_paced;
extern uint32_t g_solo_stale_totals;
bool on_dna(float& delta);
float dna_pace_gain(float amount);
int dna_pace_announce();
int dna_pace_meal(int amount);
int pace_div();
void dna_host_gain(const dna_ledger::Part& p, float before);
void dna_note(const dna_ledger::Part& p, uint8_t me);
void dna_notices_tick(double now);
void dna_pace_heard(int div);
void dna_spend_note(float d);
void dna_spend_resync();
void dna_spend_sample();
void dna_spend_tick(double now);
// WP-D (BUG-1291): own DNA per player (dna_own.h)
extern bool g_own_dna_fix, g_dna_bar_set_fix, g_own_dna_store_fix, g_bar_setback_fix;
extern dna_own::Book g_book;
extern std::map<uint8_t, float> g_dna_sent_to;
extern uint32_t g_own_totals, g_own_applied;
extern bool g_own_heard;
extern float g_own_bar_heard;
bool own_dna_on();
bool dna_peer_reads(uint8_t id, net::Family fam);
bool dna_own_wallet(uint8_t id, net::Family fam);
float dna_own_budget(uint8_t id);
void dna_own_spend(uint8_t id, float d);
float dna_total_for(uint8_t id, float host_total, dna_own::Tail& tail);
bool dna_own_expect(uint8_t id, float& v);                        // BUG-1310: the lab's gates
bool dna_own_expect_cell(uint8_t id, int host_spent, int& v);     // BUG-1310
std::string dna_own_field();                                      // BUG-1310: the host's trace line field
void own_dna_tick();
void own_dna_credit_now(); // BUG-1341: the totals phase credits this frame's party gain first
std::string own_dna_status();

// ---- defined in vote.cpp ----
alignas(16) extern uint8_t g_blank_e84[0x1000];
extern BlockingErrors_t o_BlockingErrors;
extern CellToCreature_t o_CellToCreature;
extern CellStageEnd_t o_CellStageEnd;
unsigned own_end_held_count();           // BUG-1469 (tests): Cell stage-end clicks held for the partners' bars
void own_progress_resume(bool at_end); // BUG-1469 (vote.cpp): the host's kept click on History's check runs now
extern CreatureGameMsg_t o_CreatureGameMsg;
extern ReqImage g_editreq_game;
extern SelectUI_t o_SelectUI;
extern SimMsg_t o_SimMsg;
extern StageMsg_t o_StageMsg;
extern Submit_t o_Submit;
extern Vote g_vote;
extern bool g_editor_request_fix;
extern bool g_editreq_capture;
extern bool g_offer_opened_ui;
extern bool g_own_editor;
extern bool g_own_editor_charge_due;
extern bool g_own_editor_end_fix;
extern bool g_own_editor_flags_fix;
extern bool g_vote_drop_editor_fix;
extern bool g_vote_opened_ui;
extern bool g_vote_scope_fix;
extern bool g_was_in_editor;
extern double g_editor_left_at;
extern double g_offer_until;
extern double g_own_editor_due_at;
extern double g_own_editor_due_first;
extern double g_own_editor_hold_until;
extern std::function<void()> g_vote_action_due;
extern uint32_t g_last_editor_req;
extern uint32_t g_last_stage;
extern uint32_t g_offer_editor;
extern uint32_t g_own_editor_due;
extern bool g_own_editor_arriving;  // BUG-1240
extern bool g_own_editor_arrival_fix;
extern bool g_own_editor_no_avatar_fix; // BUG-1421
extern double g_vote_noavatar_until; // BUG-1421 (tests)
extern unsigned g_own_editor_arrivals;
unsigned vote_in_editor_count(); // BUG-1239 (tests)
unsigned vote_stuck_kicks(); // BUG-1420 (tests)
unsigned vote_mates_held();      // BUG-1245 (tests)
unsigned arrival_not_waited();   // BUG-1245 (tests)
unsigned arrival_waited();       // BUG-1336 (tests)
unsigned cell_editor_now_count(); // BUG-1263 (tests)
unsigned refused_mates_count();   // BUG-1265 (tests)
unsigned switch_end_held_count(); // BUG-1269 (tests)
unsigned own_no_replace_count(); // BUG-1255 (tests)
extern unsigned g_evo_flags_cleared;
extern unsigned g_offers_expired;
extern unsigned g_offers_got;
extern unsigned g_offers_sent;
extern unsigned g_offers_taken;
extern unsigned g_own_editor_items_set;
extern unsigned g_own_editor_waits;
extern unsigned g_own_keep_ctor;
extern unsigned g_part_list_waivers;
extern unsigned g_outfit_part_waivers; // BUG-1308
extern unsigned g_quits_delayed;
extern unsigned g_req_own_busy;
extern unsigned g_select_guards;
extern unsigned g_stage_end_guest_refused;
extern unsigned g_stage_end_votes;
extern unsigned g_stage_msg_guards;
extern unsigned g_stage_msg_other;
extern unsigned g_submit_cines_ended;
extern unsigned g_submits_after_cine;
extern unsigned g_submits_held;
extern unsigned g_submits_own_pause;
extern unsigned g_submits_repeated;
extern unsigned g_switch_arrivals;
extern unsigned g_switch_guest_held;
extern unsigned g_switch_held;
extern unsigned g_switch_votes;
extern unsigned g_vote_actions_run;
extern unsigned g_vote_actions_waited;
extern unsigned g_votes_dropped;
extern void* g_creature_game;
extern void* g_submit_later;
ReqImage req_image(void* req);
bool SK_CDECL h_Submit(void* req);
bool SK_THISCALL h_CreatureGameMsg(void* self, uint32_t id, void* data);
bool SK_THISCALL h_StageMsg(void* self, uint32_t id, void* msg);
bool design_stage(uint32_t m);
bool host_creature_mode();
bool evolve_own_text(uint32_t to); // BUG-1258: the host's evolution notice names its own creature (Creature, not one creature)
bool start_action_vote(const std::string& text, const std::string& done, std::function<void()> action);
bool tribe_civ_editor(uint32_t id);
std::string tribe_end_line(bool set, uint32_t archetype); // tests (BUG-1280): coop.vote tribeend
bool tribe_graduating();
const char* save_blocker();
int SK_THISCALL h_SimMsg(void* self, uint32_t id, void* data);
int editor_pause_count();
int evo_flag_now();
uint32_t* SK_THISCALL h_BlockingErrors(void* ed, uint32_t* out, void* res, void* model, int flag);
unsigned game_busy();
bool editor_coming(double now); // C3 (BUG-1142)
void SK_CDECL h_CellToCreature();
extern unsigned g_quit_switch_dropped; // BUG-1436 (tests): the game's own switches to Creature dropped while leaving Cell
extern bool g_cellswitch_after_quit;   // BUG-1436 (tests, coop.cellswitch quit)
void SK_CDECL h_CellStageEnd();
void SK_CDECL h_SelectUI(void* sel, void* a2, void* a3, uint32_t a4, float a5);
void drop_vote(const char* why);
void evo_flag_tick(double now);
void finish_vote(const char* why);
void offer_take();
void offer_tick(double now);
void own_editor_tick(double now);
// ---- defined in civ_arrival.cpp (WP-X1, BUG-1276): a guest's own City Hall and vehicle editors at its Civ arrival ----
void civ_arrival_vote(uint32_t editor, uint32_t mode); // the host's vote ended on this guest (its editor, our stage)
void civ_arrival_tick(double now);
void civ_arrival_result(const void* data); // civ_editors.cpp: an editor's result message 0x30C11C7
void civ_arrival_init();
std::string civ_arrival_status();
// ---- BUG-1531 (in civ_arrival.cpp): a guest's own spaceship editor at the Civ -> Space vote ----
void civ_ship_editor_vote(uint32_t editor, uint32_t mode);
void civ_ship_editor_result(const void* data);
std::string civ_ship_editor_status();
void start_vote(void* req, const std::string& text);
void submit_tick(double now);
void vote_action_tick(double now);
void vote_tick(double now);
void* build_editor_request(EditKind kind, uint32_t editor_id, const game::Key& k);
void* collected_parts();

// ---- defined in puppets.cpp ----
extern CollEnabled_t o_CollEnabled;
extern HoverFilter_t o_HoverFilter;
extern bool g_frame_fix;
extern bool g_lifecycle_fix;
extern bool g_partner_pass_fix;
extern bool g_puppet_hold;
extern bool g_save_on_share;
extern const modes::Adapter* g_adapter;
extern double g_puppet_hide_until;
extern double g_state_mode_until;
extern double g_trace_at;
extern double g_trace_body_at;
extern double g_trace_stage_at;
extern std::vector<Stray> g_strays;
extern uint32_t g_anims_dropped;
extern uint32_t g_anims_not_creature;
extern uint32_t g_anims_played;
extern uint32_t g_anims_rx;
extern uint32_t g_anims_unlisted;
extern uint32_t g_appeared_quiet;
extern uint32_t g_appeared_told;
extern uint32_t g_despawns_dropped;
extern uint32_t g_despawns_unlisted;
extern uint32_t g_local_refused;
extern uint32_t g_mode;
extern uint32_t g_pass_hover_skipped;
extern uint32_t g_social_hover_skipped; // BUG-1250
extern volatile bool g_social_hover_on;
extern uint32_t g_pass_pairs;
extern uint32_t g_pass_phantom_own;
extern uint32_t g_pass_phantom_pairs;
extern uint32_t g_pass_phantom_partner;
extern uint32_t g_pass_seen_own;
extern uint32_t g_puppet_refused;
extern uint32_t g_state_mode_override;
extern uint32_t g_strays_removed;
extern uint32_t g_world;
extern uintptr_t a_AbilityModeCore;
extern void* g_local;
extern void* volatile g_pass_puppets[8];
extern volatile bool g_pass_on;
bool SK_CDECL h_HoverFilter(void** hit);
bool creature_puppet(const modes::Adapter* a);
bool parts_census(int& unlocked, uint32_t& hash, int& unlockable, std::vector<std::pair<uint32_t, uint32_t>>* locked = nullptr);
bool same_place(const net::PlayerState& s);
const char* role_str();
int c_bodyrec(void*, int argc, const char* const* argv, SkOut* out);
int c_track(void*, int argc, const char* const* argv, SkOut* out);
net::PlayerState local_state();
std::vector<syncdbg::Entry> sync_all();
uint8_t* SK_THISCALL h_CollEnabled(void* self, uint8_t* out, uint8_t* a, uint8_t* b);
void bodyrec_tick(double now);
void despawn(Remote& r);
void drive_puppet(Remote& r, double now);
void forget_puppets();
void monitor_frame_start(double now);
void on_anim(void* creature, uint32_t anim, int block, bool disable_blend, bool to_target);
void on_own_behaviour_anim(void* creature, uint32_t anim, int a, int b); // BUG-1253
extern uint32_t g_own_bhv_anims, g_bhv_anims_played; // BUG-1253 (tests)
void on_remote_anim(const net::AnimEvent& ev);
void partner_pass_tick();
void remove_strays(double now);
void trace_tick(double now);
void track_tick(double now);
void* creature_puppet_live(uint8_t id);
void* local_now();
void* puppet_now(const Remote& r);

// ---- defined in session_glue.cpp ----
extern bool g_evo_key_done;
extern bool g_evo_save_test_off;
extern bool g_id_reuse_fix;
extern bool g_settings_in;
extern bool g_share_cine_fix;
extern double g_evo_key_until;
extern double g_share_due_at;
extern net::LookupFn g_real_lookup;
extern std::atomic<int> g_slow_dns_ms;
extern std::string g_space_key;
extern uint32_t g_evo_keys_set;
extern uint32_t g_share_retries;
extern unsigned g_load_waits, g_load_wait_timeouts; // BUG-1435 (session_glue.cpp)
void copy_notice_tick(); // BUG-1434: a host playing from a co-op copy is told whose it is (session_glue.cpp)
extern uint32_t g_space_load_shares;
extern uint8_t g_give_pick;
extern unsigned g_evo_fallback;
extern unsigned g_evo_inplace;
extern unsigned g_evo_saves;
bool do_host(uint16_t port, std::string& err);
bool do_join(const std::string& addr, std::string& err);
bool slow_lookup(const std::string& host, uint32_t& ip);
const char* role_name(uint8_t r);
const char* share_wait_reason();
int share_all();
uint16_t default_port();
void do_leave();
void end_session(EndPath path, bool cell);
void evo_key_tick(double now);
void follow_phase(double now);
void lobby_tick(double now);
void on_mode_change(uint32_t from, uint32_t to);
void set_ready(bool on);
void set_role(uint8_t role);
void setup_callbacks();
void share_failed(uint8_t peer, uint32_t tag, bool planet_missing);
void share_wait_tick();

} // namespace coop_core
using namespace coop_core;
