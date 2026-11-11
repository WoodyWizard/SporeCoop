// The session settings (F3 of docs/plan-0.7.12.md, docs/07-modes.md#modes-session-settings): one table of the
// co-op mechanics the host can switch, declared once here (name, group, label, default, scope, help), read by
// every module through settings.h. This part is portable (no game calls) so that `make test` checks the rules,
// the wire format and the file format (tests/validate_tests.cpp).
//
// Scope: Session = the host sets it before the start (in the lobby, the galaxy menu: no world played), it is kept
// with the host's world; Live = the host can change it during play too. A guest never sets one: it plays with
// the host's table (MSG_SETTINGS) and with its own values only while no host sent one (an older host).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "net/buffer.h"

namespace settings {

enum class Scope : uint8_t { Session, Live };

// one row per setting; the order is the UI's order (F1 lists them by group)
enum Id : int {
    kCellRegen,      // Cell: regeneration near a partner ("shared healing", cellsync.cpp symbiosis_tick)
    kPartnerArrows,  // all stages: an arrow to every partner off screen (F2 draws it); off in Rivals mode
    kPartnerMap,     // Creature, Tribe: the partners on the game's minimap as triangles in their colour (F2)
    kDnaPace,        // all stages: DNA / food at the single-player pace (BUG-0510, dna_pace.h)
    kNpcTargets,     // Creature: the host's NPC animals treat partners like the host's creature (npc.cpp)
    kPackRescue,     // Creature: a deadly hit near a partner leaves 40 % health (teamplay.cpp)
    kPackHeal,       // Creature: partners heal each other nearby (teamplay.cpp tick)
    kJointHunt,      // Creature: a target another player hit deals +25 % (teamplay.cpp on_player_hit)
    // step 3 of F3 (feat/w46-settings-all): every other co-op mechanic of the plan's table, defaults = 0.7.11
    kNameTags,       // all stages: the name tag over every partner (coop.cpp on_overlay)
    kFriendlyFire,   // Cell, Creature, Tribe, Civ: players can hurt each other (Space keeps its rivals mode)
    kPlayerLimit,     // the players a session takes (2..4, the host counted; net/session.cpp admit)
    kLateJoin,       // a player may join a session whose host plays its world already
    kCellKnockout,   // Cell: a lethal hit next to a partner knocks out (revive within 10 s) instead of killing
    kCellRespawn,    // Cell: nobody revived the knocked-out cell in 10 s: it respawns next to the partner, small
    kSwitchVote,     // Cell -> Creature: the host's switch waits for a vote of everybody (BUG-0315)
    kCreatureMode,   // Cell, Creature: 0 every player's own creature, 1 everybody plays the host's (F5)
    kEditorNow,      // Creature: the host's evolution editor opens at once, guests get an offer (BUG-0516)
    kTribePincer,    // Tribe: members of two players hitting one tribe within 10 s deal +50 %
    kTribeAura,      // Tribe: members near their own chief hit +20 %, a chief next to a partner's member keeps 30 %
    kTribeDuet,      // Tribe: performances of two players for one village fill its mini-game faster
    kTribeEpic,      // Tribe: now and then an epic creature comes to the village (80 food killed together)
    kCivRoles,       // Civ: the governors' roles (military, economic, religious) and their bonuses
    kCivSiege,       // Civ: two players attacking one city capture it 50 % faster
    kCivSwVote,      // Civ: a superweapon fires after a vote of everybody
    kSpaceDisable,   // Space: a destroyed ship next to a partner is only disabled for 20 s
    kSpaceTrade,     // Space: tool gifts and trade between players (space_trade.cpp)
    kSpaceContracts, // Space: co-op contracts (space_contracts.cpp)
    kSpaceDiplomacy, // Space: shared relations with the NPC empires (space_diplomacy.cpp)
    kSpaceAllies,    // Space: the partners' colonies as an allied empire (space_allies.cpp)
    kSpaceFinale,    // Space: the galaxy's core together (space_finale.cpp)
    kSpaceRivals,    // Space: the host may switch to the rivals mode (space_rivalry.cpp)
    kCount
};

struct Def {
    const char* key;     // the console / wire / file name ("cell.regen"); never renamed (stored worlds keep it)
    const char* group;   // "All stages", "Cell", "Creature", ...
    const char* label;   // what the lobby shows
    const char* help;    // one sentence: what on and off do
    int def;             // the built-in default (a new world: coop.ini's `ini` key overrides it)
    int min, max;        // 0..1 for a switch
    Scope scope;
    const char* ini;     // coop.ini [coop] key whose value is the default of a new world, or nullptr
    bool off_in_rivals;  // read as off while the Rivals mode is on (the value itself is kept)
    const char* names = nullptr; // a choice of named values: "name0|name1|..." (value_text, the lobby), else on / off
};

inline const Def kDefs[kCount] = {
    {"cell.regen", "Cell", "Regeneration near a partner",
     "on: a player's cell regains health while a partner swims close by; off: health comes back as in a solo game",
     1, 0, 1, Scope::Live, nullptr, false},
    {"all.partner_arrows", "All stages", "Partner arrows",
     "on: an arrow at the screen edge points to every partner off screen (never in Rivals mode); off: no arrows",
     1, 0, 1, Scope::Live, nullptr, true},
    {"all.partner_map", "All stages", "Partners on the minimap",
     "on: every partner shows on the minimap (Creature, Tribe) as a triangle in its colour; off: not shown",
     1, 0, 1, Scope::Live, nullptr, false},
    {"all.dna_pace", "All stages", "Progress at the single-player pace",
     "on: every gain of DNA / food counts 1/N for N players (a solo game's pace); off: every player's gain counts whole",
     1, 0, 1, Scope::Session, "dna_pace", false},
    {"creature.npcs_target_partners", "Creature", "NPC animals treat partners like the host",
     "on: the host's animals notice, chase and attack every player's creature; off: only the host's own",
     1, 0, 1, Scope::Session, "npcs_target_partners", false},
    {"creature.pack_rescue", "Creature", "Pack rescue",
     "on: a deadly hit next to a partner leaves 40 % health (once a minute); off: the hit kills",
     1, 0, 1, Scope::Session, "teamplay", false},
    {"creature.pack_heal", "Creature", "Partners heal each other nearby",
     "on: health regenerates while a partner is close; off: as in a solo game",
     1, 0, 1, Scope::Live, "teamplay", false},
    {"creature.joint_hunt", "Creature", "Joint hunt bonus",
     "on: a hit on a target another player hit in the last seconds deals +25 % damage; off: no bonus",
     1, 0, 1, Scope::Session, "teamplay", false},
    {"all.name_tags", "All stages", "Name tags over players",
     "on: every partner's name shows over its creature, tribe chief, vehicle or ship; off: no name tags",
     1, 0, 1, Scope::Live, nullptr, false},
    {"all.friendly_fire", "All stages", "Friendly fire",
     "on: players can hurt each other in Cell, Creature, Tribe and Civ (Space keeps its rivals mode); off: no player hurts another",
     0, 0, 1, Scope::Session, nullptr, false},
    {"all.max_players", "All stages", "Players who can join",
     "how many players the session takes, the host counted (2 to 4); a player over it is told the session is full",
     4, 2, 4, Scope::Live, nullptr, false},
    {"all.late_join", "All stages", "Joining after the start",
     "on: a player can join while the host plays its world; off: only in the galaxy menu (a player who lost the connection may come back)",
     1, 0, 1, Scope::Live, nullptr, false},
    {"cell.knockout", "Cell", "Knock-out instead of death",
     "on: a lethal hit next to a partner knocks the cell out, a partner revives it within 10 s; off: the cell dies as in a solo game",
     1, 0, 1, Scope::Session, nullptr, false},
    {"cell.respawn_small", "Cell", "Respawn next to the partner",
     "on: a knocked-out cell nobody revived respawns next to the partner as a small cell; off: it dies as in a solo game",
     1, 0, 1, Scope::Session, nullptr, false},
    {"core.switch_vote", "Cell", "Vote before the Creature stage",
     "on: the host's switch from Cell to Creature waits for a vote of everybody; off: the stage starts at once",
     1, 0, 1, Scope::Session, nullptr, false},
    {"creature.mode", "Creature", "Creature",
     "own: every player plays its own creature and evolves it; host: everybody plays the host's creature, only the host evolves it (the partners get no editor)",
     0, 0, 1, Scope::Session, nullptr, false, "own|host"},
    {"core.editor_now", "Creature", "Host's editor opens at once",
     "on: the host's evolution editor opens at once, each partner gets an Evolve now offer; off: everybody votes first",
     1, 0, 1, Scope::Session, nullptr, false},
    {"tribe.pincer", "Tribe", "Pincer bonus",
     "on: members of two players hitting the same tribe within 10 s deal +50 % damage; off: no bonus",
     1, 0, 1, Scope::Session, nullptr, false},
    {"tribe.chief_aura", "Tribe", "Chief aura",
     "on: members near their own chief hit +20 %, a chief about to fall next to a partner's member keeps 30 % health; off: no aura",
     1, 0, 1, Scope::Session, nullptr, false},
    {"tribe.duet", "Tribe", "Duet",
     "on: performances of two players' members for the same village fill its mini-game faster; off: as in a solo game",
     1, 0, 1, Scope::Session, nullptr, false},
    {"tribe.epic", "Tribe", "Epic creature event",
     "on: now and then an epic creature comes to the village (80 food when killed together); off: no co-op epic",
     1, 0, 1, Scope::Session, nullptr, false},
    {"civ.roles", "Civilization", "Roles",
     "on: every player governs with a role (military, economic, religious) and its bonus; off: no roles, no bonuses",
     1, 0, 1, Scope::Session, nullptr, false},
    {"civ.joint_siege", "Civilization", "Joint siege bonus",
     "on: a city attacked by two players' vehicles is captured 50 % faster; off: no bonus",
     1, 0, 1, Scope::Session, nullptr, false},
    {"civ.superweapon_vote", "Civilization", "Superweapons need a vote",
     "on: a superweapon fires after a vote of everybody; off: it fires at once",
     1, 0, 1, Scope::Session, nullptr, false},
    {"space.disable", "Space", "Disabled, not destroyed",
     "on: a destroyed ship next to a partner is only disabled for 20 s and the partner repairs it; off: it is destroyed as in a solo game",
     1, 0, 1, Scope::Session, nullptr, false},
    {"space.trade", "Space", "Trade and gifts",
     "on: players give each other tools and trade spice; off: no trade between players",
     1, 0, 1, Scope::Session, nullptr, false},
    {"space.contracts", "Space", "Co-op contracts",
     "on: contracts for the players together; off: none",
     1, 0, 1, Scope::Session, nullptr, false},
    {"space.diplomacy", "Space", "Shared diplomacy",
     "on: the relations with the NPC empires are the players' together; off: every player's own",
     1, 0, 1, Scope::Session, nullptr, false},
    {"space.allies", "Space", "Partners as allies",
     "on: the partners' colonies show as an allied empire; off: not shown",
     1, 0, 1, Scope::Session, nullptr, false},
    {"space.finale", "Space", "Finale together",
     "on: the galaxy's core is entered together; off: as in a solo game",
     1, 0, 1, Scope::Session, nullptr, false},
    {"space.rivals", "Space", "Rivals mode allowed",
     "on: the host can switch Space to the rivals mode (own empires, budgets, fights); off: the players stay allies",
     1, 0, 1, Scope::Session, nullptr, false},
};

inline const Def& def(int id) { return kDefs[id]; }

inline int find(const std::string& key)
{
    for (int i = 0; i < kCount; i++)
        if (key == kDefs[i].key)
            return i;
    return -1;
}

inline int clamp_value(int id, int v) { return v < kDefs[id].min ? kDefs[id].min : v > kDefs[id].max ? kDefs[id].max : v; }

// the name of value v of a named setting ("" when it has no names or no such value)
inline std::string value_name(int id, int v)
{
    const char* n = kDefs[id].names;
    for (int i = 0; n && *n; i++) {
        const char* e = strchr(n, '|');
        size_t len = e ? (size_t)(e - n) : strlen(n);
        if (i == v)
            return std::string(n, len);
        n = e ? e + 1 : nullptr;
    }
    return "";
}

// "on", "off", "yes", "no", "default", a value's name (creature.mode: own, host) or a number; false when it is none of them
inline bool parse_value(int id, const std::string& s, int& out)
{
    if (s == "on" || s == "yes" || s == "true") {
        out = 1;
    } else if (s == "off" || s == "no" || s == "false") {
        out = 0;
    } else if (s == "default") {
        out = kDefs[id].def;
    } else if (kDefs[id].names && [&] {
                   for (int i = kDefs[id].min; i <= kDefs[id].max; i++)
                       if (value_name(id, i) == s)
                           return out = i, true;
                   return false;
               }()) {
    } else {
        char* end = nullptr;
        long v = strtol(s.c_str(), &end, 10);
        if (s.empty() || !end || *end)
            return false;
        out = (int)v;
    }
    if (out < kDefs[id].min || out > kDefs[id].max)
        return false;
    return true;
}

inline std::string value_text(int id, int v)
{
    std::string nm = value_name(id, v);
    if (!nm.empty())
        return nm;
    if (kDefs[id].min == 0 && kDefs[id].max == 1)
        return v ? "on" : "off";
    return std::to_string(v);
}

struct Values {
    int v[kCount];
    Values()
    {
        for (int i = 0; i < kCount; i++)
            v[i] = kDefs[i].def;
    }
    bool operator==(const Values& o) const { return !memcmp(v, o.v, sizeof v); }
    bool operator!=(const Values& o) const { return !(*this == o); }
};

// who may change a setting now. host_or_alone: this game hosts or plays without a session (a guest never sets
// one); started: this game plays a world (the host's world is open: the session is under way)
enum class Refusal { None, Guest, SessionStarted };
inline Refusal can_set(int id, bool host_or_alone, bool started)
{
    if (!host_or_alone)
        return Refusal::Guest;
    if (kDefs[id].scope == Scope::Session && started)
        return Refusal::SessionStarted;
    return Refusal::None;
}

inline const char* refusal_text(Refusal r)
{
    switch (r) {
    case Refusal::Guest: return "the host decides the session settings";
    case Refusal::SessionStarted: return "a session setting: the host changes it before the start (in the galaxy menu, no world loaded)";
    case Refusal::None: break;
    }
    return "";
}

// ---- MSG_SETTINGS (host -> guests, reliable): u8 kind 0, u8 flags (1: the host plays its world, the session
// settings are fixed), u8 n, n x {str8 key, i32 value}. By name, so that a newer or older peer skips what it does
// not know; a setting the host did not send keeps the guest's own value.
constexpr uint8_t kFlagStarted = 1;

inline void write_msg(net::Writer& w, const Values& v, bool started)
{
    w.u8(0);
    w.u8(started ? kFlagStarted : 0);
    w.u8((uint8_t)kCount);
    for (int i = 0; i < kCount; i++) {
        w.str8(kDefs[i].key);
        w.u32((uint32_t)v.v[i]);
    }
}

// fills `got` (the values the host sent; others untouched) and `sent` (which ids it named); false when the
// message is cut short or not a kind this build knows (nothing is applied then)
inline bool read_msg(net::Reader& r, Values& got, bool (&sent)[kCount], bool& started)
{
    for (auto& s : sent)
        s = false;
    uint8_t kind = r.u8();
    uint8_t flags = r.u8();
    unsigned n = r.u8();
    if (!r.ok() || kind != 0)
        return false;
    Values tmp = got;
    bool seen[kCount] = {};
    for (unsigned i = 0; i < n; i++) {
        std::string key = r.str8();
        int32_t val = (int32_t)r.u32();
        if (!r.ok())
            return false;
        int id = find(key);
        if (id < 0)
            continue; // a newer host's setting
        tmp.v[id] = clamp_value(id, val);
        seen[id] = true;
    }
    got = tmp;
    for (int i = 0; i < kCount; i++)
        sent[i] = seen[i];
    started = (flags & kFlagStarted) != 0;
    return true;
}

// ---- the stored file of a world (<profile>/Spore/SporeCoop/settings/<crc>.txt): "world <name>", then
// "<key> <value>" per setting. A key this build does not know is skipped; a missing one takes the default.
inline std::string format_file(const std::string& world, const Values& v)
{
    std::string s = "world " + world + "\n";
    for (int i = 0; i < kCount; i++)
        s += std::string(kDefs[i].key) + " " + std::to_string(v.v[i]) + "\n";
    return s;
}

// false when the file names another world (a crc collision) or has no world line; `out` starts from `defaults`
inline bool parse_file(const std::string& text, const std::string& world, const Values& defaults, Values& out)
{
    out = defaults;
    bool same = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        std::string l = text.substr(at, e == std::string::npos ? std::string::npos : e - at);
        at = e == std::string::npos ? text.size() : e + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' '))
            l.pop_back();
        if (l.rfind("world ", 0) == 0) {
            same = l.substr(6) == world;
            continue;
        }
        size_t sp = l.find(' ');
        if (sp == std::string::npos)
            continue;
        int id = find(l.substr(0, sp)), v = 0;
        if (id >= 0 && parse_value(id, l.substr(sp + 1), v))
            out.v[id] = v;
    }
    if (!same)
        out = defaults;
    return same;
}

// What a world opened by the host (or a player alone) plays with. pending: the host changed settings while no world
// was open (the lobby): they are this world's from now on; else the world's stored values; a new world takes the
// defaults (coop.ini).
enum class Opened { Pending, Stored, Defaults };
inline Opened on_world_open(bool pending, bool has_stored, const Values& stored, const Values& defaults, Values& cur)
{
    if (pending)
        return Opened::Pending; // (cur stays: the lobby's choice)
    cur = has_stored ? stored : defaults;
    return has_stored ? Opened::Stored : Opened::Defaults;
}

// the settings that differ from the defaults, "key=value ..." ("" when none): the support report and the lobby
inline std::string diff_text(const Values& v, const Values& defaults)
{
    std::string s;
    for (int i = 0; i < kCount; i++)
        if (v.v[i] != defaults.v[i])
            s += (s.empty() ? "" : " ") + std::string(kDefs[i].key) + "=" + std::to_string(v.v[i]);
    return s;
}

} // namespace settings
