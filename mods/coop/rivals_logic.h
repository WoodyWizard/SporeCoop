// Portable shared Rivals mode logic (WP-R1 of 0.8.0, BUG-0794, docs/11-rivals.md sections 11.2, 11.4-11.8).
// No Windows or SporeKit dependencies so native Linux unit tests and fuzzers cover every state transition.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rivals {

constexpr int kStages = 5;
constexpr int kMaxPlayers = 4;
constexpr int kRelPairs = 6; // upper triangle of 4x4: (0,1),(0,2),(0,3),(1,2),(1,3),(2,3)
constexpr int kMaxProposals = 16;
constexpr int kMaxBreakNotices = 6;
constexpr int kMaxProtections = 64;
constexpr int kMaxDraftItems = 16;
constexpr int kMaxChronicleHistory = 32;
constexpr size_t kMaxDraftLabelLen = 48;
constexpr size_t kMaxReasonLen = 64;

constexpr uint16_t kMinCatchupSecs = 60;
constexpr uint16_t kMaxCatchupSecs = 600;
constexpr uint16_t kDefaultCatchupSecs = 300;

constexpr uint8_t kMaxDraftPicks = 5;
constexpr uint8_t kDefaultDraftPicks = 3;

constexpr double kDefaultBreakNoticeSecs = 60.0;
constexpr double kDefaultAllyBreakGraceSecs = 2.5; // BUG-1579: prevent double-click on Accept Ally from immediately firing BreakAlly
constexpr double kDefaultProtectSecs = 600.0;
constexpr double kDefaultDraftTimeoutSecs = 60.0;

constexpr int32_t kDraftBasePayout = 100;
constexpr int32_t kDraftPayoutStep = 50;

constexpr int32_t kDefaultInfluenceBuyBase = 50;
constexpr int32_t kInfluenceBuyGain = 25;
constexpr int32_t kCostTurnAgainst = 30;
constexpr int32_t kCostProtect = 20;
constexpr int32_t kCostSmoothOver = 25;

// Chronicle point defaults (11.7)
constexpr int32_t kPtsStageWin = 100;
constexpr int32_t kPtsSecond = 60;
constexpr int32_t kPtsThird = 35;
constexpr int32_t kPtsNpcFaction = 15;
constexpr int32_t kPtsWarWon = 40;
constexpr int32_t kPtsSpaceColony = 10;
constexpr int32_t kPtsSpaceAlly = 20;
constexpr int32_t kPtsGalaxyCore = 250;

// Game mode IDs (matching modes.h)
constexpr uint32_t kModeCell = 0x1654C00;
constexpr uint32_t kModeCreature = 0x1654C01;
constexpr uint32_t kModeTribe = 0x1654C02;
constexpr uint32_t kModeCiv = 0x1654C04;
constexpr uint32_t kModeSpace = 0x1654C05;

enum class Stage : uint8_t {
    Cell = 0,
    Creature = 1,
    Tribe = 2,
    Civ = 3,
    Space = 4,
    None = 255,
};

enum class StageMode : uint8_t {
    Together = 0,
    Rivals = 1,
};

enum class Preset : uint8_t {
    AllTogether = 0,
    AllRivals = 1,
    Custom = 2,
};

enum class BaseDistance : uint8_t {
    Near = 0,
    Middle = 1,
    Far = 2,
};

enum class Relation : uint8_t {
    Neutral = 0,
    Ally = 1,
    War = 2,
};

enum class ProposalKind : uint8_t {
    Ally = 1,
    Peace = 2,
};

enum class RelAction : uint8_t {
    ProposeAlly = 1,
    AcceptAlly = 2,
    DeclineAlly = 3,
    ProposePeace = 4,
    AcceptPeace = 5,
    DeclinePeace = 6,
    DeclareWar = 7,
    BreakAlly = 8,
};

enum class InfluenceAction : uint8_t {
    Buy = 1,
    TurnAgainst = 2,
    Protect = 3,
    SmoothOver = 4,
};

inline bool valid_stage(Stage s) { return (uint8_t)s < kStages; }
inline bool valid_player(uint8_t id) { return id < kMaxPlayers; }

// BUG-1579: Influence purchase gate and per-stage display price scaling (Creature DNA 1x, Tribe food 1x,
// Civ Sporebucks 10x, Space Sporebucks 200x).
inline bool can_buy_influence_in_stage(Stage st, bool has_hook)
{
    return valid_stage(st) && st != Stage::Cell && has_hook;
}

inline int32_t stage_influence_buy_price(Stage st, int32_t base_price)
{
    if (base_price <= 0)
        return 0;
    int64_t mult = 1;
    if (st == Stage::Civ)
        mult = 10;
    else if (st == Stage::Space)
        mult = 200;
    int64_t res = (int64_t)base_price * mult;
    if (res > 99999999LL)
        res = 99999999LL;
    return (int32_t)res;
}

// BUG-1579: Decide how opening a world store affects the active Rivals config so a lobby Rivals preset
// never contaminates an existing Together save while remaining sticky for new worlds.
enum class WorldOpenAction : uint8_t {
    LoadStored = 0,
    SuppressTogetherSave = 1,
    ApplyLobbyConfig = 2,
};

inline WorldOpenAction classify_world_open(bool has_rivals_record, size_t existing_records,
                                           size_t existing_players, int64_t saved_ts)
{
    if (has_rivals_record)
        return WorldOpenAction::LoadStored;
    if (existing_records > 0 || existing_players > 0 || saved_ts != 0)
        return WorldOpenAction::SuppressTogetherSave;
    return WorldOpenAction::ApplyLobbyConfig;
}

inline Stage stage_from_mode(uint32_t mode)
{
    switch (mode) {
    case kModeCell: return Stage::Cell;
    case kModeCreature: return Stage::Creature;
    case kModeTribe: return Stage::Tribe;
    case kModeCiv: return Stage::Civ;
    case kModeSpace: return Stage::Space;
    default: return Stage::None;
    }
}

inline Stage next_stage(Stage s)
{
    switch (s) {
    case Stage::Cell: return Stage::Creature;
    case Stage::Creature: return Stage::Tribe;
    case Stage::Tribe: return Stage::Civ;
    case Stage::Civ: return Stage::Space;
    default: return Stage::None;
    }
}

inline const char* stage_name(Stage s, bool ru = false)
{
    switch (s) {
    case Stage::Cell: return ru ? "Клетка" : "Cell";
    case Stage::Creature: return ru ? "Существо" : "Creature";
    case Stage::Tribe: return ru ? "Племя" : "Tribe";
    case Stage::Civ: return ru ? "Цивилизация" : "Civilization";
    case Stage::Space: return ru ? "Космос" : "Space";
    default: return ru ? "Нет" : "None";
    }
}

inline Stage parse_stage(const std::string& s)
{
    std::string lo;
    lo.reserve(s.size());
    for (char c : s)
        lo.push_back((char)(c >= 'A' && c <= 'Z' ? c + 32 : c));
    if (lo == "cell" || lo == "0")
        return Stage::Cell;
    if (lo == "creature" || lo == "1")
        return Stage::Creature;
    if (lo == "tribe" || lo == "2")
        return Stage::Tribe;
    if (lo == "civ" || lo == "civilization" || lo == "3")
        return Stage::Civ;
    if (lo == "space" || lo == "4")
        return Stage::Space;
    return Stage::None;
}

inline const char* mode_name(StageMode m, bool ru = false)
{
    if (m == StageMode::Rivals)
        return ru ? "Соперники" : "Rivals";
    return ru ? "Вместе" : "Together";
}

inline const char* preset_name(Preset p, bool ru = false)
{
    switch (p) {
    case Preset::AllTogether: return ru ? "Все вместе" : "All together";
    case Preset::AllRivals: return ru ? "Все соперники" : "All rivals";
    default: return ru ? "Свой" : "Custom";
    }
}

inline const char* distance_name(BaseDistance d, bool ru = false)
{
    switch (d) {
    case BaseDistance::Near: return ru ? "Близко" : "Near";
    case BaseDistance::Far: return ru ? "Далеко" : "Far";
    default: return ru ? "Средне" : "Middle";
    }
}

inline const char* relation_name(Relation r, bool ru = false)
{
    switch (r) {
    case Relation::Ally: return ru ? "Союз" : "Ally";
    case Relation::War: return ru ? "Война" : "War";
    default: return ru ? "Нейтралитет" : "Neutral";
    }
}

inline int pair_index(uint8_t a, uint8_t b)
{
    if (a >= kMaxPlayers || b >= kMaxPlayers || a == b)
        return -1;
    if (a > b)
        std::swap(a, b);
    // (0,1)->0, (0,2)->1, (0,3)->2, (1,2)->3, (1,3)->4, (2,3)->5
    static constexpr int kIdx[4][4] = {
        {-1, 0, 1, 2},
        {0, -1, 3, 4},
        {1, 3, -1, 5},
        {2, 4, 5, -1},
    };
    return kIdx[a][b];
}

inline std::pair<uint8_t, uint8_t> pair_from_index(int idx)
{
    static constexpr std::pair<uint8_t, uint8_t> kPairs[kRelPairs] = {
        {0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3},
    };
    return (idx >= 0 && idx < kRelPairs) ? kPairs[idx] : std::pair<uint8_t, uint8_t>{0, 0};
}

// ---------------- 11.2 Stage Modes & Lobby Config ----------------
struct Config {
    StageMode stages[kStages] = {
        StageMode::Together, StageMode::Together, StageMode::Together, StageMode::Together, StageMode::Together,
    };
    uint16_t catchup_secs = kDefaultCatchupSecs;
    uint8_t draft_picks = kDefaultDraftPicks;
    BaseDistance base_distance = BaseDistance::Middle;

    void clamp()
    {
        for (int i = 0; i < kStages; i++)
            if ((uint8_t)stages[i] > 1)
                stages[i] = StageMode::Together;
        if (catchup_secs < kMinCatchupSecs)
            catchup_secs = kMinCatchupSecs;
        if (catchup_secs > kMaxCatchupSecs)
            catchup_secs = kMaxCatchupSecs;
        if (draft_picks > kMaxDraftPicks)
            draft_picks = kMaxDraftPicks;
        if ((uint8_t)base_distance > 2)
            base_distance = BaseDistance::Middle;
    }

    Preset preset() const
    {
        int r = 0;
        for (int i = 0; i < kStages; i++)
            r += (stages[i] == StageMode::Rivals);
        if (r == 0)
            return Preset::AllTogether;
        if (r == kStages)
            return Preset::AllRivals;
        return Preset::Custom;
    }

    void apply_preset(Preset p)
    {
        if (p == Preset::AllTogether) {
            for (int i = 0; i < kStages; i++)
                stages[i] = StageMode::Together;
        } else if (p == Preset::AllRivals) {
            for (int i = 0; i < kStages; i++)
                stages[i] = StageMode::Rivals;
        }
    }

    bool set_stage_mode(Stage s, StageMode m)
    {
        if (!valid_stage(s))
            return false;
        stages[(uint8_t)s] = m;
        return true;
    }

    StageMode stage_mode(Stage s) const { return valid_stage(s) ? stages[(uint8_t)s] : StageMode::Together; }
    bool stage_is_rivals(Stage s) const { return stage_mode(s) == StageMode::Rivals; }
    bool any_rivals() const { return preset() != Preset::AllTogether; }

    bool operator==(const Config& o) const
    {
        for (int i = 0; i < kStages; i++)
            if (stages[i] != o.stages[i])
                return false;
        return catchup_secs == o.catchup_secs && draft_picks == o.draft_picks && base_distance == o.base_distance;
    }
    bool operator!=(const Config& o) const { return !(*this == o); }
};

// ---------------- 11.4 Symmetric 4x4 Player Relations ----------------
struct Proposal {
    uint8_t from = 0;
    uint8_t to = 0;
    ProposalKind kind = ProposalKind::Ally;
    bool operator==(const Proposal& o) const { return from == o.from && to == o.to && kind == o.kind; }
};

struct BreakNotice {
    uint8_t a = 0; // a < b
    uint8_t b = 0;
    uint8_t initiator = 0;
    double expires_at = 0;
    double remaining_secs = 0;
};

struct Relations {
    Relation matrix[kMaxPlayers][kMaxPlayers] = {};
    std::vector<Proposal> proposals;
    std::vector<BreakNotice> break_notices;
    double break_delay_secs = kDefaultBreakNoticeSecs;
    double ally_break_grace_secs = kDefaultAllyBreakGraceSecs; // BUG-1579: 2.5 s debounce after forming alliance
    double allied_since[kMaxPlayers][kMaxPlayers] = {};
    bool has_allied_since[kMaxPlayers][kMaxPlayers] = {};

    void reset()
    {
        for (int i = 0; i < kMaxPlayers; i++)
            for (int j = 0; j < kMaxPlayers; j++) {
                matrix[i][j] = Relation::Neutral;
                allied_since[i][j] = 0.0;
                has_allied_since[i][j] = false;
            }
        proposals.clear();
        break_notices.clear();
    }

    Relation get(uint8_t a, uint8_t b) const
    {
        if (!valid_player(a) || !valid_player(b) || a == b)
            return Relation::Neutral;
        return matrix[a][b];
    }

    void drop_proposals(uint8_t a, uint8_t b)
    {
        proposals.erase(std::remove_if(proposals.begin(), proposals.end(),
                                       [a, b](const Proposal& p) {
                                           return (p.from == a && p.to == b) || (p.from == b && p.to == a);
                                       }),
                        proposals.end());
    }

    void drop_break_notice(uint8_t a, uint8_t b)
    {
        if (a > b)
            std::swap(a, b);
        break_notices.erase(
            std::remove_if(break_notices.begin(), break_notices.end(),
                           [a, b](const BreakNotice& n) { return n.a == a && n.b == b; }),
            break_notices.end());
    }

    bool set(uint8_t a, uint8_t b, Relation r)
    {
        if (!valid_player(a) || !valid_player(b) || a == b)
            return false;
        if ((uint8_t)r > 2)
            r = Relation::Neutral;
        matrix[a][b] = r;
        matrix[b][a] = r;
        if (r != Relation::Ally) {
            has_allied_since[a][b] = false;
            has_allied_since[b][a] = false;
        }
        drop_proposals(a, b);
        drop_break_notice(a, b);
        return true;
    }

    bool has_proposal(uint8_t from, uint8_t to, ProposalKind kind) const
    {
        for (const auto& p : proposals)
            if (p.from == from && p.to == to && p.kind == kind)
                return true;
        return false;
    }

    const BreakNotice* find_break_notice(uint8_t a, uint8_t b) const
    {
        if (a > b)
            std::swap(a, b);
        for (const auto& n : break_notices)
            if (n.a == a && n.b == b)
                return &n;
        return nullptr;
    }

    // Returns "" on success, or an error string describing why the action was rejected.
    std::string apply_action(uint8_t actor, uint8_t target, RelAction action, double now, std::string* event_out = nullptr)
    {
        if (!valid_player(actor) || !valid_player(target) || actor == target)
            return "invalid player";
        Relation cur = get(actor, target);
        auto set_ev = [&](const std::string& e) {
            if (event_out)
                *event_out = e;
        };
        switch (action) {
        case RelAction::ProposeAlly:
            if (cur != Relation::Neutral)
                return "can only propose alliance when neutral";
            if (has_proposal(target, actor, ProposalKind::Ally)) {
                set(actor, target, Relation::Ally);
                allied_since[actor][target] = allied_since[target][actor] = now;
                has_allied_since[actor][target] = has_allied_since[target][actor] = true;
                set_ev("allied");
                return "";
            }
            if (!has_proposal(actor, target, ProposalKind::Ally) && (int)proposals.size() < kMaxProposals)
                proposals.push_back({actor, target, ProposalKind::Ally});
            set_ev("proposed_ally");
            return "";
        case RelAction::AcceptAlly:
            if (cur != Relation::Neutral)
                return "not neutral";
            if (!has_proposal(target, actor, ProposalKind::Ally))
                return "no alliance proposal";
            set(actor, target, Relation::Ally);
            allied_since[actor][target] = allied_since[target][actor] = now;
            has_allied_since[actor][target] = has_allied_since[target][actor] = true;
            set_ev("allied");
            return "";
        case RelAction::DeclineAlly:
            if (!has_proposal(target, actor, ProposalKind::Ally))
                return "no alliance proposal";
            proposals.erase(std::remove_if(proposals.begin(), proposals.end(),
                                           [actor, target](const Proposal& p) {
                                               return p.from == target && p.to == actor && p.kind == ProposalKind::Ally;
                                           }),
                            proposals.end());
            set_ev("declined_ally");
            return "";
        case RelAction::DeclareWar:
            if (cur == Relation::War)
                return "already at war";
            if (cur == Relation::Ally)
                return "allied: must break alliance first (60 s notice)";
            set(actor, target, Relation::War);
            set_ev("war");
            return "";
        case RelAction::BreakAlly: {
            if (cur != Relation::Ally)
                return "not allied";
            // BUG-1579: 2.5 s grace window after forming an alliance so double-clicking Accept Ally
            // (which flips into the Break Ally button in place) cannot accidentally break the alliance.
            if (ally_break_grace_secs > 0.0 && has_allied_since[actor][target]) {
                double elapsed = now - allied_since[actor][target];
                if (elapsed >= 0.0 && elapsed < ally_break_grace_secs)
                    return "Alliance just formed, wait a moment";
            }
            uint8_t lo = actor < target ? actor : target;
            uint8_t hi = actor < target ? target : actor;
            if (find_break_notice(lo, hi))
                return "alliance break already pending";
            double delay = break_delay_secs > 0.0 ? break_delay_secs : 0.0;
            if (delay <= 0.0) {
                set(actor, target, Relation::War);
                set_ev("war");
                return "";
            }
            if ((int)break_notices.size() < kMaxBreakNotices)
                break_notices.push_back({lo, hi, actor, now + delay, delay});
            set_ev("break_pending");
            return "";
        }
        case RelAction::ProposePeace:
            if (cur != Relation::War)
                return "not at war";
            if (has_proposal(target, actor, ProposalKind::Peace)) {
                set(actor, target, Relation::Neutral);
                set_ev("peace");
                return "";
            }
            if (!has_proposal(actor, target, ProposalKind::Peace) && (int)proposals.size() < kMaxProposals)
                proposals.push_back({actor, target, ProposalKind::Peace});
            set_ev("proposed_peace");
            return "";
        case RelAction::AcceptPeace:
            if (cur != Relation::War)
                return "not at war";
            if (!has_proposal(target, actor, ProposalKind::Peace))
                return "no peace proposal";
            set(actor, target, Relation::Neutral);
            set_ev("peace");
            return "";
        case RelAction::DeclinePeace:
            if (!has_proposal(target, actor, ProposalKind::Peace))
                return "no peace proposal";
            proposals.erase(std::remove_if(proposals.begin(), proposals.end(),
                                           [actor, target](const Proposal& p) {
                                               return p.from == target && p.to == actor && p.kind == ProposalKind::Peace;
                                           }),
                            proposals.end());
            set_ev("declined_peace");
            return "";
        }
        return "unknown relation action";
    }

    // Updates remaining countdowns and transitions expired alliance breaks to War.
    std::vector<BreakNotice> tick(double now)
    {
        std::vector<BreakNotice> expired;
        std::vector<BreakNotice> kept;
        for (auto n : break_notices) {
            if (get(n.a, n.b) != Relation::Ally)
                continue;
            double rem = n.expires_at - now;
            if (rem <= 0.0) {
                n.remaining_secs = 0.0;
                expired.push_back(n);
            } else {
                n.remaining_secs = rem;
                kept.push_back(n);
            }
        }
        break_notices = std::move(kept);
        for (const auto& n : expired) {
            matrix[n.a][n.b] = Relation::War;
            matrix[n.b][n.a] = Relation::War;
            drop_proposals(n.a, n.b);
        }
        return expired;
    }
};

// ---------------- 11.4 Influence Ledger & NPC Protection ----------------
struct Protection {
    uint8_t owner = 0;
    uint32_t npc_id = 0;
    double expires_at = 0;
    double remaining_secs = 0;
};

struct Influence {
    int32_t balance[kMaxPlayers] = {};
    uint16_t buys[kMaxPlayers] = {};
    std::vector<Protection> protections;
    double protect_duration_secs = kDefaultProtectSecs;

    void reset_all()
    {
        for (int i = 0; i < kMaxPlayers; i++) {
            balance[i] = 0;
            buys[i] = 0;
        }
        protections.clear();
    }

    void reset_stage()
    {
        for (int i = 0; i < kMaxPlayers; i++)
            buys[i] = 0;
        protections.clear();
    }

    int32_t buy_cost(uint8_t player, int32_t base = kDefaultInfluenceBuyBase) const
    {
        if (!valid_player(player) || base <= 0)
            return 0;
        int64_t cost = (int64_t)base * (1 + (int64_t)buys[player]);
        if (cost > 1000000000LL)
            cost = 1000000000LL;
        return (int32_t)cost;
    }

    bool earn(uint8_t player, int32_t amount)
    {
        if (!valid_player(player) || amount <= 0)
            return false;
        int64_t sum = (int64_t)balance[player] + amount;
        balance[player] = sum > 1000000000LL ? 1000000000 : (int32_t)sum;
        return true;
    }

    bool buy(uint8_t player, int32_t& resource_cost_out, int32_t base = kDefaultInfluenceBuyBase,
             int32_t gain = kInfluenceBuyGain)
    {
        if (!valid_player(player) || base <= 0 || gain <= 0)
            return false;
        resource_cost_out = buy_cost(player, base);
        if (buys[player] < 65000)
            buys[player]++;
        earn(player, gain);
        return true;
    }

    bool is_protected(uint8_t owner, uint32_t npc_id, double now) const
    {
        if (!valid_player(owner))
            return false;
        for (const auto& p : protections)
            if (p.owner == owner && p.npc_id == npc_id && p.expires_at > now)
                return true;
        return false;
    }

    std::string spend_turn_against(uint8_t actor, uint32_t npc_id, uint8_t rival_id, double now,
                                   int32_t cost = kCostTurnAgainst)
    {
        if (!valid_player(actor) || !valid_player(rival_id) || actor == rival_id)
            return "invalid player";
        if (is_protected(rival_id, npc_id, now))
            return "rival bond is protected";
        if (balance[actor] < cost)
            return "not enough influence";
        balance[actor] -= cost;
        return "";
    }

    std::string can_spend_protect(uint8_t actor, int32_t cost = kCostProtect) const
    {
        if (!valid_player(actor))
            return "invalid player";
        if (balance[actor] < cost)
            return "not enough influence";
        return "";
    }

    std::string spend_protect(uint8_t actor, uint32_t npc_id, double now, int32_t cost = kCostProtect)
    {
        std::string err = can_spend_protect(actor, cost);
        if (!err.empty())
            return err;
        balance[actor] -= cost;
        double dur = protect_duration_secs > 0.0 ? protect_duration_secs : kDefaultProtectSecs;
        for (auto& p : protections) {
            if (p.owner == actor && p.npc_id == npc_id) {
                p.expires_at = now + dur;
                p.remaining_secs = dur;
                return "";
            }
        }
        if ((int)protections.size() >= kMaxProtections)
            protections.erase(protections.begin());
        protections.push_back({actor, npc_id, now + dur, dur});
        return "";
    }

    template <typename ValidateFn>
    std::string try_spend_protect(uint8_t actor, uint32_t npc_id, double now, ValidateFn&& validate,
                                  int32_t cost = kCostProtect)
    {
        std::string err = can_spend_protect(actor, cost);
        if (!err.empty())
            return err;
        if (!validate())
            return "npc rejected protect";
        return spend_protect(actor, npc_id, now, cost);
    }

    std::string spend_smooth_over(uint8_t actor, uint32_t npc_id, double /*now*/, int32_t cost = kCostSmoothOver)
    {
        (void)npc_id;
        if (!valid_player(actor))
            return "invalid player";
        if (balance[actor] < cost)
            return "not enough influence";
        balance[actor] -= cost;
        return "";
    }

    bool tick(double now)
    {
        bool pruned = false;
        std::vector<Protection> kept;
        for (auto p : protections) {
            double rem = p.expires_at - now;
            if (rem <= 0.0) {
                pruned = true;
            } else {
                p.remaining_secs = rem;
                kept.push_back(p);
            }
        }
        protections = std::move(kept);
        return pruned;
    }
};

// ---------------- 11.5 Stage Race & Catch-up Timer ----------------
struct PlayerProgress {
    uint8_t player_id = 0;
    bool present = false;
    uint8_t finish_rank = 0; // 0 = unfinished, 1..4 = finish placement
    float primary = 0.0f;    // Cell: bar 0..1; Creature: brain 0..5; Tribe: subjugated; Civ: cities; Space: colonies
    float secondary = 0.0f;  // Cell/Creature: DNA; Tribe: food; Civ/Space: money
    uint16_t stat_a = 0;     // Creature: allies; Tribe: members; Civ: vehicles; Space: stars
    uint16_t stat_b = 0;     // Creature: extinct; Tribe: tribes; Civ: allies; Space: allies
    float extra = 0.0f;      // Space: dist to core; or target count
};

struct Race {
    Stage stage = Stage::None;
    PlayerProgress players[kMaxPlayers] = {};
    uint8_t winner = 255;
    uint8_t finished_count = 0;
    bool catchup_active = false;
    bool stage_finished = false;
    double catchup_ends_at = 0;
    double catchup_remaining_secs = 0;

    void reset(Stage s = Stage::None)
    {
        stage = s;
        for (int i = 0; i < kMaxPlayers; i++) {
            players[i] = PlayerProgress{};
            players[i].player_id = (uint8_t)i;
        }
        winner = 255;
        finished_count = 0;
        catchup_active = false;
        stage_finished = false;
        catchup_ends_at = 0;
        catchup_remaining_secs = 0;
    }

    void set_progress(uint8_t id, const PlayerProgress& p)
    {
        if (!valid_player(id))
            return;
        uint8_t keep_rank = players[id].finish_rank;
        players[id] = p;
        players[id].player_id = id;
        players[id].present = true;
        if (keep_rank > 0 && players[id].finish_rank == 0)
            players[id].finish_rank = keep_rank;
    }

    int present_count() const
    {
        int n = 0;
        for (int i = 0; i < kMaxPlayers; i++)
            if (players[i].present)
                n++;
        return n;
    }

    int present_finished_count() const
    {
        int n = 0;
        for (int i = 0; i < kMaxPlayers; i++)
            if (players[i].present && players[i].finish_rank > 0)
                n++;
        return n;
    }

    // Re-evaluates whether all remaining active players have finished.
    // Returns true if this call ended an active catch-up timer.
    bool check_all_finished(int active_players)
    {
        int target = std::max(active_players, present_count());
        if (target <= 0)
            return false;
        if (present_finished_count() >= target) {
            bool was_catchup = catchup_active;
            catchup_active = false;
            catchup_remaining_secs = 0.0;
            stage_finished = true;
            return was_catchup;
        }
        return false;
    }

    // Updates presence when a player disconnects; returns true if catch-up ended immediately.
    bool on_player_leave(uint8_t id, int remaining_active_players)
    {
        if (valid_player(id))
            players[id].present = false;
        if (!catchup_active)
            return false;
        return check_all_finished(remaining_active_players);
    }

    // Returns assigned rank (1..4), or 0 if invalid.
    uint8_t report_complete(uint8_t id, int active_players, double catchup_secs, double now)
    {
        if (!valid_player(id))
            return 0;
        players[id].player_id = id;
        players[id].present = true;
        if (players[id].finish_rank > 0)
            return players[id].finish_rank;
        if (finished_count >= kMaxPlayers)
            return 0;
        uint8_t rank = ++finished_count;
        players[id].finish_rank = rank;
        if (rank == 1) {
            winner = id;
            double dur = catchup_secs > 0.0 ? catchup_secs : 0.0;
            int target = std::max(active_players, present_count());
            if (target <= 1 || dur <= 0.0) {
                catchup_active = false;
                catchup_remaining_secs = 0;
                stage_finished = true;
            } else {
                catchup_active = true;
                catchup_ends_at = now + dur;
                catchup_remaining_secs = dur;
                stage_finished = false;
            }
        }
        check_all_finished(active_players);
        return rank;
    }

    // Returns true on the tick the catch-up timer expires.
    bool tick(double now)
    {
        if (!catchup_active)
            return false;
        double rem = catchup_ends_at - now;
        if (rem <= 0.0) {
            catchup_active = false;
            catchup_remaining_secs = 0.0;
            stage_finished = true;
            return true;
        }
        catchup_remaining_secs = rem;
        return false;
    }
};

// ---------------- 11.6 Stage-End Winner Draft ----------------
struct Draft {
    struct Item {
        uint32_t id = 0;
        uint8_t kind = 0; // stage-specific item kind (part, tool, vehicle, building)
        std::string label;
    };
    struct PlayerDraft {
        uint8_t picks_left = 0;
        bool done = false;
        std::vector<uint32_t> picked;
    };

    bool active = false;
    Stage stage = Stage::None;
    uint8_t winner = 255;
    std::vector<Item> items;
    PlayerDraft players[kMaxPlayers] = {};
    uint16_t total_picks = 0;
    int32_t winner_payout = 0;
    double ends_at = 0;
    double remaining_secs = 0;
    double timeout_secs = kDefaultDraftTimeoutSecs;
    int32_t pending_payout[kMaxPlayers] = {};
    Stage pending_payout_stage[kMaxPlayers] = {Stage::None, Stage::None, Stage::None, Stage::None};

    void reset_active()
    {
        active = false;
        stage = Stage::None;
        winner = 255;
        items.clear();
        for (int i = 0; i < kMaxPlayers; i++)
            players[i] = PlayerDraft{};
        total_picks = 0;
        winner_payout = 0;
        ends_at = 0;
        remaining_secs = 0;
    }

    void reset_all()
    {
        reset_active();
        for (int i = 0; i < kMaxPlayers; i++) {
            pending_payout[i] = 0;
            pending_payout_stage[i] = Stage::None;
        }
    }

    static uint8_t compute_picks(uint8_t base_picks, uint8_t finish_rank)
    {
        if (base_picks == 0 || finish_rank == 1)
            return 0;
        if (finish_rank >= 2)
            return base_picks > 1 ? (uint8_t)(base_picks - 1) : (uint8_t)1;
        return base_picks;
    }

    bool start(Stage s, uint8_t winner_id, const std::vector<Item>& offered, uint8_t base_picks,
               const uint8_t finish_ranks[kMaxPlayers], const bool present[kMaxPlayers], double now)
    {
        reset_active();
        if (!valid_player(winner_id) || offered.empty() || base_picks == 0)
            return false;
        stage = s;
        winner = winner_id;
        for (size_t i = 0; i < offered.size() && items.size() < kMaxDraftItems; i++) {
            Item it = offered[i];
            if (it.label.size() > kMaxDraftLabelLen)
                it.label.resize(kMaxDraftLabelLen);
            items.push_back(std::move(it));
        }
        bool any_picker = false;
        for (int i = 0; i < kMaxPlayers; i++) {
            if (!present[i] || i == winner_id) {
                players[i].picks_left = 0;
                players[i].done = true;
            } else {
                uint8_t p = compute_picks(base_picks, finish_ranks[i]);
                players[i].picks_left = p;
                players[i].done = (p == 0);
                if (p > 0)
                    any_picker = true;
            }
        }
        if (!any_picker)
            return false;
        active = true;
        double dur = timeout_secs > 0.0 ? timeout_secs : kDefaultDraftTimeoutSecs;
        ends_at = now + dur;
        remaining_secs = dur;
        return true;
    }

    int32_t next_pick_payout() const
    {
        return kDraftBasePayout + kDraftPayoutStep * (int32_t)total_picks;
    }

    bool all_done() const
    {
        for (int i = 0; i < kMaxPlayers; i++)
            if (!players[i].done)
                return false;
        return true;
    }

    std::string pick(uint8_t player_id, uint32_t item_id, int32_t* payout_out = nullptr)
    {
        if (!active)
            return "no active draft";
        if (!valid_player(player_id) || player_id == winner)
            return "cannot pick";
        auto& pd = players[player_id];
        if (pd.done || pd.picks_left == 0)
            return "no picks left";
        bool found = false;
        for (const auto& it : items)
            if (it.id == item_id)
                found = true;
        if (!found)
            return "unknown draft item";
        for (uint32_t prev : pd.picked)
            if (prev == item_id)
                return "already picked";
        pd.picked.push_back(item_id);
        pd.picks_left--;
        if (pd.picks_left == 0)
            pd.done = true;
        int32_t pay = next_pick_payout();
        total_picks++;
        winner_payout += pay;
        if (valid_player(winner)) {
            if (pending_payout_stage[winner] != stage)
                pending_payout[winner] = 0;
            pending_payout[winner] += pay;
            pending_payout_stage[winner] = stage;
        }
        if (payout_out)
            *payout_out = pay;
        if (all_done()) {
            active = false;
            remaining_secs = 0.0;
        }
        return "";
    }

    std::string skip(uint8_t player_id)
    {
        if (!active)
            return "no active draft";
        if (!valid_player(player_id) || player_id == winner)
            return "cannot skip";
        players[player_id].picks_left = 0;
        players[player_id].done = true;
        if (all_done()) {
            active = false;
            remaining_secs = 0.0;
        }
        return "";
    }

    bool tick(double now)
    {
        if (!active)
            return false;
        double rem = ends_at - now;
        if (rem <= 0.0) {
            active = false;
            remaining_secs = 0.0;
            for (int i = 0; i < kMaxPlayers; i++)
                players[i].done = true;
            return true;
        }
        remaining_secs = rem;
        return false;
    }

    int32_t take_pending_payout(uint8_t player_id)
    {
        if (!valid_player(player_id))
            return 0;
        int32_t v = pending_payout[player_id];
        pending_payout[player_id] = 0;
        pending_payout_stage[player_id] = Stage::None;
        return v;
    }

    int32_t take_pending_payout_for(Stage expected_stage, uint8_t player_id)
    {
        if (!valid_player(player_id) || expected_stage == Stage::None)
            return 0;
        if (pending_payout[player_id] == 0)
            return 0;
        if (pending_payout_stage[player_id] != Stage::None &&
            pending_payout_stage[player_id] != expected_stage)
            return 0;
        int32_t v = pending_payout[player_id];
        pending_payout[player_id] = 0;
        pending_payout_stage[player_id] = Stage::None;
        return v;
    }
};

// ---------------- 11.7 Chronicle (Cumulative Score) ----------------
struct Chronicle {
    struct Entry {
        Stage stage = Stage::None;
        uint8_t player_id = 0;
        int32_t points = 0;
        std::string reason;
        std::string player_key; // optional stable player key for disk persistence & adopt_key
    };

    int32_t totals[kMaxPlayers] = {};
    std::vector<Entry> history;

    void reset()
    {
        for (int i = 0; i < kMaxPlayers; i++)
            totals[i] = 0;
        history.clear();
    }

    bool add_points(uint8_t player_id, Stage stage, int32_t points, const std::string& reason)
    {
        if (!valid_player(player_id) || points == 0)
            return false;
        int64_t sum = (int64_t)totals[player_id] + points;
        if (sum > 1000000000LL)
            sum = 1000000000LL;
        if (sum < -1000000000LL)
            sum = -1000000000LL;
        totals[player_id] = (int32_t)sum;
        Entry e;
        e.stage = stage;
        e.player_id = player_id;
        e.points = points;
        e.reason = reason.size() > kMaxReasonLen ? reason.substr(0, kMaxReasonLen) : reason;
        if ((int)history.size() >= kMaxChronicleHistory)
            history.erase(history.begin());
        history.push_back(std::move(e));
        return true;
    }

    void award_stage_placements(Stage stage, const uint8_t finish_ranks[kMaxPlayers])
    {
        for (int i = 0; i < kMaxPlayers; i++) {
            if (finish_ranks[i] == 1)
                add_points((uint8_t)i, stage, kPtsStageWin, "stage win");
            else if (finish_ranks[i] == 2)
                add_points((uint8_t)i, stage, kPtsSecond, "2nd place");
            else if (finish_ranks[i] == 3)
                add_points((uint8_t)i, stage, kPtsThird, "3rd place");
        }
    }

    uint8_t leader() const
    {
        uint8_t best = 255;
        int32_t best_pts = 0;
        for (int i = 0; i < kMaxPlayers; i++) {
            if (totals[i] > best_pts) {
                best_pts = totals[i];
                best = (uint8_t)i;
            }
        }
        return best;
    }
};

// ---------------- Combined Session State ----------------
struct State {
    Config config;
    Relations rel;
    Influence inf;
    Race race;
    Draft draft;
    Chronicle chronicle;
    uint32_t rev = 0;

    void reset_all()
    {
        config = Config{};
        rel.reset();
        inf.reset_all();
        race.reset();
        draft.reset_all();
        chronicle.reset();
        rev = 0;
    }

    // Resets stage-scoped Race and Influence only when entering a different real gameplay stage.
    // Editor round-trips (Stage -> Editor(None) -> same Stage) never reset Race or Influence (BUG-1551).
    bool on_stage_enter(Stage to_s, const bool present[kMaxPlayers] = nullptr)
    {
        if (!valid_stage(to_s) || to_s == race.stage)
            return false;
        race.reset(to_s);
        inf.reset_stage();
        if (present) {
            for (int i = 0; i < kMaxPlayers; i++)
                race.players[i].present = present[i];
        }
        return true;
    }
};

// ---------------- Persistent Store Format (for store.cpp) ----------------
// Simple line-oriented text stored in pstore::Store world record "rivals.state" v1.
struct PersistedWorld {
    Config config;
    std::map<std::pair<std::string, std::string>, Relation> rel_by_key; // sorted stable key pair -> relation
    std::map<std::string, int32_t> chronicle_by_key;                    // stable key -> cumulative points
    std::vector<Chronicle::Entry> history;

    static std::pair<std::string, std::string> norm_pair(const std::string& a, const std::string& b)
    {
        return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    }

    static std::string sanitize_token(const std::string& s, size_t max_len = 64)
    {
        std::string out;
        out.reserve(std::min(s.size(), max_len));
        for (char c : s) {
            if (out.size() >= max_len)
                break;
            if (c == '\n' || c == '\r' || c == '\t')
                out.push_back(' ');
            else
                out.push_back(c);
        }
        return out;
    }

    static std::string sanitize_key(const std::string& s, size_t max_len = 40)
    {
        std::string out;
        out.reserve(std::min(s.size(), max_len));
        for (char c : s) {
            if (out.size() >= max_len)
                break;
            if (c <= ' ' || c > '~')
                continue;
            out.push_back(c);
        }
        return out;
    }

    bool has_non_default_data() const
    {
        return config.any_rivals() || config.catchup_secs != kDefaultCatchupSecs ||
               config.draft_picks != kDefaultDraftPicks || config.base_distance != BaseDistance::Middle ||
               !rel_by_key.empty() || !chronicle_by_key.empty() || !history.empty();
    }

    // Write whenever non-default data exists OR a record was previously stored on disk
    // (so switching back to AllTogether overwrites the stale record instead of resurrecting AllRivals).
    bool should_write(bool had_existing_record) const
    {
        return has_non_default_data() || had_existing_record;
    }

    std::vector<uint8_t> encode() const
    {
        std::string out;
        char buf[192];
        snprintf(buf, sizeof buf, "cfg %u %u %u %u %u %u %u %u\n",
                 (unsigned)config.stages[0], (unsigned)config.stages[1], (unsigned)config.stages[2],
                 (unsigned)config.stages[3], (unsigned)config.stages[4],
                 (unsigned)config.catchup_secs, (unsigned)config.draft_picks, (unsigned)config.base_distance);
        out += buf;
        for (const auto& [kp, r] : rel_by_key) {
            std::string ka = sanitize_key(kp.first, 40);
            std::string kb = sanitize_key(kp.second, 40);
            if (ka.empty() || kb.empty() || ka == kb || r == Relation::Neutral)
                continue;
            snprintf(buf, sizeof buf, "rel %s %s %u\n", ka.c_str(), kb.c_str(), (unsigned)r);
            out += buf;
        }
        for (const auto& [k, pts] : chronicle_by_key) {
            std::string sk = sanitize_key(k, 40);
            if (sk.empty() || pts == 0)
                continue;
            snprintf(buf, sizeof buf, "chr %s %d\n", sk.c_str(), (int)pts);
            out += buf;
        }
        for (const auto& e : history) {
            uint8_t pid = valid_player(e.player_id) ? e.player_id : 0;
            std::string sk = sanitize_key(e.player_key, 40);
            if (!sk.empty()) {
                snprintf(buf, sizeof buf, "hisk %u %u %s %d %s\n", (unsigned)e.stage, (unsigned)pid, sk.c_str(),
                         (int)e.points, sanitize_token(e.reason, kMaxReasonLen).c_str());
            } else {
                snprintf(buf, sizeof buf, "his %u %u %d %s\n", (unsigned)e.stage, (unsigned)pid,
                         (int)e.points, sanitize_token(e.reason, kMaxReasonLen).c_str());
            }
            out += buf;
        }
        return std::vector<uint8_t>(out.begin(), out.end());
    }

    bool decode(const std::vector<uint8_t>& blob)
    {
        *this = PersistedWorld{};
        std::string text(blob.begin(), blob.end());
        size_t pos = 0;
        bool saw_cfg = false;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = nl == std::string::npos ? text.substr(pos) : text.substr(pos, nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            if (line.empty())
                continue;
            if (line.rfind("cfg ", 0) == 0) {
                unsigned s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, cu = kDefaultCatchupSecs, dp = kDefaultDraftPicks, bd = 1;
                if (sscanf(line.c_str(), "cfg %u %u %u %u %u %u %u %u", &s0, &s1, &s2, &s3, &s4, &cu, &dp, &bd) == 8) {
                    config.stages[0] = s0 ? StageMode::Rivals : StageMode::Together;
                    config.stages[1] = s1 ? StageMode::Rivals : StageMode::Together;
                    config.stages[2] = s2 ? StageMode::Rivals : StageMode::Together;
                    config.stages[3] = s3 ? StageMode::Rivals : StageMode::Together;
                    config.stages[4] = s4 ? StageMode::Rivals : StageMode::Together;
                    config.catchup_secs = (uint16_t)cu;
                    config.draft_picks = (uint8_t)dp;
                    config.base_distance = bd <= 2 ? (BaseDistance)bd : BaseDistance::Middle;
                    config.clamp();
                    saw_cfg = true;
                }
            } else if (line.rfind("rel ", 0) == 0) {
                char a[64] = {}, b[64] = {};
                unsigned r = 0;
                if (sscanf(line.c_str(), "rel %63s %63s %u", a, b, &r) == 3 && r <= 2 && strcmp(a, b) != 0)
                    rel_by_key[norm_pair(a, b)] = (Relation)r;
            } else if (line.rfind("chr ", 0) == 0) {
                char k[64] = {};
                int pts = 0;
                if (sscanf(line.c_str(), "chr %63s %d", k, &pts) == 2)
                    chronicle_by_key[k] = pts;
            } else if (line.rfind("hisk ", 0) == 0) {
                unsigned st = 255, pid = 0;
                char pk[64] = {};
                int pts = 0;
                int consumed = 0;
                if (sscanf(line.c_str(), "hisk %u %u %63s %d %n", &st, &pid, pk, &pts, &consumed) >= 4 &&
                    pid < kMaxPlayers && (st < kStages || st == 255)) {
                    Chronicle::Entry e;
                    e.stage = (Stage)st;
                    e.player_id = (uint8_t)pid;
                    e.points = pts;
                    e.player_key = sanitize_key(pk, 40);
                    if (consumed > 0 && (size_t)consumed < line.size())
                        e.reason = sanitize_token(line.substr((size_t)consumed), kMaxReasonLen);
                    if ((int)history.size() < kMaxChronicleHistory)
                        history.push_back(std::move(e));
                }
            } else if (line.rfind("his ", 0) == 0) {
                unsigned st = 255, pid = 0;
                int pts = 0;
                int consumed = 0;
                if (sscanf(line.c_str(), "his %u %u %d %n", &st, &pid, &pts, &consumed) >= 3 && pid < kMaxPlayers &&
                    (st < kStages || st == 255)) {
                    Chronicle::Entry e;
                    e.stage = (Stage)st;
                    e.player_id = (uint8_t)pid;
                    e.points = pts;
                    if (consumed > 0 && (size_t)consumed < line.size())
                        e.reason = sanitize_token(line.substr((size_t)consumed), kMaxReasonLen);
                    if ((int)history.size() < kMaxChronicleHistory)
                        history.push_back(std::move(e));
                }
            }
        }
        return saw_cfg;
    }

    void adopt_key(const std::string& from, const std::string& to)
    {
        if (from.empty() || to.empty() || from == to)
            return;
        if (auto it = chronicle_by_key.find(from); it != chronicle_by_key.end()) {
            chronicle_by_key[to] = it->second;
            chronicle_by_key.erase(it);
        }
        std::map<std::pair<std::string, std::string>, Relation> moved;
        for (const auto& [kp, r] : rel_by_key) {
            std::string a = kp.first == from ? to : kp.first;
            std::string b = kp.second == from ? to : kp.second;
            if (a != b)
                moved[norm_pair(a, b)] = r;
        }
        rel_by_key = std::move(moved);
        for (auto& e : history) {
            if (e.player_key == from)
                e.player_key = to;
        }
    }

    static void remap_history_ids(std::vector<Chronicle::Entry>& entries,
                                  const std::map<std::string, uint8_t>& key_to_id)
    {
        for (auto& e : entries) {
            if (e.player_key.empty())
                continue;
            if (auto it = key_to_id.find(e.player_key); it != key_to_id.end() && valid_player(it->second))
                e.player_id = it->second;
        }
    }
};

} // namespace rivals
