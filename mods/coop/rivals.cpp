// Shared Rivals mode runtime module (WP-R1 of 0.8.0, BUG-0794, docs/11-rivals.md sections 11.2, 11.4-11.8).
#include "rivals.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "fixes.h"
#include "i18n.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/manifest.h"
#include "settings.h"
#include "space_ext.h"
#include "space_rivalry.h"
#include "store.h"
#include "wire/rivals.h"
#include "wire/rivals_faction.h"

namespace rivals {

static const SkHost* H = nullptr;
static SkMod* M = nullptr;
static Ctx C;

// BUG-0794: master switch for shared Rivals mode systems (stage modes, 4x4 relations + 60 s break notice,
// influence & NPC diplomacy ledger, race progress & catch-up timer, draft, Chronicle, MSG_RIVALS 80).
// Off (SPORE_FIX_OFF=rivals.core): disables Rivals mode state, messaging, commands, and UI.
static bool g_rivals_core_fix = true;
COOP_FIX("rivals.core", "BUG-0794", g_rivals_core_fix,
         "rivals: shared Rivals systems (stage modes, 4x4 relations, influence, race, draft, chronicle)");
// BUG-1579: never auto-activate in-memory lobby Rivals config on an existing Together save (store has prior records
// and no rivals.state), and never persist All rivals into a Together save; keep lobby preset intact for new worlds.
static bool g_protect_together_fix = true;
COOP_FIX("rivals.protect_together_save", "BUG-1579", g_protect_together_fix,
         "rivals: suppress lobby Rivals preset when loading an existing Together save so it is never hijacked or contaminated");

static State g_state;
static Config g_lobby_config;            // BUG-1579: lobby's configured preset preserved across loaded Together saves
static bool g_together_save_suppressed = false;
static bool g_have_snapshot = false;
static bool g_fast_timers = false;
static double g_fast_catchup_secs = 0.0;
static double g_last_broadcast = 0.0;
static double g_last_tick_sync = 0.0;
static uint32_t g_tx = 0, g_rx = 0, g_bad = 0, g_store_reads = 0, g_store_writes = 0;
static NpcInfluenceCallback g_npc_cb;
static NpcInfluenceCallback g_npc_stage_cb[kStages];
static FactionMsgHandler g_faction_cb[kStages];
static StageHooks g_stage_hooks[kStages];

static NpcInfluenceCallback* active_npc_cb(Stage s)
{
    int idx = (int)s;
    if (idx >= 0 && idx < kStages && g_npc_stage_cb[idx])
        return &g_npc_stage_cb[idx];
    return g_npc_cb ? &g_npc_cb : nullptr;
}

static StageHooks* active_stage_hooks(Stage s)
{
    int idx = (int)s;
    return (idx >= 0 && idx < kStages) ? &g_stage_hooks[idx] : nullptr;
}

// Per-world persistence (store.cpp "rivals.state" v1)
static PersistedWorld g_persisted;
static std::string g_persisted_world;
static uint32_t g_store_serial = 0;

static constexpr const char* kStoreFeature = "rivals.state";
static constexpr uint16_t kStoreVer = 1;

static double now_s() { return H ? H->time_seconds() : 0.0; }
static bool connected() { return C.sess && C.sess->connected(); }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->connected() && C.sess->role() == net::Role::Client; }
static uint8_t my_id() { return C.sess ? C.sess->my_id() : 0; }
static bool ru() { return C.russian && C.russian(); }
static const char* tr(const char* en, const char* r) { return i18n::tr(en, r); }
static const char* tr_stage(Stage s) { return i18n::tr(stage_name(s, false), stage_name(s, true)); }
static const char* tr_mode(StageMode m) { return i18n::tr(mode_name(m, false), mode_name(m, true)); }
static const char* tr_preset(Preset p) { return i18n::tr(preset_name(p, false), preset_name(p, true)); }
static const char* tr_dist(BaseDistance d) { return i18n::tr(distance_name(d, false), distance_name(d, true)); }
static const char* tr_rel(Relation r) { return i18n::tr(relation_name(r, false), relation_name(r, true)); }

static const char* stage_currency_label(Stage st)
{
    switch (st) {
    case Stage::Creature: return tr("DNA", "ДНК");
    case Stage::Tribe: return tr("food", "еды");
    case Stage::Civ:
    case Stage::Space: return "$";
    default: return "";
    }
}

static std::string npc_display_name(Stage st, uint8_t viewer, uint32_t npc_id)
{
    if (auto* hk = active_stage_hooks(st); hk && hk->on_npc_ui_list) {
        auto entries = hk->on_npc_ui_list(viewer);
        for (const auto& e : entries) {
            if (e.npc_id == npc_id && !e.label.empty())
                return e.label;
        }
        if (viewer != 0) {
            auto host_entries = hk->on_npc_ui_list(0);
            for (const auto& e : host_entries) {
                if (e.npc_id == npc_id && !e.label.empty())
                    return e.label;
            }
        }
    }
    char fb[48];
    snprintf(fb, sizeof fb, "%s #%u", tr("Faction", "Фракция"), (unsigned)((npc_id % 99u) + 1u));
    return fb;
}

static std::string localize_error_text(const std::string& err)
{
    if (err.empty())
        return {};
    return i18n::tr(err.c_str());
}

static std::string name_of(uint8_t id)
{
    if (C.sess) {
        std::string n = C.sess->name_of(id);
        if (!n.empty())
            return n;
    }
    return "P" + std::to_string(id);
}

static bool peer_reads_rivals(uint8_t id)
{
    if (!C.sess)
        return false;
    bool has = false;
    std::vector<net::FamilyRev> fams;
    if (is_host()) {
        if (!C.sess->peer_families(id, has, fams))
            return false;
        return wire::rivals::peer_reads(has, fams);
    }
    if (!C.sess->host_families(has, fams))
        return false;
    return wire::rivals::peer_reads(has, fams);
}

static int active_player_count()
{
    if (!C.sess)
        return 1;
    int n = (int)C.sess->players().size();
    return n > 0 ? n : 1;
}

static void fill_present(bool present[kMaxPlayers])
{
    for (int i = 0; i < kMaxPlayers; i++)
        present[i] = false;
    if (C.sess && !C.sess->players().empty()) {
        for (const auto& p : C.sess->players())
            if (valid_player(p.id))
                present[p.id] = true;
    } else {
        present[my_id() < kMaxPlayers ? my_id() : 0] = true;
    }
}

static void emit_notice(const std::string& msg)
{
    if (msg.empty())
        return;
    if (C.notice)
        C.notice(msg);
    else if (C.feed)
        C.feed(msg);
}

static void send_event_to(uint8_t target, uint8_t code, uint8_t a, uint8_t b, int32_t value, const std::string& text)
{
    if (!g_rivals_core_fix || !is_host() || !C.sess || target == 0 || !peer_reads_rivals(target) || text.empty())
        return;
    wire::rivals::Event ev{code, a, b, value, text};
    net::Writer w;
    w.u8(wire::rivals::K_EVENT);
    ev.write(w);
    C.sess->send_reliable_to(target, net::MSG_RIVALS, w);
    g_tx++;
}

static void send_event_to_guests(uint8_t code, uint8_t a, uint8_t b, int32_t value, const std::string& text)
{
    if (!g_rivals_core_fix || !is_host() || !C.sess)
        return;
    wire::rivals::Event ev{code, a, b, value, text};
    net::Writer w;
    w.u8(wire::rivals::K_EVENT);
    ev.write(w);
    for (const auto& p : C.sess->players()) {
        if (p.id == 0 || !peer_reads_rivals(p.id))
            continue;
        C.sess->send_reliable_to(p.id, net::MSG_RIVALS, w);
        g_tx++;
    }
}

static void sync_persisted_from_state()
{
    if (!is_host())
        return;
    g_persisted.config = g_state.config;
    if (C.sess) {
        for (auto& e : g_state.chronicle.history) {
            if (e.player_key.empty() && valid_player(e.player_id)) {
                std::string k = store::key_of(e.player_id);
                if (!k.empty())
                    e.player_key = k;
            }
        }
        auto ps = C.sess->players();
        for (size_t i = 0; i < ps.size(); i++) {
            uint8_t id_a = ps[i].id;
            std::string ka = store::key_of(id_a);
            if (!ka.empty() && valid_player(id_a)) {
                if (g_state.chronicle.totals[id_a] != 0 || g_persisted.chronicle_by_key.count(ka))
                    g_persisted.chronicle_by_key[ka] = g_state.chronicle.totals[id_a];
            }
            for (size_t j = i + 1; j < ps.size(); j++) {
                uint8_t id_b = ps[j].id;
                std::string kb = store::key_of(id_b);
                if (ka.empty() || kb.empty() || ka == kb || !valid_player(id_a) || !valid_player(id_b))
                    continue;
                Relation r = g_state.rel.get(id_a, id_b);
                auto kp = PersistedWorld::norm_pair(ka, kb);
                if (r == Relation::Neutral)
                    g_persisted.rel_by_key.erase(kp);
                else
                    g_persisted.rel_by_key[kp] = r;
            }
        }
    }
    g_persisted.history = g_state.chronicle.history;
}

static void apply_persisted_for_present_players()
{
    if (!is_host() || !C.sess)
        return;
    auto ps = C.sess->players();
    std::map<std::string, uint8_t> key_to_id;
    for (size_t i = 0; i < ps.size(); i++) {
        uint8_t id_a = ps[i].id;
        std::string ka = store::key_of(id_a);
        if (!ka.empty() && valid_player(id_a)) {
            key_to_id[ka] = id_a;
            if (auto it = g_persisted.chronicle_by_key.find(ka); it != g_persisted.chronicle_by_key.end())
                g_state.chronicle.totals[id_a] = it->second;
        }
        for (size_t j = i + 1; j < ps.size(); j++) {
            uint8_t id_b = ps[j].id;
            std::string kb = store::key_of(id_b);
            if (ka.empty() || kb.empty() || ka == kb || !valid_player(id_a) || !valid_player(id_b))
                continue;
            auto kp = PersistedWorld::norm_pair(ka, kb);
            if (auto it = g_persisted.rel_by_key.find(kp); it != g_persisted.rel_by_key.end())
                g_state.rel.set(id_a, id_b, it->second);
        }
    }
    PersistedWorld::remap_history_ids(g_state.chronicle.history, key_to_id);
    PersistedWorld::remap_history_ids(g_persisted.history, key_to_id);
}

static void host_broadcast(bool bump_rev = true, int only_peer = -1)
{
    if (!g_rivals_core_fix || !is_host() || !C.sess)
        return;
    if (bump_rev)
        g_state.rev++;
    sync_persisted_from_state();
    double now = now_s();
    wire::rivals::Snapshot sn = wire::rivals::Snapshot::from_state(g_state, now);
    net::Writer w;
    w.u8(wire::rivals::K_SNAPSHOT);
    sn.write(w);
    for (const auto& p : C.sess->players()) {
        if (p.id == 0)
            continue;
        if (only_peer >= 0 && p.id != (uint8_t)only_peer)
            continue;
        if (!peer_reads_rivals(p.id))
            continue;
        C.sess->send_reliable_to(p.id, net::MSG_RIVALS, w);
        g_tx++;
    }
    g_last_broadcast = now;
}

// ---- store.cpp persistence hooks ----
static void disk_load_if_opened()
{
    if (!g_rivals_core_fix)
        return;
    pstore::Store* w = store::world();
    if (!w) {
        // BUG-1579: when returning to the galaxy menu after closing a world, restore the lobby's configured preset.
        if (g_protect_together_fix && !g_persisted_world.empty() && C.mode && C.mode() == modes::kMenu) {
            bool was_suppressed = g_together_save_suppressed;
            g_together_save_suppressed = false;
            g_persisted_world.clear();
            g_store_serial = 0;
            if (g_state.config != g_lobby_config) {
                g_state.config = g_lobby_config;
                if (H && M && was_suppressed)
                    H->log(M, SK_INFO, "rivals: returned to galaxy menu, restored lobby preset (%s) (BUG-1579)",
                           preset_name(g_lobby_config.preset()));
                if (is_host())
                    host_broadcast(true);
            }
        }
        return;
    }
    uint32_t ser = store::serial();
    if (ser == g_store_serial)
        return;
    g_store_serial = ser;
    const pstore::Record* rec = w->get_world(kStoreFeature);
    bool has_valid_rec = rec && rec->ver == kStoreVer;
    WorldOpenAction act = g_protect_together_fix
                              ? classify_world_open(has_valid_rec, w->records(), w->players.size(), w->saved)
                              : (has_valid_rec ? WorldOpenAction::LoadStored : WorldOpenAction::ApplyLobbyConfig);
    if (act == WorldOpenAction::SuppressTogetherSave) {
        bool had_rivals = g_state.config.any_rivals() || g_lobby_config.any_rivals();
        g_together_save_suppressed = true;
        g_persisted = PersistedWorld{};
        g_persisted_world = w->world;
        g_state.config = Config{};
        g_state.rel.reset();
        g_state.chronicle.reset();
        if (H && M && had_rivals)
            H->log(M, SK_INFO,
                   "rivals: existing Together save '%s' (%u records, no rivals.state): suppressing lobby Rivals preset (%s) so save is not contaminated (BUG-1579)",
                   w->world.c_str(), (unsigned)w->records(), preset_name(g_lobby_config.preset()));
        if (is_host()) {
            host_broadcast(true);
            sync_space_rivalry();
        }
        return;
    }
    if (act == WorldOpenAction::ApplyLobbyConfig) {
        g_together_save_suppressed = false;
        if (g_persisted_world != w->world) {
            g_persisted = PersistedWorld{};
            g_persisted_world = w->world;
            if (g_protect_together_fix && g_state.config != g_lobby_config) {
                g_state.config = g_lobby_config;
                if (is_host()) {
                    host_broadcast(true);
                    sync_space_rivalry();
                }
            }
        }
        return;
    }
    g_together_save_suppressed = false;
    PersistedWorld loaded;
    if (loaded.decode(rec->data)) {
        g_persisted = std::move(loaded);
        g_persisted_world = w->world;
        g_store_reads++;
        g_state.config = g_persisted.config;
        g_state.chronicle.history = g_persisted.history;
        apply_persisted_for_present_players();
        if (H && M)
            H->log(M, SK_INFO, "rivals: loaded persisted state for %s (preset=%s, rels=%u, chronicle=%u)",
                   w->world.c_str(), preset_name(g_state.config.preset()), (unsigned)g_persisted.rel_by_key.size(),
                   (unsigned)g_persisted.chronicle_by_key.size());
        if (is_host()) {
            host_broadcast();
            sync_space_rivalry();
        }
    }
}

static void disk_save(pstore::Store& s, int64_t now)
{
    if (!g_rivals_core_fix || !g_store_serial || g_store_serial != store::serial() || &s != store::world())
        return;
    if (g_protect_together_fix && g_together_save_suppressed)
        return; // BUG-1579: never write rivals.state into a loaded Together save
    if (is_host())
        sync_persisted_from_state();
    // Write when non-default data exists OR when a record already exists on disk (BUG-1551: switching back to
    // AllTogether must overwrite the previous AllRivals record with a clean default record).
    bool had_record = s.get_world(kStoreFeature) != nullptr;
    if (!g_persisted.should_write(had_record))
        return;
    std::vector<std::string> owners;
    for (const auto& [kp, r] : g_persisted.rel_by_key) {
        (void)r;
        owners.push_back(kp.first);
        owners.push_back(kp.second);
    }
    for (const auto& [k, pts] : g_persisted.chronicle_by_key) {
        (void)pts;
        owners.push_back(k);
    }
    for (const auto& e : g_persisted.history) {
        if (!e.player_key.empty())
            owners.push_back(e.player_key);
    }
    store::mark_owners(s, "rivals.owner", owners, now);
    s.put_world(kStoreFeature, kStoreVer, g_persisted.encode());
    g_store_writes++;
}

static void disk_adopt(const std::string& from, const std::string& to)
{
    if (!g_rivals_core_fix)
        return;
    g_persisted.adopt_key(from, to);
    for (auto& e : g_state.chronicle.history) {
        if (e.player_key == from)
            e.player_key = to;
    }
    apply_persisted_for_present_players();
    if (is_host())
        host_broadcast();
}

static void disk_opened()
{
    if (!g_rivals_core_fix)
        return;
    // BUG-1551: load any existing "rivals.state" before disk_save() runs (do not just mark serial loaded).
    disk_load_if_opened();
}

static bool g_store_reg = store::add({kStoreFeature, disk_save, disk_adopt, disk_opened});

SESSION_STATE(
    "rivals.session", nullptr,
    [] {
        g_state.reset_all();
        g_lobby_config = Config{};
        g_together_save_suppressed = false;
        g_persisted = PersistedWorld{};
        g_persisted_world.clear();
        g_store_serial = 0;
        g_have_snapshot = false;
        g_fast_timers = false;
        g_fast_catchup_secs = 0.0;
        g_state.rel.break_delay_secs = kDefaultBreakNoticeSecs;
        g_state.rel.ally_break_grace_secs = kDefaultAllyBreakGraceSecs;
        g_state.inf.protect_duration_secs = kDefaultProtectSecs;
        g_state.draft.timeout_secs = kDefaultDraftTimeoutSecs;
        g_last_broadcast = 0.0;
        g_last_tick_sync = 0.0;
    },
    []() -> size_t {
        return (g_have_snapshot ? 1 : 0) + (g_state.draft.active ? 1 : 0) + (g_state.race.catchup_active ? 1 : 0) +
               g_state.rel.proposals.size() + g_state.rel.break_notices.size();
    });

// ---- Space rivalry sync helper ----
static spaceext::Relation to_space_rel(Relation r)
{
    switch (r) {
    case Relation::Ally: return spaceext::Relation::Ally;
    case Relation::War: return spaceext::Relation::War;
    default: return spaceext::Relation::Neutral;
    }
}

void sync_space_rivalry()
{
    if (!g_rivals_core_fix || !connected())
        return;
    if (current_stage() != Stage::Space)
        return;
    bool sp_rivals = g_state.config.stage_is_rivals(Stage::Space);
    std::vector<std::tuple<uint8_t, uint8_t, spaceext::Relation>> rels;
    for (int i = 0; i < kRelPairs; i++) {
        auto [a, b] = pair_from_index(i);
        rels.push_back({a, b, to_space_rel(g_state.rel.get(a, b))});
    }
    rivalry::sync_from_rivals(sp_rivals, rels);
}

// ---- Host action handlers ----
static std::string host_apply_relation(uint8_t actor, uint8_t target, RelAction action)
{
    if (!g_rivals_core_fix)
        return "rivals.core is off";
    if (!is_host())
        return "not host";
    std::string ev;
    double now = now_s();
    std::string err = g_state.rel.apply_action(actor, target, action, now, &ev);
    if (!err.empty())
        return err;
    std::string msg;
    std::string na = name_of(actor), nb = name_of(target);
    if (ev == "proposed_ally")
        msg = na + " proposed an alliance to " + nb;
    else if (ev == "allied")
        msg = na + " and " + nb + " are now allies";
    else if (ev == "declined_ally")
        msg = na + " declined alliance with " + nb;
    else if (ev == "war")
        msg = na + " declared war on " + nb + "!";
    else if (ev == "break_pending") {
        char b[128];
        snprintf(b, sizeof b, "%s ends the alliance with %s: war in %.0f s", na.c_str(), nb.c_str(),
                 g_state.rel.break_delay_secs);
        msg = b;
    } else if (ev == "proposed_peace")
        msg = na + " proposed peace to " + nb;
    else if (ev == "peace")
        msg = na + " and " + nb + " signed peace";
    else if (ev == "declined_peace")
        msg = na + " declined peace with " + nb;

    host_broadcast();
    if (!msg.empty()) {
        emit_notice(msg);
        send_event_to_guests(wire::rivals::E_REL, actor, target, (int32_t)action, msg);
    }
    sync_space_rivalry();
    return "";
}

static std::string host_apply_influence(uint8_t actor, InfluenceAction action, uint32_t npc_id, uint8_t rival_id)
{
    if (!g_rivals_core_fix)
        return "rivals.core is off";
    if (!is_host())
        return "not host";
    if (!valid_player(actor))
        return "invalid player";
    double now = now_s();
    std::string err;
    std::string msg;
    int32_t cost_out = 0;
    switch (action) {
    case InfluenceAction::Buy: {
        Stage st = current_stage();
        auto* hk = active_stage_hooks(st);
        // BUG-1579: require an active non-Cell stage with a registered resource-charging hook (prevents free buys in Galaxy menu).
        if (!can_buy_influence_in_stage(st, hk && hk->on_buy_influence))
            return "Influence can only be bought during an active stage";
        int32_t next_price = g_state.inf.buy_cost(actor);
        if (!hk->on_buy_influence(actor, next_price, kInfluenceBuyGain))
            return "insufficient resources to buy influence";
        if (!g_state.inf.buy(actor, cost_out))
            return "buy failed";
        int32_t shown_cost = stage_influence_buy_price(st, cost_out);
        char b[160];
        snprintf(b, sizeof b, "%s %s +%d %s (%d %s)", name_of(actor).c_str(), tr("bought", "купил(а)"),
                 (int)kInfluenceBuyGain, tr("influence", "влияния"), (int)shown_cost, stage_currency_label(st));
        msg = b;
        break;
    }
    case InfluenceAction::TurnAgainst: {
        Stage st = current_stage();
        err = g_state.inf.spend_turn_against(actor, npc_id, rival_id, now);
        if (!err.empty())
            return err;
        if (auto* cb = active_npc_cb(st); cb && !(*cb)(st, action, actor, npc_id, rival_id)) {
            g_state.inf.earn(actor, kCostTurnAgainst);
            return "npc rejected turn against";
        }
        std::string fname = npc_display_name(st, actor, npc_id);
        char b[160];
        snprintf(b, sizeof b, "%s %s %s %s %s", name_of(actor).c_str(), tr("turned", "настроил(а)"),
                 fname.c_str(), tr("against", "против"), name_of(rival_id).c_str());
        msg = b;
        break;
    }
    case InfluenceAction::Protect: {
        Stage st = current_stage();
        err = g_state.inf.can_spend_protect(actor);
        if (!err.empty())
            return err;
        // BUG-1551: if the active stage exposes live NPC diplomacy status, verify actor is allied / allowed to protect.
        if (auto* hk = active_stage_hooks(st); hk && hk->on_npc_ui_list) {
            auto entries = hk->on_npc_ui_list(actor);
            bool found = false;
            bool ok_protect = false;
            for (const auto& e : entries) {
                if (e.npc_id == npc_id) {
                    found = true;
                    ok_protect = e.can_protect || e.allied;
                    break;
                }
            }
            if (found && !ok_protect)
                return "must be allied with faction to protect";
        }
        auto* cb = active_npc_cb(st);
        err = g_state.inf.try_spend_protect(actor, npc_id, now, [&] {
            return !cb || (*cb)(st, action, actor, npc_id, 255);
        });
        if (!err.empty())
            return err;
        std::string fname = npc_display_name(st, actor, npc_id);
        char b[160];
        snprintf(b, sizeof b, "%s %s %s (%.0f s)", name_of(actor).c_str(),
                 tr("protected bond with", "защитил(а) союз с"), fname.c_str(), g_state.inf.protect_duration_secs);
        msg = b;
        break;
    }
    case InfluenceAction::SmoothOver: {
        Stage st = current_stage();
        err = g_state.inf.spend_smooth_over(actor, npc_id, now);
        if (!err.empty())
            return err;
        if (auto* cb = active_npc_cb(st); cb && !(*cb)(st, action, actor, npc_id, 255)) {
            g_state.inf.earn(actor, kCostSmoothOver);
            return "npc rejected smooth over";
        }
        std::string fname = npc_display_name(st, actor, npc_id);
        char b[160];
        snprintf(b, sizeof b, "%s %s %s", name_of(actor).c_str(),
                 tr("smoothed over relations with", "улучшил(а) отношения с"), fname.c_str());
        msg = b;
        break;
    }
    default:
        return "unknown influence action";
    }
    host_broadcast();
    if (!msg.empty()) {
        emit_notice(msg);
        send_event_to_guests(wire::rivals::E_INFLUENCE, actor, rival_id, (int32_t)npc_id, msg);
    }
    return "";
}

static std::string host_apply_draft(uint8_t actor, bool skip, uint32_t item_id)
{
    if (!g_rivals_core_fix)
        return "rivals.core is off";
    if (!is_host())
        return "not host";
    int32_t payout = 0;
    std::string err = skip ? g_state.draft.skip(actor) : g_state.draft.pick(actor, item_id, &payout);
    if (!err.empty())
        return err;
    char b[160];
    if (skip)
        snprintf(b, sizeof b, "%s skipped remaining draft picks", name_of(actor).c_str());
    else {
        std::string item_lbl;
        for (const auto& it : g_state.draft.items) {
            if (it.id == item_id) {
                item_lbl = it.label;
                if (auto* hk = active_stage_hooks(g_state.draft.stage); hk && hk->on_draft_pick)
                    hk->on_draft_pick(actor, it.id, it.kind, it.label);
                break;
            }
        }
        if (item_lbl.empty())
            item_lbl = "#" + std::to_string((unsigned)item_id);
        snprintf(b, sizeof b, "%s %s %s (%s +%d)", name_of(actor).c_str(), tr("copied", "взял(а)"),
                 item_lbl.c_str(), tr("winner", "победителю"), (int)payout);
    }
    host_broadcast();
    emit_notice(b);
    send_event_to_guests(wire::rivals::E_DRAFT, actor, g_state.draft.winner, payout, b);
    return "";
}

// ---- Public lifecycle ----
void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    g_state.reset_all();
    g_lobby_config = Config{};
    g_together_save_suppressed = false;
    g_have_snapshot = false;
    g_fast_timers = false;
    g_fast_catchup_secs = 0.0;
    g_state.rel.ally_break_grace_secs = kDefaultAllyBreakGraceSecs;
    g_store_serial = 0;
    if (H && M)
        H->log(M, SK_INFO, "rivals: initialized (rivals.core=%d)", (int)g_rivals_core_fix);
}

void tick(double now)
{
    if (!g_rivals_core_fix)
        return;
    disk_load_if_opened();
    if (!connected())
        return;

    if (is_host()) {
        bool dirty = false;
        auto expired_breaks = g_state.rel.tick(now);
        for (const auto& bn : expired_breaks) {
            dirty = true;
            std::string msg = name_of(bn.a) + " and " + name_of(bn.b) + ": alliance ended, now at war!";
            emit_notice(msg);
            send_event_to_guests(wire::rivals::E_REL, bn.a, bn.b, (int32_t)Relation::War, msg);
            sync_space_rivalry();
        }
        if (g_state.inf.tick(now))
            dirty = true;
        if (g_state.race.tick(now)) {
            dirty = true;
            std::string msg = "Catch-up timer ended! Ready to advance to the next stage.";
            emit_notice(msg);
            send_event_to_guests(wire::rivals::E_RACE, g_state.race.winner, 0, 0, msg);
            if (auto* hk = active_stage_hooks(g_state.race.stage); hk && hk->on_stage_race_end)
                hk->on_stage_race_end(g_state.race.winner);
        }
        if (g_state.draft.tick(now)) {
            dirty = true;
            std::string msg = "Draft finished!";
            emit_notice(msg);
            send_event_to_guests(wire::rivals::E_DRAFT, g_state.draft.winner, 0, g_state.draft.winner_payout, msg);
        }
        if (dirty)
            host_broadcast();
        else if (now - g_last_broadcast >= 5.0)
            host_broadcast(false);
    } else {
        // Guest updates local countdown display smoothly between host snapshots
        if (now - g_last_tick_sync >= 0.1) {
            g_last_tick_sync = now;
            for (auto& b : g_state.rel.break_notices)
                b.remaining_secs = std::max(0.0, b.expires_at - now);
            for (auto& p : g_state.inf.protections)
                p.remaining_secs = std::max(0.0, p.expires_at - now);
            if (g_state.race.catchup_active)
                g_state.race.catchup_remaining_secs = std::max(0.0, g_state.race.catchup_ends_at - now);
            if (g_state.draft.active)
                g_state.draft.remaining_secs = std::max(0.0, g_state.draft.ends_at - now);
        }
    }
}

void on_message(uint8_t from, net::Reader& r)
{
    if (!g_rivals_core_fix)
        return;
    uint8_t kind = r.u8();
    if (!r.ok()) {
        g_bad++;
        return;
    }
    if (kind == wire::rivals::K_SNAPSHOT) {
        // Guests accept authoritative snapshots ONLY from host (player 0).
        if (!is_guest() || from != 0)
            return;
        wire::rivals::Snapshot sn;
        if (!sn.read(r)) {
            g_bad++;
            return;
        }
        sn.apply_to(g_state, now_s());
        g_have_snapshot = true;
        g_rx++;
        sync_space_rivalry();
        return;
    }
    if (kind == wire::rivals::K_EVENT) {
        if (!is_guest() || from != 0)
            return;
        wire::rivals::Event ev;
        if (!ev.read(r)) {
            g_bad++;
            return;
        }
        g_rx++;
        if (!ev.text.empty()) {
            // BUG-1579: localize host-sent Rivals error notices into the guest's chosen UI language.
            if (ev.value == -1 && ev.text.rfind("Rivals: ", 0) == 0)
                emit_notice(std::string(tr("Rivals: ", "Соперники: ")) + localize_error_text(ev.text.substr(8)));
            else
                emit_notice(ev.text);
        }
        return;
    }
    if (!is_host() || from == 0 || !valid_player(from))
        return;
    if (kind == wire::rivals::K_REL_REQ) {
        wire::rivals::RelReq req;
        if (!req.read(r)) {
            g_bad++;
            return;
        }
        g_rx++;
        std::string err = host_apply_relation(from, req.target, (RelAction)req.action);
        if (!err.empty())
            send_event_to(from, wire::rivals::E_REL, from, req.target, -1, "Rivals: " + err);
    } else if (kind == wire::rivals::K_INF_REQ) {
        wire::rivals::InfReq req;
        if (!req.read(r)) {
            g_bad++;
            return;
        }
        g_rx++;
        std::string err = host_apply_influence(from, (InfluenceAction)req.action, req.npc_id, req.rival_id);
        if (!err.empty())
            send_event_to(from, wire::rivals::E_INFLUENCE, from, req.rival_id, -1, "Rivals: " + err);
    } else if (kind == wire::rivals::K_DRAFT_REQ) {
        wire::rivals::DraftReq req;
        if (!req.read(r)) {
            g_bad++;
            return;
        }
        g_rx++;
        std::string err = host_apply_draft(from, req.skip != 0, req.item_id);
        if (!err.empty())
            send_event_to(from, wire::rivals::E_DRAFT, from, 0, -1, "Rivals: " + err);
    } else {
        g_bad++;
    }
}

void on_player_join(uint8_t id)
{
    if (!g_rivals_core_fix || !is_host())
        return;
    if (valid_player(id)) {
        g_state.race.players[id].player_id = id;
        g_state.race.players[id].present = true;
    }
    apply_persisted_for_present_players();
    host_broadcast(true);
    sync_space_rivalry();
}

void on_player_leave(uint8_t id)
{
    if (!g_rivals_core_fix || !is_host())
        return;
    bool race_ended = false;
    bool draft_ended = false;
    if (valid_player(id)) {
        bool pres[kMaxPlayers];
        fill_present(pres);
        for (int i = 0; i < kMaxPlayers; i++)
            g_state.race.players[i].present = pres[i];
        race_ended = g_state.race.on_player_leave(id, active_player_count());
        if (g_state.draft.active) {
            bool was_active = g_state.draft.active;
            g_state.draft.skip(id);
            draft_ended = was_active && !g_state.draft.active;
        }
    }
    host_broadcast(true);
    if (race_ended) {
        std::string msg = "All remaining players finished! Ready to advance to the next stage.";
        emit_notice(msg);
        send_event_to_guests(wire::rivals::E_RACE, g_state.race.winner, 0, 0, msg);
        if (auto* hk = active_stage_hooks(g_state.race.stage); hk && hk->on_stage_race_end)
            hk->on_stage_race_end(g_state.race.winner);
    }
    if (draft_ended) {
        std::string msg = "Draft finished!";
        emit_notice(msg);
        send_event_to_guests(wire::rivals::E_DRAFT, g_state.draft.winner, 0, g_state.draft.winner_payout, msg);
    }
}

LIFECYCLE_HOOK("rivals.player", lifecycle::kPlayerJoin | lifecycle::kPlayerLeave, 0,
               [](const lifecycle::Ctx& c) {
                   if (c.event == lifecycle::kPlayerJoin)
                       on_player_join(c.player);
                   else
                       on_player_leave(c.player);
               },
               false);

void on_stage_change(uint32_t from_mode, uint32_t to_mode)
{
    (void)from_mode;
    if (!g_rivals_core_fix)
        return;
    Stage to_s = stage_from_mode(to_mode);
    if (valid_stage(to_s)) {
        // BUG-1551: compare against g_state.race.stage (not from_mode) so Stage -> Editor -> Stage never wipes race/inf.
        if (is_host()) {
            bool pres[kMaxPlayers];
            fill_present(pres);
            if (g_state.on_stage_enter(to_s, pres))
                host_broadcast(true);
        }
        if (to_s == Stage::Space)
            sync_space_rivalry();
    }
}

// ---- Public API ----
bool enabled() { return g_rivals_core_fix; }
Stage current_stage() { return C.mode ? stage_from_mode(C.mode()) : Stage::None; }
bool stage_is_rivals(Stage s)
{
    return g_rivals_core_fix && !(g_protect_together_fix && g_together_save_suppressed) && g_state.config.stage_is_rivals(s);
}
bool active_in_stage() { return stage_is_rivals(current_stage()); }
bool active_in_mode(uint32_t mode) { return stage_is_rivals(stage_from_mode(mode)); }
const Config& config() { return g_state.config; }
bool lobby_any_rivals() { return g_rivals_core_fix && g_lobby_config.any_rivals(); }
Preset lobby_preset() { return g_lobby_config.preset(); }
BaseDistance base_distance() { return g_state.config.base_distance; }

bool set_stage_mode(Stage s, StageMode m)
{
    if (!g_rivals_core_fix || !valid_stage(s))
        return false;
    if (connected() && !is_host())
        return false;
    g_together_save_suppressed = false;
    g_state.config.set_stage_mode(s, m);
    g_lobby_config.set_stage_mode(s, m);
    if (is_host()) {
        host_broadcast(true);
        std::string msg = std::string("Stage ") + stage_name(s) + " set to " + mode_name(m);
        send_event_to_guests(wire::rivals::E_CONFIG, (uint8_t)s, (uint8_t)m, 0, msg);
        if (s == Stage::Space)
            sync_space_rivalry();
    }
    return true;
}

bool set_preset(Preset p)
{
    if (!g_rivals_core_fix || (connected() && !is_host()))
        return false;
    g_together_save_suppressed = false;
    g_state.config.apply_preset(p);
    g_lobby_config.apply_preset(p);
    // BUG-1558: automatically apply matching default Settings preset for All rivals / All together (never Custom)
    if (p == Preset::AllRivals || p == Preset::AllTogether)
        settings::apply_mode_preset(p == Preset::AllRivals);
    if (is_host()) {
        host_broadcast(true);
        std::string msg = std::string("Rivals preset: ") + preset_name(p);
        send_event_to_guests(wire::rivals::E_CONFIG, 255, (uint8_t)p, 0, msg);
        sync_space_rivalry();
    }
    return true;
}

bool set_catchup_secs(uint16_t secs)
{
    if (!g_rivals_core_fix || (connected() && !is_host()))
        return false;
    g_state.config.catchup_secs = secs;
    g_state.config.clamp();
    g_lobby_config.catchup_secs = g_state.config.catchup_secs;
    if (is_host())
        host_broadcast(true);
    return true;
}

bool set_draft_picks(uint8_t picks)
{
    if (!g_rivals_core_fix || (connected() && !is_host()))
        return false;
    g_state.config.draft_picks = picks;
    g_state.config.clamp();
    g_lobby_config.draft_picks = g_state.config.draft_picks;
    if (is_host())
        host_broadcast(true);
    return true;
}

bool set_base_distance(BaseDistance d)
{
    if (!g_rivals_core_fix || (connected() && !is_host()))
        return false;
    g_state.config.base_distance = d;
    g_state.config.clamp();
    g_lobby_config.base_distance = g_state.config.base_distance;
    if (is_host())
        host_broadcast(true);
    return true;
}

Relation relation(uint8_t p1, uint8_t p2)
{
    if (!g_rivals_core_fix)
        return Relation::Neutral;
    return g_state.rel.get(p1, p2);
}

bool at_war(uint8_t p1, uint8_t p2) { return relation(p1, p2) == Relation::War; }
bool allied(uint8_t p1, uint8_t p2) { return relation(p1, p2) == Relation::Ally; }

std::string request_relation(uint8_t target, RelAction action)
{
    if (!g_rivals_core_fix)
        return "rivals.core is off";
    if (!valid_player(target))
        return "invalid target";
    if (is_host())
        return host_apply_relation(0, target, action);
    if (is_guest()) {
        if (!peer_reads_rivals(0))
            return "host does not support MSG_RIVALS";
        wire::rivals::RelReq req{(uint8_t)action, target};
        net::Writer w;
        w.u8(wire::rivals::K_REL_REQ);
        req.write(w);
        C.sess->send_reliable_to(0, net::MSG_RIVALS, w);
        g_tx++;
        return "";
    }
    return "not connected";
}

void set_player_progress(uint8_t player_id, const PlayerProgress& p)
{
    if (!g_rivals_core_fix || !valid_player(player_id))
        return;
    if (connected() && !is_host())
        return;
    if (g_state.race.stage == Stage::None)
        g_state.race.stage = current_stage();
    g_state.race.set_progress(player_id, p);
    if (is_host())
        host_broadcast(true);
}

uint8_t report_stage_complete(uint8_t player_id)
{
    if (!g_rivals_core_fix || !valid_player(player_id))
        return 0;
    if (connected() && !is_host())
        return 0;
    if (g_state.race.stage == Stage::None)
        g_state.race.stage = current_stage();
    // Idempotent: never re-award Chronicle points or re-emit stage win notices for a player already ranked.
    if (g_state.race.players[player_id].finish_rank != 0)
        return g_state.race.players[player_id].finish_rank;
    bool pres[kMaxPlayers];
    fill_present(pres);
    for (int i = 0; i < kMaxPlayers; i++)
        g_state.race.players[i].present = pres[i];
    double cu = (g_fast_timers && g_fast_catchup_secs > 0.0) ? g_fast_catchup_secs : (double)g_state.config.catchup_secs;
    uint8_t rank = g_state.race.report_complete(player_id, active_player_count(), cu, now_s());
    if (rank > 0) {
        int32_t pts = rank == 1 ? kPtsStageWin : (rank == 2 ? kPtsSecond : (rank == 3 ? kPtsThird : 0));
        if (pts > 0) {
            const char* reason = rank == 1 ? "stage win" : (rank == 2 ? "2nd place" : "3rd place");
            if (g_state.chronicle.add_points(player_id, g_state.race.stage, pts, reason) && is_host() &&
                !g_state.chronicle.history.empty()) {
                g_state.chronicle.history.back().player_key = store::key_of(player_id);
            }
        }
        char b[200];
        if (rank == 1 && g_state.race.catchup_active) {
            if (g_state.race.stage == Stage::Tribe) {
                const auto& wp = g_state.race.players[player_id];
                int need = wp.extra >= 1.0f ? (int)wp.extra : std::max(1, (int)wp.stat_b / 2 + 1);
                snprintf(b, sizeof b, "%s %s %s (%.0f/%d, >50%% NPC)! %.0f %s", name_of(player_id).c_str(),
                         tr("wins the", "побеждает на этапе"), tr_stage(g_state.race.stage), wp.primary, need, cu,
                         tr("s left to catch up", "с на завершение"));
            } else {
                snprintf(b, sizeof b, "%s wins the %s stage! %.0f s left to catch up", name_of(player_id).c_str(),
                         stage_name(g_state.race.stage), cu);
            }
        } else {
            snprintf(b, sizeof b, "%s finished the %s stage (#%u)", name_of(player_id).c_str(),
                     stage_name(g_state.race.stage), (unsigned)rank);
        }
        if (is_host()) {
            host_broadcast(true);
            emit_notice(b);
            send_event_to_guests(wire::rivals::E_RACE, player_id, rank, (int32_t)cu, b);
            if (g_state.race.stage_finished) {
                if (auto* hk = active_stage_hooks(g_state.race.stage); hk && hk->on_stage_race_end)
                    hk->on_stage_race_end(g_state.race.winner);
            }
        }
    }
    return rank;
}

bool is_catchup_active() { return g_rivals_core_fix && g_state.race.catchup_active; }
double catchup_remaining() { return g_rivals_core_fix && g_state.race.catchup_active ? g_state.race.catchup_remaining_secs : 0.0; }
const Race& race_state() { return g_state.race; }

void grant_influence(uint8_t player_id, int32_t amount, const std::string& reason)
{
    if (!g_rivals_core_fix || !valid_player(player_id) || amount <= 0)
        return;
    if (connected() && !is_host())
        return;
    if (g_state.inf.earn(player_id, amount) && is_host()) {
        host_broadcast(true);
        if (!reason.empty()) {
            char b[128];
            snprintf(b, sizeof b, "%s +%d influence (%s)", name_of(player_id).c_str(), (int)amount, reason.c_str());
            send_event_to_guests(wire::rivals::E_INFLUENCE, player_id, 255, amount, b);
        }
    }
}

void set_npc_influence_callback(NpcInfluenceCallback cb) { g_npc_cb = std::move(cb); }

void register_npc_influence_callback(Stage stage, NpcInfluenceCallback cb)
{
    int idx = (int)stage;
    if (idx >= 0 && idx < kStages)
        g_npc_stage_cb[idx] = std::move(cb);
}

void register_faction_handler(Stage stage, FactionMsgHandler cb)
{
    int idx = (int)stage;
    if (idx >= 0 && idx < kStages)
        g_faction_cb[idx] = std::move(cb);
}

void register_stage_hooks(Stage stage, StageHooks hooks)
{
    int idx = (int)stage;
    if (idx >= 0 && idx < kStages)
        g_stage_hooks[idx] = std::move(hooks);
}

bool peer_reads_faction(uint8_t peer_id)
{
    if (!C.sess)
        return false;
    bool has = false;
    std::vector<net::FamilyRev> fams;
    if (is_host()) {
        if (!C.sess->peer_families(peer_id, has, fams))
            return false;
        return wire::rivals_faction::peer_reads(has, fams);
    }
    if (!C.sess->host_families(has, fams))
        return false;
    return wire::rivals_faction::peer_reads(has, fams);
}

bool send_faction_to(uint8_t target, Stage stage, uint8_t subkind, const net::Writer& body)
{
    if (!g_rivals_core_fix || !C.sess || !connected() || (int)stage >= kStages || subkind == 0)
        return false;
    if (!peer_reads_faction(target))
        return false;
    net::Writer w;
    wire::rivals_faction::write_header(w, stage, subkind);
    if (!body.data.empty())
        w.raw(body.data.data(), body.data.size());
    C.sess->send_reliable_to(target, net::MSG_RIVALS_FACTION, w);
    g_tx++;
    return true;
}

void broadcast_faction(Stage stage, uint8_t subkind, const net::Writer& body, uint8_t except)
{
    if (!g_rivals_core_fix || !C.sess || !is_host() || (int)stage >= kStages || subkind == 0)
        return;
    net::Writer w;
    wire::rivals_faction::write_header(w, stage, subkind);
    if (!body.data.empty())
        w.raw(body.data.data(), body.data.size());
    for (const auto& p : C.sess->players()) {
        if (p.id == 0 || p.id == except || !peer_reads_faction(p.id))
            continue;
        C.sess->send_reliable_to(p.id, net::MSG_RIVALS_FACTION, w);
        g_tx++;
    }
}

void on_faction_message(uint8_t from, net::Reader& r)
{
    if (!g_rivals_core_fix)
        return;
    Stage st = Stage::None;
    uint8_t subkind = 0;
    if (!wire::rivals_faction::read_header(r, st, subkind)) {
        g_bad++;
        return;
    }
    int idx = (int)st;
    if (idx < 0 || idx >= kStages || !g_faction_cb[idx])
        return;
    g_rx++;
    g_faction_cb[idx](from, subkind, r);
}

std::string request_influence(InfluenceAction action, uint32_t npc_id, uint8_t rival_id)
{
    if (!g_rivals_core_fix)
        return "rivals.core is off";
    if (is_host())
        return host_apply_influence(0, action, npc_id, rival_id);
    if (is_guest()) {
        if (!peer_reads_rivals(0))
            return "host does not support MSG_RIVALS";
        wire::rivals::InfReq req{(uint8_t)action, npc_id, rival_id};
        net::Writer w;
        w.u8(wire::rivals::K_INF_REQ);
        req.write(w);
        C.sess->send_reliable_to(0, net::MSG_RIVALS, w);
        g_tx++;
        return "";
    }
    return "not connected";
}

int32_t influence_balance(uint8_t player_id)
{
    return g_rivals_core_fix && valid_player(player_id) ? g_state.inf.balance[player_id] : 0;
}

bool is_npc_protected(uint8_t owner, uint32_t npc_id)
{
    return g_rivals_core_fix && g_state.inf.is_protected(owner, npc_id, now_s());
}

bool start_draft(uint8_t winner, const std::vector<Draft::Item>& items)
{
    if (!g_rivals_core_fix || !valid_player(winner))
        return false;
    if (connected() && !is_host())
        return false;
    uint8_t ranks[kMaxPlayers] = {};
    for (int i = 0; i < kMaxPlayers; i++)
        ranks[i] = g_state.race.players[i].finish_rank;
    ranks[winner] = 1;
    bool pres[kMaxPlayers] = {};
    fill_present(pres);
    Stage st = g_state.race.stage != Stage::None ? g_state.race.stage : current_stage();
    if (!g_state.draft.start(st, winner, items, g_state.config.draft_picks, ranks, pres, now_s()))
        return false;
    if (is_host()) {
        host_broadcast(true);
        char b[128];
        snprintf(b, sizeof b, "Draft started! Copy items from %s (%.0f s)", name_of(winner).c_str(),
                 g_state.draft.timeout_secs);
        emit_notice(b);
        send_event_to_guests(wire::rivals::E_DRAFT, winner, 0, (int32_t)items.size(), b);
    }
    return true;
}

std::string request_draft_pick(uint32_t item_id)
{
    if (!g_rivals_core_fix)
        return "rivals.core is off";
    if (is_host())
        return host_apply_draft(0, false, item_id);
    if (is_guest()) {
        if (!peer_reads_rivals(0))
            return "host does not support MSG_RIVALS";
        wire::rivals::DraftReq req{0, item_id};
        net::Writer w;
        w.u8(wire::rivals::K_DRAFT_REQ);
        req.write(w);
        C.sess->send_reliable_to(0, net::MSG_RIVALS, w);
        g_tx++;
        return "";
    }
    return "not connected";
}

std::string request_draft_skip()
{
    if (!g_rivals_core_fix)
        return "rivals.core is off";
    if (is_host())
        return host_apply_draft(0, true, 0);
    if (is_guest()) {
        if (!peer_reads_rivals(0))
            return "host does not support MSG_RIVALS";
        wire::rivals::DraftReq req{1, 0};
        net::Writer w;
        w.u8(wire::rivals::K_DRAFT_REQ);
        req.write(w);
        C.sess->send_reliable_to(0, net::MSG_RIVALS, w);
        g_tx++;
        return "";
    }
    return "not connected";
}

int32_t take_pending_draft_payout(uint8_t player_id)
{
    if (!g_rivals_core_fix)
        return 0;
    int32_t v = g_state.draft.take_pending_payout(player_id);
    if (v != 0 && is_host())
        host_broadcast(true);
    return v;
}

int32_t take_pending_draft_payout_for(Stage expected_stage, uint8_t player_id)
{
    if (!g_rivals_core_fix)
        return 0;
    int32_t v = g_state.draft.take_pending_payout_for(expected_stage, player_id);
    if (v != 0 && is_host())
        host_broadcast(true);
    return v;
}

const Draft& draft_state() { return g_state.draft; }

void add_chronicle_points(uint8_t player_id, int32_t pts, const std::string& reason)
{
    if (!g_rivals_core_fix || !valid_player(player_id) || pts == 0)
        return;
    if (connected() && !is_host())
        return;
    Stage st = current_stage();
    if (g_state.chronicle.add_points(player_id, st, pts, reason) && is_host()) {
        if (!g_state.chronicle.history.empty())
            g_state.chronicle.history.back().player_key = store::key_of(player_id);
        host_broadcast(true);
        char b[128];
        snprintf(b, sizeof b, "Chronicle: %s +%d (%s)", name_of(player_id).c_str(), (int)pts, reason.c_str());
        send_event_to_guests(wire::rivals::E_CHRONICLE, player_id, (uint8_t)st, pts, b);
    }
}

const Chronicle& chronicle_state() { return g_state.chronicle; }

// ---- UI Rows for F9 "Rivals" / "Race" Page ----
std::vector<UiRow> ui_rows()
{
    std::vector<UiRow> rows;
    if (!g_rivals_core_fix) {
        rows.push_back({tr("Rivals systems are disabled (rivals.core off)", "Режим соперников отключён (rivals.core off)"),
                        "", nullptr, 0xFFE86A6A});
        return rows;
    }
    bool host = !connected() || is_host();
    uint32_t kGold = 0xFFF2D479, kGreen = 0xFF7BE882, kRed = 0xFFE86A6A, kYellow = 0xFFF2C85B, kDim = 0xFFB0BEC8;
    char buf[192];

    // Section 1: Stage Mode Table & Settings (11.2)
    snprintf(buf, sizeof buf, "%s: %s", tr("Stage modes preset", "Пресет режимов"),
             tr_preset(g_state.config.preset()));
    rows.push_back({buf,
                    host ? (g_state.config.preset() == Preset::AllRivals ? tr("All together", "Все вместе")
                                                                         : tr("All rivals", "Все соперники"))
                         : "",
                    host ? std::function<void()>([=] {
                        set_preset(g_state.config.preset() == Preset::AllRivals ? Preset::AllTogether : Preset::AllRivals);
                    })
                         : nullptr,
                    kGold});

    for (int i = 0; i < kStages; i++) {
        Stage s = (Stage)i;
        bool riv = g_state.config.stage_is_rivals(s);
        snprintf(buf, sizeof buf, "  %s: %s", tr_stage(s), tr_mode(g_state.config.stages[i]));
        rows.push_back({buf,
                        host ? (riv ? tr("Together", "Вместе") : tr("Rivals", "Соперники")) : "",
                        host ? std::function<void()>([s, riv] {
                            set_stage_mode(s, riv ? StageMode::Together : StageMode::Rivals);
                        })
                             : nullptr,
                        riv ? kYellow : 0});
    }

    snprintf(buf, sizeof buf, "%s: %u s", tr("Catch-up timer", "Таймер догона"), (unsigned)g_state.config.catchup_secs);
    rows.push_back({buf,
                    host ? tr("+60 s", "+60 с") : "",
                    host ? std::function<void()>([] {
                        uint16_t nxt = g_state.config.catchup_secs + 60;
                        if (nxt > kMaxCatchupSecs)
                            nxt = kMinCatchupSecs;
                        set_catchup_secs(nxt);
                    })
                         : nullptr,
                    kDim});

    snprintf(buf, sizeof buf, "%s: %u", tr("Draft picks", "Выборов драфта"), (unsigned)g_state.config.draft_picks);
    rows.push_back({buf,
                    host ? tr("Cycle", "Сменить") : "",
                    host ? std::function<void()>([] {
                        set_draft_picks((uint8_t)((g_state.config.draft_picks + 1) % (kMaxDraftPicks + 1)));
                    })
                         : nullptr,
                    kDim});

    snprintf(buf, sizeof buf, "%s: %s", tr("Base distance", "Дистанция баз"),
             tr_dist(g_state.config.base_distance));
    rows.push_back({buf,
                    host ? tr("Cycle", "Сменить") : "",
                    host ? std::function<void()>([] {
                        set_base_distance((BaseDistance)(((uint8_t)g_state.config.base_distance + 1) % 3));
                    })
                         : nullptr,
                    kDim});

    // Section 2: Race Progress & Catch-up Timer (11.5)
    if (connected()) {
        Stage cur_st = current_stage();
        if (g_state.race.catchup_active) {
            snprintf(buf, sizeof buf, "%s: %s %s (%.0f s)", tr("RACE", "ГОНКА"),
                     name_of(g_state.race.winner).c_str(), tr("won! Catch-up", "победил! Догон"),
                     g_state.race.catchup_remaining_secs);
            rows.push_back({buf, "", nullptr, kYellow});
        } else {
            snprintf(buf, sizeof buf, "%s (%s)", tr("Race progress", "Прогресс гонки"),
                     tr_stage(cur_st));
            rows.push_back({buf, "", nullptr, kGold});
        }
        // BUG-1575: human-readable per-stage race progress instead of raw "(a=X b=Y)" debug counters
        for (const auto& p : C.sess->players()) {
            if (!valid_player(p.id))
                continue;
            const auto& pr = g_state.race.players[p.id];
            char rnk[24] = "";
            if (pr.finish_rank > 0)
                snprintf(rnk, sizeof rnk, " [#%u]", (unsigned)pr.finish_rank);
            if (cur_st == Stage::Tribe) {
                int need = pr.extra >= 1.0f ? (int)pr.extra : std::max(1, (int)pr.stat_b / 2 + 1);
                snprintf(buf, sizeof buf, "  %s: %.0f/%d (>50%% NPC) | %s %.0f | %u%s", p.name.c_str(), pr.primary, need,
                         tr("Food", "Еда"), pr.secondary, (unsigned)pr.stat_a, rnk);
            } else if (cur_st == Stage::Civ) {
                snprintf(buf, sizeof buf, "  %s: %.0f/%u | $%.0f%s", p.name.c_str(), pr.primary,
                         std::max(1u, (unsigned)pr.stat_b), pr.secondary, rnk);
            } else if (cur_st == Stage::Space) {
                snprintf(buf, sizeof buf, "  %s: %.0f | $%.0f%s", p.name.c_str(), pr.primary, pr.secondary, rnk);
            } else if (cur_st == Stage::Cell || cur_st == Stage::Creature) {
                snprintf(buf, sizeof buf, "  %s: %.0f%% | DNA %.0f%s", p.name.c_str(), pr.primary * 100.0f,
                         pr.secondary, rnk);
            } else {
                snprintf(buf, sizeof buf, "  %s: %.0f%s", p.name.c_str(), pr.primary, rnk);
            }
            rows.push_back({buf, "", nullptr, pr.finish_rank == 1 ? kGreen : 0});
        }

        auto report_ui_err = [](const std::string& err) {
            if (!err.empty())
                emit_notice(std::string(tr("Rivals: ", "Соперники: ")) + localize_error_text(err));
        };

        // Section 3: Relations (11.4)
        rows.push_back({tr("Player relations", "Отношения игроков"), "", nullptr, kGold});
        uint8_t me = my_id();
        if (valid_player(me)) {
            for (const auto& p : C.sess->players()) {
                if (!valid_player(p.id) || p.id == me)
                    continue;
                Relation rel = g_state.rel.get(me, p.id);
                const BreakNotice* bn = g_state.rel.find_break_notice(me, p.id);
                bool in_ally = g_state.rel.has_proposal(p.id, me, ProposalKind::Ally);
                bool in_peace = g_state.rel.has_proposal(p.id, me, ProposalKind::Peace);
                std::string btn;
                RelAction act = RelAction::ProposeAlly;
                if (bn) {
                    snprintf(buf, sizeof buf, "  %s: %s (war in %.0f s)", p.name.c_str(), tr_rel(rel),
                             bn->remaining_secs);
                } else if (in_ally) {
                    snprintf(buf, sizeof buf, "  %s: %s (%s)", p.name.c_str(), tr_rel(rel),
                             tr("ally offered", "предложен союз"));
                    btn = tr("Accept ally", "Принять союз");
                    act = RelAction::AcceptAlly;
                } else if (in_peace) {
                    snprintf(buf, sizeof buf, "  %s: %s (%s)", p.name.c_str(), tr_rel(rel),
                             tr("peace offered", "предложен мир"));
                    btn = tr("Accept peace", "Принять мир");
                    act = RelAction::AcceptPeace;
                } else if (rel == Relation::Neutral) {
                    snprintf(buf, sizeof buf, "  %s: %s", p.name.c_str(), tr_rel(rel));
                    btn = tr("Propose ally", "Союз");
                    act = RelAction::ProposeAlly;
                } else if (rel == Relation::Ally) {
                    snprintf(buf, sizeof buf, "  %s: %s", p.name.c_str(), tr_rel(rel));
                    btn = tr("Break ally", "Разорвать");
                    act = RelAction::BreakAlly;
                } else {
                    snprintf(buf, sizeof buf, "  %s: %s", p.name.c_str(), tr_rel(rel));
                    btn = tr("Propose peace", "Мир");
                    act = RelAction::ProposePeace;
                }
                uint8_t tid = p.id;
                rows.push_back({buf, btn,
                                btn.empty() ? nullptr : std::function<void()>([tid, act, report_ui_err] {
                                    report_ui_err(request_relation(tid, act));
                                }),
                                rel == Relation::War ? kRed : (rel == Relation::Ally ? kGreen : 0)});
                if (in_ally && !bn) {
                    snprintf(buf, sizeof buf, "    %s %s", tr("Ally offer from", "Предложение союза:"), p.name.c_str());
                    rows.push_back({buf, tr("Decline", "Отклонить"),
                                    [tid, report_ui_err] { report_ui_err(request_relation(tid, RelAction::DeclineAlly)); },
                                    kDim});
                } else if (in_peace && !bn) {
                    snprintf(buf, sizeof buf, "    %s %s", tr("Peace offer from", "Предложение мира:"), p.name.c_str());
                    rows.push_back({buf, tr("Decline", "Отклонить"),
                                    [tid, report_ui_err] { report_ui_err(request_relation(tid, RelAction::DeclinePeace)); },
                                    kDim});
                }
                if (rel == Relation::Neutral && !bn) {
                    snprintf(buf, sizeof buf, "    %s %s", tr("Hostility:", "Вражда:"), p.name.c_str());
                    rows.push_back({buf, tr("Declare war", "Война"),
                                    [tid, report_ui_err] { report_ui_err(request_relation(tid, RelAction::DeclareWar)); },
                                    kRed});
                }
            }
        }

        // Section 4: Influence & NPC Diplomacy (11.4, BUG-1551, BUG-1579)
        Stage st = current_stage();
        if (st == Stage::Cell) {
            rows.push_back({tr("Influence: not used in Cell stage", "Влияние: не используется на этапе Клетки"),
                            "", nullptr, kDim});
        } else if (!valid_stage(st)) {
            if (valid_player(me)) {
                int32_t my_inf = influence_balance(me);
                snprintf(buf, sizeof buf, "%s: %d (%s)", tr("Influence", "Влияние"), (int)my_inf,
                         tr("available in active stage", "доступно на активном этапе"));
                rows.push_back({buf, "", nullptr, kDim});
            }
        } else if (valid_player(me)) {
            int32_t my_inf = influence_balance(me);
            int32_t next_cost = stage_influence_buy_price(st, g_state.inf.buy_cost(me));
            snprintf(buf, sizeof buf, "%s: %d | %s %d | %s %d | %s %d", tr("Influence", "Влияние"), (int)my_inf,
                     tr("Turn", "Ссора"), (int)kCostTurnAgainst, tr("Protect", "Защита"), (int)kCostProtect,
                     tr("Smooth", "Мир"), (int)kCostSmoothOver);
            char bbtn[64];
            snprintf(bbtn, sizeof bbtn, "%s (%d %s)", tr("Buy +25", "Купить +25"), (int)next_cost,
                     stage_currency_label(st));
            rows.push_back({buf, bbtn,
                            [report_ui_err] { report_ui_err(request_influence(InfluenceAction::Buy, 0, 255)); }, kGold});
        }

        // Section 5: Active Draft (11.6)
        if (g_state.draft.active && valid_player(me)) {
            snprintf(buf, sizeof buf, "%s: %s (%.0f s, picks=%u, pay=%d)", tr("DRAFT", "ДРАФТ"),
                     name_of(g_state.draft.winner).c_str(), g_state.draft.remaining_secs,
                     (unsigned)g_state.draft.players[me].picks_left, (int)g_state.draft.winner_payout);
            bool can_skip = me != g_state.draft.winner && !g_state.draft.players[me].done;
            rows.push_back({buf, can_skip ? tr("Skip", "Пропустить") : "",
                            can_skip ? std::function<void()>([report_ui_err] { report_ui_err(request_draft_skip()); })
                                     : nullptr,
                            kYellow});
            for (const auto& it : g_state.draft.items) {
                bool picked = std::find(g_state.draft.players[me].picked.begin(),
                                        g_state.draft.players[me].picked.end(), it.id) !=
                              g_state.draft.players[me].picked.end();
                bool can_pick = can_skip && g_state.draft.players[me].picks_left > 0 && !picked;
                snprintf(buf, sizeof buf, "  #%u %s%s", (unsigned)it.id, it.label.c_str(),
                         picked ? tr(" [copied]", " [взято]") : "");
                uint32_t iid = it.id;
                rows.push_back({buf, can_pick ? tr("Copy", "Взять") : "",
                                can_pick ? std::function<void()>([iid, report_ui_err] {
                                    report_ui_err(request_draft_pick(iid));
                                })
                                         : nullptr,
                                picked ? kGreen : 0});
            }
        }

        // Section 6: Chronicle (11.7)
        rows.push_back({tr("Chronicle (score)", "Хроника (очки)"), "", nullptr, kGold});
        uint8_t ldr = g_state.chronicle.leader();
        for (const auto& p : C.sess->players()) {
            if (!valid_player(p.id))
                continue;
            snprintf(buf, sizeof buf, "  %s: %d pts%s", p.name.c_str(), (int)g_state.chronicle.totals[p.id],
                     p.id == ldr ? " *" : "");
            rows.push_back({buf, "", nullptr, p.id == ldr ? kGreen : 0});
        }

        // Section 7: Stage-specific Rivals rows (Wave 2: Cell/Creature/Tribe/Civ/Space)
        if (auto* hk = active_stage_hooks(st); hk && hk->on_extra_ui_rows) {
            auto extra = hk->on_extra_ui_rows();
            for (auto& er : extra)
                rows.push_back(std::move(er));
        }

        // Section 8: Live NPC Influence targets (Turn / Protect / SmoothOver)
        if (st != Stage::Cell && st != Stage::None && valid_player(me)) {
            if (auto* hk = active_stage_hooks(st); hk && hk->on_npc_ui_list) {
                auto npcs = hk->on_npc_ui_list(me);
                std::vector<uint8_t> rivals_list;
                for (const auto& p : C.sess->players()) {
                    if (valid_player(p.id) && p.id != me)
                        rivals_list.push_back(p.id);
                }
                double now = now_s();
                auto prot_rem = [&](uint8_t owner, uint32_t nid) -> double {
                    for (const auto& pr : g_state.inf.protections) {
                        if (pr.owner == owner && pr.npc_id == nid) {
                            double rem = pr.expires_at > now ? (pr.expires_at - now) : pr.remaining_secs;
                            if (rem > 0.0)
                                return rem;
                        }
                    }
                    return 0.0;
                };
                if (!npcs.empty())
                    rows.push_back({tr("NPC Diplomacy & Influence", "Дипломатия NPC и Влияние"), "", nullptr, kGold});
                unsigned npc_num = 0;
                for (const auto& e : npcs) {
                    npc_num++;
                    uint32_t nid = e.npc_id;
                    std::string lbl = e.label;
                    if (lbl.empty()) {
                        char nb[48];
                        snprintf(nb, sizeof nb, "%s #%u", tr("Faction", "Фракция"), npc_num);
                        lbl = nb;
                    }
                    double my_prot = prot_rem(me, nid);
                    std::string status = e.allied ? tr("Allied", "Союз")
                                                  : (e.hostile ? tr("Hostile", "Враг") : tr("Neutral", "Нейтрал"));
                    if (my_prot > 0.0) {
                        char pb[48];
                        snprintf(pb, sizeof pb, ", %s %.0fs", tr("Protected", "Защита"), my_prot);
                        status += pb;
                    }
                    snprintf(buf, sizeof buf, "  %s [%s]", lbl.c_str(), status.c_str());
                    bool offer_protect = e.can_protect && my_prot <= 0.0;
                    bool offer_smooth = e.can_smooth;
                    uint32_t col = e.allied ? kGreen : (e.hostile ? kRed : 0);
                    if (offer_protect) {
                        rows.push_back({buf, tr("Protect (20)", "Защита (20)"),
                                        [nid, report_ui_err] {
                                            report_ui_err(request_influence(InfluenceAction::Protect, nid, 255));
                                        },
                                        col});
                    } else if (offer_smooth) {
                        rows.push_back({buf, tr("Smooth (25)", "Мир (25)"),
                                        [nid, report_ui_err] {
                                            report_ui_err(request_influence(InfluenceAction::SmoothOver, nid, 255));
                                        },
                                        col});
                    } else {
                        rows.push_back({buf, "", nullptr, col});
                    }
                    if (offer_protect && offer_smooth) {
                        snprintf(buf, sizeof buf, "    %s", tr("Smooth over relations", "Улучшить отношения"));
                        rows.push_back({buf, tr("Smooth (25)", "Мир (25)"),
                                        [nid, report_ui_err] {
                                            report_ui_err(request_influence(InfluenceAction::SmoothOver, nid, 255));
                                        },
                                        kDim});
                    }
                    if (e.can_turn && !rivals_list.empty()) {
                        for (uint8_t rid : rivals_list) {
                            double rprot = prot_rem(rid, nid);
                            std::string rname = name_of(rid);
                            if (rprot > 0.0) {
                                snprintf(buf, sizeof buf, "    %s %s (%s %.0fs)", tr("Turn vs", "Ссора с"),
                                         rname.c_str(), tr("Protected", "Защита"), rprot);
                                rows.push_back({buf, "", nullptr, kDim});
                            } else {
                                snprintf(buf, sizeof buf, "    %s %s", tr("Turn vs", "Ссора с"), rname.c_str());
                                rows.push_back({buf, tr("Turn (30)", "Ссора (30)"),
                                                [nid, rid, report_ui_err] {
                                                    report_ui_err(
                                                        request_influence(InfluenceAction::TurnAgainst, nid, rid));
                                                },
                                                kYellow});
                            }
                        }
                    }
                }
            }
        }
    }
    return rows;
}

std::string status_summary()
{
    char b[512];
    snprintf(b, sizeof b,
             "rivals: fix=%d rev=%u preset=%s stages=[cell=%s creature=%s tribe=%s civ=%s space=%s] "
             "catchup=%u draft_picks=%u dist=%s fasttimers=%d tx=%u rx=%u bad=%u store_r=%u store_w=%u\n",
             (int)g_rivals_core_fix, (unsigned)g_state.rev, preset_name(g_state.config.preset()),
             mode_name(g_state.config.stages[0]), mode_name(g_state.config.stages[1]),
             mode_name(g_state.config.stages[2]), mode_name(g_state.config.stages[3]),
             mode_name(g_state.config.stages[4]), (unsigned)g_state.config.catchup_secs,
             (unsigned)g_state.config.draft_picks, distance_name(g_state.config.base_distance),
             (int)g_fast_timers, g_tx, g_rx, g_bad, g_store_reads, g_store_writes);
    std::string out = b;

    // Relations
    out += "relations:";
    for (int i = 0; i < kRelPairs; i++) {
        auto [x, y] = pair_from_index(i);
        snprintf(b, sizeof b, " %u-%u=%s", (unsigned)x, (unsigned)y, relation_name(g_state.rel.get(x, y)));
        out += b;
    }
    if (!g_state.rel.proposals.empty()) {
        out += " props=[";
        for (size_t i = 0; i < g_state.rel.proposals.size(); i++) {
            const auto& p = g_state.rel.proposals[i];
            snprintf(b, sizeof b, "%s%u->%u:%s", i ? "," : "", (unsigned)p.from, (unsigned)p.to,
                     p.kind == ProposalKind::Ally ? "ally" : "peace");
            out += b;
        }
        out += "]";
    }
    if (!g_state.rel.break_notices.empty()) {
        out += " breaks=[";
        for (size_t i = 0; i < g_state.rel.break_notices.size(); i++) {
            const auto& bn = g_state.rel.break_notices[i];
            snprintf(b, sizeof b, "%s%u-%u:%.1fs", i ? "," : "", (unsigned)bn.a, (unsigned)bn.b, bn.remaining_secs);
            out += b;
        }
        out += "]";
    }
    out += "\n";

    // Influence
    snprintf(b, sizeof b, "influence: bal=[%d,%d,%d,%d] buys=[%u,%u,%u,%u] prots=%u\n",
             (int)g_state.inf.balance[0], (int)g_state.inf.balance[1], (int)g_state.inf.balance[2],
             (int)g_state.inf.balance[3], (unsigned)g_state.inf.buys[0], (unsigned)g_state.inf.buys[1],
             (unsigned)g_state.inf.buys[2], (unsigned)g_state.inf.buys[3], (unsigned)g_state.inf.protections.size());
    out += b;

    // Race
    snprintf(b, sizeof b, "race: stage=%s winner=%u finished=%u catchup=%d rem=%.1f done=%d ranks=[%u,%u,%u,%u]\n",
             stage_name(g_state.race.stage), (unsigned)g_state.race.winner, (unsigned)g_state.race.finished_count,
             (int)g_state.race.catchup_active, g_state.race.catchup_remaining_secs, (int)g_state.race.stage_finished,
             (unsigned)g_state.race.players[0].finish_rank, (unsigned)g_state.race.players[1].finish_rank,
             (unsigned)g_state.race.players[2].finish_rank, (unsigned)g_state.race.players[3].finish_rank);
    out += b;

    // Draft
    snprintf(b, sizeof b, "draft: active=%d winner=%u items=%u picks_left=[%u,%u,%u,%u] total_picks=%u payout=%d pending=[%d,%d,%d,%d] rem=%.1f\n",
             (int)g_state.draft.active, (unsigned)g_state.draft.winner, (unsigned)g_state.draft.items.size(),
             (unsigned)g_state.draft.players[0].picks_left, (unsigned)g_state.draft.players[1].picks_left,
             (unsigned)g_state.draft.players[2].picks_left, (unsigned)g_state.draft.players[3].picks_left,
             (unsigned)g_state.draft.total_picks, (int)g_state.draft.winner_payout,
             (int)g_state.draft.pending_payout[0], (int)g_state.draft.pending_payout[1],
             (int)g_state.draft.pending_payout[2], (int)g_state.draft.pending_payout[3], g_state.draft.remaining_secs);
    out += b;

    // Chronicle
    snprintf(b, sizeof b, "chronicle: totals=[%d,%d,%d,%d] leader=%u entries=%u\n",
             (int)g_state.chronicle.totals[0], (int)g_state.chronicle.totals[1], (int)g_state.chronicle.totals[2],
             (int)g_state.chronicle.totals[3], (unsigned)g_state.chronicle.leader(),
             (unsigned)g_state.chronicle.history.size());
    out += b;
    return out;
}

// ---- Console command: coop.rivals ----
int command(void*, int argc, const char* const* argv, SkOut* out)
{
    auto say = [&](const std::string& s) {
        if (H && out)
            H->out_printf(out, "%s", s.c_str());
    };
    if (!g_rivals_core_fix) {
        say("rivals: disabled (rivals.core is off)\n");
        return 0;
    }
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        say(status_summary());
        return 0;
    }
    std::string sub = argv[1];
    if (sub == "mode" && argc >= 4) {
        Stage s = parse_stage(argv[2]);
        if (!valid_stage(s)) {
            say("unknown stage (use cell|creature|tribe|civ|space)\n");
            return 0;
        }
        std::string mv = argv[3];
        StageMode m = (mv == "rivals" || mv == "1") ? StageMode::Rivals : StageMode::Together;
        if (!set_stage_mode(s, m)) {
            say("only the host can change stage modes\n");
            return 0;
        }
        say(std::string("stage ") + stage_name(s) + " = " + mode_name(m) + "\n");
        return 0;
    }
    if (sub == "preset" && argc >= 3) {
        std::string pv = argv[2];
        Preset p = (pv == "all_rivals" || pv == "rivals") ? Preset::AllRivals : Preset::AllTogether;
        if (!set_preset(p)) {
            say("only the host can change preset\n");
            return 0;
        }
        say(std::string("preset = ") + preset_name(p) + "\n");
        return 0;
    }
    if (sub == "catchup" && argc >= 3) {
        uint16_t s = (uint16_t)std::max(0, atoi(argv[2]));
        if (!set_catchup_secs(s)) {
            say("only the host can change catchup timer\n");
            return 0;
        }
        say("catchup = " + std::to_string(g_state.config.catchup_secs) + " s\n");
        return 0;
    }
    if (sub == "draftpicks" && argc >= 3) {
        uint8_t n = (uint8_t)std::max(0, atoi(argv[2]));
        if (!set_draft_picks(n)) {
            say("only the host can change draft picks\n");
            return 0;
        }
        say("draft_picks = " + std::to_string(g_state.config.draft_picks) + "\n");
        return 0;
    }
    if (sub == "distance" && argc >= 3) {
        std::string dv = argv[2];
        BaseDistance d = dv == "near" ? BaseDistance::Near : (dv == "far" ? BaseDistance::Far : BaseDistance::Middle);
        if (!set_base_distance(d)) {
            say("only the host can change base distance\n");
            return 0;
        }
        say(std::string("base_distance = ") + distance_name(g_state.config.base_distance) + "\n");
        return 0;
    }
    if ((sub == "ally" || sub == "war" || sub == "break" || sub == "peace" || sub == "accept" || sub == "decline") &&
        argc >= 3) {
        uint8_t tid = (uint8_t)atoi(argv[2]);
        RelAction act = RelAction::ProposeAlly;
        if (sub == "ally")
            act = RelAction::ProposeAlly;
        else if (sub == "war")
            act = RelAction::DeclareWar;
        else if (sub == "break")
            act = RelAction::BreakAlly;
        else if (sub == "peace")
            act = RelAction::ProposePeace;
        else if (sub == "accept") {
            uint8_t me = my_id();
            act = g_state.rel.has_proposal(tid, me, ProposalKind::Peace) ? RelAction::AcceptPeace : RelAction::AcceptAlly;
        } else {
            uint8_t me = my_id();
            act = g_state.rel.has_proposal(tid, me, ProposalKind::Peace) ? RelAction::DeclinePeace : RelAction::DeclineAlly;
        }
        std::string err = request_relation(tid, act);
        say(err.empty() ? "ok\n" : ("error: " + err + "\n"));
        return 0;
    }
    if (sub == "influence" && argc >= 3) {
        std::string act = argv[2];
        if (act == "add" && argc >= 5) {
            uint8_t pid = (uint8_t)atoi(argv[3]);
            int32_t amt = atoi(argv[4]);
            grant_influence(pid, amt, "console");
            say("influence[" + std::to_string(pid) + "] = " + std::to_string(influence_balance(pid)) + "\n");
            return 0;
        }
        if (act == "buy") {
            std::string err = request_influence(InfluenceAction::Buy, 0, 255);
            say(err.empty() ? "ok\n" : ("error: " + err + "\n"));
            return 0;
        }
        if (act == "turn" && argc >= 5) {
            uint32_t npc = (uint32_t)strtoul(argv[3], nullptr, 0);
            uint8_t rid = (uint8_t)atoi(argv[4]);
            std::string err = request_influence(InfluenceAction::TurnAgainst, npc, rid);
            say(err.empty() ? "ok\n" : ("error: " + err + "\n"));
            return 0;
        }
        if (act == "protect" && argc >= 4) {
            uint32_t npc = (uint32_t)strtoul(argv[3], nullptr, 0);
            std::string err = request_influence(InfluenceAction::Protect, npc, 255);
            say(err.empty() ? "ok\n" : ("error: " + err + "\n"));
            return 0;
        }
        if (act == "smooth" && argc >= 4) {
            uint32_t npc = (uint32_t)strtoul(argv[3], nullptr, 0);
            std::string err = request_influence(InfluenceAction::SmoothOver, npc, 255);
            say(err.empty() ? "ok\n" : ("error: " + err + "\n"));
            return 0;
        }
        say("usage: coop.rivals influence add <id> <amt> | buy | turn <npc> <rival> | protect <npc> | smooth <npc>\n");
        return 0;
    }
    if (sub == "progress" && argc >= 5) {
        PlayerProgress p;
        p.player_id = (uint8_t)atoi(argv[2]);
        p.present = true;
        p.primary = (float)atof(argv[3]);
        p.secondary = (float)atof(argv[4]);
        if (argc >= 6)
            p.stat_a = (uint16_t)atoi(argv[5]);
        if (argc >= 7)
            p.stat_b = (uint16_t)atoi(argv[6]);
        if (argc >= 8)
            p.extra = (float)atof(argv[7]);
        set_player_progress(p.player_id, p);
        say("ok\n");
        return 0;
    }
    if (sub == "win" && argc >= 3) {
        uint8_t pid = (uint8_t)atoi(argv[2]);
        uint8_t rnk = report_stage_complete(pid);
        say(rnk ? ("rank=" + std::to_string(rnk) + "\n") : "error: could not record win\n");
        return 0;
    }
    if (sub == "draft" && argc >= 3) {
        std::string da = argv[2];
        if (da == "start" && argc >= 4) {
            uint8_t w = (uint8_t)atoi(argv[3]);
            int cnt = argc >= 5 ? std::clamp(atoi(argv[4]), 1, kMaxDraftItems) : 4;
            std::vector<Draft::Item> items;
            for (int i = 1; i <= cnt; i++)
                items.push_back({(uint32_t)i, 1, "Item " + std::to_string(i)});
            bool ok = start_draft(w, items);
            say(ok ? "draft started\n" : "error: could not start draft\n");
            return 0;
        }
        if (da == "pick" && argc >= 4) {
            uint32_t iid = (uint32_t)strtoul(argv[3], nullptr, 0);
            std::string err = request_draft_pick(iid);
            say(err.empty() ? "ok\n" : ("error: " + err + "\n"));
            return 0;
        }
        if (da == "skip") {
            std::string err = request_draft_skip();
            say(err.empty() ? "ok\n" : ("error: " + err + "\n"));
            return 0;
        }
        if (da == "payout" && argc >= 4) {
            uint8_t pid = (uint8_t)atoi(argv[3]);
            int32_t v = take_pending_draft_payout(pid);
            say("payout=" + std::to_string(v) + "\n");
            return 0;
        }
        say("usage: coop.rivals draft start <winner> [count] | pick <item_id> | skip | payout <id>\n");
        return 0;
    }
    if (sub == "chronicle") {
        if (argc >= 6 && strcmp(argv[2], "add") == 0) {
            int raw_pid = atoi(argv[3]);
            if (raw_pid < 0 || raw_pid >= kMaxPlayers) {
                say("error: invalid player id (0..3)\n");
                return 0;
            }
            uint8_t pid = (uint8_t)raw_pid;
            int32_t pts = atoi(argv[4]);
            add_chronicle_points(pid, pts, argv[5]);
            say("chronicle[" + std::to_string(pid) + "] = " + std::to_string(g_state.chronicle.totals[pid]) + "\n");
            return 0;
        }
        char b[160];
        snprintf(b, sizeof b, "chronicle: totals=[%d,%d,%d,%d] leader=%u\n",
                 (int)g_state.chronicle.totals[0], (int)g_state.chronicle.totals[1],
                 (int)g_state.chronicle.totals[2], (int)g_state.chronicle.totals[3],
                 (unsigned)g_state.chronicle.leader());
        std::string s = b;
        for (const auto& e : g_state.chronicle.history) {
            snprintf(b, sizeof b, "  stage=%s p=%u pts=%+d reason=%s\n", stage_name(e.stage), (unsigned)e.player_id,
                     (int)e.points, e.reason.c_str());
            s += b;
        }
        say(s);
        return 0;
    }
    if (sub == "fasttimers" && argc >= 3) {
        g_fast_timers = atoi(argv[2]) != 0;
        g_state.rel.break_delay_secs = g_fast_timers ? 2.0 : kDefaultBreakNoticeSecs;
        g_state.rel.ally_break_grace_secs = g_fast_timers ? 0.0 : kDefaultAllyBreakGraceSecs;
        g_fast_catchup_secs = g_fast_timers ? 3.0 : 0.0;
        g_state.draft.timeout_secs = g_fast_timers ? 5.0 : kDefaultDraftTimeoutSecs;
        say(std::string("fasttimers = ") + (g_fast_timers ? "1 (break=2s catchup=3s draft=5s)" : "0 (defaults)") + "\n");
        return 0;
    }
    say("usage: coop.rivals [status | mode <stage> <together|rivals> | preset <all_together|all_rivals> | "
        "catchup <s> | draftpicks <n> | distance <near|middle|far> | ally|war|break|peace|accept|decline <id> | "
        "influence <add|buy|turn|protect|smooth> ... | progress <id> <p> <s> | win <id> | "
        "draft <start|pick|skip|payout> ... | chronicle [add <id> <pts> <reason>] | fasttimers <1|0>]\n");
    return 0;
}

} // namespace rivals
