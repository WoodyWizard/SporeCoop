// Own progress: the third value of the DNA setting (WP-P1 / P2 of 0.7.17, BUG-1469; the idea, the "waiting for X 64 %"
// notice, the stage-end gate and the social-win credit from PR #209 by IltonBJSilva). docs/03-gameplay.md#gameplay-own-progress.
//
// all.shared_dna: 0 own wallet (0.7.16's default: every gain counts for everybody, each spends its own, dna_own.h),
// 1 shared (one pool), 2 own progress: in Cell and Creature every player earns alone (its own meals, kills, friends,
// goal cards), has its own DNA and its own evolution bar; nobody evolves alone: the stage moves on when every bar is
// full and the vote agrees.
//
// * Each game keeps its own values (Cell: food S+0x1C and spent S+0x2C; Creature: DNA, the bar cPlayer +0x10F0, the
//   brain level [SporeCoop::g_BrainLevel]). No shared total (MSG_DNA, the Cell food total), no 1/N pace, a guest's
//   gain is applied on its own game; what the host's game pays for a guest's deed (a guest's kill of a host NPC, a
//   guest's social win: the hunt credit and the Actor scope) goes to that guest (MSG_PROGRESS kGain).
// * The host keeps every guest's values (kReport, every second) and the per-player store record `progress` beside its
//   save; a guest that opened a copy of the host's world (a join, a rejoin, a reload) asks (kAsk) and gets its own
//   values back (kSet): the copy holds the host's.
// * The stage end: Cell, the host's click on History's check is kept before the game's end sequence until every bar is
//   full (then the click runs by itself and the switch vote follows); Creature, the host's stage-end request is
//   refused until every bar is full (then BUG-1085's vote). A second click within kForceWindow s moves on without the
//   players still filling (a partner away from the keyboard): the vote decides.
// * Cell levels together (BUG-9287 of PR #209): the one pool is the host's game, populated at the host's level; an own
//   meal over the next level line (every 100 food) while a partner is still below it is clipped, the rest banked and
//   released when every partner reached the line.
// * Old peers: a peer below creature 1.3 / cell 1.2 does not read it. The host refuses value 2 at the start when one
//   is in the session (own wallet is played, a notice names it) and does not let a late one in while it plays it.
//
// Portable (no game, no Windows): tests/dna_tests.cpp.
#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "net/buffer.h"
#include "net/compat.h"

namespace own_progress {

// all.shared_dna's values (the key and its ini key stay as 0.7.16's)
enum Mode : int { kOwnWallet = 0, kShared = 1, kOwnProgress = 2 };
// (the default of a new world is settings_core.h kSharedDnaDefault: owner question 2 of docs/plan-0.7.17.md)

// the stage ids (modes.h is not portable)
constexpr uint32_t kCell = 0x1654C00, kCreature = 0x1654C01;
constexpr int kCellFull = 1000;  // Cell: food S+0x1C reaches 1000 (docs/re/cell.md 4)
constexpr int kCreatureFull = 4; // Creature: brain level 4 is "fully grown" (docs/re/creature.md#creature-editor-tags-runtime)

// the family minors from which a peer plays own progress (WP-0 of 0.7.17: creature 1.3, cell 1.2)
constexpr uint16_t kMinorCreature = 3, kMinorCell = 2;
inline bool peer_reads(const std::vector<net::FamilyRev>& fams, bool has)
{
    return net::compat::has_minor(fams, has, (uint8_t)net::Family::creature, 1, kMinorCreature) &&
           net::compat::has_minor(fams, has, (uint8_t)net::Family::cell, 1, kMinorCell);
}

// the setting as played: value 2 is read as 0 (own wallet) with `coop.fix core.own_progress off`
inline int mode_of(int setting, bool fix)
{
    if (setting == kOwnProgress)
        return fix ? kOwnProgress : kOwnWallet;
    return setting == kShared ? kShared : kOwnWallet;
}

// ---- one player's progress in its own game
struct Values {
    uint32_t mode = 0;  // kCell or kCreature (0: none)
    float dna = 0;      // Creature: GetEvolutionPoints (the HUD's DNA)
    float bar = 0;      // Creature: cPlayer +0x10F0 (the evolution bar, the stage's total earned)
    int32_t food = 0;   // Cell: S+0x1C (the bar, 0..1000)
    int32_t spent = 0;  // Cell: S+0x2C (the DNA its own cell editors spent; DNA = DNAForFood(food) - spent)
    uint8_t level = 0;  // Creature: the brain level
    bool operator==(const Values&) const = default;
};

inline bool is_stage(uint32_t m) { return m == kCell || m == kCreature; }

inline bool full(const Values& v)
{
    if (v.mode == kCell)
        return v.food >= kCellFull;
    if (v.mode == kCreature)
        return v.level >= kCreatureFull;
    return false;
}

inline int percent(const Values& v)
{
    int p = 0;
    if (v.mode == kCell)
        p = (int)((int64_t)v.food * 100 / kCellFull);
    else if (v.mode == kCreature)
        p = v.level * 100 / kCreatureFull;
    return std::clamp(p, 0, 100);
}

inline bool sane(const Values& v)
{
    return is_stage(v.mode) && std::isfinite(v.dna) && std::isfinite(v.bar) && v.dna >= 0 && v.dna < 1.0e7f &&
           v.bar > -1.0e7f && v.bar < 1.0e7f && v.food >= -2000 && v.food <= 2000 && v.spent >= -100000 &&
           v.spent <= 100000 && v.level <= 16;
}

inline void write_values(net::Writer& w, const Values& v)
{
    w.u32(v.mode);
    w.f32(v.dna);
    w.f32(v.bar);
    w.u32((uint32_t)v.food);
    w.u32((uint32_t)v.spent);
    w.u8(v.level);
}
inline bool read_values(net::Reader& r, Values& v)
{
    Values t;
    t.mode = r.u32();
    t.dna = r.f32();
    t.bar = r.f32();
    t.food = (int32_t)r.u32();
    t.spent = (int32_t)r.u32();
    t.level = r.u8();
    if (!r.ok() || !sane(t))
        return false;
    v = t;
    return true;
}

// ---- who still fills its bar (the players of the host's world: a guest elsewhere is not waited for)
struct Player {
    uint8_t id = 0;
    std::string name;
    bool known = false; // its progress of this stage is known (the host's own, a guest's report)
    uint32_t mode = 0;
    bool full = false;
    int pct = 0;
    int32_t food = 0; // Cell: its food (the level line)
};

// every listed player is full in `mode` (the list holds the player itself too); `waiting` names the others who are not
// ("Bob 64 %, Ann (not known yet)"), `me` is left out of it
inline bool all_full(const std::vector<Player>& ps, uint32_t mode, uint8_t me, std::string* waiting = nullptr)
{
    bool all = !ps.empty();
    std::string w;
    for (const auto& p : ps) {
        if (p.known && p.mode == mode && p.full)
            continue;
        all = false;
        if (p.id == me)
            continue;
        if (!w.empty())
            w += ", ";
        w += p.name.empty() ? "player " + std::to_string(p.id) : p.name;
        w += p.known && p.mode == mode ? " " + std::to_string(p.pct) + " %" : " (not known yet)";
    }
    if (waiting)
        *waiting = w;
    return all;
}

// ---- the stage end (host). A click (Cell: History's check; Creature: the stage-end request) while not every bar is
// full is held; a second one within kForceWindow s of the refusal goes on without the others (the vote decides)
constexpr double kForceWindow = 10.0;
enum class End { Go, Hold, Force };
struct EndGate {
    double refused_at = -1e9;
    End click(bool all, double now)
    {
        if (all) {
            refused_at = -1e9;
            return End::Go;
        }
        if (now - refused_at <= kForceWindow) {
            refused_at = -1e9;
            return End::Force;
        }
        refused_at = now;
        return End::Hold;
    }
};

// ---- the start (host): value 2 with an older peer in the session is refused; the notice names the players
struct Start {
    bool active = false;
    std::string notice; // "" none
};
inline Start decide_start(bool want, const std::vector<std::string>& old_names)
{
    Start s;
    if (!want)
        return s;
    if (old_names.empty()) {
        s.active = true;
        return s;
    }
    std::string n;
    for (size_t i = 0; i < old_names.size(); i++)
        n += (i ? (i + 1 == old_names.size() ? " and " : ", ") : "") + old_names[i];
    s.notice = "Own progress is not played in this world: " + n + (old_names.size() > 1 ? " play" : " plays") +
               " an older SporeCoop (own wallet is played instead; everybody needs 0.7.17 or newer)";
    return s;
}
// what a late joiner whose mod does not read own progress is told (the host does not let it in)
inline std::string update_text()
{
    return "this session plays own progress (each player's own DNA and evolution bar): update SporeCoop to 0.7.17 or "
           "newer to join it";
}

// ---- Cell levels together (BUG-9287 of PR #209). The level of a food count: cell_logic.h level_of_food
inline int level_of_food(int food) { return food >= 1000 ? 10 : food < 0 ? 1 : 1 + food / 100; }
// the highest food this game may have now, from the partners' food (INT_MAX: no limit). Within the lowest partner's
// level; once every partner is at that level's last point the next level opens (both at 499 held each other for good)
inline int food_cap(const std::vector<int32_t>& partners, int* low_level = nullptr)
{
    if (partners.empty())
        return INT_MAX;
    int low = *std::min_element(partners.begin(), partners.end());
    const int lv = level_of_food(low);
    if (low_level)
        *low_level = lv;
    if (lv >= level_of_food(kCellFull))
        return INT_MAX;
    const int line_end = lv * 100 - 1;
    return low >= line_end ? line_end + 100 : line_end;
}
// an own meal of `amount` at food `cur` under `cap`: the part the game takes now (the rest goes to `bank`)
inline int clip(int amount, int cur, int cap, int& bank)
{
    if (amount <= 0 || cap == INT_MAX || cur + amount <= cap)
        return amount;
    const int give = std::max(0, cap - cur);
    bank += amount - give;
    return give;
}
// the banked food that fits now
inline int release(int bank, int cur, int cap)
{
    if (bank <= 0)
        return 0;
    return cap == INT_MAX ? bank : std::clamp(cap - cur, 0, bank);
}

// ---- Creature: how restored values reach the game. The bar goes up through the game's AddEvolutionPoints (the brain
// upgrades see it), then the DNA is set (SetEvolutionPoints) and a bar that must go down is written directly
struct CreatureApply {
    float up = 0;          // through AddEvolutionPoints
    float dna = 0;         // then SetEvolutionPoints
    float bar_write = NAN; // then cPlayer +0x10F0 (only down)
    int level = -1;        // then the brain level (only down: a higher one comes with the bar), -1 none
};
inline CreatureApply creature_apply(float game_bar, int game_level, const Values& want)
{
    CreatureApply a;
    a.dna = want.dna;
    if (std::isfinite(game_bar)) {
        if (want.bar > game_bar + 0.5f)
            a.up = want.bar - game_bar;
        else if (want.bar < game_bar - 0.5f)
            a.bar_write = want.bar;
    }
    if (game_level >= 0 && want.level < game_level)
        a.level = want.level;
    return a;
}

// ---- MSG_PROGRESS (76, creature family): u8 kind, payload
enum Kind : uint8_t {
    kAsk = 1,    // guest -> host: u32 mode. A copy of the host's world opened here: my values of this stage, please
    kSet = 2,    // host -> guest: u32 mode, u8 has, Values when has (0: none kept, the world's values are yours)
    kReport = 3, // guest -> host: Values (every second while it plays the host's stage, at once on a change)
    kTable = 4,  // host -> guests: u8 flags (1 own progress played), u8 n, n x Row
    kGain = 5,   // host -> guest: f32 DNA its deed earned on the host's game, u8 cause (GainCause)
    kDebt = 6,   // WP-R1 (BUG-1472), the revive's progress debt: u8 op (DebtOp), f32 amount. host -> guest: kDebtAdd (a
                 // price charged) or kDebtSet (the record's debt, after its kAsk); guest -> host: kDebtSet (its debt now,
                 // after its gains paid some of it)
};
enum DebtOp : uint8_t { kDebtAdd = 1, kDebtSet = 2 };
enum GainCause : uint8_t { kGainHunt = 1, kGainFriend = 2, kGainActor = 3 };
constexpr uint8_t kTableActive = 1;
constexpr uint8_t kRowKnown = 1, kRowFull = 2;
constexpr size_t kMaxRows = 16;

inline void write_table(net::Writer& w, bool active, const std::vector<Player>& ps)
{
    w.u8(kTable);
    w.u8(active ? kTableActive : 0);
    size_t n = std::min(ps.size(), kMaxRows);
    w.u8((uint8_t)n);
    for (size_t i = 0; i < n; i++) {
        const Player& p = ps[i];
        w.u8(p.id);
        w.u32(p.mode);
        w.u8((uint8_t)((p.known ? kRowKnown : 0) | (p.full ? kRowFull : 0)));
        w.u8((uint8_t)std::clamp(p.pct, 0, 100));
        w.u32((uint32_t)p.food);
    }
}
// after the kind byte
inline bool read_table(net::Reader& r, bool& active, std::vector<Player>& out)
{
    uint8_t f = r.u8();
    unsigned n = r.u8();
    if (!r.ok() || n > kMaxRows)
        return false;
    std::vector<Player> t;
    for (unsigned i = 0; i < n; i++) {
        Player p;
        p.id = r.u8();
        p.mode = r.u32();
        uint8_t fl = r.u8();
        p.pct = std::min<int>(r.u8(), 100);
        p.food = (int32_t)r.u32();
        p.known = (fl & kRowKnown) != 0;
        p.full = (fl & kRowFull) != 0;
        if (!r.ok())
            return false;
        t.push_back(p);
    }
    active = (f & kTableActive) != 0;
    out = std::move(t);
    return true;
}

// ---- the per-player store record `progress` (pstore::kRecProgress) v1, under the player's stable key: u32 world (the
// host's stage world, modes::current_world), Values, f32 debt (the revive's progress debt, WP-R1 BUG-1472: revive_logic.h pay_from_gain). 29 bytes
constexpr uint16_t kStoreVersion = 1;
struct Record {
    uint32_t world = 0;
    Values v;
    float debt = 0;
    bool operator==(const Record&) const = default;
};
inline std::vector<uint8_t> encode(const Record& x)
{
    net::Writer w;
    w.u32(x.world);
    write_values(w, x.v);
    w.f32(x.debt);
    return w.data;
}
inline bool decode(const std::vector<uint8_t>& d, Record& out)
{
    if (d.size() != 29)
        return false;
    net::Reader r(d.data(), d.size());
    Record x;
    x.world = r.u32();
    if (!read_values(r, x.v))
        return false;
    x.debt = r.f32();
    if (!r.ok() || !std::isfinite(x.debt) || x.debt < 0 || x.debt > 1.0e7f)
        return false;
    out = x;
    return true;
}
// what a guest that opened the host's world gets back: its record of this world and stage, or nothing (a new player,
// another stage or world: the world's values are its own from now on)
inline const Values* restore_for(const Record* r, uint32_t world, uint32_t mode)
{
    return r && r->world == world && r->v.mode == mode && is_stage(mode) ? &r->v : nullptr;
}

// ---- the notices
inline std::string full_text(bool cell, const std::string& waiting)
{
    return std::string("Your evolution bar is full. Nobody evolves alone: keep playing; ") +
           (cell ? "click History's check" : "use the evolve button") +
           " once every partner's bar is full too (you will be told). Waiting for: " + waiting;
}
inline std::string remind_text(const std::string& waiting) { return "Your bar is full: still waiting for " + waiting; }
inline std::string hold_text(bool cell, const std::string& waiting)
{
    return std::string("Not yet: you move on to the ") + (cell ? "Creature" : "Tribe") +
           " stage together once every player's bar is full (waiting for: " + waiting + "). " +
           (cell ? "Your click is kept: the vote starts by itself then. Click again within 10 s to move on without them "
                   "(the vote decides)"
                 : "Use the evolve button again then, or within 10 s to move on without them (the vote decides)");
}
inline std::string force_text(const std::string& waiting)
{
    return "Moving on without " + waiting + ": the vote decides";
}

} // namespace own_progress
