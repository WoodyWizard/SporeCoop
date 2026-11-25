// MSG_RIVALS_FACTION = 81 (Family::npc 1.3, Wave 2 scaffold for 0.8.0, BUG-1546..BUG-1550):
// stage-partitioned reliable envelope for per-stage Rivals mode synchronization (faction assignment,
// base sites, per-player NPC relation tables, perspective swap, alliance gifts, PvP hits, and Space
// homeworld emigration). Payload layout: u8 stage (rivals::Stage 0..4), u8 subkind (1..255), followed
// by stage-defined bytes. Rules in wire/common.h.
#pragma once

#include <cstdint>
#include <vector>

#include "../net/protocol.h"
#include "../rivals_logic.h"
#include "common.h"

namespace wire::rivals_faction {

constexpr uint8_t kFamily = 4; // Family::npc in net/messages.def
constexpr uint16_t kMinor = 3; // npc 1.3 (0.8.0) adds MSG_RIVALS_FACTION = 81

// Conventional subkinds shared across stage modules (each stage owns its own payload after stage+subkind).
enum SubKind : uint8_t {
    SK_ASSIGN = 1,      // host -> guests: faction/base/species ownership assignment
    SK_NPC_RELS = 2,    // host -> guest: per-player NPC faction relation table
    SK_GIFT = 3,        // any -> host -> target: alliance resource/tool gift
    SK_PVP_HIT = 4,     // guest -> host or host -> target: war PvP hit / raid / capture event
    SK_EMIGRATE = 5,    // host -> guest / guest -> host: Space homeworld emigration notice/state
    SK_PROGRESS = 6,    // guest -> host: stage-specific progress / win signal
    SK_DRAFT_APPLY = 7, // host -> guest: apply copied draft unlock or winner resource payout
    SK_STAGE_CUSTOM = 16,
};

// Does a peer whose HELLO listed `families` read MSG_RIVALS_FACTION (npc 1.3+)?
inline bool peer_reads(bool has_families, const std::vector<net::FamilyRev>& families)
{
    if (!has_families)
        return false;
    for (const auto& f : families)
        if (f.family == kFamily)
            return f.rev == 1 && f.minor >= kMinor;
    return false;
}

inline void write_header(net::Writer& w, ::rivals::Stage stage, uint8_t subkind)
{
    w.u8((uint8_t)stage);
    w.u8(subkind);
}

inline bool read_header(net::Reader& r, ::rivals::Stage& stage_out, uint8_t& subkind_out)
{
    uint8_t s = r.u8();
    uint8_t k = r.u8();
    if (!r.ok() || s >= ::rivals::kStages || k == 0)
        return false;
    stage_out = (::rivals::Stage)s;
    subkind_out = k;
    return true;
}

} // namespace wire::rivals_faction
