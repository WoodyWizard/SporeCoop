// MSG_RIVALS = 80 (Family::session 1.4, WP-R1 of 0.8.0, BUG-0794): portable wire payloads for shared Rivals mode
// systems (stage modes, 4x4 player relations + 60 s break notice, influence & NPC protections, race progress & catch-up
// timer, stage-end draft, and Chronicle). Rules in wire/common.h.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "../net/protocol.h"
#include "../rivals_logic.h"
#include "common.h"

namespace wire::rivals {

constexpr uint8_t kFamily = 1;      // Family::session in net/messages.def
constexpr uint16_t kMinor = 4;      // session 1.4 (0.8.0) adds MSG_RIVALS = 80
constexpr uint8_t kWireVersion = 1; // Snapshot payload version

enum Kind : uint8_t {
    K_SNAPSHOT = 1,  // host -> guests: authoritative full Rivals state
    K_REL_REQ = 2,   // guest -> host: player relation action (ally/peace/war/break)
    K_INF_REQ = 3,   // guest -> host: influence action (buy/turn/protect/smooth)
    K_DRAFT_REQ = 4, // guest -> host: draft pick or skip
    K_EVENT = 5,     // host -> guests: human-readable Rivals event notice
};

enum EventCode : uint8_t {
    E_CONFIG = 1,
    E_REL = 2,
    E_INFLUENCE = 3,
    E_RACE = 4,
    E_DRAFT = 5,
    E_CHRONICLE = 6,
};

// Does a peer whose HELLO listed `families` read MSG_RIVALS (session 1.4+)?
inline bool peer_reads(bool has_families, const std::vector<net::FamilyRev>& families)
{
    if (!has_families)
        return false;
    for (const auto& f : families)
        if (f.family == kFamily)
            return f.rev == 1 && f.minor >= kMinor;
    return false;
}

inline uint16_t secs_to_tenths(double secs)
{
    if (!std::isfinite(secs) || secs <= 0.0)
        return 0;
    double t = std::floor(secs * 10.0 + 0.5);
    if (t > 65535.0)
        return 65535;
    return (uint16_t)t;
}

inline double tenths_to_secs(uint16_t t) { return (double)t / 10.0; }

inline bool valid_stage_byte(uint8_t s) { return s < ::rivals::kStages || s == 255; }
inline bool valid_player_or_none(uint8_t id) { return id < ::rivals::kMaxPlayers || id == 255; }

// K_SNAPSHOT (host -> guests): full authoritative Rivals state snapshot.
struct Snapshot {
    struct Prop {
        uint8_t from = 0;
        uint8_t to = 0;
        uint8_t kind = 0;
    };
    struct Break {
        uint8_t a = 0;
        uint8_t b = 0;
        uint8_t initiator = 0;
        uint16_t remaining_tenths = 0;
    };
    struct Prot {
        uint8_t owner = 0;
        uint32_t npc_id = 0;
        uint16_t remaining_tenths = 0;
    };
    struct Prog {
        uint8_t present = 0;
        uint8_t finish_rank = 0;
        float primary = 0.0f;
        float secondary = 0.0f;
        uint16_t stat_a = 0;
        uint16_t stat_b = 0;
        float extra = 0.0f;
    };
    struct DraftItem {
        uint32_t id = 0;
        uint8_t kind = 0;
        std::string label;
    };
    struct DraftPlayer {
        uint8_t picks_left = 0;
        uint8_t done = 0;
        std::vector<uint32_t> picked;
    };
    struct ChronEntry {
        uint8_t stage = 255;
        uint8_t player_id = 0;
        int32_t points = 0;
        std::string reason;
    };

    uint8_t version = kWireVersion;
    uint32_t rev = 0;

    // Config
    uint8_t stage_modes[::rivals::kStages] = {};
    uint16_t catchup_secs = ::rivals::kDefaultCatchupSecs;
    uint8_t draft_picks = ::rivals::kDefaultDraftPicks;
    uint8_t base_distance = (uint8_t)::rivals::BaseDistance::Middle;

    // Relations
    uint8_t rel_pairs[::rivals::kRelPairs] = {};
    std::vector<Prop> proposals;
    std::vector<Break> breaks;

    // Influence
    int32_t inf_balance[::rivals::kMaxPlayers] = {};
    uint16_t inf_buys[::rivals::kMaxPlayers] = {};
    std::vector<Prot> protections;

    // Race
    uint8_t race_stage = 255;
    uint8_t race_winner = 255;
    uint8_t race_finished = 0;
    uint8_t race_flags = 0; // bit 0: catchup_active, bit 1: stage_finished
    uint16_t catchup_remaining_tenths = 0;
    Prog progress[::rivals::kMaxPlayers] = {};

    // Draft
    uint8_t draft_active = 0;
    uint8_t draft_stage = 255;
    uint8_t draft_winner = 255;
    uint16_t draft_total_picks = 0;
    int32_t draft_winner_payout = 0;
    uint16_t draft_remaining_tenths = 0;
    std::vector<DraftItem> draft_items;
    DraftPlayer draft_players[::rivals::kMaxPlayers] = {};
    int32_t pending_payout[::rivals::kMaxPlayers] = {};

    // Chronicle
    int32_t chronicle_totals[::rivals::kMaxPlayers] = {};
    std::vector<ChronEntry> chronicle_history;

    static Snapshot from_state(const ::rivals::State& st, double now)
    {
        Snapshot sn;
        sn.version = kWireVersion;
        sn.rev = st.rev;
        for (int i = 0; i < ::rivals::kStages; i++)
            sn.stage_modes[i] = (uint8_t)st.config.stages[i];
        sn.catchup_secs = st.config.catchup_secs;
        sn.draft_picks = st.config.draft_picks;
        sn.base_distance = (uint8_t)st.config.base_distance;

        for (int i = 0; i < ::rivals::kRelPairs; i++) {
            auto [a, b] = ::rivals::pair_from_index(i);
            sn.rel_pairs[i] = (uint8_t)st.rel.get(a, b);
        }
        for (const auto& p : st.rel.proposals) {
            if ((int)sn.proposals.size() >= ::rivals::kMaxProposals)
                break;
            sn.proposals.push_back({p.from, p.to, (uint8_t)p.kind});
        }
        for (const auto& b : st.rel.break_notices) {
            if ((int)sn.breaks.size() >= ::rivals::kMaxBreakNotices)
                break;
            double rem = b.expires_at > now ? (b.expires_at - now) : b.remaining_secs;
            sn.breaks.push_back({b.a, b.b, b.initiator, secs_to_tenths(rem)});
        }

        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            sn.inf_balance[i] = st.inf.balance[i];
            sn.inf_buys[i] = st.inf.buys[i];
        }
        for (const auto& p : st.inf.protections) {
            if ((int)sn.protections.size() >= ::rivals::kMaxProtections)
                break;
            double rem = p.expires_at > now ? (p.expires_at - now) : p.remaining_secs;
            if (rem > 0.0)
                sn.protections.push_back({p.owner, p.npc_id, secs_to_tenths(rem)});
        }

        sn.race_stage = (uint8_t)st.race.stage;
        sn.race_winner = st.race.winner;
        sn.race_finished = st.race.finished_count;
        sn.race_flags = (st.race.catchup_active ? 1u : 0u) | (st.race.stage_finished ? 2u : 0u);
        double cu_rem = st.race.catchup_active
                            ? (st.race.catchup_ends_at > now ? (st.race.catchup_ends_at - now) : st.race.catchup_remaining_secs)
                            : 0.0;
        sn.catchup_remaining_tenths = secs_to_tenths(cu_rem);
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            const auto& sp = st.race.players[i];
            sn.progress[i].present = sp.present ? 1 : 0;
            sn.progress[i].finish_rank = sp.finish_rank;
            sn.progress[i].primary = std::isfinite(sp.primary) ? sp.primary : 0.0f;
            sn.progress[i].secondary = std::isfinite(sp.secondary) ? sp.secondary : 0.0f;
            sn.progress[i].stat_a = sp.stat_a;
            sn.progress[i].stat_b = sp.stat_b;
            sn.progress[i].extra = std::isfinite(sp.extra) ? sp.extra : 0.0f;
        }

        sn.draft_active = st.draft.active ? 1 : 0;
        sn.draft_stage = (uint8_t)st.draft.stage;
        sn.draft_winner = st.draft.winner;
        sn.draft_total_picks = st.draft.total_picks;
        sn.draft_winner_payout = st.draft.winner_payout;
        double dr_rem = st.draft.active
                            ? (st.draft.ends_at > now ? (st.draft.ends_at - now) : st.draft.remaining_secs)
                            : 0.0;
        sn.draft_remaining_tenths = secs_to_tenths(dr_rem);
        for (const auto& it : st.draft.items) {
            if ((int)sn.draft_items.size() >= ::rivals::kMaxDraftItems)
                break;
            std::string lbl = it.label.size() > ::rivals::kMaxDraftLabelLen ? it.label.substr(0, ::rivals::kMaxDraftLabelLen) : it.label;
            sn.draft_items.push_back({it.id, it.kind, std::move(lbl)});
        }
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            sn.draft_players[i].picks_left = st.draft.players[i].picks_left;
            sn.draft_players[i].done = st.draft.players[i].done ? 1 : 0;
            sn.draft_players[i].picked = st.draft.players[i].picked;
            if ((int)sn.draft_players[i].picked.size() > ::rivals::kMaxDraftItems)
                sn.draft_players[i].picked.resize(::rivals::kMaxDraftItems);
            sn.pending_payout[i] = st.draft.pending_payout[i];
        }

        for (int i = 0; i < ::rivals::kMaxPlayers; i++)
            sn.chronicle_totals[i] = st.chronicle.totals[i];
        for (const auto& e : st.chronicle.history) {
            if ((int)sn.chronicle_history.size() >= ::rivals::kMaxChronicleHistory)
                break;
            std::string rs = e.reason.size() > ::rivals::kMaxReasonLen ? e.reason.substr(0, ::rivals::kMaxReasonLen) : e.reason;
            sn.chronicle_history.push_back({(uint8_t)e.stage, e.player_id, e.points, std::move(rs)});
        }
        return sn;
    }

    void apply_to(::rivals::State& st, double now) const
    {
        st.rev = rev;
        for (int i = 0; i < ::rivals::kStages; i++)
            st.config.stages[i] = stage_modes[i] ? ::rivals::StageMode::Rivals : ::rivals::StageMode::Together;
        st.config.catchup_secs = catchup_secs;
        st.config.draft_picks = draft_picks;
        st.config.base_distance = (::rivals::BaseDistance)base_distance;
        st.config.clamp();

        st.rel.reset();
        for (int i = 0; i < ::rivals::kRelPairs; i++) {
            auto [a, b] = ::rivals::pair_from_index(i);
            st.rel.matrix[a][b] = (::rivals::Relation)rel_pairs[i];
            st.rel.matrix[b][a] = (::rivals::Relation)rel_pairs[i];
        }
        for (const auto& p : proposals)
            st.rel.proposals.push_back({p.from, p.to, (::rivals::ProposalKind)p.kind});
        for (const auto& b : breaks) {
            double rem = tenths_to_secs(b.remaining_tenths);
            st.rel.break_notices.push_back({b.a, b.b, b.initiator, now + rem, rem});
        }

        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            st.inf.balance[i] = inf_balance[i];
            st.inf.buys[i] = inf_buys[i];
        }
        st.inf.protections.clear();
        for (const auto& p : protections) {
            double rem = tenths_to_secs(p.remaining_tenths);
            st.inf.protections.push_back({p.owner, p.npc_id, now + rem, rem});
        }

        st.race.stage = (::rivals::Stage)race_stage;
        st.race.winner = race_winner;
        st.race.finished_count = race_finished;
        st.race.catchup_active = (race_flags & 1u) != 0;
        st.race.stage_finished = (race_flags & 2u) != 0;
        st.race.catchup_remaining_secs = tenths_to_secs(catchup_remaining_tenths);
        st.race.catchup_ends_at = st.race.catchup_active ? (now + st.race.catchup_remaining_secs) : 0.0;
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            auto& dp = st.race.players[i];
            dp.player_id = (uint8_t)i;
            dp.present = progress[i].present != 0;
            dp.finish_rank = progress[i].finish_rank;
            dp.primary = progress[i].primary;
            dp.secondary = progress[i].secondary;
            dp.stat_a = progress[i].stat_a;
            dp.stat_b = progress[i].stat_b;
            dp.extra = progress[i].extra;
        }

        st.draft.active = draft_active != 0;
        st.draft.stage = (::rivals::Stage)draft_stage;
        st.draft.winner = draft_winner;
        st.draft.total_picks = draft_total_picks;
        st.draft.winner_payout = draft_winner_payout;
        st.draft.remaining_secs = tenths_to_secs(draft_remaining_tenths);
        st.draft.ends_at = st.draft.active ? (now + st.draft.remaining_secs) : 0.0;
        st.draft.items.clear();
        for (const auto& it : draft_items)
            st.draft.items.push_back({it.id, it.kind, it.label});
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            st.draft.players[i].picks_left = draft_players[i].picks_left;
            st.draft.players[i].done = draft_players[i].done != 0;
            st.draft.players[i].picked = draft_players[i].picked;
            st.draft.pending_payout[i] = pending_payout[i];
        }

        for (int i = 0; i < ::rivals::kMaxPlayers; i++)
            st.chronicle.totals[i] = chronicle_totals[i];
        st.chronicle.history.clear();
        for (const auto& e : chronicle_history)
            st.chronicle.history.push_back({(::rivals::Stage)e.stage, e.player_id, e.points, e.reason});
    }

    void write(net::Writer& w) const
    {
        w.u8(version);
        w.u32(rev);
        for (int i = 0; i < ::rivals::kStages; i++)
            w.u8(stage_modes[i]);
        w.u16(catchup_secs);
        w.u8(draft_picks);
        w.u8(base_distance);

        for (int i = 0; i < ::rivals::kRelPairs; i++)
            w.u8(rel_pairs[i]);
        uint8_t pn = (uint8_t)std::min<size_t>(proposals.size(), ::rivals::kMaxProposals);
        w.u8(pn);
        for (uint8_t i = 0; i < pn; i++) {
            w.u8(proposals[i].from);
            w.u8(proposals[i].to);
            w.u8(proposals[i].kind);
        }
        uint8_t bn = (uint8_t)std::min<size_t>(breaks.size(), ::rivals::kMaxBreakNotices);
        w.u8(bn);
        for (uint8_t i = 0; i < bn; i++) {
            w.u8(breaks[i].a);
            w.u8(breaks[i].b);
            w.u8(breaks[i].initiator);
            w.u16(breaks[i].remaining_tenths);
        }

        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            w.u32((uint32_t)inf_balance[i]);
            w.u16(inf_buys[i]);
        }
        uint8_t prn = (uint8_t)std::min<size_t>(protections.size(), ::rivals::kMaxProtections);
        w.u8(prn);
        for (uint8_t i = 0; i < prn; i++) {
            w.u8(protections[i].owner);
            w.u32(protections[i].npc_id);
            w.u16(protections[i].remaining_tenths);
        }

        w.u8(race_stage);
        w.u8(race_winner);
        w.u8(race_finished);
        w.u8(race_flags);
        w.u16(catchup_remaining_tenths);
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            w.u8(progress[i].present);
            w.u8(progress[i].finish_rank);
            w.f32(progress[i].primary);
            w.f32(progress[i].secondary);
            w.u16(progress[i].stat_a);
            w.u16(progress[i].stat_b);
            w.f32(progress[i].extra);
        }

        w.u8(draft_active);
        w.u8(draft_stage);
        w.u8(draft_winner);
        w.u16(draft_total_picks);
        w.u32((uint32_t)draft_winner_payout);
        w.u16(draft_remaining_tenths);
        uint8_t in = (uint8_t)std::min<size_t>(draft_items.size(), ::rivals::kMaxDraftItems);
        w.u8(in);
        for (uint8_t i = 0; i < in; i++) {
            w.u32(draft_items[i].id);
            w.u8(draft_items[i].kind);
            std::string lbl = draft_items[i].label.size() > ::rivals::kMaxDraftLabelLen
                                  ? draft_items[i].label.substr(0, ::rivals::kMaxDraftLabelLen)
                                  : draft_items[i].label;
            w.str8(lbl);
        }
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            w.u8(draft_players[i].picks_left);
            w.u8(draft_players[i].done);
            uint8_t kn = (uint8_t)std::min<size_t>(draft_players[i].picked.size(), ::rivals::kMaxDraftItems);
            w.u8(kn);
            for (uint8_t k = 0; k < kn; k++)
                w.u32(draft_players[i].picked[k]);
            w.u32((uint32_t)pending_payout[i]);
        }

        for (int i = 0; i < ::rivals::kMaxPlayers; i++)
            w.u32((uint32_t)chronicle_totals[i]);
        uint8_t hn = (uint8_t)std::min<size_t>(chronicle_history.size(), ::rivals::kMaxChronicleHistory);
        w.u8(hn);
        for (uint8_t i = 0; i < hn; i++) {
            w.u8(chronicle_history[i].stage);
            w.u8(chronicle_history[i].player_id);
            w.u32((uint32_t)chronicle_history[i].points);
            std::string rs = chronicle_history[i].reason.size() > ::rivals::kMaxReasonLen
                                 ? chronicle_history[i].reason.substr(0, ::rivals::kMaxReasonLen)
                                 : chronicle_history[i].reason;
            w.str8(rs);
        }
    }

    bool read(net::Reader& r)
    {
        *this = Snapshot{};
        version = r.u8();
        if (!r.ok() || version != kWireVersion)
            return false;
        rev = r.u32();
        for (int i = 0; i < ::rivals::kStages; i++) {
            stage_modes[i] = r.u8();
            if (stage_modes[i] > 1)
                return false;
        }
        catchup_secs = r.u16();
        draft_picks = r.u8();
        base_distance = r.u8();
        if (!r.ok() || catchup_secs < ::rivals::kMinCatchupSecs || catchup_secs > ::rivals::kMaxCatchupSecs ||
            draft_picks > ::rivals::kMaxDraftPicks || base_distance > 2)
            return false;

        for (int i = 0; i < ::rivals::kRelPairs; i++) {
            rel_pairs[i] = r.u8();
            if (rel_pairs[i] > 2)
                return false;
        }
        uint8_t pn = r.u8();
        if (!r.ok() || pn > ::rivals::kMaxProposals)
            return false;
        proposals.reserve(pn);
        for (uint8_t i = 0; i < pn; i++) {
            Prop p;
            p.from = r.u8();
            p.to = r.u8();
            p.kind = r.u8();
            if (!r.ok() || p.from >= ::rivals::kMaxPlayers || p.to >= ::rivals::kMaxPlayers || p.from == p.to ||
                (p.kind != 1 && p.kind != 2))
                return false;
            proposals.push_back(p);
        }
        uint8_t bn = r.u8();
        if (!r.ok() || bn > ::rivals::kMaxBreakNotices)
            return false;
        breaks.reserve(bn);
        for (uint8_t i = 0; i < bn; i++) {
            Break b;
            b.a = r.u8();
            b.b = r.u8();
            b.initiator = r.u8();
            b.remaining_tenths = r.u16();
            if (!r.ok() || b.a >= ::rivals::kMaxPlayers || b.b >= ::rivals::kMaxPlayers || b.a >= b.b ||
                (b.initiator != b.a && b.initiator != b.b))
                return false;
            breaks.push_back(b);
        }

        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            inf_balance[i] = (int32_t)r.u32();
            inf_buys[i] = r.u16();
        }
        uint8_t prn = r.u8();
        if (!r.ok() || prn > ::rivals::kMaxProtections)
            return false;
        protections.reserve(prn);
        for (uint8_t i = 0; i < prn; i++) {
            Prot p;
            p.owner = r.u8();
            p.npc_id = r.u32();
            p.remaining_tenths = r.u16();
            if (!r.ok() || p.owner >= ::rivals::kMaxPlayers)
                return false;
            protections.push_back(p);
        }

        race_stage = r.u8();
        race_winner = r.u8();
        race_finished = r.u8();
        race_flags = r.u8();
        catchup_remaining_tenths = r.u16();
        if (!r.ok() || !valid_stage_byte(race_stage) || !valid_player_or_none(race_winner) ||
            race_finished > ::rivals::kMaxPlayers)
            return false;
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            progress[i].present = r.u8();
            progress[i].finish_rank = r.u8();
            progress[i].primary = r.f32();
            progress[i].secondary = r.f32();
            progress[i].stat_a = r.u16();
            progress[i].stat_b = r.u16();
            progress[i].extra = r.f32();
            if (!r.ok() || progress[i].present > 1 || progress[i].finish_rank > ::rivals::kMaxPlayers ||
                !std::isfinite(progress[i].primary) || !std::isfinite(progress[i].secondary) ||
                !std::isfinite(progress[i].extra))
                return false;
        }

        draft_active = r.u8();
        draft_stage = r.u8();
        draft_winner = r.u8();
        draft_total_picks = r.u16();
        draft_winner_payout = (int32_t)r.u32();
        draft_remaining_tenths = r.u16();
        uint8_t in = r.u8();
        if (!r.ok() || draft_active > 1 || !valid_stage_byte(draft_stage) || !valid_player_or_none(draft_winner) ||
            in > ::rivals::kMaxDraftItems)
            return false;
        draft_items.reserve(in);
        for (uint8_t i = 0; i < in; i++) {
            DraftItem it;
            it.id = r.u32();
            it.kind = r.u8();
            it.label = r.str8();
            if (!r.ok() || it.label.size() > ::rivals::kMaxDraftLabelLen)
                return false;
            draft_items.push_back(std::move(it));
        }
        for (int i = 0; i < ::rivals::kMaxPlayers; i++) {
            draft_players[i].picks_left = r.u8();
            draft_players[i].done = r.u8();
            uint8_t kn = r.u8();
            if (!r.ok() || draft_players[i].picks_left > ::rivals::kMaxDraftPicks || draft_players[i].done > 1 ||
                kn > ::rivals::kMaxDraftItems)
                return false;
            draft_players[i].picked.reserve(kn);
            for (uint8_t k = 0; k < kn; k++)
                draft_players[i].picked.push_back(r.u32());
            pending_payout[i] = (int32_t)r.u32();
            if (!r.ok())
                return false;
        }

        for (int i = 0; i < ::rivals::kMaxPlayers; i++)
            chronicle_totals[i] = (int32_t)r.u32();
        uint8_t hn = r.u8();
        if (!r.ok() || hn > ::rivals::kMaxChronicleHistory)
            return false;
        chronicle_history.reserve(hn);
        for (uint8_t i = 0; i < hn; i++) {
            ChronEntry e;
            e.stage = r.u8();
            e.player_id = r.u8();
            e.points = (int32_t)r.u32();
            e.reason = r.str8();
            if (!r.ok() || !valid_stage_byte(e.stage) || e.player_id >= ::rivals::kMaxPlayers ||
                e.reason.size() > ::rivals::kMaxReasonLen)
                return false;
            chronicle_history.push_back(std::move(e));
        }
        return r.ok();
    }
};

// K_REL_REQ (guest -> host): request a player relation change.
struct RelReq {
    uint8_t action = 0;
    uint8_t target = 0;

    void write(net::Writer& w) const
    {
        w.u8(action);
        w.u8(target);
    }
    bool read(net::Reader& r)
    {
        action = r.u8();
        target = r.u8();
        return r.ok() && action >= 1 && action <= 8 && target < ::rivals::kMaxPlayers;
    }
};

// K_INF_REQ (guest -> host): request an influence action.
struct InfReq {
    uint8_t action = 0;
    uint32_t npc_id = 0;
    uint8_t rival_id = 255;

    void write(net::Writer& w) const
    {
        w.u8(action);
        w.u32(npc_id);
        w.u8(rival_id);
    }
    bool read(net::Reader& r)
    {
        action = r.u8();
        npc_id = r.u32();
        rival_id = r.u8();
        return r.ok() && action >= 1 && action <= 4 && valid_player_or_none(rival_id);
    }
};

// K_DRAFT_REQ (guest -> host): pick an item or skip the stage-end draft.
struct DraftReq {
    uint8_t skip = 0;
    uint32_t item_id = 0;

    void write(net::Writer& w) const
    {
        w.u8(skip);
        w.u32(item_id);
    }
    bool read(net::Reader& r)
    {
        skip = r.u8();
        item_id = r.u32();
        return r.ok() && skip <= 1;
    }
};

// K_EVENT (host -> guests): notification event for UI/toast feed.
struct Event {
    uint8_t code = 0;
    uint8_t a = 0;
    uint8_t b = 0;
    int32_t value = 0;
    std::string text;

    void write(net::Writer& w) const
    {
        w.u8(code);
        w.u8(a);
        w.u8(b);
        w.u32((uint32_t)value);
        std::string t = text.size() > 128 ? text.substr(0, 128) : text;
        w.str8(t);
    }
    bool read(net::Reader& r)
    {
        code = r.u8();
        a = r.u8();
        b = r.u8();
        value = (int32_t)r.u32();
        text = r.str8();
        return r.ok() && code >= 1 && code <= 6 && text.size() <= 128;
    }
};

} // namespace wire::rivals
