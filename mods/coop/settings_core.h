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
    kDnaPace,        // all stages: DNA / food at the single-player pace (BUG-0510, dna_pace.h)
    kNpcTargets,     // Creature: the host's NPC animals treat partners like the host's creature (npc.cpp)
    kPackRescue,     // Creature: a deadly hit near a partner leaves 40 % health (teamplay.cpp)
    kPackHeal,       // Creature: partners heal each other nearby (teamplay.cpp tick)
    kJointHunt,      // Creature: a target another player hit deals +25 % (teamplay.cpp on_player_hit)
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
};

inline const Def kDefs[kCount] = {
    {"cell.regen", "Cell", "Regeneration near a partner",
     "on: a player's cell regains health while a partner swims close by; off: health comes back as in a solo game",
     1, 0, 1, Scope::Live, nullptr, false},
    {"all.partner_arrows", "All stages", "Partner arrows",
     "on: an arrow at the screen edge points to every partner off screen (never in Rivals mode); off: no arrows",
     1, 0, 1, Scope::Live, nullptr, true},
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

// "on", "off", "yes", "no", "default" or a number; false when it is none of them
inline bool parse_value(int id, const std::string& s, int& out)
{
    if (s == "on" || s == "yes" || s == "true") {
        out = 1;
    } else if (s == "off" || s == "no" || s == "false") {
        out = 0;
    } else if (s == "default") {
        out = kDefs[id].def;
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
