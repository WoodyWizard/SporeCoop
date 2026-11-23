#include "cellsync_internal.h"
#include "own_progress.h"
#include "effects.h"
#include "net/manifest.h"

namespace coop_core { // dna.cpp (WP-D, BUG-1291)
bool dna_own_wallet(uint8_t id, net::Family fam);
int dna_own_cell_spent(uint8_t id, int host_spent);
int dna_own_cell_spend(uint8_t id, int spent, int host_spent);
}

namespace cellsync {

const SkHost* H;
SkMod* M;
Ctx C;
static bool g_ok;
uintptr_t a_Wipe, a_Cine, a_Pair, a_GetData, a_DamageCell, a_KillCell, a_Break, a_Destroy, a_AddFood, a_Populate, a_Rollover, a_GetDamage,
    a_RefCreate, a_Serializer, a_S, a_Fnv, a_Effect, a_TargetParams, a_IsPrey, a_IsThreat, a_ScaleDiff, a_NoAttack, a_Alert, a_React, a_Rest,
    a_Soft, a_ChunkA, a_ChunkB, a_Bite, a_SpawnLoot, a_EatFood, a_ModelData, a_CellUI, a_DnaFor;



Counters g_n;

bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
bool in_cell() { return g_ok && game::mode_id() == modes::kCell && modes::cell_game(); }

uint8_t* G() { return modes::cell_game(); }
uint32_t avatar_idx() { return G() ? F<uint32_t>(G(), 0x411C) : 0; }
uint8_t* avatar() { return G() ? modes::cell_at(avatar_idx()) : nullptr; }
// the mate the player's mating call brought (0 = none); the editor opens when the avatar reaches it
uint32_t mate_idx() { return G() ? F<uint32_t>(G(), 0x51D4) : 0; }

// BUG-1245: the mating meeting 0xE78D10 runs in every cell step while the avatar and the mate G+0x51D4 are alive and
// within 4 m; a request the host's vote holds leaves both there, so the game played the mating scene, the egg laying
// (0xE77930) and asked for the editor again every ~10 s. Forgetting the index (the cell itself stays, nothing is
// freed) ends the meetings; restore_mate puts it back (the vote dropped while the stage goes on).
uint32_t forget_mate()
{
    uint8_t* g = in_cell() ? G() : nullptr;
    uint32_t m = g ? F<uint32_t>(g, 0x51D4) : 0;
    if (!m || !modes::cell_at(m))
        return 0;
    F<uint32_t>(g, 0x51D4) = 0;
    return m;
}
bool restore_mate(uint32_t m)
{
    uint8_t* g = in_cell() ? G() : nullptr;
    if (!g || !m || F<uint32_t>(g, 0x51D4) || !modes::cell_at(m))
        return false;
    F<uint32_t>(g, 0x51D4) = m;
    return true;
}
// tests (BUG-1245): the mating scenes the own avatar played (its animation +0x1B0 entering 0x42, 0xE75EA0) and egg
// layings (0x29, 0xE77930) in this game
uint32_t g_matings = 0, g_egg_lays = 0;
static uint32_t g_anim_seen = 0;
static void mating_count()
{
    uint8_t* av = in_cell() ? avatar() : nullptr;
    uint32_t a = av ? F<uint32_t>(av, 0x1B0) : 0;
    if (a != g_anim_seen) {
        if (a == 0x42)
            g_matings++;
        if (a == 0x29)
            g_egg_lays++;
    }
    g_anim_seen = a;
}
static void spend_forget(int id); // BUG-1249
static void spend_reset();
static void spend_tick(bool cell, bool was_in_cell);
static void spend_in(int from, int spent, uint32_t meal, bool cell);
void* fg_query() { return G() ? F<void*>(G(), 0x40FC) : nullptr; }
// cCellSerializableData: cCellGame+0x5190 (the global at 0x16B4DC0 holds the same pointer).
// Only in the Cell stage: cCellGame stays allocated after it (the Creature stage), its pool and
// what it points to do not (the player report of 2026-10-25, cell_at in the Creature stage).
uint8_t* S()
{
    if (!in_cell())
        return nullptr;
    uint8_t* g = G();
    uint8_t* s = g ? F<uint8_t*>(g, 0x5190) : nullptr;
    return s ? s : (a_S ? *(uint8_t**)a_S : nullptr);
}

int* part_states()
{
    uint8_t* s = S();
    return s && avatar() && skg::readable(s + 0x34, 13 * 4) ? (int*)(s + 0x34) : nullptr;
}


net::Vec3 pos_of(uint8_t* c)
{
    auto* p = (float*)(c + kPos);
    return {p[0], p[1], p[2]};
}

void set_pos(uint8_t* c, const net::Vec3& p)
{
    auto* d = (float*)(c + kPos);
    d[0] = p.x; d[1] = p.y; d[2] = p.z;
    auto* pp = (float*)(c + kPrevPos);
    pp[0] = p.x; pp[1] = p.y; pp[2] = p.z;
    F<uint16_t>(c, kFlags) |= 4;
    F<uint16_t>(c, kCounter)++;
}

// ---------------- host ----------------
double g_last_send, g_last_food;
uint16_t g_cycle;
int32_t g_food_sent = -1, g_plant_sent = -1;

// ---------------- guest ----------------
std::unordered_map<uint32_t, Mirror> g_mirrors;   // host index -> mirror
std::unordered_map<uint32_t, uint32_t> g_host_of; // local index -> host index
std::unordered_set<uint32_t> g_cycle_seen;
bool g_our_destroy, g_food_guard, g_replay;
CALL_GUARD("cellsync.our_destroy", g_our_destroy); // BUG-1047
CALL_GUARD("cellsync.food_guard", g_food_guard);
CALL_GUARD("cellsync.replay", g_replay);
// what the guest's avatar ate this frame (AddFoodPoints): sent with the eaten cell (MSG_CELL_EAT) so the
// host adds it only if the cell was still there (eaten once), else on its own at the end of the frame
int g_pend_food;
bool g_pend_plant;
// coop.pool mirrorfix 0|1 (tests): 0 = the guest of 0.7.2 (mirrors not turned, chunk and lethal bites not
// sent to the host), to show what the fixes change in the same session
bool g_mirror_fixes = true;
bool g_leave_fix = true; // coop.pool leavefix 0|1: reproduce voluntary leave before BUG-0028
COOP_FIX("pool.mirror", "BUG-0001", g_mirror_fixes, "cell: the guest turns the host cells and sends chunk and lethal bites (coop.pool mirrorfix)");
static bool g_fix_forget = true; // off: mirrors kept after the stage, destroyed in its freed pool (BUG-0006, tests)
COOP_FIX("pool.forget", "BUG-0006", g_fix_forget, "cell: mirrors and puppet indices are forgotten once the Cell stage is left");
COOP_FIX("pool.leave", "BUG-0028", g_leave_fix, "cell: a guest leaving the session drops its mirror cells (coop.pool leavefix)");

bool voluntary_leave_cleanup_enabled() { return g_leave_fix; }

// Fixes of bug wave 2 with their old behaviour kept for the tests (`coop.pool fix <name>|all 0|1`):
// a test that never failed proves nothing (CLAUDE.md 3).
bool g_fix_purge = true;    // BUG-0032: mirrors removed only after a complete state round
bool g_fix_hostof = true;   // BUG-0088: a recreated mirror drops its old local index
bool g_fix_foodseq = true;  // BUG-0051: the guest keeps its meals the host has not counted yet
bool g_fix_resend = true;   // BUG-0128: food / DNA totals sent again every 2 s and to newcomers
bool g_fix_kostale = true;  // BUG-0061: a remote knockout ends when its owner leaves or stops refreshing it
bool g_fix_koreset = true;  // BUG-0070: our knockout ends with the session or the stage
bool g_fix_bodylevel = true; // BUG-0041: a level-up during the regrowth gives the level's own size
bool g_fix_guestbite = true; // BUG-0078: a guest's bite passes DamageCell's gate (ShouldNotAttack(victim, puppet), +0x17C, +0x111)
COOP_FIX("pool.purge", "BUG-0032", g_fix_purge, "cell: mirrors removed only after a complete state round");
COOP_FIX("pool.hostof", "BUG-0088", g_fix_hostof, "cell: a recreated mirror drops its old local index");
COOP_FIX("pool.foodseq", "BUG-0051", g_fix_foodseq, "cell: the guest keeps its meals the host has not counted yet");
COOP_FIX("pool.meals_trim", "BUG-0219", cell_logic::g_meals_trim, "cell: a host without meal acks (0.7.2) does not grow the guest's meal list");
COOP_FIX("pool.resend", "BUG-0128", g_fix_resend, "cell: food / DNA totals sent again every 2 s and to newcomers");
COOP_FIX("pool.kostale", "BUG-0061", g_fix_kostale, "cell: a remote knockout ends when its owner leaves or stops refreshing it");
COOP_FIX("pool.koreset", "BUG-0070", g_fix_koreset, "cell: our knockout ends with the session or the stage");
COOP_FIX("pool.bodylevel", "BUG-0041", g_fix_bodylevel, "cell: a level-up during the regrowth gives the level's own size");
COOP_FIX("pool.guestbite", "BUG-0078", g_fix_guestbite, "cell: a guest's bite passes the game's ShouldNotAttack gate");
// wave 5 (review of 0.7.3.0)
static bool g_fix_ko_grace = true; // BUG-0238: a remote knockout lasts to its own deadline through late refreshes
COOP_FIX("pool.ko_grace", "BUG-0238", g_fix_ko_grace, "cell: a late knockout refresh does not end it, replay its effect; KO_UP when ours ends on a stage leave");
// BUG-0511: a level change rescales the world over ~3 s (WorldScaleStep 0xE73E50: positions, velocities
// and sizes times old/new scale G+0x514C, about the origin), on each game at its own moment (the guest's
// follows the host's shared food total). The host's cell poses were applied in the guest's scale: up to
// 10 m off and gliding fast for seconds after every level-up. The host sends its scale at the end of each
// state chunk (older guests ignore it); the guest converts the pose by host scale / own scale.
float world_scale()
{
    uint8_t* g = G();
    float s = g ? F<float>(g, 0x514C) : 0.f;
    return std::isfinite(s) && s > 1e-6f && s < 1e9f ? s : 0.f;
}
// BUG-0509: a knocked-out cell (held at hp 1, invulnerable, see go_down) still bit, spiked and poisoned: the game's
// attack code ran for the avatar (Bite 0xE7B0A0, DamageCell with the avatar as attacker, BreakOrSwallow on a lethal
// bite), a guest's bites went to the host as MSG_CELL_HIT and killed. Now a downed avatar's attacks do nothing on its
// own game, and the host refuses MSG_CELL_HIT from a guest it knows to be knocked out (KO_OUT).
// BUG-0746: the swallow of interaction kinds 9 / 10 (meat or a plant swallowed whole, StartEatSwallow 0xE7E020 ->
// event type 1 -> SwallowWhole 0xE7D880 -> FinishEatEvent 0xE7D800 -> OnEat -> AvatarEat) is not EatFood: a
// knocked-out carnivore still swallowed meat that touched its mouth. With the fix the executor 0xE7E7F0 runs such a
// pair of our knocked-out avatar as a plain contact (kind 1, the push the game uses when the swallow refuses).
bool g_fix_ko_no_swallow = true;
COOP_FIX("cell.ko_no_swallow", "BUG-0746", g_fix_ko_no_swallow, "cell: a knocked-out cell does not swallow meat or a plant whole (interaction kinds 9 / 10), so it does not eat while it is down");
double g_kodrop_until; // tests (`coop.pool kodrop <s>`): MSG_CELL_KO ignored until then
double g_statehold_until; // tests (`coop.pool statehold <s>`): MSG_CELL_STATE dropped until then (a dead link, BUG-0480)
bool g_test_away; // tests (`coop.pool away 1|0`, host): the Cell messages are handled as in an editor
// BUG-0205 / BUG-0151 (GitHub #130): while the host is away (main menu, an editor or a load, the
// state coop.cpp calls "host away") its pool is not simulated and no state comes. The guest was told
// "you play on alone", but its mirrors stood AI-locked, it spawned nothing (h_Populate) and the food
// it ate went to a host that dropped it. Now the guest plays its own pool meanwhile: the mirrors
// become cells of its own game (AI lock and keep-alive cleared, no longer mirrors), its game spawns,
// drops loot and runs every collision pair, and what it eats is kept. When the host is back, the
// first state round of the host's pool purges the guest's own cells as strays (as at a join) and
// mirrors the host's again; the kept food goes to the host as meals (BUG-0051 numbering).
bool g_solo;        // guest: playing its own pool now (the host away)
int g_solo_food[2]; // guest: meat / plant points eaten meanwhile (the guest's game has them)
bool g_solo_owed;   // the host is back: g_solo_food goes out before its first state or total is applied
// BUG-0797: the guest plays its own pool also while it plays another Cell world than the host's (joined from its own
// game, loaded a save of its own: own_world::elsewhere) or while the host already plays another stage and this guest
// is still in Cell (own_world::host_left, BUG-0638's path): the host's pool never comes there, and the guarded hooks
// (h_Populate, h_SpawnLoot, h_Pair) emptied the guest's pool, dropped its loot and its predators' pairs. Off: only
// the host in the menu / an editor / a load counts (0.7.12).
// CELL-5 (BUG-1433): the food a guest ate while the host was away (in its editor, the menu, a load) is kept in
// g_solo_food until the host is back. The guest's own Cell editor (its mating) is not Cell (in_cell() false, mode
// kEditor), and both resets (the tick's "the stage was left" and solo_tick's) dropped that food: it was in the guest's
// game only, so the host's next total took it back. On: kept while the mode is the editor (a Cell editor, or the
// creature editor at the stage's end: then the next stage drops it). Off: dropped (0.7.16).
static bool g_fix_solo_editor = true;
COOP_FIX("cell.solo_editor", "BUG-1433", g_fix_solo_editor,
         "cell: the food a guest ate while the host was away still reaches the party when the guest opens its own cell editor first");
bool solo_keep_in_editor() { return g_fix_solo_editor && game::mode_id() == modes::kEditor; }
bool g_fix_solo_elsewhere = true;
COOP_FIX("cell.solo_elsewhere", "BUG-0797", g_fix_solo_elsewhere,
         "cell: a guest in another Cell world (or still in Cell while the host is in another stage) plays its own pool");
// BUG-0809: the host sends its live cells (MSG_CELL_STATE, 10 per second, up to kSendCap cells) only to a guest whose
// latest state is in the host's Cell world (own_world::peer_here): a guest in the menu, on the loading screen,
// downloading the world or in a Cell world of its own drops them, and on a slow link they competed with the world's
// reliable chunks. Off: every guest every cycle (0.7.12).
bool g_fix_send_here = true;
COOP_FIX("cell.send_here", "BUG-0809", g_fix_send_here, "cell: the host sends live Cell state only to guests in its Cell world");
bool totals_resend_enabled() { return g_fix_resend; }
cell_logic::Rounds g_rounds;
// Class BUG-0420: a world event on one cell acted on once (cell_logic::Once, docs/re/cell.md#cell-once).
// BUG-0632: the guest keeps biting a mirror it broke (the host's break sets no dying flag until its
// animation ends, the mirror still looks alive): it sent 2-3 lethal bites per kill 0.6 s apart and the
// host broke the cell each time (two deaths, two loot drops on one spot). The guest sends a kill of a
// host index once, the host applies a kill of an index once. Off: 0.7.9.
static bool g_fix_kill_once = true;
COOP_FIX("cell.kill_once", "BUG-0632", g_fix_kill_once, "cell: a guest's lethal bite on a host creature is sent once and applied once by the host (a second break dropped a second loot)");
static cell_logic::Once g_kill_sent(10.0), g_kill_applied(10.0);
// BUG-0821: the host wrote a guest's kill into g_kill_applied before it decided on it: a kill it refused (the guest
// knocked out) or one the game's Bite spared (the host's hp higher than the guest's) still made every later kill of
// that creature by any guest "refused (killed here already)" for 10 s, and the guest's own g_kill_sent dropped its
// next lethal bites of it as sent already. Now the host records a kill only when it applied it and the creature is
// dying; a guest sends a kill again when the creature still lives here 2 s after the last one went (the host's death
// comes with its state within ~1 s). Off: 0.7.12.
// BUG-1511 (CELL-9 of the external audit of 0.7.15.5): BreakOrSwallow (the host's own lethal bite) plays the break
// animation (+0x18C 7 / 8 / 9) and posts event 5 (loot, DestroyCell) 0.1-0.3 s later without setting +0x113, and the
// host's handlers took such a cell for alive: a guest's ACT_KILL or a type-6 chunk hit at hp 0 in that window ran
// KillCell on the breaking cell (a second death, a second loot drop). On: a cell in a break or swallow state counts as
// dying in MSG_CELL_HIT and MSG_CELL_EAT. Off (`coop.fix cell.breaking_dying off`): 0.7.16.1.
bool g_fix_breaking_dying = true;
COOP_FIX("cell.breaking_dying", "BUG-1511", g_fix_breaking_dying,
         "cell: a creature the host's bite is breaking takes no second kill from a guest (a second death and loot)");
unsigned g_breaking_refused; // tests (BUG-1511)
static bool breaking(uint8_t* c)
{
    int st = c ? F<int>(c, 0x18C) : 0; // 7 / 8 / 9: the game's break / swallow states (h_Break)
    return g_fix_breaking_dying && (st == 7 || st == 8 || st == 9);
}
static bool g_fix_kill_refused = true;
COOP_FIX("cell.kill_refused", "BUG-0821", g_fix_kill_refused, "cell: a guest's kill the host refused or the game spared does not block that creature's next kills for 10 s");
// BUG-0959: the host's trace line for a knocked-out guest's kill said "applied" and the next line refused it (a support
// log read as if the creature had been killed). Now it says "refused (knocked out)". Diagnostics only. Off: 0.7.12.
static bool g_fix_ko_trace = true;
COOP_FIX("cell.ko_trace", "BUG-0959", g_fix_ko_trace, "cell: the host's trace names a knocked-out guest's refused kill as refused, not applied");
// guest: whether a lethal bite of mirror `hidx` goes out (false: sent already, the host's answer is on its way)
bool kill_send_first(uint32_t hidx, double now)
{
    if (g_fix_kill_refused && g_fix_kill_once && g_kill_sent.has(hidx, now)) {
        auto it = g_kill_sent.at.find(hidx);
        if (it != g_kill_sent.at.end() && now - it->second >= 2.0) {
            g_kill_sent.drop(hidx); // the host left it alive: its refusal (or its spare) is over
            g_n.kills_resent++;
        }
    }
    return g_kill_sent.first(hidx, now, g_fix_kill_once);
}
bool g_in_guest_attack; // BUG-0714: a guest's attack replayed through the game (KillCell inside it counts as the player's kill)
CALL_GUARD("cellsync.guest_attack", g_in_guest_attack); // BUG-1047
// BUG-0636: the guest's mirror of a cell it ate (ACT_GONE) was made again from the next state chunk
// before the host's state dropped it (one round trip) and the guest's game ate it again (its food
// twice). The guest keeps a tombstone of the host indices it ate and mirrors them no more (5 s).
// Off: 0.7.9.
bool g_fix_eat_once = true;
COOP_FIX("cell.eat_once", "BUG-0636", g_fix_eat_once, "cell: a cell the guest ate is not mirrored again from a state sent before the host removed it (it was eaten twice)");
cell_logic::Once g_eaten(5.0);
std::unordered_map<uint32_t, uint32_t> g_gone_sends; // tests: ACT_GONE sends per host index (last 512)
cell_logic::Meals g_meals;              // guest: meals sent, not yet in a host total
std::unordered_map<int, uint32_t> g_food_ack; // host: the last meal number of each guest it applied
// CELL-1 (BUG-1431): a guest's meal is acknowledged (g_food_ack) when its message is read, but since BUG-0633
// (cell.food_in_step) its food is added only at the next cell step. host_tick's total of that frame (every 0.5 s) then
// went out without the meal and with its number: the guest dropped the meal from "on the way" and its food dipped by it
// for 0.5 s (BUG-0051's symptom; over a level line a level down and up). On: the acknowledgement waits in the same queue
// as the food, after it. Off: at once (0.7.16).
static bool g_fix_ack_in_step = true;
COOP_FIX("cell.ack_in_step", "BUG-1431", g_fix_ack_in_step,
         "cell: the host acknowledges a guest's meal together with its food, so the guest's food does not dip for a moment");
static void ack_meal(int from, uint32_t meal)
{
    if (g_fix_ack_in_step)
        food_in_step([from, meal] {
            if (C.sess && net::is_player(*C.sess, (uint8_t)from)) // (a player that left meanwhile is forgotten)
                g_food_ack[from] = meal;
        });
    else
        g_food_ack[from] = meal;
}
std::unordered_map<int, FoodTx> g_food_tx; // host: what each guest got last

// ---------------- both: symbiosis ----------------
// A lethal hit does not kill a player who has a partner in the pool: the cell is knocked out for
// kDownTime seconds. A partner who stays right next to it for kReviveHold seconds revives it
// (half health). Otherwise it respawns next to the partner as a small cell (half size), which
// grows back by eating or at the next level up. Alone in the pool the vanilla death happens
// (it rebuilds the whole level).
constexpr double kDownTime = 10.0, kReviveHold = 1.5;
double g_last_regen, g_invuln_until, g_last_tick;
bool g_downed;
static double g_down_since, g_hold;
static net::Vec3 g_down_pos;
// BUG-0798: g_down_pos was kept in the metres of the knockout's moment, but a level change in the 10 s rescales
// the world (0xE6DB70: every position times old / new scale, docs/re/cell.md#cell-world-scale): the cell was held
// 3x (level 1 -> 2) or 3.3x (2 -> 3) farther out than the world it lay in, away from the partner and the food, no
// revive. Now the spot is kept with the world scale of that moment and drawn in today's (pos x scale stays the same).
// Off: 0.7.12 (the spot of the knockout's metres).
static bool g_fix_down_scale = true;
COOP_FIX("cell.down_scale", "BUG-0798", g_fix_down_scale, "cell: a knocked-out cell stays at its place in the world while a level change rescales it (it was held 3x farther out, no revive)");
static float g_down_scale; // the world scale g_down_pos is in (0: unknown)
static net::Vec3 down_pos()
{
    float now = world_scale();
    if (!g_fix_down_scale || g_down_scale <= 0 || now <= 0 || now == g_down_scale)
        return g_down_pos;
    float k = g_down_scale / now;
    return {g_down_pos.x * k, g_down_pos.y * k, g_down_pos.z};
}
float g_body = 1.f;    // body size factor after a respawn (1 = full size)
float g_body_size = 0; // the target size we wrote, to notice the game resizing the avatar
// BUG-1002: Cell state of one session or one Cell world that no reset cleared (the food queue, the join after load,
// the own cell's last good pose, the body factor). Reset by the lifecycle registry; `coop.fix cell.session_reset off`:
// kept (0.7.13).
bool g_fix_session_reset = true;
COOP_FIX("cell.session_reset", "BUG-1002", g_fix_session_reset, "cell: the food queue, the join after load, the own cell's last good pose and the body factor end with their session or Cell world");
// the factor of a small respawn belongs to that Cell world (a session end in it keeps the small body growing back)
WORLD_STATE("cell.body", &g_fix_session_reset, [] {
    g_body = 1.f;
    g_body_size = 0;
}, []() -> size_t { return g_body < 1.f ? 1 : 0; });

std::string nearest_partner(float max_dist, net::Vec3* where, float* dist, bool helper);


// "right next to": the cells (almost) touch
float revive_dist(uint8_t* a)
{
    float size = F<float>(a, kSize);
    return std::max(3.f, size * 3.f);
}

// A cell with a soft body (the avatar) takes its position and velocity from its particles
// (modes::cell_move moves their positions and, for a stop, clears their velocities);
// +0x1C4 is the steering acceleration the cell step adds to the particle velocities.
// BUG-0343: the cell camera (cCellUI, update 0xE600A0, docs/re/cell.md#cell-camera) chases a target
// point +0x54 that moves towards the avatar only at 1.5 x the avatar's speed, and keeps the drawn
// point +0x48 within a box around the avatar: after a jump of a stopped cell (our respawn next to the
// partner, a guest put next to the host after a load or an editor) the view stayed off centre until
// the player swam a while. The game's own jumps (0xE611B0, 0xE794F0) set +0x48, +0x54 and +0x60 to
// the new place; we do the same for every jump of our own cell.
void camera_snap(const net::Vec3& p)
{
    uint8_t* ui = a_CellUI ? *(uint8_t**)a_CellUI : nullptr;
    if (!ui || !skg::readable(ui + 0x48, 0x24))
        return;
    for (int off : {0x48, 0x54, 0x60}) {
        auto* q = (float*)(ui + off);
        q[0] = p.x; q[1] = p.y; q[2] = p.z;
    }
    g_n.camera_snaps++;
}

// BUG-0604: the camera update (0xE600A0) eases the drawn point +0x48 with factors computed from the
// avatar's speed |+0x90|; one frame with the own cell's velocity NaN (the guest's level-up, mask 0x104,
// BUG-0307) makes +0x48 NaN, and from then on every frame keeps it NaN (x + (y - NaN) * k): its box
// clamp around the avatar compares a NaN distance and never runs. The view boxes are built around +0x48,
// so the screen showed only the water for the rest of the session, while the cell kept swimming. Any
// non-finite camera point goes back onto our own cell as the game's own jumps set them.
static bool camera_finite()
{
    uint8_t* ui = a_CellUI ? *(uint8_t**)a_CellUI : nullptr;
    if (!ui || !skg::readable(ui + 0x48, 0x24))
        return true;
    for (int i = 0; i < 9; i++)
        if (!std::isfinite(((float*)(ui + 0x48))[i]))
            return false;
    return true;
}
static void camera_heal(uint8_t* av)
{
    if (!av || camera_finite())
        return;
    net::Vec3 p = pos_of(av);
    if (!views::finite_pos(p))
        return; // the own cell's heal puts it back first; the camera follows next frame
    camera_snap(p);
    if (++g_n.camera_heals <= 10)
        H->log(M, SK_WARN, "coop: cell: the camera was not finite, put back on our own cell (%.1f %.1f) (BUG-0604)", p.x, p.y);
}

// BUG-1394 (diagnostics): WorldScaleStep 0xE73E50 passes the camera's drawn point (cCellUI +0x48) to 0xE6DB70, which
// rescales the world about the origin's centre 0x16B3C28 while that point is within 1000 m of the origin, and about the
// point itself (snapped by 0xE4F900) when it is farther: then every position of this game is moved by that point, which
// no partner's game does (campaign_short#2 of lab 20261121-094225: the host's cell went (149 176) ->
// (2724 -13403) through the end of Cell while the guests' went to 0: sync-check "p: 1 apart (p0 171.3)"). Said once
// per scale change with the camera's state, to tell why the point was that far the next time.
static void scale_centre_note()
{
    static float said_target = 0;
    uint8_t* g = G();
    uint8_t* ui = a_CellUI ? *(uint8_t**)a_CellUI : nullptr;
    if (!g || !ui || !skg::readable(g + 0x514C, 8) || !skg::readable(ui + 0x48, 0x24) || !skg::readable(ui + 0xE0, 4))
        return;
    float sc = F<float>(g, 0x514C), target = F<float>(g, 0x5150);
    if (sc == target || target == said_target)
        return;
    const float* q = (const float*)(ui + 0x48);
    float r2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
    if (!(r2 > 1e6f))
        return;
    said_target = target;
    uint8_t* av = avatar();
    net::Vec3 a = av ? pos_of(av) : net::Vec3{};
    H->log(M, SK_WARN,
           "coop: cell: the world scale change %.4g -> %.4g moves this game's world about the camera point (%.1f %.1f %.1f), "
           "%.0f m from the origin (target +0x54 (%.1f %.1f), followed cell %08X, own cell (%.1f %.1f), cinematic %d) (BUG-1394)",
           sc, target, q[0], q[1], q[2], std::sqrt(r2), q[3], q[4], F<uint32_t>(ui, 0xE0), a.x, a.y,
           (int)game::cinematic_playing());
}

void stop_at(uint8_t* c, const net::Vec3& p)
{
    if (c && c == avatar() && net::dist(pos_of(c), p) > 2.f)
        camera_snap(p);
    modes::cell_move(c, p, true);
    auto* bv = (float*)(c + 0x1C4);
    bv[0] = bv[1] = bv[2] = 0;
    auto* t = (float*)(c + kTarget);
    t[0] = p.x; t[1] = p.y; t[2] = p.z;
    auto* v = (float*)(c + kVel);
    v[0] = v[1] = v[2] = 0;
}

// scale the avatar body (target size; the game smooths +0x58 towards it), keeping the mass in step
void set_body(uint8_t* a, float factor)
{
    float cur = F<float>(a, kTargetSize);
    if (cur <= 0)
        return;
    float full = g_body < 1.f ? cur / g_body : cur;
    float want = full * factor, k = want / cur;
    F<float>(a, kTargetSize) = want;
    F<float>(a, 0xB8) *= k * k; // mass ~ size^2 (as SetCellSize 0xE57190 computes it)
    F<float>(a, 0xBC) /= k * k; // inverse mass
    g_body = factor;
    g_body_size = want;
}

// ---- what everybody sees ----
std::unordered_map<int, Ko> g_ko; // remote player -> knockout state

// the nearest partner; `helper`: one who can help: rescue (a lethal hit becomes a knockout), revive,
// the spot to respawn at. A knocked-out partner (held at hp 1, never flagged dead) cannot (BUG-0131: two cells
// knocked out side by side revived each other).
std::string nearest_partner(float max_dist, net::Vec3* where, float* dist, bool helper)
{
    uint8_t* a = avatar();
    if (!a || !C.partners)
        return {};
    std::unordered_set<std::string> out;
    if (helper && C.sess)
        for (auto& [id, k] : g_ko)
            if (k.state == KO_OUT)
                out.insert(C.sess->name_of((uint8_t)id));
    net::Vec3 me = pos_of(a);
    std::string best;
    float bd = max_dist;
    for (auto& [n, p] : C.partners()) {
        if (out.count(n))
            continue;
        float d = net::dist(me, p);
        if (d <= bd) {
            bd = d;
            best = n;
            if (where)
                *where = p;
        }
    }
    if (dist)
        *dist = best.empty() ? -1.f : bd;
    return best;
}
// BUG-0792: with 3-4 players every guest was put at the nearest partner + one fixed offset (a load: (2.5, 1), a
// knockout respawn: (2, 1)), two guests on one spot, and the nearest partner could be another guest (both at the
// pool's start after a load: no move at all). Now a guest goes to its own slot around the host's cell
// (cell_logic::player_spawn_offset of its session id); the host (or a guest whose host is not here or knocked out
// for a respawn) keeps the old rule. Off: 0.7.12.
bool g_fix_spawn_slots = true;
COOP_FIX("cell.spawn_slots", "BUG-0792", g_fix_spawn_slots, "cell: with 3-4 players each guest is placed at its own spot around the host after a load or a knockout respawn (they landed on one spot)");
static int g_spawn_slot = -1; // tests: the slot of our last placement (-1: the old rule)
bool guest_knocked_out(uint32_t from);
uint8_t* puppet_cell(int id);
// where our cell goes next to the others: *at (and the anchor's distance in *dist); false: nobody to go to.
// helper: the anchor must be able to help (a respawn: not knocked out). old_dx / old_dy: the old offset.
bool spawn_spot(net::Vec3* at, float* dist, bool helper, float old_dx, float old_dy)
{
    uint8_t* a = avatar();
    uint8_t* hc = g_fix_spawn_slots && is_guest() && C.sess ? puppet_cell(0) : nullptr;
    if (a && hc && !(helper && guest_knocked_out(0))) {
        net::Vec3 h = pos_of(hc);
        float dx, dy;
        cell_logic::player_spawn_offset(C.sess->my_id(), 4.f, &dx, &dy);
        *at = {h.x + dx, h.y + dy, 0.f};
        if (dist)
            *dist = net::dist(pos_of(a), h);
        g_spawn_slot = C.sess->my_id();
        return true;
    }
    if (g_fix_spawn_slots && is_guest() && !helper)
        return false; // a load: wait for the host's cell (not to the cell of another guest)
    net::Vec3 p;
    if (nearest_partner(1e9f, &p, dist, helper).empty())
        return false;
    *at = {p.x + old_dx, p.y + old_dy, 0.f};
    g_spawn_slot = -1;
    return true;
}
// BUG-0792: another player's cell (as we draw it) within `r` of ours
bool crowded(uint8_t* a, float r)
{
    if (!a || !C.sess)
        return false;
    for (int id = 0; id < 8; id++)
        if (uint8_t* pc = id != C.sess->my_id() ? puppet_cell(id) : nullptr)
            if (net::dist(pos_of(pc), pos_of(a)) < r)
                return true;
    return false;
}
static double g_ko_sent;
constexpr float kKoOpacity = 0.35f;

// a one-shot game effect on a cell (names from the game: cell_death_oneshot, cell_hatch_ice, ...)
void effect(uint8_t* c, const char* name)
{
    if (!c || !a_Fnv || !a_Effect || !F<uint32_t>(c, 0x248))
        return;
    uint32_t id = skg::cdecl_call<uint32_t>(a_Fnv, name, 0x811C9DC5u, 1);
    skg::cdecl_call<void*>(a_Effect, c, id);
}

static uint8_t g_ko_logged;
static void send_ko(uint8_t state, float left, float hold)
{
    if (!C.sess || !C.sess->connected())
        return;
    net::Writer w;
    w.u8((uint8_t)C.sess->my_id());
    w.u8(state);
    w.u8((uint8_t)std::max(0.f, std::min(left * 10.f, 255.f)));
    w.u8((uint8_t)std::max(0.f, std::min(hold * 255.f, 255.f)));
    if (C.sess->role() == net::Role::Host)
        C.sess->broadcast_reliable(net::MSG_CELL_KO, w);
    else
        C.sess->send_reliable_to(0, net::MSG_CELL_KO, w);
    if (state != g_ko_logged) // the knockout state is refreshed every second: changes only
        trace::line("ko", "own cell state %u (0 up, 1 out, 2 revived, 3 respawned) left=%.1f hold=%.2f", state, left, hold);
    g_ko_logged = state;
    g_ko_sent = H->time_seconds();
}

// BUG-0823 (class BUG-0426, as BUG-0342): the partner's puppet cell as the remote table holds it. A level rebuild (the
// host's death without a helper, an own-cell apply) frees every cell and makes the own avatar first, often at the
// address a puppet had; the remote loop drops that puppet only after cellsync::tick and the frame's messages, so in
// that frame the host's own cell was taken for the partner's (the knockout look, a guest's bite with the host's cell
// as the attacker, the puppet's poison step). Now only a cell that is still that puppet by its pool index
// (modes::cell_is_puppet) is returned. Old (`coop.fix cell.puppet_cell_index off`): the table's pointer.
static bool g_puppet_cell_fix = true;
COOP_FIX("cell.puppet_cell_index", "BUG-0823", g_puppet_cell_fix, "cell: the host's own cell is never taken for a partner's cell in the frame of a level rebuild (it got the knockout look or bit for the partner)");
uint8_t* avatar();
uint8_t* puppet_cell(int id)
{
    if (!C.puppets)
        return nullptr;
    for (auto& p : C.puppets())
        if (p.id == id) {
            if (g_puppet_cell_fix && p.cell && !modes::cell_is_puppet(p.cell)) {
                g_n.puppet_cell_refused++;
                return nullptr;
            }
            if (p.cell && (uint8_t*)p.cell == avatar()) // (tests: the own cell handed out as the partner's)
                g_n.puppet_cell_own++;
            return (uint8_t*)p.cell;
        }
    return nullptr;
}

static void on_ko(int id, uint8_t state, float left, float hold)
{
    Ko& k = g_ko[id];
    uint8_t prev = k.state;
    k.left = left;
    k.hold = hold;
    k.refreshed = H->time_seconds();
    bool expired = k.expired;
    k.expired = false;
    if (state == prev)
        return;
    k.state = state;
    k.since = k.refreshed;
    trace::line("ko", "player %d cell state %u -> %u (0 up, 1 out, 2 revived, 3 respawned)%s", id, prev, state,
                in_cell() && puppet_cell(id) ? "" : " (no puppet here)");
    // outside the Cell stage only the state is kept (BUG-0061: the "up again" was dropped there);
    // the puppets of other stages are not cells
    uint8_t* c = in_cell() ? puppet_cell(id) : nullptr;
    if (state == KO_OUT && expired && g_fix_ko_grace && c)
        F<float>(c, kOpacity) = kKoOpacity; // the same knockout after a gap: no second death effect
    else if (state == KO_OUT) {
        effect(c, "cell_death_oneshot");
        k.fx += c != nullptr;
    }
    else if (state == KO_REVIVED && !effects::cell_own_only_fix()) // BUG-1243: cell_hatch_ice blacks out this screen
        effect(c, "cell_hatch_ice");
    else if (state == KO_RESPAWNED)
        k.fx_at = k.since + 0.4; // after the puppet has moved next to its partner
    if (state != KO_OUT && c)
        F<float>(c, kOpacity) = 1.f;
}

// BUG-0061: a "knocked out" its owner no longer refreshes (it left, its "up again" was lost while we
// were outside the Cell stage) ends here: the partner was drawn translucent with a frozen "KO" label
static void ko_expire(double now)
{
    for (auto& [id, k] : g_ko) {
        if (k.state != KO_OUT || !cell_logic::ko_stale(now, k.refreshed, k.left, g_fix_kostale, g_fix_ko_grace))
            continue;
        k.state = KO_UP;
        k.since = now;
        k.expired = true;
        if (uint8_t* c = in_cell() ? puppet_cell(id) : nullptr)
            F<float>(c, kOpacity) = 1.f;
    }
}

bool SK_CDECL h_KillCell(uint8_t* c, uint32_t killer, void* res, bool by_player, int type, float dt);
static bool g_ko_dying = false; // cell.respawn_small off: the timed-out knock-out ends in the game's own death
CALL_GUARD("cellsync.ko_dying", g_ko_dying); // BUG-1047

bool go_down(uint8_t* c)
{
    if (!c || c != avatar() || !C.sess || !C.sess->connected() || g_ko_dying)
        return false;
    // F3 (BUG-0768): cell.knockout off: a lethal hit kills as in a solo game, also next to a partner
    if (!g_downed && !settings::on(settings::kCellKnockout))
        return false;
    if (!g_downed) {
        if (nearest_partner(1e9f, nullptr, nullptr, true).empty())
            return false; // alone in the pool: the vanilla death
        g_downed = true;
        g_down_since = H->time_seconds();
        g_hold = 0;
        g_down_pos = pos_of(c);
        g_down_scale = world_scale();
        effect(c, "cell_death_oneshot");
        send_ko(KO_OUT, (float)kDownTime, 0);
        if (C.notice)
            C.notice(C.my_name() + " is knocked out: swim right next to them within 10 s");
    }
    F<int>(c, kHp) = 1;
    F<uint8_t>(c, kDead) = 0;
    F<uint8_t>(c, kDying) = 0;
    F<uint8_t>(c, kInvuln) = 1;
    g_invuln_until = 0;
    stop_at(c, down_pos());
    return true;
}

static void downed_tick(uint8_t* a, double now, double dt)
{
    if (F<uint8_t>(a, kDying) || F<uint8_t>(a, kDead)) {
        F<uint8_t>(a, kDead) = 0;
        F<uint8_t>(a, kDying) = 0;
    }
    F<int>(a, kHp) = 1;
    F<uint8_t>(a, kInvuln) = 1;
    F<float>(a, kOpacity) = kKoOpacity;
    stop_at(a, down_pos());
    float d = 0;
    std::string helper = nearest_partner(revive_dist(a), nullptr, &d, true);
    g_hold = helper.empty() ? 0 : g_hold + dt;
    if (now - g_ko_sent > 0.25)
        send_ko(KO_OUT, (float)std::max(0.0, kDownTime - (now - g_down_since)), (float)(g_hold / kReviveHold));
    if (!helper.empty() && g_hold >= kReviveHold) { // revived by the partner
        g_downed = false;
        F<int>(a, kHp) = 3;
        F<float>(a, kOpacity) = 1.f;
        g_invuln_until = now + 2.0;
        g_n.revives++;
        effect(a, "cell_hatch_ice");
        send_ko(KO_REVIVED, 0, 1);
        if (C.notice)
            C.notice(helper + " revived " + C.my_name());
        return;
    }
    if (now - g_down_since < kDownTime)
        return;
    g_downed = false;
    if (!settings::on(settings::kCellRespawn)) { // F3 (BUG-0768): nobody came: the game's own death and respawn
        F<float>(a, kOpacity) = 1.f;
        F<uint8_t>(a, kInvuln) = 0;
        F<int>(a, kHp) = 0;
        g_invuln_until = 0;
        send_ko(KO_UP, 0, 0);
        g_n.ko_deaths++;
        if (C.notice)
            C.notice(C.my_name() + " was not revived in time");
        {
            callguard::Hold ko_dying(g_ko_dying);
            h_KillCell(a, avatar_idx(), F<void*>(a, kRes), false, 0, 0.f);
        }
        return;
    }
    // nobody came: respawn next to the partner, small again
    net::Vec3 at = down_pos();
    std::string partner = nearest_partner(1e9f, nullptr, nullptr, true);
    if (!spawn_spot(&at, nullptr, true, 2.f, 1.f)) // BUG-0792: our own slot around the host
        at = down_pos();
    stop_at(a, at);
    F<uint8_t>(a, 0x04) = 1; // idle
    F<int>(a, kHp) = 6;
    g_invuln_until = now + 3.0;
    set_body(a, kSmall);
    F<float>(a, kOpacity) = 1.f;
    g_n.respawns++;
    effect(a, "cell_egg_L0_autohatch");
    send_ko(KO_RESPAWNED, 0, 0);
    if (C.notice)
        C.notice(C.my_name() + " respawned small" + (partner.empty() ? "" : " next to " + partner));
}

// the resource's cell type (+0xB4: 2 solid, 3 plant, 5 poison, ...) and ai.type (docs/re/cell.md 1:
// the first int of the AI block chosen by difficulty, as IsDamageable 0xE52940 picks it); false when
// the resource data cannot be read. One reader for kind_of, ai_type and the guest-bite gate.
bool res_types(uint8_t* c, int* cell_type, int* ai_type)
{
    void* ref = c ? F<void*>(c, kRes) : nullptr;
    if (!ref || !a_GetData)
        return false;
    void* holder = nullptr;
    uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, ref, &holder);
    if (holder)
        --F<int>(holder, 0xC);
    if (!res)
        return false;
    int d = S() ? F<int>(S(), 0x7C) : 1;
    uint8_t* ai = res + 0xE0;
    if (d == 0 && F<int>(res, 0x248) != -1)
        ai = res + 0x248;
    else if (d == 2 && F<int>(res, 0x194) != -1)
        ai = res + 0x194;
    *cell_type = F<int>(res, 0xB4);
    *ai_type = F<int>(ai, 0);
    return true;
}

// ai.type of a cell, -1 unknown
int ai_type(uint8_t* c)
{
    int ct, ai;
    return res_types(c, &ct, &ai) ? ai : -1;
}

// a host total or a partner's meal of the last frame of a session or a Cell world is not applied after it
LIFECYCLE_STATE("cell.food_queue", lifecycle::kSession | lifecycle::kWorld, &g_fix_session_reset, [] { g_food_q.clear(); },
                []() -> size_t { return g_food_q.size(); });




bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetData = H->sym("Simulator::Cell::GetData");
    a_Pair = H->sym("Simulator::Cell::HandleCollisionPair");
    a_Cine = H->sym("Simulator::cCinematicManager::Get");
    a_Wipe = H->sym("Simulator::Cell::RemoveCellsOfOldScale");
    a_DamageCell = H->sym("Simulator::Cell::DamageCell");
    a_Fnv = H->sym("FNV_hash");
    a_Effect = H->sym("Simulator::Cell::cCellGFX::InstanceEffectOnCell");
    a_KillCell = H->sym("Simulator::Cell::KillCell");
    a_Break = H->sym("Simulator::Cell::BreakOrSwallow");
    a_Destroy = H->sym("Simulator::Cell::DestroyCell");
    a_AddFood = H->sym("Simulator::Cell::AddFoodPoints");
    a_Populate = H->sym("Simulator::Cell::PopulateMarker");
    a_Rollover = H->sym("Simulator::Cell::ShowHealthRollover");
    a_CellUI = H->sym("Simulator::Cell::cCellUI::_ptr"); // rollover_of (tests), camera_snap (BUG-0343)
    a_DnaFor = H->sym("Simulator::Cell::DNAForFood"); // BUG-0351 diagnostics (dna_note)
    a_GetDamage = H->sym("Simulator::Cell::GetDamageAmount");
    a_TargetParams = H->sym("Simulator::Cell::GetTargetSearchParams");
    a_IsPrey = H->sym("Simulator::Cell::IsPrey");
    a_ModelData = H->sym("Simulator::Cell::GetCellModelData");
    a_IsThreat = H->sym("Simulator::Cell::IsThreat");
    a_ScaleDiff = H->sym("Simulator::Cell::GetScaleDifference");
    a_NoAttack = H->sym("Simulator::Cell::ShouldNotAttack");
    a_Alert = H->sym("Simulator::Cell::AlertSpeciesOfAttack");
    a_React = H->sym("Simulator::Cell::ReactToAttacker");
    a_Rest = H->sym("Simulator::Cell::RestAfterBite");
    a_Soft = H->sym("Simulator::Cell::SoftBodyStep");
    a_ContactPass = H->sym("Simulator::Cell::ContactSolvePass");
    a_ContactSolve = H->sym("Simulator::Cell::ContactSolve");
    a_ChunkA = H->sym("Simulator::Cell::EatChunkA");
    a_ChunkB = H->sym("Simulator::Cell::EatChunkB");
    a_Bite = H->sym("Simulator::Cell::Bite");
    a_SpawnLoot = H->sym("Simulator::Cell::SpawnLoot");
    a_EatFood = H->sym("Simulator::Cell::EatFood");
    a_RefCreate = H->sym("Simulator::Cell::cCellDataReference_::Create");
    a_Serializer = H->sym("Simulator::Cell::cCellSerializer::_ptr");
    a_S = H->sym("Simulator::Cell::cCellSerializableData::_ptr");
    a_PopTiles = H->sym("Simulator::Cell::PopulateTiles");
    a_PopTile = H->sym("Simulator::Cell::PopulateTile");
    a_TileActive = H->sym("Simulator::Cell::IsTileActive");
    a_ViewBoxes = H->sym("Simulator::Cell::ViewBoxes");
    a_TileSizes = H->sym("Simulator::Cell::TileSizes"); // BUG-0975
    a_WorldScale = H->sym("Simulator::Cell::WorldScaleStep");
    a_PickTargets = H->sym("Simulator::Cell::PickTargets");
    g_ok = a_DamageCell && a_KillCell && a_Break && a_Destroy && a_AddFood && a_Populate && a_RefCreate &&
           a_Serializer && a_S;
    if (!g_ok) {
        H->log(M, SK_WARN, "cellsync: symbols missing, cell co-op disabled");
        return false;
    }
    H->hook_create(M, (void*)a_DamageCell, (void*)h_DamageCell, (void**)&o_DamageCell);
    a_PoisonCullRet = H->sym("SporeCoop::Cell_PoisonCull_ret"); // tests (BUG-0716)
    a_CullRet = H->sym("SporeCoop::Cell_CullDestroy_ret");    // tests (BUG-1064)
    a_PoisonStep = H->sym("SporeCoop::Cell_PoisonStep"); // BUG-0716
    if (uintptr_t cl = H->sym("SporeCoop::Cell_PoisonCloud")) // tests (BUG-0716)
        H->hook_create(M, (void*)cl, (void*)h_Cloud, (void**)&o_Cloud);
    a_Zap = H->sym("SporeCoop::Cell_ElectricAttack"); // BUG-0715
    if (a_Zap)
        H->hook_create(M, (void*)a_Zap, (void*)h_Zap, (void**)&o_Zap);
    if (a_GetDamage) // BUG-0714: the guest's damage for its replayed attack
        H->hook_create(M, (void*)a_GetDamage, (void*)h_GetDamage, (void**)&o_GetDamage);
    H->hook_create(M, (void*)a_KillCell, (void*)h_KillCell, (void**)&o_KillCell);
    H->hook_create(M, (void*)a_Break, (void*)h_Break, (void**)&o_Break);
    if (uintptr_t sw = H->sym("Simulator::Cell::SwallowWhole")) // BUG-0827
        H->hook_create(M, (void*)sw, (void*)h_Swallow, (void**)&o_Swallow);
    H->hook_create(M, (void*)a_Destroy, (void*)h_Destroy, (void**)&o_Destroy);
    H->hook_create(M, (void*)a_AddFood, (void*)h_AddFood, (void**)&o_AddFood);
    H->hook_create(M, (void*)a_Populate, (void*)h_Populate, (void**)&o_Populate);
    if (a_PopTiles && a_PopTile && a_TileActive && a_ViewBoxes && a_WorldScale) { // GOG 2024 only (BUG-0174)
        H->hook_create(M, (void*)a_WorldScale, (void*)h_WorldScale, (void**)&o_WorldScale);
        H->hook_create(M, (void*)a_PopTiles, (void*)h_PopTiles, (void**)&o_PopTiles);
        H->hook_create(M, (void*)a_PopTile, (void*)h_PopTile, (void**)&o_PopTile);
        H->hook_create(M, (void*)a_TileActive, (void*)h_TileActive, (void**)&o_TileActive);
    } else
        a_ViewBoxes = 0;
    if (a_Pair)
        H->hook_create(M, (void*)a_Pair, (void*)h_Pair, (void**)&o_Pair);
    if (a_Wipe)
        H->hook_create(M, (void*)a_Wipe, (void*)h_Wipe, (void**)&o_Wipe);
    if (a_NoAttack)
        H->hook_create(M, (void*)a_NoAttack, (void*)h_NoAttack, (void**)&o_NoAttack);
    if (a_SpawnLoot)
        H->hook_create(M, (void*)a_SpawnLoot, (void*)h_SpawnLoot, (void**)&o_SpawnLoot);
    if (a_Soft)
        H->hook_create(M, (void*)a_Soft, (void*)h_Soft, (void**)&o_Soft);
    modes::set_cell_after_step(mirrors_after_step); // BUG-1247
    kin_init(); // BUG-1257
    if (a_ContactPass && a_ContactSolve) { // GOG 2024 only (BUG-0307)
        H->hook_create(M, (void*)a_ContactPass, (void*)h_ContactPass, (void**)&o_ContactPass);
        H->hook_create(M, (void*)a_ContactSolve, (void*)h_ContactSolve, (void**)&o_ContactSolve);
    }
    if (a_Bite)
        H->hook_create(M, (void*)a_Bite, (void*)h_Bite, (void**)&o_Bite);
    if (a_PickTargets) // BUG-0306
        H->hook_create(M, (void*)a_PickTargets, (void*)h_PickTargets, (void**)&o_PickTargets);
    a_CreateSoft = H->sym("Simulator::Cell::CreateSoftBody");
    a_GfxPtr = H->sym("Simulator::Cell::cCellGFX::_ptr");
    if (a_CreateSoft && a_GfxPtr) // GOG 2024 only (BUG-0355)
        H->hook_create(M, (void*)a_CreateSoft, (void*)h_CreateSoft, (void**)&o_CreateSoft);
    a_PartAlloc = H->sym("Simulator::Cell::ParticleAlloc");
    a_PartFree = H->sym("Simulator::Cell::ParticleFree");
    if (a_PartAlloc && a_PartFree) { // GOG 2024 only (BUG-0338)
        H->hook_create(M, (void*)a_PartAlloc, (void*)h_PartAlloc, (void**)&o_PartAlloc);
        H->hook_create(M, (void*)a_PartFree, (void*)h_PartFree, (void**)&o_PartFree);
    }
    a_UpdatePairs = H->sym("SweepPrune::UpdatePairs");
    a_MergeAxis = H->sym("SweepPrune::MergeAxis");
    a_Overlap = H->sym("SweepPrune::OverlapOther");
    if (a_UpdatePairs && a_MergeAxis && a_Overlap) // GOG 2024 only (BUG-0337)
        H->hook_create(M, (void*)a_UpdatePairs, (void*)h_UpdatePairs, (void**)&o_UpdatePairs);
    if (a_EatFood) // BUG-0364
        H->hook_create(M, (void*)a_EatFood, (void*)h_EatFood, (void**)&o_EatFood);
    a_ExecInter = H->sym("Simulator::Cell::ExecuteInteraction");
    if (a_ExecInter) // BUG-0746 (GOG 2024)
        H->hook_create(M, (void*)a_ExecInter, (void*)h_ExecInter, (void**)&o_ExecInter);
    if (a_ChunkA && a_ChunkB) {
        H->hook_create(M, (void*)a_ChunkA, (void*)h_ChunkA, (void**)&o_ChunkA);
        H->hook_create(M, (void*)a_ChunkB, (void*)h_ChunkB, (void**)&o_ChunkB);
    }
    return true;
}


// a NaN in the first frame of a new avatar is not healed to the old world's pose and size
WORLD_STATE("cell.own_good", &g_fix_session_reset, [] { g_own_good_set = false; }, []() -> size_t { return g_own_good_set ? 1 : 0; });
// a new avatar of the next session with the old avatar's pool index is seen as new (the guest is put next to the host)
LIFECYCLE_STATE("cell.join", lifecycle::kSession | lifecycle::kWorld, &g_fix_session_reset, [] {
    g_join_avatar = 0;
    g_join_since = g_join_check = -1;
    g_join_tries = 0;
}, []() -> size_t { return g_join_avatar || g_join_since >= 0 || g_join_check >= 0 ? 1 : 0; });

// ---------------- both ----------------
// BUG-0762 (F3 of 0.7.12): the regeneration near a partner is the host's live session setting cell.regen. Off
// (`coop.fix settings.cell_regen off`): the setting is not read, the regeneration always runs (0.7.11).
static bool g_regen_setting = true;
COOP_FIX("settings.cell_regen", "BUG-0762", g_regen_setting, "cell: the regeneration near a partner follows the host's setting cell.regen");
static void symbiosis_tick(double now)
{
    uint8_t* a = avatar();
    double dt = g_last_tick > 0 ? std::min(now - g_last_tick, 0.5) : 0;
    g_last_tick = now;
    if (!a)
        return;
    if (g_body < 1.f && std::fabs(F<float>(a, kTargetSize) - g_body_size) > g_body_size * 0.01f)
        g_body = 1.f; // the game resized the avatar (level change): full size again
    if (C.puppets) // a partner's cell replaced by a new one (the partner blinked out and in)
        for (auto& p : C.puppets()) {
            void*& last = g_puppet_seen[p.id];
            if (last && last != p.cell)
                g_n.puppet_new++;
            last = p.cell;
        }
    ko_expire(now);
    for (auto& [id, k] : g_ko) {
        uint8_t* c = k.state == KO_OUT || k.fx_at > 0 ? puppet_cell(id) : nullptr;
        if (k.state == KO_OUT && c)
            F<float>(c, kOpacity) = kKoOpacity;
        if (k.fx_at > 0 && now >= k.fx_at) {
            k.fx_at = 0;
            // BUG-1243: the respawn's hatch (cell_egg_L0_autohatch) blacks out this player's screen and draws the egg
            // over it: not for a partner's puppet (effects.cell_own_only)
            if (!effects::cell_own_only_fix())
                effect(c, "cell_egg_L0_autohatch");
        }
    }
    if (g_downed) {
        downed_tick(a, now, dt);
        return;
    }
    if (g_invuln_until > 0 && now > g_invuln_until) {
        F<uint8_t>(a, kInvuln) = 0;
        g_invuln_until = 0;
    }
    if (now - g_last_regen < 3.0)
        return;
    g_last_regen = now;
    int hp = F<int>(a, kHp);
    // the session setting cell.regen (live, the host's): off = no health from a partner nearby, as in a solo game
    if ((!g_regen_setting || settings::on(settings::kCellRegen)) && hp > 0 && hp < 6 && !nearest_partner(12.f).empty()) {
        F<int>(a, kHp) = hp + 1;
        if (a_Rollover && F<uint32_t>(a, 0x248))
            skg::cdecl_call<void>(a_Rollover, a, hp);
    }
}

// the host forwards a bite of its NPC on our puppet: apply it to our own cell
void hurt_avatar(int dmg, int type)
{
    uint8_t* a = avatar();
    if (!a || dmg <= 0 || F<uint8_t>(a, kInvuln) || F<uint8_t>(a, kDying))
        return;
    int old = F<int>(a, kHp), hp = old - dmg;
    g_n.bites_taken++;
    g_n.damage_taken += std::min(old, dmg);
    if (hp > 0) {
        F<int>(a, kHp) = hp;
        if (a_Rollover && F<uint32_t>(a, 0x248))
            skg::cdecl_call<void>(a_Rollover, a, old);
        return;
    }
    F<int>(a, kHp) = 0;
    h_KillCell(a, avatar_idx(), F<void*>(a, kRes), false, type, 0.f); // the pack may save us
}

// The mirrors live in the cell pool: once the stage is left (the editor's Creature switch, a
// save loaded) their indices mean nothing and the pool may be freed. A player report (0.7.2.1,
// 2026-10-25): the guest faulted in cell_at on session end in the Creature stage, destroying
// mirrors the Cell stage had left behind.
static void forget_mirrors()
{
    g_mirrors.clear();
    g_host_of.clear();
    g_touched.clear();
    g_swallowed.clear();
    g_puppet_seen.clear();
    g_rounds.reset();
    g_kill_sent.reset();
    g_kill_applied.reset();
    g_partner_clouds.clear(); // BUG-0958
    g_eaten.reset();
}

// BUG-0070: the knockout ends with the session and with the stage. Before, a session ending within
// the 2-3 s after a revive or respawn left the cell invulnerable for good, one ending during a
// knockout left it translucent at 1 hp, and a knockout across a stage change moved the next Cell
// game's avatar to the old place and respawned it small.
static void reset_knockout()
{
    if (!g_fix_koreset)
        return;
    // BUG-0238: the partners keep our knockout up to its deadline now; tell them it ended (KO_UP is
    // state 0, which every version handles). On a session end the session is closed: nothing goes out.
    if (g_downed && g_fix_ko_grace)
        send_ko(KO_UP, 0, 0);
    if (uint8_t* a = in_cell() ? avatar() : nullptr) {
        if (g_downed || g_invuln_until > 0)
            F<uint8_t>(a, kInvuln) = 0;
        if (g_downed) {
            F<float>(a, kOpacity) = 1.f;
            F<int>(a, kHp) = std::max(F<int>(a, kHp), 1);
        }
    }
    g_downed = false;
    g_hold = 0;
    g_down_since = 0;
    g_invuln_until = 0;
    g_ko.clear();
}

void on_player_leave(int id)
{
    g_food_ack.erase(id);
    spend_forget(id); // BUG-1249
    mate_player_left(id); // BUG-1246
    g_food_tx.erase(id);
    pace_forget(id); // BUG-1309
    if (!g_fix_kostale)
        return;
    // its knockout ends with it: a newcomer gets the same id (BUG-0061)
    if (auto it = g_ko.find(id); it != g_ko.end()) {
        if (uint8_t* c = it->second.state == KO_OUT && in_cell() ? puppet_cell(id) : nullptr)
            F<float>(c, kOpacity) = 1.f;
        g_ko.erase(it);
    }
    g_puppet_seen.erase(id);
}


// BUG-0314: the game populates the background decoration (query G+0x4100) of the tiles around the
// camera and frees those cells only on a level change (RemoveCellsOfOldScale). A cell that jumps far
// again and again (BUG-0307's NaN heals, 100-200 m each) left them behind at every place: 1099
// background cells after 40 jumps, both roles; a guest's pool reached its mirror cap (1500 live
// cells, BUG-0295) and it created no mirror any more (coverage 1% in lab 20261030-104149). Now background
// cells farther than kBgFar from our own cell are destroyed (the game's own DestroyCell, as
// RemoveCellsOfOldScale does for cells outside the view), every 2 s, at most 300 a pass.
constexpr float kBgFar = 150.f;
static double g_bg_cull_at;
static void bg_cull(double now)
{
    if (now - g_bg_cull_at < 2.0 || scale_moving())
        return;
    g_bg_cull_at = now;
    uint8_t* g = G();
    uint8_t* av = avatar();
    if (!g || !av)
        return;
    net::Vec3 me = pos_of(av);
    if (!views::finite_pos(me))
        return;
    uint8_t* data = F<uint8_t*>(g, 0x1C);
    uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
    void* bq = F<void*>(g, 0x4100);
    if (!data || size != 0x398 || !bq)
        return;
    std::vector<uint32_t> gone;
    for (uint32_t slot = 0; slot < used && slot < 4096 && gone.size() < 300; slot++) {
        uint8_t* c = data + (size_t)slot * size;
        uint32_t idx = F<uint32_t>(c, 0);
        if ((idx & 0xFFFF) != slot || F<void*>(c, kQuery) != bq || F<uint8_t>(c, kKeepAlive))
            continue;
        net::Vec3 q = pos_of(c);
        if (views::finite_pos(q) && std::hypot(q.x - me.x, q.y - me.y) > kBgFar)
            gone.push_back(idx);
    }
    for (uint32_t idx : gone)
        destroy_local(idx);
    if (!gone.empty() && (g_n.bg_culled == 0 || (g_n.bg_culled + gone.size()) / 1000 != g_n.bg_culled / 1000))
        trace::ev("cell", "bg cull", "%u background cells over %.0f m from our cell removed (%u in all, BUG-0314)",
                  (unsigned)gone.size(), kBgFar, g_n.bg_culled + (unsigned)gone.size());
    g_n.bg_culled += (uint32_t)gone.size();
}

static bool g_dna_note_due;
// the bounding sphere Collisions 0xE7F260 tests before HandleCollisionPair (0xE6D790): the model
// data's centre and radius (GetCellModelData +0x1238 / +0x1244) times the scale +0x58, turned by
// +0x5C when +0x48 bit 2 is set; a cell without a model: +0x4C and +0x58
static float cell_sphere(uint8_t* c, float* ctr)
{
    float sc = F<float>(c, 0x58);
    auto* k = (uint32_t*)(c + kModelKey);
    if (!k[0]) {
        ctr[0] = F<float>(c, 0x4C), ctr[1] = F<float>(c, 0x50), ctr[2] = F<float>(c, 0x54);
        return sc;
    }
    uint8_t* md = a_ModelData ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
    if (!md)
        return -1.f;
    float v[3] = {F<float>(md, 0x1238), F<float>(md, 0x123C), F<float>(md, 0x1240)};
    if (F<uint8_t>(c, 0x48) & 2) {
        float* m = (float*)(c + 0x5C);
        float x = v[0], y = v[1], z = v[2];
        v[0] = m[3] * y + m[6] * z + x * m[0];
        v[1] = m[1] * x + m[4] * y + m[7] * z;
        v[2] = m[2] * x + m[5] * y + m[8] * z;
    }
    for (int i = 0; i < 3; i++)
        ctr[i] = F<float>(c, 0x4C + 4 * i) + v[i] * sc;
    return F<float>(md, 0x1244) * sc;
}

// BUG-1249 (players' video: a guest left its own cell editor with new parts and still had its old DNA, 15, until the
// host left its own editor, then 9): the Cell DNA is DNAForFood(food) - spent (S+0x2C, docs/re/cell.md#cell-editor-dna);
// the food is the party's (MSG_CELL_FOOD), the spent count the host's (wsync row cell_spent, BUG-0560). The return from
// a guest's own cell editor (its mating call, 0xE809A0) writes the guest's S+0x2C = DNAForFood(food) - remaining, and
// the host's next row put the host's count back over it within half a second: the guest's purchase was free and its
// DNA showed the old value until the host's own editor changed the host's count. On: the guest sends what its own
// editor spent (a MSG_CELL_FOOD of 0 food with the spend after the meal number; an older host ignores it); the host adds
// it to its count at once in Cell, or after its own editor's result when it is in an editor (the result is written
// over S+0x2C), and tells which spend it took in the row cell_spend_ack (wsync), in the same message as its count, so
// the guest shows the host's count plus its own spends the host has not taken yet. Off: nothing sent (0.7.15).
static bool g_fix_guest_spend = true;
COOP_FIX("cell.guest_spend", "BUG-1249", g_fix_guest_spend,
         "cell: what a guest's own cell editor spent comes off the party's DNA and stays off in the guest's game (the host's count put the old DNA back)");
static int g_spent_seen = 0;           // guest: S+0x2C in the last tick in Cell
static bool g_spent_seen_ok = false;
static bool g_own_ed_due = false;      // guest: back from its own cell editor, its spend not measured yet
static int g_own_ed_before = 0;
static int g_spend_pending = 0;        // guest: sent, not in a host count yet
static uint32_t g_spend_meal = 0;      // guest: the meal number of the last spend sent
struct Owed {
    int spent = 0;
    uint32_t meal = 0;
    uint32_t world = 0; // BUG-1507: the host's world when it came (paid only back in that world)
};
// BUG-1507 (CELL-4 of the external audit of 0.7.15.5): a guest that does not play the host's Cell world (its own Cell
// world: own_world::elsewhere, BUG-0797 / BUG-0809; or the host already in another stage: host_left) still sent its own
// editor's spend (spend_measure) and its mate's pose (mate_tick runs before the solo guard), and the host took both
// without asking where that guest is: the spend came off the host's (or the party's) DNA, or waited in g_spend_owed,
// which no stage or world change cleared, and was paid into whatever Cell game the host entered next; the mate stood as
// a ghost in the host's pool. On: a guest elsewhere sends neither (a gone mate once, so the host drops its copy), the
// host refuses both from a guest that plays another world (an older guest), and a spend owed goes when the host leaves
// the Cell stage (no Cell game and no editor) or is paid only in the world it came in. Off
// (`coop.fix cell.elsewhere_spend off`): 0.7.16.1.
bool g_fix_elsewhere_spend = true;
COOP_FIX("cell.elsewhere_spend", "BUG-1507", g_fix_elsewhere_spend,
         "cell: a guest that plays another world sends no editor spend and no mate to the host, and a spend owed stays in its world");
unsigned g_elsewhere_spend_held, g_elsewhere_spend_refused, g_owed_dropped; // tests (BUG-1507)
// guest: we do not play the host's Cell world now
bool not_host_world()
{
    return g_fix_elsewhere_spend && is_guest() && (own_world::elsewhere() || own_world::host_left());
}
static std::unordered_map<int, Owed> g_spend_owed;        // host: spends that came while it had no Cell game
static std::unordered_map<int, uint32_t> g_spend_acked;   // host: the last spend of each guest in its count
static double g_cell_entered_at = -100;
static unsigned g_spends_sent, g_spends_got, g_spends_owed_paid, g_spends_acked; // tests (coop.pool spend)

// WP-D (BUG-1291, dna_own.h): own DNA per player. The host keeps every guest's own spent count (dna.cpp's wallets,
// first sight: the host's count) and sends it in the wsync row cell_spent_own (key the player, v0 its count); its own
// S+0x2C stays its own editors' spends. A guest that reads the row shows DNAForFood(food) - its own count; an older guest
// ignores the row and plays the host's count (the shared pool, and its spends come off the host's count as in 0.7.15).
static int g_host_spent_last = 0;      // host: S+0x2C in the last tick in Cell (first sight of a guest in an editor)
static int g_own_spent_heard = -1;     // guest: our own count from the host's last row cell_spent_own (-1: none)
static double g_own_spent_at = -100;
static unsigned g_own_spent_rows = 0;  // tests

static void spend_in(int from, int spent, uint32_t meal, bool cell)
{
    if (!is_host() || !g_fix_guest_spend || spent == 0 || own_progress::active()) // BUG-1469: the guest's own DNA
        return;
    if (g_fix_elsewhere_spend && own_world::peer_elsewhere((uint8_t)from)) { // BUG-1507: its own world's spend
        g_elsewhere_spend_refused++;
        H->log(M, SK_INFO, "cell: player %d's own editor spent %d DNA in another world: not ours (BUG-1507)", from, spent);
        return;
    }
    g_spends_got++;
    if (coop_core::dna_own_wallet((uint8_t)from, net::Family::npc)) { // WP-D: its own count, the host's untouched
        uint8_t* hs = cell ? S() : nullptr;
        coop_core::dna_own_cell_spend((uint8_t)from, spent, hs ? F<int32_t>(hs, 0x2C) : g_host_spent_last);
        g_spend_acked[from] = meal; // (the next row cell_spent_own holds it)
        return;
    }
    uint8_t* s = cell && H->time_seconds() - g_cell_entered_at > 1.0 ? S() : nullptr;
    if (!s || !g_spend_owed.empty()) { // (in order: an older spend still owed goes first)
        Owed& o = g_spend_owed[from];
        o.spent += spent;
        o.meal = meal;
        o.world = modes::current_world();
        H->log(M, SK_INFO, "cell: player %d's own editor spent %d DNA: kept until this game is back in Cell (BUG-1249)", from, spent);
        return;
    }
    int before = F<int32_t>(s, 0x2C);
    F<int32_t>(s, 0x2C) = before + spent;
    g_spend_acked[from] = meal;
    H->log(M, SK_INFO, "cell: player %d's own editor spent %d DNA: spent %d -> %d (BUG-1249)", from, spent, before, before + spent);
}

static void spend_tick(bool cell, bool was_in_cell)
{
    double now = H->time_seconds();
    if (cell && !was_in_cell)
        g_cell_entered_at = now;
    if (!C.sess || !C.sess->connected())
        return;
    if (is_host()) {
        if (uint8_t* hs = cell ? S() : nullptr)
            g_host_spent_last = F<int32_t>(hs, 0x2C);
        if (g_fix_elsewhere_spend && !g_spend_owed.empty()) { // BUG-1507: owed only to the Cell stage of its world
            const uint32_t m = game::mode_id(), w = modes::current_world();
            const bool left = m != modes::kCell && m != modes::kEditor;
            for (auto it = g_spend_owed.begin(); it != g_spend_owed.end();) {
                if (left || (cell && it->second.world && w && it->second.world != w)) {
                    H->log(M, SK_INFO, "cell: player %d's own editor spend of %d DNA dropped: %s (BUG-1507)", it->first,
                           it->second.spent, left ? "the Cell stage was left" : "another world");
                    g_owed_dropped++;
                    it = g_spend_owed.erase(it);
                } else
                    ++it;
            }
        }
        uint8_t* s = cell && avatar() && now - g_cell_entered_at > 1.0 ? S() : nullptr;
        if (s && !g_spend_owed.empty()) {
            for (auto& [id, o] : g_spend_owed) {
                int before = F<int32_t>(s, 0x2C);
                F<int32_t>(s, 0x2C) = before + o.spent;
                g_spend_acked[id] = o.meal;
                g_spends_owed_paid++;
                H->log(M, SK_INFO, "cell: player %d's own editor spend of %d DNA taken now: spent %d -> %d (BUG-1249)", id,
                       o.spent, before, before + o.spent);
            }
            g_spend_owed.clear();
        }
        return;
    }
    if (!is_guest())
        return;
    uint32_t m = game::mode_id();
    if (m != modes::kCell && m != modes::kEditor) {
        g_own_ed_due = false; // another stage: nothing of the Cell counts any more
        g_spent_seen_ok = false;
        g_spend_pending = 0;
        return;
    }
    if (!cell && was_in_cell && m == modes::kEditor && g_spent_seen_ok) { // a guest's Cell editor is its own (9.4.1)
        g_own_ed_due = true;
        g_own_ed_before = g_spent_seen;
    }
    spend_measure();
    if (uint8_t* s = cell ? S() : nullptr) {
        g_spent_seen = F<int32_t>(s, 0x2C);
        g_spent_seen_ok = true;
    }
}

// guest: the own editor's spend, once its result is written (back in Cell); also from the row's apply, which may come
// first in the frame of the return
void spend_measure()
{
    uint8_t* s = g_own_ed_due && in_cell() && avatar() ? S() : nullptr;
    if (!s)
        return;
    g_own_ed_due = false;
    int d = F<int32_t>(s, 0x2C) - g_own_ed_before;
    H->log(M, SK_INFO, "cell: our own editor: spent %d -> %d (%+d)%s (BUG-1249)", g_own_ed_before, F<int32_t>(s, 0x2C), d,
           !g_fix_guest_spend ? ", not sent (cell.guest_spend off)" : d ? ", sent to the host" : "");
    if (!g_fix_guest_spend || !d || !C.sess || !C.sess->connected() || own_progress::active()) // BUG-1469: ours
        return;
    if (not_host_world()) { // BUG-1507: the spend of our own world's editor is not the host's
        g_elsewhere_spend_held++;
        H->log(M, SK_INFO, "cell: our own editor's spend of %d DNA is our own world's: not sent (BUG-1507)", d);
        return;
    }
    net::Writer w;
    w.u32(0); // no food
    w.u8(0);
    g_spend_meal = g_meals.sent(0);
    w.u32(g_spend_meal);
    w.u32((uint32_t)d);
    C.sess->send_reliable_to(0, net::MSG_CELL_FOOD, w);
    g_spend_pending += d;
    g_spends_sent++;
}

// guest: the host's count to show here (the host's own plus our spends it has not taken yet)
int spent_guest_want(int host_spent)
{
    spend_measure();
    if (is_guest() && g_own_spent_heard >= 0 && H->time_seconds() - g_own_spent_at < 5.0) // WP-D: our own count
        host_spent = g_own_spent_heard;
    return g_fix_guest_spend && is_guest() ? host_spent + g_spend_pending : host_spent;
}

// guest: the host's row cell_spent_own (from the message, before cell_spent of the same message is applied)
void own_spent_heard(int v)
{
    g_own_spent_heard = v;
    g_own_spent_at = H->time_seconds();
    g_own_spent_rows++;
}
// host: the row cell_spent_own (false: not in Cell or nobody has an own count)
bool own_spent_rows(std::vector<std::pair<int, int>>& out)
{
    uint8_t* s = in_cell() ? S() : nullptr;
    if (!s || !C.sess)
        return false;
    int host_spent = F<int32_t>(s, 0x2C);
    for (auto& p : C.sess->players())
        if (p.id != 0 && coop_core::dna_own_wallet(p.id, net::Family::npc))
            out.push_back({p.id, coop_core::dna_own_cell_spent(p.id, host_spent)});
    return !out.empty();
}

// guest: the host's row cell_spend_ack: the last spend of ours in the count of the same message
void spend_acked(uint32_t meal)
{
    if (g_spend_pending && (int32_t)(meal - g_spend_meal) >= 0) {
        H->log(M, SK_INFO, "cell: the host took our editor's spend of %d DNA (BUG-1249)", g_spend_pending);
        g_spend_pending = 0;
        g_spends_acked++;
    }
}

// host: the row cell_spend_ack (false: not in Cell; a spend still owed is not in it: the guest keeps adding it)
bool spend_acks(std::vector<std::pair<int, uint32_t>>& out)
{
    if (!in_cell() || !S())
        return false;
    for (auto& [id, meal] : g_spend_acked)
        out.push_back({id, meal});
    return true;
}
bool spend_owed() { return !g_spend_owed.empty(); }

static void spend_forget(int id)
{
    g_spend_owed.erase(id);
    g_spend_acked.erase(id);
}
static void spend_reset()
{
    g_spend_owed.clear();
    g_spend_acked.clear();
    g_own_ed_due = g_spent_seen_ok = false;
    g_spend_pending = 0;
    g_own_spent_heard = -1;
}
std::string spend_status()
{
    char b[200];
    snprintf(b, sizeof b, "spend: sent=%u got=%u owed_paid=%u acked=%u pending=%d owed=%d due=%d own_heard=%d own_rows=%u",
             g_spends_sent, g_spends_got, g_spends_owed_paid, g_spends_acked, g_spend_pending, (int)g_spend_owed.size(),
             g_own_ed_due ? 1 : 0, g_own_spent_heard, g_own_spent_rows);
    return b;
}

void tick(double now)
{
    static bool was_in_cell;
    bool cell = in_cell();
    if (!cell)
        g_food_q.clear(); // the stage was left: its food went with it
    else
        food_drain(true); // BUG-0633: no cell step for 0.5 s (paused, away): applied here, as before
    if (!cell && (was_in_cell || !g_mirrors.empty()) && g_fix_forget) {
        if (was_in_cell)
            reset_knockout(); // no avatar any more: only our state is reset
        forget_mirrors();
        partner_tiles_reset(false); // the tile pool went with the stage
        g_loot_recent.clear();
        modes::cell_forget(); // the partners' puppets were in the same pool
        mate_forget();        // BUG-1246: and the copies of the partners' mates
    }
    if (!cell && (g_solo || g_solo_owed) && !solo_keep_in_editor())
        solo_reset(); // BUG-0205: the stage was left (BUG-1433: not for the editor)
    if (cell && !was_in_cell && C.sess && C.sess->connected())
        g_dna_note_due = true; // BUG-0351: once the Cell game has its data (a load, an editor's return)
    if (cell && g_dna_note_due && S() && avatar()) {
        g_dna_note_due = false;
        dna_note("on entering Cell");
    }
    spend_tick(cell, was_in_cell); // BUG-1249
    was_in_cell = cell;
    mating_count(); // BUG-1245 (tests)
    if (cell && avatar() && game::leaving_stage()) { // BUG-0308: no pool writes while the stage is torn down
        if (modes::cell_leaving())
            return;
        g_n.leave_writes++;
    }
    solo_tick();
    if (cell) {
        camera_heal(avatar()); // BUG-0604: solo too (a session that ended keeps the lost view)
        scale_centre_note();   // BUG-1394
    }
    if (!in_cell() || !C.sess || !C.sess->connected() || !avatar())
        return;
    mate_tick(now); // BUG-1246
    bg_cull(now);
    spare_tick(); // BUG-0355
    if (is_host())
        host_tick(now);
    else if (is_guest() && !g_solo)
        guest_tick(now);
    symbiosis_tick(now);
}

// the host's reads of a guest's MSG_CELL_EAT / MSG_CELL_FOOD, shared by the in-Cell handlers and the
// acknowledgement of a meal that cannot be counted (BUG-0239), so the two cannot drift apart
struct EatMsg {
    uint32_t idx = 0;
    uint8_t kind = ACT_GONE, arg = 0;
    int food = 0;
    bool plant = false;
    bool has_meal = false;
    uint32_t meal = 0;
};
static bool read_eat(net::Reader& r, EatMsg& m)
{
    m.idx = r.u32();
    if (!r.ok())
        return false;
    m.kind = r.u8(), m.arg = r.u8();
    m.food = (int)r.u32();
    m.plant = r.u8() != 0;
    if (!r.ok()) // an older guest: the index only
        m.kind = ACT_GONE, m.arg = 0, m.food = 0, m.plant = false;
    m.meal = r.u32();
    m.has_meal = r.ok();
    return true;
}
struct FoodMsg {
    int amount = 0;
    bool plant = false, valid = false, has_meal = false;
    uint32_t meal = 0;
    bool has_spent = false; // BUG-1249: a guest's own cell editor spent DNA (older guests send no such field)
    int spent = 0;
};
static FoodMsg read_food(net::Reader& r)
{
    FoodMsg m;
    m.amount = (int)r.u32();
    m.plant = r.u8() != 0;
    m.valid = r.ok();
    m.meal = r.u32();
    m.has_meal = r.ok();
    if (m.has_meal) {
        m.spent = (int)r.u32();
        m.has_spent = r.ok() && m.spent > -10000 && m.spent < 10000;
    }
    return m;
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    // a knockout state is kept (and relayed) in any stage: an "up again" dropped while we were in an
    // editor or loading left the partner knocked out here (BUG-0061)
    bool cell = in_cell() && avatar() && !g_test_away;
    if (cell && game::leaving_stage()) { // BUG-0308: the stage is being torn down
        if (modes::cell_leaving()) {
            g_n.leave_dropped++;
            cell = false;
        } else if (type == net::MSG_CELL_STATE)
            g_n.leave_writes++;
    }
    // BUG-0239: a guest's meal that reaches us in an editor (or without an avatar, around a rebuild)
    // is acknowledged without its points, as a meal eaten first: the guest takes it back at our next
    // total instead of keeping it "on the way" until a later meal drops it all at once
    if (!cell && is_host() && (type == net::MSG_CELL_EAT || type == net::MSG_CELL_FOOD)) {
        EatMsg e;
        FoodMsg f;
        bool got = type == net::MSG_CELL_EAT ? read_eat(r, e) && e.has_meal : (f = read_food(r)).has_meal;
        if (got) // no game call here: outside Cell the pool can be gone, without an avatar AddFoodPoints is unsafe
            ack_meal(from, type == net::MSG_CELL_EAT ? e.meal : f.meal); // (BUG-1431: after food still queued)
        if (type == net::MSG_CELL_FOOD && f.has_spent)
            spend_in(from, f.spent, f.meal, false); // BUG-1249: kept until our own editor's result is written

        trace::ev("eat", "refused away", "from %u: meal %u refused (this game is not in the Cell stage / has no cell)",
                  from, type == net::MSG_CELL_EAT ? e.meal : f.meal);
        return;
    }
    if (!cell && !(type == net::MSG_CELL_KO && g_fix_kostale))
        return;
    if (type == net::MSG_CELL_HIT && is_guest()) {
        uint32_t idx = r.u32();
        int dmg = r.u8(), t = r.u8();
        if (r.ok() && idx == 0xFFFFFFFFu) {
            g_n.hurt_fwd++;
            hurt_avatar(dmg, t);
        }
        return;
    }
    if (type == net::MSG_CELL_KO) {
        if (g_kodrop_until > 0 && H->time_seconds() < g_kodrop_until)
            return; // tests: a refresh that comes late (coop.pool kodrop)
        uint8_t id = r.u8(), state = r.u8(), left = r.u8(), hold = r.u8();
        if (!r.ok() || (is_host() && id != from))
            return; // a guest tells only about its own cell
        if (is_host() && from != 0) { // relay to the other guests
            net::Writer w;
            w.u8(id); w.u8(state); w.u8(left); w.u8(hold);
            C.sess->broadcast_reliable(net::MSG_CELL_KO, w, from);
        }
        if (id != C.sess->my_id())
            on_ko(id, state, left / 10.f, hold / 255.f);
        return;
    }
    if (!cell)
        return;
    if (type == net::MSG_CELL_MATE) { // BUG-1246: a guest's mate
        mate_in(from, r);
        return;
    }
    if (g_solo && is_guest() && (type == net::MSG_CELL_STATE || type == net::MSG_CELL_FOOD))
        return; // BUG-0205: our own pool until the host's away state ends (a state of the host's pool comes then)
    if (g_solo_owed && is_guest() && (type == net::MSG_CELL_STATE || type == net::MSG_CELL_FOOD))
        solo_send(); // before the host's first total is applied: it then counts them as on the way (no dip)
    if (type == net::MSG_CELL_STATE && is_guest()) {
        if (g_statehold_until > 0 && H->time_seconds() < g_statehold_until) {
            g_n.statehold_dropped++;
            return; // tests: the host's state lost on the way (coop.pool statehold)
        }
        guest_state(r);
    } else if (type == net::MSG_CELL_HIT && is_host()) {
        uint32_t idx = r.u32();
        int dmg = r.u8(), t = r.u8();
        uint8_t* c = modes::cell_at(idx);
        // a bite lands on an NPC of our pool: never on a player's cell (ours or a puppet); BUG-1008: nor on a dead one
        bool dead = c && g_fix_chunk_killer && F<uint8_t>(c, kDead);
        bool copy = mate_copy_refuse(c); // BUG-1432: the copy of a partner's mate is its owner's game's
        bool brk = c && !copy && breaking(c); // BUG-1511: the host's bite is breaking it
        if (!r.ok() || !c || F<uint8_t>(c, kDying) || dead || c == avatar() || modes::cell_is_puppet(c) || copy || brk) {
            if (brk)
                g_breaking_refused++;
            trace::ev("bite", "refused", "from %u c%u refused: %s", from, idx,
                      !c ? "no such cell here" : c == avatar() || modes::cell_is_puppet(c) ? "a player's cell" : copy ? "a partner's mate"
                      : dead ? "dead" : brk ? "breaking" : "dying");
            return;
        }
        if (guest_knocked_out(from)) { // BUG-0509: a knocked-out guest bites nothing (an old guest, or one in flight)
            g_n.ko_hits_refused++;
            trace::ev("bite", "refused ko", "from %u c%u refused: that player is knocked out", from, idx);
            return;
        }
        g_n.hits_in++;
        uint8_t* pc = puppet_cell(from);
        if (t != 6 && guest_attack(from, c, pc, dmg, t, false)) // BUG-0714: the game's Bite / DamageCell
            return;
        // BUG-0078: judged as DamageCell judges the host's own bite (0xE7A7C0): nothing happens when
        // the victim is not damageable (dead, hp 0), ShouldNotAttack(victim, attacker) is true (same
        // species, friend group, the species' "won't attack the player" flags: through the hook, so
        // the guest's puppet counts as the player) or the victim has +0x17C / +0x111 set. A mouth's
        // chunk (type 6) is EatChunk's, which asks none of these. Old (`fix guestbite 0`): the
        // arguments swapped, no gate.
        // BUG-0240: and IsDamageable's resource part (GOG 2024 0xE52940): the cell type is 2 (solid) and
        // the AI type is not 0 (meat; the victim is never the avatar here). Unreadable data: not spared.
        int ct = -1, ait = -1;
        bool not_damageable = res_types(c, &ct, &ait) && (ct != 2 || ait == 0);
        if (g_fix_guestbite && t != 6 &&
            (F<uint8_t>(c, kDead) == 1 || F<int>(c, kHp) == 0 || F<uint8_t>(c, 0x17C) || F<uint8_t>(c, kInvuln) ||
             not_damageable || (pc && o_NoAttack && h_NoAttack(c, pc)))) {
            g_n.hits_spared++;
            trace::ev("bite", "spared", "from %u c%u (%s) dmg=%d spared by the game's rules (dead/hp0/invulnerable/"
                      "not damageable/no-attack)", from, idx, kind_of(c), dmg);
            return;
        }
        // the NPC and its kind turn on the guest who bit it, as DamageCell does for the host's bites
        // (species alert 0xE71CE0, then the victim's own reaction: only AI type 0x1006 reacts,
        // through 0xE57710 -> 0xE57610), so biting is as risky for the guest as for the host
        if (pc && a_Alert && o_NoAttack && (g_fix_guestbite ? !h_NoAttack(c, pc) : !o_NoAttack(pc, c))) {
            uint32_t pidx = F<uint32_t>(pc, 0);
            skg::cdecl_call<void>(a_Alert, c, pidx);
            if (a_React && ai_type(c) == 0x1006)
                skg::cdecl_call<void>(a_React, c, pidx);
            g_n.hits_aggro++;
        }
        int hp = F<int>(c, kHp) - dmg;
        trace::ev("bite", "applied", "from %u c%u (%s) dmg=%d hp %d -> %d%s", from, idx, kind_of(c), dmg, F<int>(c, kHp),
                  std::max(hp, 0), hp > 0 ? "" : " killed");
        if (hp > 0) {
            F<int>(c, kHp) = hp;
        } else {
            F<int>(c, kHp) = 0;
            if (g_fix_chunk_killer) { // BUG-1008: the guest's puppet killed it (as MSG_CELL_EAT's kill does)
                uint8_t* av = avatar();
                h_KillCell(c, pc ? F<uint32_t>(pc, 0) : avatar_idx(), pc ? F<void*>(pc, kRes) : av ? F<void*>(av, kRes) : nullptr, true, t, 0.f);
                g_n.chunk_kills++;
            } else
                h_KillCell(c, avatar_idx(), F<void*>(avatar(), kRes), true, t, 0.f); // counts as a player kill
        }
    } else if (type == net::MSG_CELL_BITTEN && is_host()) {
        // our NPC bit a guest on the guest's machine: what DamageCell does for the attacker after a
        // bite on our own cell (0xE57930: AI type 0x1006 rests, its chase ends) - otherwise it
        // hunted the guest's puppet without a pause (docs/re/cell.md 9)
        uint8_t* c = modes::cell_at(r.u32());
        if (!r.ok() || !c || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
            return;
        g_n.bitten_in++;
        if (a_Rest && ai_type(c) == 0x1006) {
            void* holder = nullptr;
            uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, F<void*>(c, kRes), &holder);
            if (holder)
                --F<int>(holder, 0xC);
            if (res)
                skg::cdecl_call<void>(a_Rest, c, res);
        }
    } else if (type == net::MSG_CELL_EAT && is_host()) {
        EatMsg e;
        if (!read_eat(r, e))
            return;
        uint32_t idx = e.idx;
        uint8_t kind = e.kind, arg = e.arg;
        int food = e.food;
        bool plant = e.plant;
        if (e.has_meal) // applied (or refused, eaten first) now: its total goes back with this number
            ack_meal(from, e.meal); // (BUG-1431: once its food below is applied in the step)
        if (food <= -1000 || food >= 1000)
            food = 0;
        uint8_t* c = modes::cell_at(idx);
        if (c && (modes::cell_is_puppet(c) || c == avatar() || mate_copy_refuse(c))) // (BUG-1432: nor a partner's mate)
            c = nullptr;
        uint8_t* pc = puppet_cell(from);
        const char* what = c ? kind_of(c) : "-";
        bool alive = c && !F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead);
        if (kind == ACT_SHRINK) { // BUG-1514: the guest's bite shrank the plant one level in its game
            if (c && alive) {
                int ct = -1, ait = -1;
                bool is_plant = g_fix_plant_shrink && res_types(c, &ct, &ait) && ct == 3;
                if (is_plant)
                    shrink_plant(c, arg);
                trace::ev("eat", "from shrink", "from %u shrink c%u (%s) to level %d food=%d: %s", from, idx, what, (int)arg,
                          food, is_plant ? "applied" : !g_fix_plant_shrink ? "cell.plant_shrink off" : "not a plant");
                if (food && !own_progress::active()) // (the bite's points, as ACT_GONE's)
                    food_in_step([food, plant = e.plant] { o_AddFood(food, plant); });
            } else if (food) { // eaten first: no points for it (our next total takes them back)
                g_n.eats_late++;
                g_food_sent = -1;
            }
            return;
        }
        if (alive && kind != ACT_GONE && breaking(c)) { // BUG-1511: a kill of a cell the host's bite is breaking
            alive = false;
            g_breaking_refused++;
        }
        // BUG-0632: a second lethal bite of the same guest kill (or of two guests) is not a second death
        // (BUG-0821: only a kill applied here is recorded, below; off: recorded at once)
        bool again = kind != ACT_GONE && alive &&
                     (g_fix_kill_refused ? g_fix_kill_once && g_kill_applied.has(idx, H->time_seconds())
                                         : !g_kill_applied.first(idx, H->time_seconds(), g_fix_kill_once));
        if (again)
            g_n.kills_dup_refused++;
        // BUG-0714 / BUG-0509: a kill of a knocked-out guest is refused below
        bool ko = kind != ACT_GONE && alive && !again && g_fix_guest_hit_game && guest_knocked_out(from);
        if (kind == ACT_GONE)
            trace::ev("eat", c ? "from applied" : food ? "from late" : "from gone", "from %u gone c%u (%s) food=%d %s meal=%u: %s",
                      from, idx, what, food, plant ? "plant" : "meat", e.meal,
                      c ? "applied" : food ? "refused (eaten first / not here): points taken back" : "not here, no points");
        else
            trace::ev("eat", ko && g_fix_ko_trace ? "from kill ko" : "from kill", "from %u kill c%u (%s): %s", from, idx, what,
                      cell_logic::kill_word(again, alive, ko && g_fix_ko_trace));
        if (kind == ACT_GONE) {
            if (c) {
                g_n.eats_in++;
                o_Destroy(idx, arg & 1, 0.f, 0); // with the loot the game asked for on the guest
                if (food && !own_progress::active()) // BUG-1469: the guest's meal is its own food
                    food_in_step([food, plant] { o_AddFood(food, plant); });
            } else if (food) {
                // somebody ate it first: no points for it. The guest already counted them: our total
                // goes out again and takes them back (MSG_CELL_FOOD -> its AddFoodPoints(diff))
                g_n.eats_late++;
                g_food_sent = -1;
            }
        } else if (ko) {
            g_n.ko_hits_refused++; // BUG-0714: a lethal bite or kill of a knocked-out guest, as MSG_CELL_HIT (BUG-0509)
            trace::ev("bite", "refused ko", "from %u kill c%u refused: that player is knocked out", from, idx);
        } else if (alive && !again) {
            // the guest's bite killed it: the kill happens here, with the death and the loot
            g_n.kills_in++;
            if (kind == ACT_BREAK && guest_attack(from, c, pc, F<int>(c, kHp), 4, true)) {
                g_n.hits_in++; // BUG-0714: the game's Bite decides with the host's hp (BreakOrSwallow, or its gates)
            } else if (kind == ACT_BREAK) { // the lethal bite: also a hit that arrived (hits_in)
                g_n.hits_in++;
                F<int>(c, kHp) = 0;
                h_Break(c, pc ? pc : avatar(), 1.f / 30.f);
            } else {
                F<int>(c, kHp) = 0;
                h_KillCell(c, pc ? F<uint32_t>(pc, 0) : avatar_idx(), pc ? F<void*>(pc, kRes) : nullptr, true, arg, 0.f);
            }
            // BUG-0821: recorded once the game killed it (a spared bite leaves it alive: the next kill is a kill)
            uint8_t* c2 = modes::cell_at(idx);
            int st2 = c2 ? F<int>(c2, 0x18C) : 0; // 7 / 8 / 9: the game's break / swallow states (h_Break)
            if (g_fix_kill_refused && (!c2 || F<uint8_t>(c2, kDying) || F<uint8_t>(c2, kDead) || F<int>(c2, kHp) <= 0 ||
                                       st2 == 7 || st2 == 8 || st2 == 9))
                g_kill_applied.first(idx, H->time_seconds(), g_fix_kill_once);
        }
    } else if (type == net::MSG_CELL_FOOD) {
        if (is_host()) { // a guest ate something: add it to the shared progress
            FoodMsg f = read_food(r);
            if (f.has_spent)
                spend_in(from, f.spent, f.meal, true); // BUG-1249
            if (f.valid && f.amount > -1000 && f.amount < 1000 && !(f.has_spent && f.amount == 0) &&
                !own_progress::active()) // BUG-1469: a guest's meal is its own food (acknowledged, not added here)
                food_in_step([from, f] {
                    uint8_t* s0 = S();
                    int before = s0 ? F<int32_t>(s0, 0x1C) : 0;
                    o_AddFood(f.amount, f.plant);
                    if (C.food_cause && s0 && f.amount != 0)
                        C.food_cause(from, before, f.amount);
                });
            trace::ev("eat", "from food", "from %u food +%d %s meal=%u: %s", from, f.amount, f.plant ? "plant" : "meat", f.meal,
                      f.valid && f.amount > -1000 && f.amount < 1000 ? "applied" : "refused (bad message)");
            if (f.has_meal)
                ack_meal(from, f.meal); // BUG-1431: after the food queued above
        } else if (is_guest()) {
            int food = (int)r.u32(), plant = (int)r.u32();
            uint8_t* s = S();
            if (!r.ok() || !s)
                return;
            uint32_t ack = r.u32(); // a host before 0.8 does not send it
            bool has_ack = r.ok();
            if (C.pace_heard) // BUG-0510: the host's pace divisor (an older host sends none: the full sum)
                C.pace_heard(r.ok() && r.left() >= 1 ? r.u8() : 1);
            if (own_progress::active()) { // BUG-1469: our food is our own: the host's total only acknowledges our meals
                g_meals.diff(food, has_ack, ack, 0, 0, g_fix_foodseq);
                return;
            }
            // the difference is taken when it is applied (in the step): two totals before a step apply once
            food_in_step([food, plant, ack, has_ack] {
                uint8_t* s = S();
                if (!s)
                    return;
                int diff = g_meals.diff(food, has_ack, ack, g_pend_food, F<int32_t>(s, 0x1C), g_fix_foodseq);
                if (diff != 0) {
                    trace::ev("eat", "total", "host total food=%d (ack meal %u) -> %+d here (pending %d)", food, ack, diff,
                              g_pend_food);
                    int before = F<int32_t>(s, 0x1C);
                    {
                        callguard::Hold food_guard(g_food_guard);
                        o_AddFood(diff, false); // through the game: level ups happen as usual
                    }
                    if (C.food_cause)
                        C.food_cause(0xFF, before, diff);
                }
                F<int32_t>(s, 0x20) = plant;
            });
        }
    }
}

void on_session_end()
{
    g_kodrop_until = 0;
    g_statehold_until = 0;
    g_test_away = false;
    if (g_fix_koreset)
        reset_knockout();
    g_ko.clear();
    if (g_downed) {
        g_downed = false;
        if (uint8_t* a = in_cell() ? avatar() : nullptr)
            F<uint8_t>(a, kInvuln) = 0;
    }
    g_meals.reset();
    g_food_ack.clear();
    g_food_tx.clear();
    pace_reset(); // BUG-1309
    spend_reset(); // BUG-1249
    mate_session_end(); // BUG-1246
    if (in_cell() || !g_fix_forget) // outside the Cell stage the indices point into a pool that is gone
        for (auto& [h, m] : g_mirrors)
            if (m.local)
                destroy_local(m.local);
    forget_mirrors();
    g_food_sent = g_plant_sent = -1;
    g_pend_food = 0;
    solo_reset();
}
// C2 (BUG-1141): end_session, when the Cell modules end too (`cell`: not a voluntary leave without its cleanup)
LIFECYCLE_HOOK("cell.session", lifecycle::kSessionEnd, -48, [](const lifecycle::Ctx& c) { if (c.flags & lifecycle::kCellToo) on_session_end(); }, true);

// BUG-0205: the guest's own pool while the host is away (`coop.pool`, a line of its own, no key the lines
// above have: tests read the first `food=`)
std::string away_status()
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        char h[80];
        snprintf(h, sizeof h, "cell_send: held_not_here=%u\n", g_n.send_held); // BUG-0809
        return h;
    }
    if (!C.sess || C.sess->role() != net::Role::Client)
        return "";
    char e[200];
    snprintf(e, sizeof e, "cell_away: solo=%d send_due=%d solo_times=%u released=%u solo_spawns=%u kept_meat=%d kept_plant=%d "
             "kept_sent=%u kept_lost=%u\n",
             (int)g_solo, (int)g_solo_owed, g_n.solo_times, g_n.solo_released, g_n.solo_spawns, g_solo_food[0], g_solo_food[1],
             g_n.solo_sent, g_n.solo_dropped);
    return e;
}

// ---- overlay: knockout countdown, revive progress, arrows to knocked out partners ----
static std::string bar(float f)
{
    int n = (int)(std::max(0.f, std::min(f, 1.f)) * 8.f + 0.5f);
    std::string s;
    for (int i = 0; i < 8; i++)
        s += i < n ? "\u25A0" : "\u25A1"; // filled / empty square
    return s;
}

static int text_w(const std::string& s) { return utf8::overlay_width(s); } // cells, not bytes (BUG-0159)

static void draw_ko(uint8_t* c, const std::string& name, float left, float hold, double now, int sw, int sh)
{
    if (!c)
        return;
    net::Vec3 p = pos_of(c);
    float size = std::max(F<float>(c, kSize), 0.3f);
    float w[3] = {p.x, p.y + size * 2.2f + 1.f, p.z}, sc[2];
    bool blink = std::fmod(now, 0.8) < 0.5;
    char t[64];
    snprintf(t, sizeof t, "KO %d", (int)std::ceil(std::max(0.f, left)));
    uint32_t red = blink ? 0xFFFF4040 : 0xFFB02020;
    bool on = H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh;
    if (on) {
        H->overlay_text((int)sc[0] - text_w(t) / 2, (int)sc[1] - 34, red, t);
        if (hold > 0.01f) {
            std::string b = bar(hold);
            H->overlay_text((int)sc[0] - 40, (int)sc[1] + 18, 0xFF60FF60, b.c_str());
        }
        return;
    }
    if (name.empty() || sw <= 0)
        return;
    // off screen: an arrow on the screen edge towards it
    float cx = sw / 2.f, cy = sh / 2.f;
    if (!H->world_to_screen(w, sc)) // behind: never happens in the flat cell world, keep a fallback
        sc[0] = cx, sc[1] = sh + 100.f;
    float dx = sc[0] - cx, dy = sc[1] - cy;
    float fx, fy;
    if (!cell_logic::edge_point(cx, cy, dx, dy, 70, 50, 1, 1, fx, fy)) // a NaN position (BUG-0160)
        return;
    int x = (int)fx, y = (int)fy;
    const char* arrow = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? "\u25BA" : "\u25C4") : (dy > 0 ? "\u25BC" : "\u25B2");
    std::string s = std::string(arrow) + " " + name + " " + t;
    H->overlay_text(x - text_w(s) / 2, y - 10, red, s.c_str());
}

void draw_overlay()
{
    if (!in_cell() || !C.sess || !C.sess->connected())
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    double now = H->time_seconds();
    if (g_downed)
        draw_ko(avatar(), "", (float)std::max(0.0, kDownTime - (now - g_down_since)), (float)(g_hold / kReviveHold), now,
                sw, sh);
    if (!C.puppets)
        return;
    for (auto& p : C.puppets()) {
        auto it = g_ko.find(p.id);
        if (it == g_ko.end() || it->second.state != KO_OUT)
            continue;
        draw_ko((uint8_t*)p.cell, p.name, it->second.left, it->second.hold, now, sw, sh);
    }
}


// the support trace (trace.h): what the own cell has around it and what it did since the last line.
// Food = plants and meat (kind_of), "screen" = what world_to_screen puts inside the window.
uint32_t own_model()
{
    uint8_t* av = in_cell() ? avatar() : nullptr;
    return av ? F<uint32_t>(av, kModelKey) : 0;
}

std::string trace_line()
{
    uint8_t* av = in_cell() ? avatar() : nullptr;
    if (!av)
        return "";
    net::Vec3 me = pos_of(av);
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    int plants = 0, meat = 0, creatures = 0, all = 0;
    float near_plant = -1, near_meat = -1;
    for_each_cell([&](uint8_t* c, uint32_t) {
        if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
            return;
        all++;
        const char* k = kind_of(c);
        bool plant = !strcmp(k, "plant"), m = !strcmp(k, "meat"), cr = !strcmp(k, "creature");
        if (!plant && !m && !cr)
            return;
        net::Vec3 p = pos_of(c);
        float d = net::dist(p, me);
        if (plant && (near_plant < 0 || d < near_plant))
            near_plant = d;
        if (m && (near_meat < 0 || d < near_meat))
            near_meat = d;
        float w[3] = {p.x, p.y, p.z}, sc[2];
        if (!H->world_to_screen(w, sc) || sc[0] < 0 || sc[1] < 0 || sc[0] >= sw || sc[1] >= sh)
            return;
        (plant ? plants : m ? meat : creatures)++;
    });
    auto* k = (uint32_t*)(av + kModelKey);
    uint8_t* md = a_ModelData && k[0] ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
    uint8_t* s = S();
    // what happened since the last line (counters of status())
    static uint32_t l_eats, l_food, l_sent, l_in, l_late, l_hits_sent, l_hits_in, l_spared, l_kills_sent, l_kills_in;
    char b[600];
    snprintf(b, sizeof b,
             "size=%.2f hp=%d model=%08X diet=%s food=%d plant=%d screen: plants=%d meat=%d creatures=%d "
             "nearest plant=%.0f meat=%.0f pool=%d mirrors=%u downed=%d body=%.2f | since last: ate=%u (+%u pts) "
             "eats_sent=%u eats_in=%u late=%u bites_sent=%u bites_in=%u spared=%u kills_sent=%u kills_in=%u",
             F<float>(av, kSize), F<int>(av, kHp), k[0],
             !md ? "?" : md[0x1250] && md[0x1251] ? "omni" : md[0x1250] ? "carn" : md[0x1251] ? "herb" : "none", s ? F<int32_t>(s, 0x1C) : -1, s ? F<int32_t>(s, 0x20) : -1,
             plants, meat, creatures, near_plant, near_meat, all, (unsigned)g_mirrors.size(), (int)g_downed, g_body,
             g_n.own_eats - l_eats, g_n.own_food - l_food, g_n.eats_sent - l_sent, g_n.eats_in - l_in, g_n.eats_late - l_late,
             g_n.hits_sent - l_hits_sent, g_n.hits_in - l_hits_in, g_n.hits_spared - l_spared, g_n.kills_sent - l_kills_sent,
             g_n.kills_in - l_kills_in);
    l_eats = g_n.own_eats, l_food = g_n.own_food, l_sent = g_n.eats_sent, l_in = g_n.eats_in, l_late = g_n.eats_late;
    l_hits_sent = g_n.hits_sent, l_hits_in = g_n.hits_in, l_spared = g_n.hits_spared, l_kills_sent = g_n.kills_sent;
    l_kills_in = g_n.kills_in;
    return b;
}

std::string status()
{
    MEMORYSTATUSEX vm{};
    vm.dwLength = sizeof vm;
    GlobalMemoryStatusEx(&vm);
    char d[320];
    snprintf(d, sizeof d, "cell_contacts: creature_pairs=%u bite_events=%u nonfinite_skipped=%u nonfinite_dropped=%u stale_targets=%u own_nf=%u contacts_bad=%u contacts_bad_own=%u contacts_unit=%u contact_solves=%u shared_parts=%u vm_free_mb=%llu created=%u destroyed=%u leave_writes=%u leave_dropped=%u unknown_models=%u\n", g_n.creature_pairs, g_n.bite_events, g_n.nonfinite_sent_skipped, g_n.nonfinite_dropped, g_n.stale_targets, g_n.own_nf_events, g_n.contacts_bad, g_n.contacts_bad_own, g_n.contacts_unit, g_n.contact_solves, g_shared_parts, vm.ullAvailVirtual >> 20, g_n.created, g_n.destroyed, g_n.leave_writes, g_n.leave_dropped, modes::cell_unknown_models());
    char b[1400];
    uint8_t* s = S();
    int food = s ? F<int32_t>(s, 0x1C) : -1;
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "cell(host): food=%d ko_deaths=%u own_bites=%u own_hits=%u own_chunks=%u hits_in=%u hits_aggro=%u hits_spared=%u bitten_in=%u puppet_hits_dropped=%u eats_in=%u revives=%u respawns=%u pairs_skipped=%u "
                 "puppet_saves=%u puppet_new=%u npc_deaths=%u loot_calls=%u loot_cells=%u kills_in=%u eats_late=%u guest_attacks=%u guest_attacks_spared=%u guest_attack_kills=%u guest_attack_types=%u/%u/%u/%u/%u/%u\n", food, g_n.ko_deaths, g_n.own_bites, g_n.own_hits, g_n.own_chunks, g_n.hits_in, g_n.hits_aggro, g_n.hits_spared, g_n.bitten_in,
                 g_n.puppet_hits_dropped, g_n.eats_in, g_n.revives, g_n.respawns, g_n.pairs_skipped, g_n.puppet_saves, g_n.puppet_new, g_n.npc_deaths, g_n.loot_calls, g_n.loot_cells, g_n.kills_in, g_n.eats_late, g_n.guest_attacks, g_n.guest_attacks_spared, g_n.guest_attack_kills, g_guest_attack_types[0], g_guest_attack_types[1], g_guest_attack_types[2], g_guest_attack_types[3], g_guest_attack_types[4], g_guest_attack_types[5]);
    else
        snprintf(b, sizeof b,
                 "cell(guest): food=%d ko_deaths=%u mirrors=%u created=%u destroyed=%u purged=%u hits_sent=%u eats_sent=%u "
                 "revives=%u respawns=%u pairs_skipped=%u hurt_local=%u hurt_fwd=%u bitten_sent=%u soft_moves=%u soft_turns=%u chunks_sent=%u chunks_taken=%u breaks_sent=%u wipes=%u kept=%u puppet_saves=%u puppet_new=%u "
                 "npc_deaths=%u loot_calls=%u loot_cells=%u mirror_kills=%u kills_sent=%u death_replays=%u loot_skipped=%u "
                 "host_of=%u late_chunks=%u rounds_partial=%u meals_unacked=%u zaps=%u zap_hits=%u product_hits=%u product_pairs=%u products=%u foreign_hits=%u/%u/%u\n",
                 food, g_n.ko_deaths, (unsigned)g_mirrors.size(), g_n.created, g_n.destroyed, g_n.purged, g_n.hits_sent, g_n.eats_sent,
                 g_n.revives, g_n.respawns, g_n.pairs_skipped, g_n.hurt_local, g_n.hurt_fwd, g_n.bitten_sent, g_n.soft_moves, g_n.soft_turns, g_n.chunks_sent, g_n.chunks_taken, g_n.breaks_sent, g_n.wipes, g_n.kept, g_n.puppet_saves, g_n.puppet_new,
                 g_n.npc_deaths, g_n.loot_calls, g_n.loot_cells, g_n.mirror_kills, g_n.kills_sent, g_n.death_replays, g_n.loot_skipped,
                 (unsigned)g_host_of.size(), g_n.late_chunks, g_n.rounds_partial, (unsigned)g_meals.unacked.size(), g_n.zaps, g_n.zap_hits,
                 g_n.product_hits, g_n.product_pairs, g_n.products_live, g_n.foreign_mirror, g_n.foreign_puppet, g_n.foreign_other);
    char t[768];
    size_t pcount = 0;
    for (auto& [id, pt] : g_ptiles)
        if (pt.list)
            pcount += (size_t)((pt.list->end - pt.list->begin) / 2);
    snprintf(t, sizeof t,
             "cell_tiles: centres=%u partner_lists=%u partner_tiles=%u populated=%u shared=%u bg_skipped=%u passes=%u kept=%u resets=%u drained=%u pool_full=%u flushes=%u unshared=%u handed=%u tile_pool=%u/%u starved=%u/%u fed=%u screen=%u send_capped=%u mirror_capped=%u cells=%u peak=%u level=%d nan_pop_skips=%u nan_heals=%u tilecap_skips=%u tilecap_max=%.0f\n",
             (unsigned)g_views.size(), (unsigned)g_ptiles.size(), (unsigned)pcount, g_n.tiles_partner,
             g_n.tiles_shared, g_n.tiles_bg, g_n.tile_passes, g_n.tiles_kept, g_n.tile_resets, g_n.tiles_drained, g_n.tiles_full, g_n.tiles_flush, g_n.tiles_unshared, g_n.tiles_handed,
             G() ? F<uint32_t>(G(), 0x38 + 0xC) : 0, G() ? F<uint32_t>(G(), 0x38 + 0x18) : 0, g_n.tiles_starved,
             (unsigned)g_starved.size(), g_n.tiles_fed, g_n.tiles_core, g_n.send_capped, g_n.mirror_capped, cells_live(), g_n.cells_peak, g_tile_level, g_n.nan_pop_skips, g_n.nan_heals, g_n.tilecap_skips, g_tilecap_max);
    char j[96];
    snprintf(j, sizeof j, "cell_guest_fx: joins_after_load=%u bite_rings=%u\n", g_n.joins_load, g_n.bite_rings); // BUG-0352, BUG-0350
    char o[300]; // BUG-0632 / BUG-0636 (class BUG-0420), BUG-0635
    snprintf(o, sizeof o, "cell_once: kill_once=%d eat_once=%d kills_dup_sent=%u kills_dup_refused=%u eaten_skipped=%u gone_again=%u wipe_calls=%u wipe_kept=%u wipe_destroyed=%u wipe_near_lost=%u\n",
             (int)g_fix_kill_once, (int)g_fix_eat_once, g_n.kills_dup_sent, g_n.kills_dup_refused, g_n.eaten_skipped, g_n.gone_again,
             g_n.wipe_calls, g_n.wipe_kept, g_n.wipe_destroyed, g_n.wipe_near_lost);
    char sc[160]; // BUG-0927
    snprintf(sc, sizeof sc, "cell_send_centres: fix=%d tile_pass_behind=%.1f max=%.1f tile_passes=%u\n", (int)g_fix_send_centres,
             g_send_centre_lag, g_send_centre_lag_max, g_n.tile_passes);
    char c1[200]; // 0.7.13 C1 (BUG-0798, 0799, 0821, 0827, 0835, 0792)
    snprintf(c1, sizeof c1, "cell_c1: down_scale=%.4g ko_clouds_blocked=%u kills_resent=%u swallows_marked=%u spawn_slot=%d\n",
             g_downed ? g_down_scale : 0.f, g_n.ko_clouds_blocked, g_n.kills_resent, g_n.swallows_marked, g_spawn_slot);
    char pp[320]; // BUG-0958
    snprintf(pp, sizeof pp, "cell_partner_poison: fix=%d hurt_each_other=%d refused=%u taken=%u partner_clouds=%u last_hit=%s\n",
             (int)g_fix_partner_poison, (int)players_hurt_each_other(), g_n.partner_poison_refused, g_n.partner_poison_taken,
             (unsigned)g_partner_clouds.size(), g_own_hit_last);
    return b + std::string(d) + t + j + o + sc + c1 + pp + audit_status();
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (!in_cell())
        return v;
    auto entry = [&](uint8_t* c, uint32_t key) {
        net::Quat q;
        modes::mat_to_quat((float*)(c + kRot), q);
        v.push_back({"c" + std::to_string(key), pos_of(c), q});
    };
    if (is_host()) {
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (!modes::cell_is_puppet(c) && !is_mate_copy(idx)) // BUG-1246: a guest's mate is its own game's
                entry(c, idx);
        });
    } else {
        for (auto& [h, m] : g_mirrors)
            if (uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr)
                entry(c, h);
    }
    return v;
}

// the divergence detector (census.h `coop.census all`): host: its foreground cells (id = cell index);
// guest: its mirrors (id = the host's index) and the cells of its own (local: the guest's own
// population, purged in a session)
void census_rows(std::vector<census::Row>& out)
{
    if (!in_cell())
        return;
    auto row = [&](uint8_t* c, int id, const char* own) {
        if (out.size() >= 6000)
            return;
        census::Row r;
        r.kind = 'l';
        r.id = id;
        r.key = F<uint32_t>(c, kModelKey);
        r.alive = F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) ? 0 : 1;
        r.hp = (float)F<int>(c, kHp);
        r.ai = F<uint8_t>(c, kAiLock) ? 0 : 1;
        r.own = own;
        r.pos = pos_of(c);
        r.scale = F<float>(c, kSize);
        r.noun = F<uint32_t>(c, kLevel);
        out.push_back(r);
    };
    if (is_guest()) {
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (modes::cell_is_puppet(c) || (idx && idx == mate_idx())) // BUG-1246: our mate is ours (its copy is on the host)
                return;
            auto it = g_host_of.find(idx);
            row(c, it != g_host_of.end() ? (int)it->second : -1, it != g_host_of.end() ? "proxy" : "local");
        });
    } else {
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (!modes::cell_is_puppet(c) && !is_mate_copy(idx)) // BUG-1246
                row(c, (int)idx, "npc");
        });
    }
}

float scale_now() { return in_cell() ? world_scale() : 0.f; }
bool food_total(int& food)
{
    uint8_t* s = in_cell() ? S() : nullptr;
    if (!s)
        return false;
    food = F<int32_t>(s, 0x1C);
    return true;
}

// BUG-1469 (own progress, from PR #209): a guest's own food back after a copy of the host's world opened, or banked meals
// released (cell.level_together). Through the game's AddFoodPoints by the difference (the level changes run as for a
// meal), in the cell step, not counted as an own meal (the food guard: not paced, not clipped, not sent)
bool set_food_total(int food)
{
    if (!(in_cell() && S() && avatar()))
        return false;
    food_in_step([food] {
        uint8_t* s = S();
        if (!s)
            return;
        int diff = std::clamp(food, 0, 1000) - F<int32_t>(s, 0x1C);
        if (!diff)
            return;
        callguard::Hold food_guard(g_food_guard);
        o_AddFood(diff, false);
        H->log(M, SK_INFO, "cell: own food set to %d (%+d, own progress BUG-1469)", F<int32_t>(s, 0x1C), diff);
    });
    return true;
}

// BUG-0351 diagnostics: what the cell editor's DNA budget is made of. The game's request (0xE59800)
// passes DNAForFood(S+0x1C) - S+0x2C (spent) to the editor (docs/re/cell.md#cell-editor-dna).
void dna_note(const char* when)
{
    uint8_t* s = S();
    if (!s)
        return;
    int food = F<int32_t>(s, 0x1C), spent = F<int32_t>(s, 0x2C);
    using DnaFor_t = int(__fastcall*)(int);
    int dna = a_DnaFor && food >= 0 && food <= 1000 ? ((DnaFor_t)a_DnaFor)(food) : -1;
    H->log(M, SK_INFO, "cell: DNA %s: food=%d plant=%d spent=%d dna_for_food=%d budget=%d", when, food,
           F<int32_t>(s, 0x20), spent, dna, dna >= 0 ? dna - spent : -1);
}

bool spent_dna(int& v)
{
    uint8_t* s = in_cell() ? S() : nullptr;
    if (!s)
        return false;
    v = F<int32_t>(s, 0x2C);
    return true;
}

void set_spent_dna(int v)
{
    if (uint8_t* s = in_cell() ? S() : nullptr)
        *(int32_t*)(s + 0x2C) = v; // the game's own writer is the editor's result (0xE809A0): no setter
}

int editor_budget()
{
    uint8_t* s = in_cell() ? S() : nullptr;
    if (!s || !a_DnaFor)
        return -1;
    int food = F<int32_t>(s, 0x1C), spent = F<int32_t>(s, 0x2C);
    using DnaFor_t = int(__fastcall*)(int);
    return food >= 0 && food <= 1000 ? ((DnaFor_t)a_DnaFor)(food) - spent : -1;
}

void join_partner()
{
    uint8_t* a = in_cell() ? avatar() : nullptr;
    net::Vec3 at;
    if (a && spawn_spot(&at, nullptr, false, 2.5f, 1.f)) // BUG-0792
        stop_at(a, at);
}

} // namespace cellsync
