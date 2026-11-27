// Cell stage Rivals mode (WP-R2 of 0.8.0, BUG-1546, docs/11-rivals.md 11.3.1, 11.5, 11.6).
// Per-player evolution bar & DNA, war-gated PvP bites with physical meat drops, Together-only KO/revive,
// no NPC diplomacy or influence, stage-end catch-up timer, and Cell part draft + escalating Creature DNA payout.
#include "cellsync_internal.h"
#include "cell_rivals_logic.h"
#include "rivals.h"
#include "store.h"
#include "wire/rivals_faction.h"

namespace cellsync {

bool g_fix_cell_rivals = true;
COOP_FIX("rivals.cell", "BUG-1546", g_fix_cell_rivals,
         "cell: Rivals mode (per-player evolution bar and DNA, war-gated PvP bites with meat drops, no knockout/revive, catch-up timer and Cell part draft)");

bool g_fix_rivals_wscale_solo_safe = true;
COOP_FIX("cell.rivals_wscale_solo_safe", "BUG-1568", g_fix_rivals_wscale_solo_safe,
         "cell: skip over-scaled rival puppet collisions during Cell Rivals wscale divergence and clamp fluid grid viewbox float32 rounding overflow (0x00E4E384)");

// BUG-1603: persist each guest's Cell Rivals food & unlocked parts across disconnects/rejoins so rejoining guests do
// not lose their evolution bar progress back to the host's lower save state.
static bool g_fix_cell_rejoin_progress = true;
COOP_FIX("rivals.cell_rejoin_progress", "BUG-1603", g_fix_cell_rejoin_progress,
         "cell: remember each player's Cell Rivals food and unlocked parts across disconnects and restore on rejoin");

extern bool scale_solo_active();
extern float last_host_scale();

struct RivalPlayer {
    int32_t food = 0;
    int32_t dna = 0;
    uint16_t parts_mask = 0;
    uint16_t kills = 0;
    bool reported = false;
};

struct RivalsStats {
    uint32_t pvp_out = 0;
    uint32_t pvp_in = 0;
    uint32_t pvp_relayed = 0;
    uint32_t pvp_blocked = 0;
    uint32_t pvp_kills = 0;
    uint32_t meat_drops = 0;
    uint32_t meat_chunks = 0;
    uint32_t rollbacks_out = 0;
    uint32_t rollbacks_in = 0;
    uint32_t progress_tx = 0;
    uint32_t progress_rx = 0;
    uint32_t draft_starts = 0;
    uint32_t draft_unlocks_sent = 0;
    uint32_t draft_unlocks_applied = 0;
    uint32_t payouts_sent = 0;
    uint32_t payouts_applied = 0;
    uint32_t wscale_puppet_pairs_skipped = 0;
    uint32_t wscale_puppets_clamped = 0;
    uint32_t fluid_box_clamps = 0;
    uint32_t rejoin_restores_sent = 0;
    uint32_t rejoin_restores_applied = 0;
};

static RivalPlayer g_rp[rivals::kMaxPlayers];
static std::string g_slot_key[rivals::kMaxPlayers];
static bool g_rejoin_checked[rivals::kMaxPlayers] = {};
static cell_rivals_logic::CellProgressMemory g_saved_prog;
static RivalsStats g_rv;
static uint8_t g_overscaled_puppets_mask = 0;
static double g_last_prog_send = -100.0;
static cell_rivals_logic::ProgressMsg g_last_sent_prog{};
static bool g_sent_prog_once = false;
static double g_last_host_check = -100.0;
static bool g_host_prog_dirty[rivals::kMaxPlayers] = {};
static bool g_draft_started_for_race = false;
static bool g_race_end_handled = false;
static bool g_host_stage_end_unlocked = false;
static uint32_t g_meat_res_inst = 0;
static uint32_t g_meat_model_key[3] = {};
static uintptr_t a_CellCollect_rivals = 0;
static uint32_t g_unlock_queue = 0;
static bool g_unlock_scheduled = false;
static int32_t g_queued_creature_dna = 0;
static int32_t g_paid_creature_dna = 0;
static double g_war_pvp_last_hit[rivals::kMaxPlayers] = {};

static void rivals_reset_session()
{
    for (int i = 0; i < rivals::kMaxPlayers; i++) {
        g_rp[i] = RivalPlayer{};
        g_slot_key[i].clear();
        g_rejoin_checked[i] = false;
        g_host_prog_dirty[i] = false;
        g_war_pvp_last_hit[i] = 0.0;
    }
    g_saved_prog.clear();
    g_last_prog_send = -100.0;
    g_last_sent_prog = {};
    g_sent_prog_once = false;
    g_last_host_check = -100.0;
    g_draft_started_for_race = false;
    g_race_end_handled = false;
    g_host_stage_end_unlocked = false;
    g_meat_res_inst = 0;
    g_meat_model_key[0] = g_meat_model_key[1] = g_meat_model_key[2] = 0;
    g_unlock_queue = 0;
    g_unlock_scheduled = false;
    g_queued_creature_dna = 0;
    g_paid_creature_dna = 0;
    g_overscaled_puppets_mask = 0;
}

SESSION_STATE(
    "cell.rivals", nullptr, [] { rivals_reset_session(); },
    []() -> size_t {
        size_t n = (g_draft_started_for_race ? 1 : 0) + (g_race_end_handled ? 1 : 0) +
                   (g_host_stage_end_unlocked ? 1 : 0) + (g_unlock_queue ? 1 : 0) + (g_queued_creature_dna > 0 ? 1 : 0) +
                   g_saved_prog.by_key.size();
        for (int i = 0; i < rivals::kMaxPlayers; i++)
            if (g_rp[i].reported)
                n++;
        return n;
    });

bool rivals_active()
{
    return g_fix_cell_rivals && C.sess && C.sess->connected() && rivals::active_in_mode(modes::kCell);
}

// BUG-1568: detect over-scaled rival puppets whose physical collision with our avatar would apply massive separation
// impulses when players diverge in Cell world_scale (e.g. host surging to wscale=15000 while guest is at 0.5).
bool rivals_wscale_puppet_unsafe(uint8_t* pup)
{
    if (!g_fix_rivals_wscale_solo_safe || !rivals_active() || !in_cell() || !pup || !modes::cell_is_puppet(pup))
        return false;
    int owner = C.puppet_owner ? C.puppet_owner(pup) : -1;
    if (owner >= 0 && owner < 8 && (g_overscaled_puppets_mask & (uint8_t)(1u << owner)))
        return true;
    uint8_t* av = avatar();
    float own_sz = av ? F<float>(av, kSize) : 0.55f;
    float pup_sz = std::max(F<float>(pup, kSize), F<float>(pup, kTargetSize));
    return cell_rivals_logic::puppet_scale_collision_unsafe(true, true, scale_solo_active(), owner == 0, own_sz, pup_sz,
                                                            last_host_scale(), world_scale());
}

void rivals_clamp_overscaled_puppets()
{
    g_overscaled_puppets_mask = 0;
    if (!g_fix_rivals_wscale_solo_safe || !rivals_active() || !in_cell() || !C.puppets)
        return;
    uint8_t* av = avatar();
    if (!av)
        return;
    float own_sz = F<float>(av, kSize);
    float own_sc = world_scale();
    float host_sc = last_host_scale();
    bool solo = scale_solo_active();
    for (auto& p : C.puppets()) {
        if (p.id < 0 || p.id >= 8 || !p.cell)
            continue;
        uint8_t* pup = (uint8_t*)p.cell;
        if (!modes::cell_is_puppet(pup))
            continue;
        float raw_sz = F<float>(pup, kSize);
        float raw_tsz = F<float>(pup, kTargetSize);
        float max_sz = std::max(raw_sz, raw_tsz);
        if (!cell_rivals_logic::puppet_scale_collision_unsafe(true, true, solo, p.id == 0, own_sz, max_sz, host_sc,
                                                              own_sc))
            continue;
        g_overscaled_puppets_mask |= (uint8_t)(1u << p.id);
        float clamped_sz = cell_rivals_logic::clamp_puppet_size_for_scale(own_sz, raw_sz);
        float clamped_tsz = cell_rivals_logic::clamp_puppet_size_for_scale(own_sz, raw_tsz);
        if (clamped_sz != raw_sz || clamped_tsz != raw_tsz) {
            F<float>(pup, kSize) = clamped_sz;
            F<float>(pup, kTargetSize) = clamped_tsz;
            g_rv.wscale_puppets_clamped++;
        }
    }
}

void rivals_note_puppet_pair_skip() { g_rv.wscale_puppet_pairs_skipped++; }
void rivals_note_fluid_box_clamp() { g_rv.fluid_box_clamps++; }

static std::string player_name(uint8_t id)
{
    if (C.sess) {
        std::string n = C.sess->name_of(id);
        if (!n.empty())
            return n;
    }
    return "P" + std::to_string(id);
}

static bool read_local_progress(cell_rivals_logic::ProgressMsg& out)
{
    if (!in_cell() || !S())
        return false;
    int food = 0;
    if (!food_total(food))
        return false;
    int dna = editor_budget();
    if (dna < 0)
        dna = 0;
    int* st = part_states();
    uint16_t mask = cell_rivals_logic::mask_from_states(st);
    out.food = std::clamp(food, 0, cell_rivals_logic::kMaxFood);
    out.dna = std::clamp(dna, 0, cell_rivals_logic::kMaxDna);
    out.parts_mask = mask;
    return true;
}

// ---- Native Cell part unlock (draft copy apply) ----
static void unlock_cell_parts_task(void*)
{
    g_unlock_scheduled = false;
    uint32_t q = g_unlock_queue;
    if (!q || !in_cell())
        return;
    int* st = part_states();
    if (!st || !a_CellCollect_rivals)
        return;
    g_unlock_queue = 0;
    int unlocked = 0;
    std::string last_name;
    for (int i = 0; i < cell_rivals_logic::kCellParts; i++) {
        if ((q & (1u << i)) == 0)
            continue;
        if (st[i] == 0) {
            skg::cdecl_call<void>(a_CellCollect_rivals, i);
            unlocked++;
            g_rv.draft_unlocks_applied++;
            last_name = cell_rivals_logic::part_desc(i).name;
        }
    }
    if (unlocked > 0) {
        H->log(M, SK_INFO, "cell_rivals: unlocked %d draft Cell part(s) (mask=0x%X)", unlocked, q);
        if (C.notice) {
            std::string msg = unlocked == 1
                                  ? ("Draft copy unlocked: " + last_name)
                                  : ("Draft unlocked " + std::to_string(unlocked) + " Cell parts");
            C.notice(msg);
        }
    }
}

static void queue_local_part_unlock(int part_idx)
{
    if (part_idx < 0 || part_idx >= cell_rivals_logic::kCellParts)
        return;
    g_unlock_queue |= (1u << part_idx);
    if (!g_unlock_scheduled && in_cell() && H) {
        g_unlock_scheduled = true;
        H->run_outside_frame(unlock_cell_parts_task, nullptr);
    }
}

// ---- Physical meat drop on rival kill (host-authoritative) ----
static void cache_meat_resource()
{
    if (g_meat_res_inst != 0 || !in_cell())
        return;
    for_each_cell([&](uint8_t* c, uint32_t) {
        if (g_meat_res_inst != 0)
            return;
        if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
            return;
        if (strcmp(kind_of(c), "meat") != 0)
            return;
        void* ref = F<void*>(c, kRes);
        if (!ref)
            return;
        uint32_t inst = *(uint32_t*)ref;
        if (!inst)
            return;
        g_meat_res_inst = inst;
        memcpy(g_meat_model_key, c + kModelKey, sizeof g_meat_model_key);
    });
}

static int drop_rival_meat(const net::Vec3& pos, int victim_level)
{
    if (!is_host() || !in_cell() || !avatar() || !fg_query())
        return 0;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
        return 0;
    cache_meat_resource();
    int lvl = cell_rivals_logic::meat_drop_level(victim_level);
    int spawned = 0;
    double now = H->time_seconds();

    if (g_meat_res_inst != 0 && a_RefCreate && a_Serializer) {
        cell_rivals_logic::MeatSpot spots[cell_rivals_logic::kPvpMeatChunks];
        cell_rivals_logic::meat_drop_spots(cell_rivals_logic::kPvpMeatChunks, cell_rivals_logic::kMeatDropRadius, spots);
        for (int i = 0; i < cell_rivals_logic::kPvpMeatChunks; i++) {
            void* ref = skg::cdecl_call<void*>(a_RefCreate, g_meat_res_inst, (void*)a_Serializer);
            if (!ref)
                continue;
            net::Vec3 p{pos.x + spots[i].dx, pos.y + spots[i].dy, pos.z};
            uint32_t idx = modes::cell_create(p, ref, lvl, g_meat_model_key, nullptr);
            if (uint8_t* mc = idx ? modes::cell_at(idx) : nullptr) {
                F<float>(mc, kVel) = spots[i].vx;
                F<float>(mc, kVel + 4) = spots[i].vy;
                g_loot_recent.push_back({idx, now});
                while (g_loot_recent.size() > 64)
                    g_loot_recent.pop_front();
                g_n.loot_cells++;
                spawned++;
            }
        }
    } else if (o_SpawnLoot && a_GetData) {
        // Fallback if no live meat cell was seen in the pool yet: borrow a foreground creature's loot table
        uint8_t* donor = nullptr;
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (!donor && !modes::cell_is_puppet(c) && !F<uint8_t>(c, kDead) && strcmp(kind_of(c), "creature") == 0 &&
                F<void*>(c, kRes))
                donor = c;
        });
        if (donor) {
            void* holder = nullptr;
            uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, F<void*>(donor, kRes), &holder);
            void* loot_ref = (res && skg::readable(res + 0xD4, 4)) ? F<void*>(res, 0xD4) : nullptr;
            if (loot_ref && skg::readable(loot_ref, 0x10)) {
                float p3[3] = {pos.x, pos.y, pos.z};
                uint32_t buf[256] = {};
                int n = 0;
                o_SpawnLoot(F<void*>(donor, kRes), donor + kModelKey, fg_query(), p3,
                            std::max(0.5f, F<float>(donor, kSize)), lvl, loot_ref, 1.f / 30.f, buf, &n);
                spawned = n;
                for (int i = 0; i < n && i < 256; i++) {
                    g_loot_recent.push_back({buf[i], now});
                    g_n.loot_cells++;
                }
                while (g_loot_recent.size() > 64)
                    g_loot_recent.pop_front();
            }
            if (holder)
                --F<int>(holder, 0xC);
        }
    }

    g_rv.meat_drops++;
    g_rv.meat_chunks += (uint32_t)spawned;
    H->log(M, SK_INFO, "cell_rivals: dropped %d physical meat chunk(s) at (%.1f %.1f) lvl=%d", spawned, pos.x, pos.y, lvl);
    return spawned;
}

static void push_player_progress_host(uint8_t pid)
{
    if (!is_host() || !rivals::valid_player(pid) || !g_rp[pid].reported)
        return;
    rivals::PlayerProgress prog{};
    prog.player_id = pid;
    prog.present = true;
    prog.primary = cell_rivals_logic::bar_fraction(g_rp[pid].food);
    prog.secondary = (float)g_rp[pid].dna;
    prog.stat_a = cell_rivals_logic::popcount16(g_rp[pid].parts_mask);
    prog.stat_b = g_rp[pid].kills;
    prog.extra = (float)g_rp[pid].food;
    rivals::set_player_progress(pid, prog);
    g_host_prog_dirty[pid] = false;
}

static void on_rival_killed_host(uint8_t killer, uint8_t victim, const net::Vec3& drop_pos, int victim_level)
{
    if (!is_host() || !rivals_active())
        return;
    g_rv.pvp_kills++;
    if (rivals::valid_player(killer)) {
        g_rp[killer].kills++;
        g_host_prog_dirty[killer] = true;
        push_player_progress_host(killer);
    }
    int chunks = drop_rival_meat(drop_pos, victim_level);
    if (C.notice) {
        char b[160];
        snprintf(b, sizeof b, "%s killed %s's cell (%d meat dropped)!", player_name(killer).c_str(),
                 player_name(victim).c_str(), chunks);
        C.notice(b);
    }
}

// ---- Report lethal rival PvP kill (direct bite/spike/electric or poison cloud) for kill credit & meat drops ----
void rivals_report_pvp_kill(uint8_t killer, const net::Vec3& pos, float scale, int victim_level)
{
    if (!rivals_active() || !rivals::valid_player(killer) || !C.sess || !C.sess->connected())
        return;
    uint8_t me = C.sess->my_id();
    if (killer == me || !rivals::at_war(killer, me))
        return;
    if (is_host()) {
        on_rival_killed_host(killer, 0, pos, victim_level);
    } else if (is_guest()) {
        net::Writer w;
        cell_rivals_logic::PvpKillMsg km{
            killer, pos.x, pos.y, pos.z,
            (std::isfinite(scale) && scale > 0.f) ? scale : 1.f,
            (uint8_t)std::clamp(victim_level, 0, 16),
        };
        cell_rivals_logic::write_pvp_kill(w, km);
        rivals::send_faction_to(0, rivals::Stage::Cell, cell_rivals_logic::kSubPvpKill, w);
    }
}

// ---- Apply real PvP damage on victim's machine (vanilla death on lethal, no KO/revive) ----
static void apply_rivals_pvp_damage(uint8_t attacker, int dmg, int type)
{
    uint8_t* a = in_cell() ? avatar() : nullptr;
    if (!a || dmg <= 0 || F<uint8_t>(a, kInvuln) || F<uint8_t>(a, kDying) || F<uint8_t>(a, kDead))
        return;
    int old = F<int>(a, kHp);
    int hp = old - dmg;
    g_n.bites_taken++;
    g_n.damage_taken += std::min(old, dmg);
    if (hp > 0) {
        F<int>(a, kHp) = hp;
        if (a_Rollover && F<uint32_t>(a, 0x248))
            skg::cdecl_call<void>(a_Rollover, a, old);
        return;
    }
    // Lethal war PvP hit: drop physical meat on host and let victim die & respawn normally as in vanilla (no KO).
    rivals_report_pvp_kill(attacker, pos_of(a), world_scale(), F<int>(a, kLevel));
    F<int>(a, kHp) = 0;
    h_KillCell(a, avatar_idx(), F<void*>(a, kRes), false, type, 0.f);
}

bool rivals_send_pvp_hit(int target_id, int dmg, int type, bool bypass_cooldown)
{
    if (!rivals_active() || !in_cell() || !C.sess || !C.sess->connected())
        return false;
    uint8_t me = C.sess->my_id();
    if (target_id < 0 || target_id >= rivals::kMaxPlayers || target_id == (int)me)
        return false;
    if (!rivals::at_war(me, (uint8_t)target_id)) {
        g_rv.pvp_blocked++;
        g_n.pvp_refused++;
        return false;
    }
    double now = H->time_seconds();
    if (!bypass_cooldown && !cell_logic::pvp_hit_ready(now, g_war_pvp_last_hit[target_id]))
        return false;
    g_war_pvp_last_hit[target_id] = now;
    int d = cell_logic::pvp_damage(dmg);
    net::Writer w;
    cell_rivals_logic::write_pvp_hit(w, {me, (uint8_t)target_id, (uint8_t)d, (uint8_t)type});
    uint8_t dest = is_host() ? (uint8_t)target_id : (uint8_t)0;
    if (!rivals::send_faction_to(dest, rivals::Stage::Cell, wire::rivals_faction::SK_PVP_HIT, w))
        return false;
    g_rv.pvp_out++;
    g_n.pvp_out++;
    trace::ev("pvp", "rivals sent", "war cell hit %u -> %d dmg=%d type=%d", (unsigned)me, target_id, d, type);
    return true;
}

void rivals_on_late_food(uint8_t from, int food)
{
    if (!is_host() || !rivals_active() || from == 0 || food <= 0)
        return;
    net::Writer w;
    cell_rivals_logic::write_food_rollback(w, -food);
    if (rivals::send_faction_to(from, rivals::Stage::Cell, cell_rivals_logic::kSubFoodRollback, w))
        g_rv.rollbacks_out++;
}

// ---- Draft payout delivery (next-stage Creature DNA via stage-tagged take_pending_draft_payout_for) ----
static int32_t deliver_payout_host(uint8_t pid)
{
    if (!is_host() || !rivals::valid_player(pid))
        return 0;
    // BUG-1552: claim only Stage::Cell draft payouts so Cell Rivals never steals Creature Rivals payouts
    int32_t pay = rivals::take_pending_draft_payout_for(rivals::Stage::Cell, pid);
    if (pay <= 0)
        return 0;
    if (pid == 0) {
        g_queued_creature_dna += pay;
        H->log(M, SK_INFO, "cell_rivals: queued +%d Creature DNA draft payout for host", (int)pay);
    } else {
        net::Writer w;
        cell_rivals_logic::write_draft_apply(w, {cell_rivals_logic::kDraftOpPayout, (uint32_t)pay});
        if (rivals::send_faction_to(pid, rivals::Stage::Cell, wire::rivals_faction::SK_DRAFT_APPLY, w)) {
            g_rv.payouts_sent++;
            H->log(M, SK_INFO, "cell_rivals: sent +%d Creature DNA draft payout to player %u", (int)pay, (unsigned)pid);
        }
    }
    return pay;
}

static std::string cell_player_key(uint8_t pid)
{
    std::string k = store::key_of(pid);
    if (!k.empty())
        return k;
    if (C.sess) {
        std::string nm = C.sess->name_of(pid);
        if (!nm.empty())
            return "name:" + nm;
    }
    return "";
}

// ---- MSG_RIVALS_FACTION (81) Stage::Cell message handler ----
static void on_faction_message(uint8_t from, uint8_t subkind, net::Reader& r)
{
    if (!g_fix_cell_rivals || !C.sess || !C.sess->connected())
        return;
    using namespace cell_rivals_logic;
    if (subkind == wire::rivals_faction::SK_PROGRESS) {
        if (!is_host() || !rivals::valid_player(from) || from == 0 || !rivals_active())
            return;
        ProgressMsg pm{};
        if (!read_progress(r, pm))
            return;
        g_rv.progress_rx++;
        std::string pkey = cell_player_key(from);
        if (!pkey.empty())
            g_slot_key[from] = pkey;
        auto& rp = g_rp[from];
        // BUG-1603: on a guest's first progress report after (re)joining, restore their saved Cell Rivals food and
        // unlocked parts if the reload dropped them back to the host's lower evolution bar.
        if (g_fix_cell_rejoin_progress && !g_rejoin_checked[from] && !pkey.empty()) {
            g_rejoin_checked[from] = true;
            if (const auto* saved = g_saved_prog.find(pkey)) {
                uint16_t missing_parts = (uint16_t)(saved->parts_mask & ~pm.parts_mask);
                if (saved->food > pm.food || missing_parts != 0) {
                    int32_t target_food = std::max(saved->food, pm.food);
                    uint16_t target_parts = (uint16_t)(saved->parts_mask | pm.parts_mask);
                    rp.food = target_food;
                    rp.dna = pm.dna;
                    rp.parts_mask = target_parts;
                    rp.kills = std::max(rp.kills, saved->kills);
                    rp.reported = true;
                    g_host_prog_dirty[from] = true;
                    push_player_progress_host(from);
                    net::Writer rw;
                    write_restore_progress(rw, {target_food, target_parts});
                    if (rivals::send_faction_to(from, rivals::Stage::Cell, kSubRestoreProgress, rw)) {
                        g_rv.rejoin_restores_sent++;
                        H->log(M, SK_INFO,
                               "cell_rivals: restoring rejoined guest %u (%s) progress food %d -> %d parts=0x%04X (BUG-1603)",
                               (unsigned)from, pkey.c_str(), (int)pm.food, (int)target_food, (unsigned)target_parts);
                    }
                    if (!rivals::race_state().stage_finished && bar_is_full(rp.food) &&
                        rivals::race_state().players[from].finish_rank == 0)
                        rivals::report_stage_complete(from);
                    return;
                }
            }
        }
        bool changed = !rp.reported || rp.food != pm.food || rp.dna != pm.dna || rp.parts_mask != pm.parts_mask;
        rp.food = pm.food;
        rp.dna = pm.dna;
        rp.parts_mask = pm.parts_mask;
        rp.reported = true;
        if (g_fix_cell_rejoin_progress && !pkey.empty())
            g_saved_prog.remember(pkey, rp.food, rp.parts_mask, rp.kills);
        if (changed) {
            g_host_prog_dirty[from] = true;
            push_player_progress_host(from);
        }
        if (!rivals::race_state().stage_finished && bar_is_full(rp.food) &&
            rivals::race_state().players[from].finish_rank == 0)
            rivals::report_stage_complete(from);
        return;
    }
    if (subkind == wire::rivals_faction::SK_PVP_HIT) {
        PvpHitMsg hm{};
        if (!read_pvp_hit(r, hm))
            return;
        if (is_host()) {
            bool vic_present = hm.victim > 0 && net::is_player(*C.sess, hm.victim);
            bool war = rivals::at_war(from, hm.victim);
            auto act = route_war_pvp_hit(rivals_active(), war, from, hm.attacker, hm.victim, vic_present);
            if (act == WarHitAction::Refuse) {
                g_rv.pvp_blocked++;
                g_n.pvp_refused++;
                trace::ev("pvp", "rivals refused", "from %u -> %u dmg=%u (war=%d)", (unsigned)from, (unsigned)hm.victim,
                          (unsigned)hm.dmg, (int)war);
                return;
            }
            if (act == WarHitAction::HurtHost) {
                g_rv.pvp_in++;
                g_n.pvp_in++;
                apply_rivals_pvp_damage(from, hm.dmg, hm.type);
            } else {
                net::Writer w;
                write_pvp_hit(w, {from, hm.victim, hm.dmg, hm.type});
                if (rivals::send_faction_to(hm.victim, rivals::Stage::Cell, wire::rivals_faction::SK_PVP_HIT, w)) {
                    g_rv.pvp_relayed++;
                    g_n.pvp_relayed++;
                }
            }
            return;
        }
        if (is_guest() && from == 0 && rivals_active() && hm.victim == C.sess->my_id()) {
            if (!rivals::at_war(hm.attacker, hm.victim)) {
                g_rv.pvp_blocked++;
                g_n.pvp_refused++;
                return;
            }
            g_rv.pvp_in++;
            g_n.pvp_in++;
            apply_rivals_pvp_damage(hm.attacker, hm.dmg, hm.type);
        }
        return;
    }
    if (subkind == kSubPvpKill) {
        if (!is_host() || !rivals_active() || from == 0 || !rivals::valid_player(from))
            return;
        PvpKillMsg km{};
        if (!read_pvp_kill(r, km) || km.killer == from)
            return;
        if (!rivals::at_war(km.killer, from)) {
            g_rv.pvp_blocked++;
            return;
        }
        net::Vec3 drop_pos{km.x, km.y, km.z};
        if (uint8_t* pc = puppet_cell(from)) {
            net::Vec3 pp = pos_of(pc);
            if (std::isfinite(pp.x) && std::isfinite(pp.y) && std::isfinite(pp.z))
                drop_pos = pp;
        } else {
            float hs = world_scale();
            drop_pos.x = convert_scale_coord(km.x, km.scale, hs);
            drop_pos.y = convert_scale_coord(km.y, km.scale, hs);
            drop_pos.z = convert_scale_coord(km.z, km.scale, hs);
        }
        on_rival_killed_host(km.killer, from, drop_pos, (int)km.level);
        return;
    }
    if (subkind == wire::rivals_faction::SK_DRAFT_APPLY) {
        if (!is_guest() || from != 0)
            return;
        DraftApplyMsg dm{};
        if (!read_draft_apply(r, dm))
            return;
        if (dm.op == kDraftOpUnlockPart) {
            queue_local_part_unlock((int)dm.value);
        } else if (dm.op == kDraftOpPayout) {
            g_queued_creature_dna += (int32_t)dm.value;
            H->log(M, SK_INFO, "cell_rivals: received +%u Creature DNA draft payout (queued=%d)", (unsigned)dm.value,
                   (int)g_queued_creature_dna);
            if (C.notice)
                C.notice("Draft payout: +" + std::to_string(dm.value) + " Creature DNA at Creature stage start");
        }
        return;
    }
    if (subkind == kSubFoodRollback) {
        if (!is_guest() || from != 0 || !rivals_active() || !in_cell())
            return;
        FoodRollbackMsg fm{};
        if (!read_food_rollback(r, fm))
            return;
        g_rv.rollbacks_in++;
        int32_t delta = fm.delta;
        food_in_step([delta] {
            uint8_t* s = S();
            if (!s)
                return;
            int32_t cur = F<int32_t>(s, 0x1C);
            int32_t adj = cell_rivals_logic::clamp_food_rollback(cur, delta);
            if (adj == 0)
                return;
            {
                callguard::Hold food_guard(g_food_guard);
                o_AddFood(adj, false);
            }
            // BUG-1538 / BUG-1552: late food rollback cannot push Cell DNA below 0 (cap spent S+0x2C at DNAForFood)
            int cur_sp = F<int32_t>(s, 0x2C);
            set_spent_dna(cur_sp);
            int next_sp = F<int32_t>(s, 0x2C);
            if (next_sp != cur_sp)
                H->log(M, SK_INFO, "cell_rivals: food rollback (%+d) capped spent %d -> %d (BUG-1538)", (int)adj, cur_sp, next_sp);
            H->log(M, SK_INFO, "cell_rivals: food chunk eaten first by rival -> rolled back %+d food", (int)adj);
        });
        return;
    }
    if (subkind == kSubRestoreProgress) {
        if (!g_fix_cell_rejoin_progress || !is_guest() || from != 0 || !rivals_active() || !in_cell())
            return;
        RestoreProgressMsg rm{};
        if (!read_restore_progress(r, rm))
            return;
        ProgressMsg cur{};
        if (!read_local_progress(cur))
            return;
        bool restored = false;
        if (rm.food > cur.food) {
            set_food_total(rm.food);
            restored = true;
        }
        uint16_t missing = (uint16_t)(rm.parts_mask & ~cur.parts_mask);
        for (int idx = 0; idx < kCellParts; idx++) {
            if (missing & (uint16_t)(1u << idx)) {
                queue_local_part_unlock(idx);
                restored = true;
            }
        }
        if (restored) {
            g_rv.rejoin_restores_applied++;
            H->log(M, SK_INFO,
                   "cell_rivals: restored own Cell Rivals progress after rejoin: food %d -> %d, parts 0x%04X -> 0x%04X (BUG-1603)",
                   (int)cur.food, (int)std::max(cur.food, rm.food), (unsigned)cur.parts_mask,
                   (unsigned)(cur.parts_mask | rm.parts_mask));
            if (C.notice) {
                char nb[128];
                snprintf(nb, sizeof nb, "Cell Rivals progress restored (%d/1000)", (int)std::max(cur.food, rm.food));
                C.notice(nb);
            }
        }
        return;
    }
}

// ---- StageHooks callbacks ----
static void on_cell_race_end(uint8_t winner)
{
    if (!is_host() || !rivals_active() || g_draft_started_for_race || !rivals::valid_player(winner))
        return;
    g_race_end_handled = true;
    // Refresh host's own parts mask before building the draft list
    cell_rivals_logic::ProgressMsg pm{};
    if (read_local_progress(pm)) {
        g_rp[0].food = pm.food;
        g_rp[0].dna = pm.dna;
        g_rp[0].parts_mask = pm.parts_mask;
        g_rp[0].reported = true;
    }
    uint16_t winner_mask = g_rp[winner].parts_mask;
    // BUG-1552: collect owned masks of reported non-winner pickers and offer union of missing winner parts
    uint16_t picker_masks[rivals::kMaxPlayers] = {};
    size_t picker_count = 0;
    if (C.sess) {
        for (const auto& p : C.sess->players()) {
            if (!rivals::valid_player(p.id) || p.id == winner || !g_rp[p.id].reported)
                continue;
            picker_masks[picker_count++] = g_rp[p.id].parts_mask;
        }
    }
    auto items = cell_rivals_logic::build_draft_items_for_pickers(winner_mask, picker_masks, picker_count);
    if (items.empty()) {
        H->log(M, SK_INFO, "cell_rivals: race ended (winner=%u) but no unowned winner Cell parts to draft",
               (unsigned)winner);
        return;
    }
    if (rivals::start_draft(winner, items)) {
        g_draft_started_for_race = true;
        g_rv.draft_starts++;
        H->log(M, SK_INFO, "cell_rivals: started Cell part draft for winner %u (%zu part(s) offered)", (unsigned)winner,
               items.size());
    }
}

static void on_cell_draft_pick(uint8_t actor, uint32_t item_id, uint8_t /*kind*/, const std::string& label)
{
    if (!is_host() || !g_fix_cell_rivals || !rivals::valid_player(actor))
        return;
    int part_idx = cell_rivals_logic::item_id_to_part_index(item_id);
    if (part_idx < 0)
        return;
    // BUG-1552: in 3-4p drafts a picker may pick a part they already own (offered because another picker lacked it);
    // resolve against the picker's known parts mask so picks are never wasted no-ops.
    int granted = part_idx;
    uint8_t winner = rivals::draft_state().winner;
    if (rivals::valid_player(winner) && g_rp[actor].reported) {
        granted = cell_rivals_logic::resolve_draft_pick(g_rp[winner].parts_mask, g_rp[actor].parts_mask, part_idx);
        if (granted < 0) {
            H->log(M, SK_INFO, "cell_rivals: player %u already owns all winner Cell parts (skipped no-op pick %d)",
                   (unsigned)actor, part_idx);
            return;
        }
        if (granted != part_idx) {
            H->log(M, SK_INFO, "cell_rivals: player %u already owned part %d (%s) -> substituted missing part %d (%s)",
                   (unsigned)actor, part_idx, label.c_str(), granted, cell_rivals_logic::part_desc(granted).name);
        }
        g_rp[actor].parts_mask |= (uint16_t)(1u << granted);
    }
    H->log(M, SK_INFO, "cell_rivals: player %u picked Cell part %d (%s) from draft", (unsigned)actor, granted,
           cell_rivals_logic::part_desc(granted).name);
    if (actor == 0) {
        queue_local_part_unlock(granted);
    } else {
        net::Writer w;
        cell_rivals_logic::write_draft_apply(w, {cell_rivals_logic::kDraftOpUnlockPart, (uint32_t)granted});
        if (rivals::send_faction_to(actor, rivals::Stage::Cell, wire::rivals_faction::SK_DRAFT_APPLY, w))
            g_rv.draft_unlocks_sent++;
    }
}

static std::vector<rivals::UiRow> on_cell_extra_ui_rows()
{
    std::vector<rivals::UiRow> rows;
    if (!rivals_active())
        return rows;
    uint32_t kGold = 0xFFF2D479, kGreen = 0xFF7BE882, kDim = 0xFFB0BEC8;
    rows.push_back({"Cell Rivals (war PvP, no KO, no NPC diplomacy)", "", nullptr, kGold});
    if (C.sess) {
        const auto& race = rivals::race_state();
        for (const auto& p : C.sess->players()) {
            if (!rivals::valid_player(p.id))
                continue;
            const auto& pr = race.players[p.id];
            int pct = (int)std::round(std::clamp(pr.primary, 0.0f, 1.0f) * 100.0f);
            int food = (int)std::round(pr.extra);
            char b[192];
            snprintf(b, sizeof b, "  %s: bar %d%% (%d/1000) DNA %.0f parts=%u kills=%u", p.name.c_str(), pct, food,
                     pr.secondary, (unsigned)pr.stat_a, (unsigned)pr.stat_b);
            rows.push_back({b, "", nullptr, pr.finish_rank == 1 ? kGreen : 0});
        }
    }
    if (g_queued_creature_dna > 0) {
        char b[128];
        snprintf(b, sizeof b, "  Queued Creature DNA draft payout: +%d", (int)g_queued_creature_dna);
        rows.push_back({b, "", nullptr, kDim});
    }
    return rows;
}

void rivals_init()
{
    a_CellCollect_rivals = H ? H->sym("Simulator::Cell::CollectPart") : 0;
    rivals::register_faction_handler(rivals::Stage::Cell, on_faction_message);
    rivals::register_npc_influence_callback(
        rivals::Stage::Cell,
        [](rivals::Stage, rivals::InfluenceAction, uint8_t, uint32_t, uint8_t) -> bool {
            return false; // 11.3.1: no NPC diplomacy or influence in Cell stage
        });
    rivals::StageHooks hooks;
    hooks.on_buy_influence = [](uint8_t, int32_t, int32_t) -> bool {
        return false; // 11.3.1: no NPC diplomacy or influence in Cell stage
    };
    hooks.on_stage_race_end = on_cell_race_end;
    hooks.on_draft_pick = on_cell_draft_pick;
    hooks.on_extra_ui_rows = on_cell_extra_ui_rows;
    rivals::register_stage_hooks(rivals::Stage::Cell, std::move(hooks));
}

void rivals_on_player_leave(int id)
{
    if (id >= 0 && id < rivals::kMaxPlayers) {
        if (g_fix_cell_rejoin_progress && g_rp[id].reported) {
            std::string pkey = !g_slot_key[id].empty() ? g_slot_key[id] : cell_player_key((uint8_t)id);
            if (!pkey.empty()) {
                g_saved_prog.remember(pkey, g_rp[id].food, g_rp[id].parts_mask, g_rp[id].kills);
                if (H && M)
                    H->log(M, SK_INFO,
                           "cell_rivals: saved leaving player %d (%s) progress food=%d parts=0x%04X for rejoin (BUG-1603)",
                           id, pkey.c_str(), (int)g_rp[id].food, (unsigned)g_rp[id].parts_mask);
            }
        }
        g_rp[id] = RivalPlayer{};
        g_slot_key[id].clear();
        g_rejoin_checked[id] = false;
        g_host_prog_dirty[id] = false;
        g_war_pvp_last_hit[id] = 0.0;
    }
}

void rivals_tick(double now)
{
    if (!H || !g_fix_cell_rivals || !C.sess || !C.sess->connected())
        return;

    // Deliver / apply escalating next-stage Creature DNA payout upon entering Creature stage
    if (game::mode_id() == modes::kCreature && game::avatar() && !game::cinematic_playing()) {
        const auto& ds = rivals::draft_state();
        if (is_host() && !ds.active) {
            for (uint8_t pid = 0; pid < rivals::kMaxPlayers; pid++) {
                if (ds.pending_payout[pid] > 0 &&
                    (ds.pending_payout_stage[pid] == rivals::Stage::Cell ||
                     ds.pending_payout_stage[pid] == rivals::Stage::None))
                    deliver_payout_host(pid);
            }
        }
        if (g_queued_creature_dna > 0) {
            int32_t amt = g_queued_creature_dna;
            g_queued_creature_dna = 0;
            game::dna_set_raw(game::dna() + (float)amt);
            g_paid_creature_dna += amt;
            g_rv.payouts_applied++;
            H->log(M, SK_INFO, "cell_rivals: applied +%d Creature DNA draft payout (now %.0f)", (int)amt, game::dna());
            if (C.notice)
                C.notice("Cell draft payout: +" + std::to_string(amt) + " Creature DNA!");
        }
    }

    if (!rivals_active())
        return;

    // Knockout & revive are Together-only (11.3.1): clear any active KO if Cell Rivals is turned on
    if (g_downed)
        reset_knockout();

    if (!in_cell() || !avatar())
        return;

    if (g_unlock_queue != 0 && !g_unlock_scheduled) {
        g_unlock_scheduled = true;
        H->run_outside_frame(unlock_cell_parts_task, nullptr);
    }

    if (is_host()) {
        cache_meat_resource();
        const auto& race = rivals::race_state();
        const auto& draft = rivals::draft_state();
        if (race.winner == 255) {
            g_draft_started_for_race = false;
            g_race_end_handled = false;
            g_host_stage_end_unlocked = false;
        }

        cell_rivals_logic::ProgressMsg pm{};
        if (read_local_progress(pm)) {
            auto& rp0 = g_rp[0];
            if (!rp0.reported || rp0.food != pm.food || rp0.dna != pm.dna || rp0.parts_mask != pm.parts_mask) {
                rp0.food = pm.food;
                rp0.dna = pm.dna;
                rp0.parts_mask = pm.parts_mask;
                rp0.reported = true;
                g_host_prog_dirty[0] = true;
            }
        }

        // BUG-1552: if a guest won Cell first and both catch-up AND draft have completely finished while host is still
        // below 1000 food, unlock the host's local evolution bar to 1000 so Spore's Cell->Creature evolve button opens
        // and the party is never stranded in Cell.
        if (g_rp[0].reported &&
            cell_rivals_logic::should_unlock_host_stage_end(true, true, g_race_end_handled, race.stage_finished,
                                                            race.catchup_active, draft.active, race.winner,
                                                            g_rp[0].food, g_host_stage_end_unlocked)) {
            g_host_stage_end_unlocked = true;
            set_food_total(cell_rivals_logic::kCellFullFood);
            H->log(M, SK_INFO,
                   "cell_rivals: guest %u won Cell and catch-up/draft finished (host food=%d) -> unlocked host Cell->Creature bar (1000)",
                   (unsigned)race.winner, (int)g_rp[0].food);
            if (C.notice)
                C.notice("Cell Rivals race & draft finished! Evolve button unlocked to advance to Creature stage.");
        }

        // Check every player's Cell bar every second (and immediately if anyone just reached 1000)
        bool any_new_finish = false;
        if (!race.stage_finished) {
            for (int i = 0; i < rivals::kMaxPlayers; i++) {
                if (g_rp[i].reported && cell_rivals_logic::bar_is_full(g_rp[i].food) &&
                    race.players[i].finish_rank == 0) {
                    any_new_finish = true;
                }
            }
        }
        if (any_new_finish || now - g_last_host_check >= 1.0) {
            g_last_host_check = now;
            for (int i = 0; i < rivals::kMaxPlayers; i++) {
                if (g_host_prog_dirty[i])
                    push_player_progress_host((uint8_t)i);
                if (!rivals::race_state().stage_finished && g_rp[i].reported &&
                    cell_rivals_logic::bar_is_full(g_rp[i].food) &&
                    rivals::race_state().players[i].finish_rank == 0) {
                    rivals::report_stage_complete((uint8_t)i);
                }
            }
        }
    } else if (is_guest()) {
        cell_rivals_logic::ProgressMsg pm{};
        if (!read_local_progress(pm))
            return;
        bool changed = !g_sent_prog_once || pm.food != g_last_sent_prog.food || pm.dna != g_last_sent_prog.dna ||
                       pm.parts_mask != g_last_sent_prog.parts_mask;
        if ((changed && now - g_last_prog_send >= 0.25) || now - g_last_prog_send >= 1.0) {
            net::Writer w;
            cell_rivals_logic::write_progress(w, pm);
            if (rivals::send_faction_to(0, rivals::Stage::Cell, wire::rivals_faction::SK_PROGRESS, w)) {
                g_last_sent_prog = pm;
                g_sent_prog_once = true;
                g_last_prog_send = now;
                g_rv.progress_tx++;
            }
        }
    }
}

std::string rivals_status_line()
{
    cell_rivals_logic::ProgressMsg pm{};
    bool has = read_local_progress(pm);
    char b[460];
    snprintf(b, sizeof b,
             "cell_rivals: fix=%d active=%d food=%d bar=%.2f dna=%d parts=%04X pvp_out=%u pvp_in=%u pvp_blocked=%u "
             "pvp_kills=%u meat_drops=%u meat_chunks=%u rollbacks=%u/%u draft_starts=%u draft_unlocks=%u/%u "
             "queued_dna=%d paid_dna=%d wscale_safe=%d puppet_skips=%u puppets_clamped=%u fluid_clamps=%u\n",
             (int)g_fix_cell_rivals, (int)rivals_active(), has ? pm.food : -1,
             has ? cell_rivals_logic::bar_fraction(pm.food) : 0.0f, has ? pm.dna : -1,
             has ? (unsigned)pm.parts_mask : 0u, g_rv.pvp_out, g_rv.pvp_in, g_rv.pvp_blocked, g_rv.pvp_kills,
             g_rv.meat_drops, g_rv.meat_chunks, g_rv.rollbacks_out, g_rv.rollbacks_in, g_rv.draft_starts,
             g_rv.draft_unlocks_sent, g_rv.draft_unlocks_applied, (int)g_queued_creature_dna, (int)g_paid_creature_dna,
             (int)g_fix_rivals_wscale_solo_safe, g_rv.wscale_puppet_pairs_skipped, g_rv.wscale_puppets_clamped,
             g_rv.fluid_box_clamps);
    return b;
}

std::string rivals_debug(const std::vector<std::string>& a)
{
    if (a.size() >= 4 && a[1] == "hit") {
        int tid = atoi(a[2].c_str());
        int dmg = atoi(a[3].c_str());
        bool ok = rivals_send_pvp_hit(tid, dmg, 4, true);
        return ok ? "rivals pvp hit sent\n" : "rivals pvp hit blocked\n";
    }
    if (a.size() >= 4 && a[1] == "tpabs") {
        uint8_t* av = in_cell() ? avatar() : nullptr;
        if (!av)
            return "no cell avatar\n";
        float x = (float)atof(a[2].c_str()), y = (float)atof(a[3].c_str());
        if (!std::isfinite(x) || !std::isfinite(y))
            return "invalid coords\n";
        stop_at(av, {x, y, 0.f});
        char b[96];
        snprintf(b, sizeof b, "moved to (%.1f %.1f)\n", x, y);
        return b;
    }
    if (a.size() >= 2 && a[1] == "payout") {
        if (!is_host())
            return "host only\n";
        uint8_t pid = a.size() >= 3 ? (uint8_t)atoi(a[2].c_str()) : rivals::draft_state().winner;
        int32_t pay = deliver_payout_host(pid);
        char b[128];
        snprintf(b, sizeof b, "payout=%d queued_dna=%d paid_dna=%d\n", (int)pay, (int)g_queued_creature_dna,
                 (int)g_paid_creature_dna);
        return b;
    }
    std::string out = rivals_status_line();
    if (is_host()) {
        for (int i = 0; i < rivals::kMaxPlayers; i++) {
            if (!g_rp[i].reported)
                continue;
            char b[128];
            snprintf(b, sizeof b, "  p%d: food=%d bar=%.2f dna=%d parts=%04X kills=%u\n", i, g_rp[i].food,
                     cell_rivals_logic::bar_fraction(g_rp[i].food), g_rp[i].dna, (unsigned)g_rp[i].parts_mask,
                     (unsigned)g_rp[i].kills);
            out += b;
        }
    }
    return out;
}

} // namespace cellsync
