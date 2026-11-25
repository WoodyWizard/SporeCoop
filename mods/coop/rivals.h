// Shared Rivals mode runtime module (WP-R1 of 0.8.0, BUG-0794, docs/11-rivals.md sections 11.2, 11.4-11.8).
// Public C++ API consumed by Wave 2 stage modules (WP-R2..R6), space_rivalry.cpp, coop_ui.cpp, vote.cpp, and coop.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/buffer.h"
#include "net/session.h"
#include "rivals_logic.h"
#include "sporekit.h"

namespace rivals {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void(const std::string&)> notice;
    std::function<uint32_t()> mode;
    std::function<bool()> russian;
};

// One row for the F9 "Rivals" / "Race" UI page (mirrors spaceext::Row).
struct UiRow {
    std::string text;
    std::string button;             // "" = label only
    std::function<void()> on_click; // invoked outside the frame by coop_ui
    uint32_t color = 0;             // 0 = default white
};

// Stage modules (WP-R3..R6) register this callback to apply NPC relation shifts when influence is spent.
// Returning false rejects the spend and refunds the influence.
using NpcInfluenceCallback =
    std::function<bool(Stage stage, InfluenceAction action, uint8_t actor, uint32_t npc_id, uint8_t rival_id)>;

// ---- lifecycle (wired in coop.cpp / session_glue.cpp) ----
void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);
void on_player_join(uint8_t id);
void on_player_leave(uint8_t id);
void on_stage_change(uint32_t from_mode, uint32_t to_mode);

// ---- COOP_FIX switch & mode queries (Wave 2 API) ----
bool enabled(); // rivals.core switch
Stage current_stage();
bool stage_is_rivals(Stage s);
bool active_in_stage();
bool active_in_mode(uint32_t mode);
const Config& config();
BaseDistance base_distance();

// Host-only config mutations (broadcasts snapshot and syncs Space rivalry when in Space)
bool set_stage_mode(Stage s, StageMode m);
bool set_preset(Preset p);
bool set_catchup_secs(uint16_t secs);
bool set_draft_picks(uint8_t picks);
bool set_base_distance(BaseDistance d);

// ---- 11.4 unified 4x4 player relations (Wave 2 API) ----
Relation relation(uint8_t p1, uint8_t p2);
bool at_war(uint8_t p1, uint8_t p2);
bool allied(uint8_t p1, uint8_t p2);
std::string request_relation(uint8_t target, RelAction action);
void sync_space_rivalry();

// ---- 11.5 stage race & catch-up timer (Wave 2 API) ----
void set_player_progress(uint8_t player_id, const PlayerProgress& p);
uint8_t report_stage_complete(uint8_t player_id);
bool is_catchup_active();
double catchup_remaining();
const Race& race_state();

// ---- 11.4 influence & NPC diplomacy (Wave 2 API) ----
void grant_influence(uint8_t player_id, int32_t amount, const std::string& reason = "");
void set_npc_influence_callback(NpcInfluenceCallback cb);
std::string request_influence(InfluenceAction action, uint32_t npc_id, uint8_t rival_id = 255);
int32_t influence_balance(uint8_t player_id);
bool is_npc_protected(uint8_t owner, uint32_t npc_id);

// ---- 11.6 stage-end winner draft (Wave 2 API) ----
bool start_draft(uint8_t winner, const std::vector<Draft::Item>& items);
std::string request_draft_pick(uint32_t item_id);
std::string request_draft_skip();
int32_t take_pending_draft_payout(uint8_t player_id);
const Draft& draft_state();

// ---- 11.7 Chronicle (Wave 2 API) ----
void add_chronicle_points(uint8_t player_id, int32_t pts, const std::string& reason);
const Chronicle& chronicle_state();

// ---- UI & console diagnostics ----
std::vector<UiRow> ui_rows();
std::string status_summary();
int command(void*, int argc, const char* const* argv, SkOut* out); // coop.rivals

} // namespace rivals
