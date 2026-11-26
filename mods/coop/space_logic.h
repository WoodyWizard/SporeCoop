// Small portable decisions of the Space co-op (spacecoop.cpp, space_map.cpp, space_allies.cpp),
// kept free of game calls for the unit tests (tests/space_tests.cpp, make test).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace spacelogic {

// ---------------- the co-op jump's target (BUG-0072, BUG-1562) ----------------
struct JumpPartner {
    uint8_t id;
    uint32_t star;      // the partner's star, or the last one known; 0 = not known
    bool allied = true; // BUG-1562: true when Space Rivals is off, or partner's relation is Ally
};

// The partner a jump goes to: `to` = that partner, 0xFF = any partner. With `skip_mine`, partners
// in my own star system are passed over (the co-op window's button names the first partner with a
// known star that is not mine and must jump to that one). The old jump(0xFF) took the first
// partner with any known star (skip_mine = false): with 3 players and the first partner in my
// system, every press was refused.
// BUG-1562: when `allies_only` is true (Space Rivals with `rivals.space_jump_ally` on), non-allied
// rivals (Neutral or War) are skipped; if `blocked_rival` is non-null and any matching partner was
// skipped because `!p.allied`, `*blocked_rival` is set to true.
inline bool pick_jump_target(const std::vector<JumpPartner>& partners, uint32_t here, uint8_t to, bool skip_mine,
                             uint8_t& id, uint32_t& star, bool allies_only = false, bool* blocked_rival = nullptr)
{
    if (blocked_rival)
        *blocked_rival = false;
    for (auto& p : partners) {
        if (p.id != to && to != 0xFF)
            continue;
        if (allies_only && !p.allied) {
            if (blocked_rival)
                *blocked_rival = true;
            continue;
        }
        if (p.star && (!skip_mine || p.star != here)) {
            id = p.id;
            star = p.star;
            return true;
        }
    }
    return false;
}

// ---------------- raids I aborted on mirror colonies (BUG-0136) ----------------
// space_allies keeps the raids it aborted (pointers only compared) so that a watched raid that
// ends by its abort does not count as won. An aborted raid that was never watched (still accepted,
// state 2) used to stay in the set for good, also across sessions: a later raid at the recycled
// address that was won counted as aborted, and the owner's raid was never completed. After every
// look at the watched raids, only aborted raids that are watched stay.
template <typename Watched>
inline void prune_aborted(std::set<void*>& aborted, const Watched& watched)
{
    for (auto it = aborted.begin(); it != aborted.end();)
        it = watched.count(*it) ? std::next(it) : aborted.erase(it);
}

// ---------------- an armada wave on the host's return to the colony (BUG-0227) ----------------
enum class WaveReturn { Fail, Won, Resume, NewRaid };
// away_result: the end state of the wave's raid seen while the host was away (0 none, 5 defended,
// 6 lost/plundered); running: the wave's mission is still a raid on the colony. A raid lost while
// away fails the contract, one defended counts as the wave won; else a running raid is resumed and
// a gone one replaced by a new raid.
inline WaveReturn wave_on_return(int away_result, bool running)
{
    if (away_result == 6)
        return WaveReturn::Fail;
    if (away_result == 5)
        return WaveReturn::Won;
    return running ? WaveReturn::Resume : WaveReturn::NewRaid;
}

// The key of the "HP raised once" set: the ship's object serial (cGameData mID) with the fix, so a
// new ship at a freed ship's address is raised too; its pointer without (0.7.3.0). Tagged so that
// the two kinds never match.
inline uint64_t tough_key(uintptr_t ptr, uint32_t serial, bool by_serial)
{
    return by_serial ? (1ull << 40) | serial : (uint64_t)ptr;
}

// ---------------- which game an owed refund belongs to (BUG-0225, GitHub #159) ----------------
// A refund (a trade gift or sale, a spice gift) is taken from the Space game in memory and may come
// back after that game was left, saved, reloaded or replaced. Where it is paid is decided by the
// game's identity, never by an address or a global save counter:
//   * epoch: spaceext::world_epoch() (one value per Space world in memory: a load, the galaxy menu,
//     another stage or a new player cEmpire give a new one; editors and adventures keep it);
//   * game: the save file of that game, "<slot dir>\<planet>.spo" in lower case (world.cpp
//     current_game_key); "" = unknown (the menu, snapshot.spo).
// The ledger records, per (epoch, game), the sequence number of the last save of that game made
// while that very world was in memory, and per slot directory the last time a received co-op world
// replaced the slot. Memory only, never saved, never sent.
struct WorldTag {
    uint32_t epoch = 0;
    std::string game;
    uint64_t seq = 0; // the ledger's clock when the tag was taken
};
enum class OwedAction { Pay, Drop, Keep };

inline std::string game_dir(const std::string& game)
{
    size_t p = game.find_last_of('\\');
    return p == std::string::npos ? std::string() : game.substr(0, p);
}

struct WorldLedger {
    uint64_t clock = 1;
    std::map<std::pair<uint32_t, std::string>, uint64_t> saved; // (epoch, game) -> seq of the last save
    std::map<std::string, uint64_t> replaced;                    // slot dir -> seq of the last replacement

    WorldTag tag(uint32_t epoch, const std::string& game) { return {epoch, game, clock++}; }
    // a save that completed, of `game`, made while the Space world `epoch` was in memory
    void note_save(uint32_t epoch, const std::string& game)
    {
        if (!epoch || game.empty())
            return;
        saved[{epoch, game}] = clock++;
        if (saved.size() > 256) // old worlds: the oldest entry goes (a refund that old is long settled)
            saved.erase(std::min_element(saved.begin(), saved.end(),
                                         [](auto& a, auto& b) { return a.second < b.second; }));
    }
    // a received world was written into the slot `dir` (not a save of the game that was there)
    void note_replaced(const std::string& dir)
    {
        if (dir.empty())
            return;
        replaced[dir] = clock++;
        if (replaced.size() > 64)
            replaced.erase(std::min_element(replaced.begin(), replaced.end(),
                                            [](auto& a, auto& b) { return a.second < b.second; }));
    }
    bool saved_after(const WorldTag& t) const
    {
        auto it = saved.find({t.epoch, t.game});
        return it != saved.end() && it->second > t.seq;
    }
    bool replaced_after(const WorldTag& t) const
    {
        auto it = replaced.find(game_dir(t.game));
        return it != replaced.end() && it->second > t.seq;
    }
};

// What to do with a refund taken under `t`, now that the Space world `epoch_now` of the game
// `game_now` is in memory:
//   * the same world in memory: Pay;
//   * the tag's game is unknown: Drop (no game could ever be proven to lack it);
//   * that game's slot got a received world since: Drop (that game no longer exists);
//   * now another game, or an unknown one: Keep (paid when that game is loaded again);
//   * the same game file loaded again: Pay if that world was saved after the take (the file lacks
//     the items), else Drop (the reloaded game still has them).
inline OwedAction owed_action(const WorldLedger& l, const WorldTag& t, uint32_t epoch_now, const std::string& game_now)
{
    if (t.epoch && t.epoch == epoch_now)
        return OwedAction::Pay;
    if (t.game.empty())
        return OwedAction::Drop;
    if (l.replaced_after(t))
        return OwedAction::Drop;
    if (game_now.empty() || game_now != t.game)
        return OwedAction::Keep;
    return l.saved_after(t) ? OwedAction::Pay : OwedAction::Drop;
}
constexpr size_t kOwedKeepMax = 32; // owed items kept for another game: the oldest go beyond this

// ---------------- the audit items of 0.7.17 (WP-X1, fix/w77-space) ----------------
// BUG-1485 (audit SPC-6): a sale's or an offer's payment that arrives now goes into the game now only when the seller
// is in Space (`here`) in the world the spice was taken from; else it is owed (paid by the owed-sales path, which
// decides by the world tag). `by_world`: the world tags are used (spaceext::owed_id(), BUG-0225)
inline bool sale_pays_now(bool here, bool by_world, uint32_t taken_epoch, uint32_t epoch_now)
{
    return here && (!by_world || taken_epoch == epoch_now);
}

// BUG-1486 (audit SPC-11): a planet record is a colony of the host's own empire (an armada contract's target): tech
// level 5 (any empire's colony, docs/re/space.md 5.2) with a cCivData of the host's political id, and not a partner's
// own-star mirror colony that carries the host's id (space_allies.cpp add_planet_own, BUG-0543)
inline bool host_colony(int tech, const std::vector<uint32_t>& civ_pols, uint32_t my_pol, bool own_mirror)
{
    if (tech != 5 || my_pol == 0xFFFFFFFFu || own_mirror)
        return false;
    return std::find(civ_pols.begin(), civ_pols.end(), my_pol) != civ_pols.end();
}

// BUG-1489 (audit SPC-15): a partner's terraforming delta (K_TERRA). A game tool's delta per message is a small part of
// the 0..1 range of the planet record; a non-finite value (a modified peer) is refused, a finite one is clamped to
// +-kTerraDeltaMax (more than the whole range never comes from a tool). true: `out` is the delta to apply
constexpr float kTerraDeltaMax = 1.0f;
inline bool terra_delta(float d, float& out)
{
    if (!std::isfinite(d))
        return false;
    out = std::clamp(d, -kTerraDeltaMax, kTerraDeltaMax);
    return true;
}

// BUG-1482 (audit SPC-3): the host gave a stored Rivals budget to a present guest (`coop.store adopt`). It sends the
// budget with a forced K_SET (have = kSetAdopted) until the guest's own report is that budget; until then the guest's
// reports (its money before the K_SET reached it) do not replace the adopted budget.
constexpr uint8_t kSetAdopted = 3;       // K_SET have: the host's word, taken even when the budget is known
constexpr double kAdoptResend = 2.0;     // s between forced K_SETs
constexpr double kAdoptGiveUp = 600.0;   // s: a guest that never confirms (stuck in an editor) gets its reports back
struct AdoptHold {
    int v = 0;
    double since = 0, sent = -1e18;
    bool on = false;
    void start(int value, double now)
    {
        v = value;
        since = now;
        sent = -1e18;
        on = true;
    }
    // a report of the guest's budget: true = take it as its budget (a report of the adopted value ends the hold)
    bool report(int got)
    {
        if (!on)
            return true;
        if (got == v) {
            on = false;
            return true;
        }
        return false;
    }
    // send the forced K_SET now?
    bool due(double now)
    {
        if (!on)
            return false;
        if (now - since > kAdoptGiveUp) {
            on = false;
            return false;
        }
        if (now - sent < kAdoptResend)
            return false;
        sent = now;
        return true;
    }
};

// BUG-1491 (audit SPC-17): an offer of ours that no answer came for: the receiver declines an offer it holds after
// kOfferLife (its tick), so with no answer kOfferLife + kOfferAnswerSlack after it went the receiver never took it (a
// full list on a 0.7.16.1 receiver, which sends no answer then): it comes back
constexpr double kOfferAnswerSlack = 30.0;
inline bool offer_unanswered(double sent, double now, double offer_life) { return now - sent > offer_life + kOfferAnswerSlack; }

// BUG-1539: Space colonize tutorial mission (`cMissionColonize`, initial tutorial id 0x398ACA5E). When an allied
// partner colonizes the mission's target planet (or completes the initial colonize tutorial mission), space_allies
// mirrors the partner's colony onto that planet so the remaining players cannot place a colony there. Advance their
// active `cMissionColonize` (state 3, mColonizeMissionState < 2) through the game's own FinishObjective helper,
// except in Rivals mode where players compete as separate empires.
constexpr uint32_t kColonizeTutorialId = 0x398ACA5Eu;
inline bool colonize_mission_done_by_partner(bool fix, bool in_space, bool connected, bool rivals, int mission_state,
                                             uint32_t colonize_state, uint32_t mission_id, bool target_colonized_by_partner,
                                             bool partner_finished_tutorial)
{
    if (!fix || !in_space || !connected || rivals)
        return false;
    if (mission_state != 3 || colonize_state >= 2)
        return false;
    return target_colonized_by_partner || (mission_id == kColonizeTutorialId && partner_finished_tutorial);
}

// BUG-1559 (GitHub #221) & BUG-1570 (Discord Doctor): Space tutorial unlock, starter drives, and
// Flight School starter tools synchronization.
// When the host skips the Space tutorial (or completes Flight School / Story201 / scans the wreck),
// Spore sets cMissionManager +0x16/+0x17/+0x18, adds Flight School starter tools via
// cPlayerInventory::AddDefaultTools (0x00FF4590, prop 0xF0609EC3: scan, laserlevel1, abduct,
// dropcargo, healthupgrade1, energyupgrade1), and adds interplanetarydrive (0x4133FE05) /
// interstellardrive (0x2569FAE8) directly without a synced cMission::SetState reward broadcast.
// The host broadcasts its monotonic tutorial/drive/starter mask via K_MISSION with a synthetic
// mission_id sentinel (kTutorialSyncMagic | mask) so wire/space_core.h and the space wire lock stay
// unchanged while older peers still receive the drive tools from K_MISSION's tool list.
constexpr uint32_t kTutorialSyncMagic = 0x54555400u; // 'T','U','T',0
constexpr uint32_t kTutorialSyncMask = 0xFFFFFF00u;
constexpr uint32_t kToolInterstellarDrive = 0x2569FAE8u;
constexpr uint32_t kToolInterplanetaryDrive = 0x4133FE05u;
constexpr uint32_t kToolScan = 0x08EF6A62u;            // scan (prop 0xF0609EC3)
constexpr uint32_t kToolLaser1 = 0x6DB01899u;          // laserlevel1 (prop 0xF0609EC3)
constexpr uint32_t kToolLaser2 = 0x6DB0189Au;          // laserlevel2
constexpr uint32_t kToolLaser3 = 0x6DB0189Bu;          // laserlevel3
constexpr uint32_t kToolAbduct = 0xF58C1654u;          // abduct (prop 0xF0609EC3)
constexpr uint32_t kToolDropCargo = 0xD001F5E2u;       // dropcargo (prop 0xF0609EC3)
constexpr uint32_t kToolHealth1 = 0x88914EF2u;         // healthupgrade1 (prop 0xF0609EC3)
constexpr uint32_t kToolHealth2 = 0x88914EF1u;         // healthupgrade2
constexpr uint32_t kToolHealth3 = 0x88914EF0u;         // healthupgrade3
constexpr uint32_t kToolHealth4 = 0x88914EF7u;         // healthupgrade4
constexpr uint32_t kToolHealth5 = 0x88914EF6u;         // healthupgrade5
constexpr uint32_t kToolEnergy1 = 0x33E11760u;         // energyupgrade1 (prop 0xF0609EC3)
constexpr uint32_t kToolEnergy2 = 0x33E11763u;         // energyupgrade2
constexpr uint32_t kToolEnergy3 = 0x33E11762u;         // energyupgrade3
constexpr uint32_t kToolEnergy4 = 0x33E11765u;         // energyupgrade4
constexpr uint32_t kToolEnergy5 = 0x33E11764u;         // energyupgrade5
constexpr uint32_t kToolInterstellarUp1 = 0x4F320866u; // interstellardrive_upgrade1
constexpr uint32_t kToolInterstellarUp2 = 0x4F320865u; // interstellardrive_upgrade2
constexpr uint32_t kToolInterstellarUp3 = 0x4F320864u; // interstellardrive_upgrade3
constexpr uint32_t kToolInterstellarUp4 = 0x4F320863u; // interstellardrive_upgrade4
constexpr uint32_t kToolInterstellarUp5 = 0x4F320862u; // interstellardrive_upgrade5

inline bool is_laser_tool(uint32_t h) { return h == kToolLaser1 || h == kToolLaser2 || h == kToolLaser3; }
inline bool is_health_tool(uint32_t h)
{
    return h == kToolHealth1 || h == kToolHealth2 || h == kToolHealth3 || h == kToolHealth4 || h == kToolHealth5;
}
inline bool is_energy_tool(uint32_t h)
{
    return h == kToolEnergy1 || h == kToolEnergy2 || h == kToolEnergy3 || h == kToolEnergy4 || h == kToolEnergy5;
}
inline bool is_interstellar_tool(uint32_t h)
{
    return h == kToolInterstellarDrive || h == kToolInterstellarUp1 || h == kToolInterstellarUp2 ||
           h == kToolInterstellarUp3 || h == kToolInterstellarUp4 || h == kToolInterstellarUp5;
}

// BUG-1570: When restoring a guest's kept cargo hold (`apply_inv` in space_trade.cpp), never strip
// permanent Flight School starter tools or starter drives that already exist in the loaded host save
// just because an older kept-hold record (saved pre-Flight-School) lacked them. For upgraded tool lines,
// protect the base tier only when the kept hold has no higher-tier replacement in that same line.
inline bool keep_starter_in_restore(bool starter_fix, uint32_t tool_hash, const std::vector<uint32_t>& want_tools)
{
    if (!starter_fix)
        return false;
    if (tool_hash == kToolScan || tool_hash == kToolAbduct || tool_hash == kToolDropCargo ||
        tool_hash == kToolInterplanetaryDrive)
        return true;
    if (tool_hash == kToolLaser1)
        return std::none_of(want_tools.begin(), want_tools.end(), is_laser_tool);
    if (tool_hash == kToolHealth1)
        return std::none_of(want_tools.begin(), want_tools.end(), is_health_tool);
    if (tool_hash == kToolEnergy1)
        return std::none_of(want_tools.begin(), want_tools.end(), is_energy_tool);
    if (tool_hash == kToolInterstellarDrive)
        return std::none_of(want_tools.begin(), want_tools.end(), is_interstellar_tool);
    return false;
}

constexpr uint8_t kTutFirstComm = 1u << 0;           // cMissionManager +0x16 mbFirstCommunication
constexpr uint8_t kTutFlightSchool = 1u << 1;        // cMissionManager +0x17 mbFlightSchoolFinished
constexpr uint8_t kTutMission201 = 1u << 2;          // cMissionManager +0x18 mbMission201Finished
constexpr uint8_t kTutInterstellarDrive = 1u << 3;   // tool 0x2569FAE8 (interstellardrive or upgrade1..5)
constexpr uint8_t kTutInterplanetaryDrive = 1u << 4; // tool 0x4133FE05 (interplanetarydrive)
constexpr uint8_t kTutStarterKit = 1u << 5;          // BUG-1570: Flight School starter tools (0xF0609EC3)
constexpr uint8_t kTutAllBits = kTutFirstComm | kTutFlightSchool | kTutMission201 | kTutInterstellarDrive |
                                kTutInterplanetaryDrive | kTutStarterKit;

struct TutorialState {
    bool first_comm = false;
    bool flight_school = false;
    bool mission201 = false;
    bool has_interstellar = false;
    bool has_interplanetary = false;
    bool has_starter_kit = false; // host mask bit 5
    bool has_scan = false;
    bool has_laser = false;       // laserlevel1..3
    bool has_abduct = false;
    bool has_drop_cargo = false;
    bool has_health = false;      // healthupgrade1..5
    bool has_energy = false;      // energyupgrade1..5
};

struct TutorialApply {
    bool set_first_comm = false;
    bool set_flight_school = false;
    bool set_mission201 = false;
    bool grant_interstellar = false;
    bool grant_interplanetary = false;
    bool grant_scan = false;
    bool grant_laser = false;
    bool grant_abduct = false;
    bool grant_drop_cargo = false;
    bool grant_health = false;
    bool grant_energy = false;
    bool any_flag() const { return set_first_comm || set_flight_school || set_mission201; }
    bool any_drive() const { return grant_interstellar || grant_interplanetary; }
    bool any_starter() const
    {
        return grant_scan || grant_laser || grant_abduct || grant_drop_cargo || grant_health || grant_energy;
    }
    bool any() const { return any_flag() || any_drive() || any_starter(); }
};

inline uint8_t tutorial_pack(const TutorialState& s)
{
    uint8_t m = 0;
    if (s.first_comm)
        m |= kTutFirstComm;
    if (s.flight_school)
        m |= kTutFlightSchool;
    if (s.mission201)
        m |= kTutMission201;
    if (s.has_interstellar)
        m |= kTutInterstellarDrive;
    if (s.has_interplanetary)
        m |= kTutInterplanetaryDrive;
    if (s.has_starter_kit)
        m |= kTutStarterKit;
    return m;
}

inline TutorialState tutorial_unpack(uint8_t mask)
{
    TutorialState s;
    s.first_comm = (mask & kTutFirstComm) != 0;
    s.flight_school = (mask & kTutFlightSchool) != 0;
    s.mission201 = (mask & kTutMission201) != 0;
    s.has_interstellar = (mask & kTutInterstellarDrive) != 0;
    s.has_interplanetary = (mask & kTutInterplanetaryDrive) != 0;
    s.has_starter_kit = (mask & kTutStarterKit) != 0;
    return s;
}

inline uint32_t tutorial_sync_mission_id(uint8_t mask) { return kTutorialSyncMagic | (mask & kTutAllBits); }

inline bool is_tutorial_sync_mission_id(uint32_t mission_id, uint8_t& out_mask)
{
    if ((mission_id & kTutorialSyncMask) != kTutorialSyncMagic)
        return false;
    out_mask = (uint8_t)(mission_id & kTutAllBits);
    return true;
}

// Guest side: decide which monotonic tutorial unlocks, starter drives, and Flight School starter tools
// to apply. Never downgrades flags (1 -> 0) and never removes tools. Respects elsewhere / space_other
// and delays tool grants while space_trade is restoring a rejoined guest's kept cargo hold (hold_grants).
inline TutorialApply tutorial_sync_decide(bool fix, bool is_host, bool in_space, bool connected, bool elsewhere,
                                          bool space_other, bool hold_grants, bool has_mission_mgr, bool has_inventory,
                                          uint8_t host_mask, const TutorialState& guest, bool starter_fix = true)
{
    TutorialApply out;
    if (!fix || is_host || !in_space || !connected || elsewhere || space_other)
        return out;
    TutorialState host = tutorial_unpack(host_mask);
    if (has_mission_mgr) {
        out.set_first_comm = host.first_comm && !guest.first_comm;
        out.set_flight_school = host.flight_school && !guest.flight_school;
        out.set_mission201 = host.mission201 && !guest.mission201;
    }
    if (has_inventory && !hold_grants) {
        out.grant_interstellar = host.has_interstellar && !guest.has_interstellar;
        out.grant_interplanetary = host.has_interplanetary && !guest.has_interplanetary;
        if (starter_fix && (host.flight_school || host.has_starter_kit)) {
            out.grant_scan = !guest.has_scan;
            out.grant_laser = !guest.has_laser;
            out.grant_abduct = !guest.has_abduct;
            out.grant_drop_cargo = !guest.has_drop_cargo;
            out.grant_health = !guest.has_health;
            out.grant_energy = !guest.has_energy;
        }
    }
    return out;
}

// Host side: decide whether to broadcast the current tutorial/drive mask now.
inline bool tutorial_sync_should_send(bool fix, bool is_host, bool in_space, bool connected, uint8_t cur_mask,
                                      uint8_t last_sent_mask, uint32_t cur_epoch, uint32_t last_sent_epoch,
                                      uint64_t cur_peers_mask, uint64_t last_sent_peers_mask)
{
    if (!fix || !is_host || !in_space || !connected || cur_mask == 0 || cur_peers_mask == 0)
        return false;
    if (cur_epoch != last_sent_epoch)
        return true;
    if (cur_mask != last_sent_mask)
        return true;
    if ((cur_peers_mask & ~last_sent_peers_mask) != 0)
        return true;
    return false;
}

// BUG-1582: Co-op Space contracts must never auto-offer in Space Rivals (regardless of space.contracts period),
// and an active/pending contract must reset cleanly when the Space world changes so timers never leak across saves.
inline bool contract_auto_offer_allowed(bool fix, bool in_space, bool has_partners, double period,
                                        bool space_rivals_active, bool legacy_rivals)
{
    if (!in_space || !has_partners || period <= 0.0)
        return false;
    if (fix && (space_rivals_active || legacy_rivals))
        return false;
    return true;
}

inline bool contract_should_reset_on_world_change(bool fix, uint32_t contract_epoch, uint32_t cur_epoch,
                                                  bool has_contract)
{
    return fix && has_contract && contract_epoch != 0 && cur_epoch != 0 && contract_epoch != cur_epoch;
}

// BUG-1582: In Space Rivals PvP, prefer the rival's mirror empire political ID for war puppets so Spore labels
// their ship with their empire name instead of "Pirates" (0xFFFFFFFF), falling back to enemy_fallback when no
// mirror empire exists yet.
inline uint32_t rival_puppet_pol(bool fix, bool rivals_on, bool at_war, uint32_t my_pol, uint32_t rival_mirror_pol,
                                 uint32_t enemy_fallback = 0xFFFFFFFFu)
{
    if (!rivals_on || !at_war)
        return my_pol;
    if (fix && rival_mirror_pol != 0 && rival_mirror_pol != 0xFFFFFFFFu && rival_mirror_pol != my_pol)
        return rival_mirror_pol;
    return enemy_fallback;
}

} // namespace spacelogic
