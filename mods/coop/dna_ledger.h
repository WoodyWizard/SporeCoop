// Where shared DNA came from (player request 2026-11-01, BUG-0351: "we get randomly a ton of DNA in
// Cell and Creature"). The pack's DNA is shared by design (docs/03-gameplay.md): the host's game
// pays every gain (its own kills, meals and friends, its pack's, a guest's kill credited to the host's
// avatar, a guest's own gain sent with MSG_DNA_ADD) and sends the total; a guest only saw its total
// jump. The host now tells with each total why it changed (MSG_DNA's optional tail), every machine
// writes `dna: <a> -> <b> cause=<...>` (the monitor's DNA cause ledger) and shows a short, gathered
// notice ("+12 DNA from Bob's hunt"). Portable (no game calls): tests/validate_tests.cpp.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dna_ledger {

enum class Cause : uint8_t {
    Host = 1,   // the host's own game: its avatar's or its pack's kills, meals, friends, goal cards
    Hunt = 2,   // a guest's kill of a host NPC (the game pays it as the host avatar's, npc.cpp)
    Earned = 3, // a guest's own gain, sent to the host (MSG_DNA_ADD)
    Owed = 4,   // guests' gains the host kept while it had no creature, paid now
    Meal = 5,   // Cell: a partner's meals (the shared food of the cell stage; player kPartners: not known which)
};
constexpr uint8_t kPartners = 0xFF;

inline const char* name(Cause c)
{
    switch (c) {
    case Cause::Host: return "host";
    case Cause::Hunt: return "hunt";
    case Cause::Earned: return "earned";
    case Cause::Owed: return "owed";
    case Cause::Meal: return "meal";
    }
    return "?";
}

struct Part {
    Cause cause;
    uint8_t player; // who: 0 the host, a guest's id
    float amount;
};

// `cause=hunt:1+12,host:0+5` (`cause=unknown` without parts: an editor's spending, a total caught up)
inline std::string causes_text(const std::vector<Part>& parts)
{
    if (parts.empty())
        return "unknown";
    std::string s;
    char b[48];
    for (auto& p : parts) {
        snprintf(b, sizeof b, "%s%s:%u%+.0f", s.empty() ? "" : ",", name(p.cause), (unsigned)p.player, p.amount);
        s += b;
    }
    return s;
}

// host: a guest's lethal hit was credited to the host's avatar; the game's DNA for that kill comes in
// the same frame or a little later (the death), so gains within kCreditSec count as that guest's hunt
// (inferred from coop_team.py: the host's total rose within 1.5 s of the kill)
constexpr double kCreditSec = 2.5;
struct Credit {
    uint8_t player = 0;
    double until = -1;
    void kill(uint8_t p, double now)
    {
        player = p;
        until = now + kCreditSec;
    }
    // the cause of a gain of the host's own game at `now`
    Part gain(float amount, double now) const
    {
        if (now <= until && player != 0)
            return {Cause::Hunt, player, amount};
        return {Cause::Host, 0, amount};
    }
};
inline Credit g_credit; // the host's (npc.cpp sets it, coop.cpp reads it)

// the notices: gains of one source gathered for kGather s, at most one notice per kGap s (a pack that
// kills five animals in a row is one line "+40 DNA ...")
constexpr double kGather = 1.5, kGap = 4.0;
constexpr double kGatherMeals = 15.0; // Cell: a partner eats every few seconds
inline double gather_of(Cause c) { return c == Cause::Meal ? kGatherMeals : kGather; }
class Notices {
public:
    void add(Cause c, uint8_t player, float amount, double now)
    {
        if (!(amount > 0))
            return;
        for (auto& p : m_open)
            if (p.part.cause == c && p.part.player == player) {
                p.part.amount += amount;
                return;
            }
        m_open.push_back({{c, player, amount}, now});
    }
    // the gathered gains whose time has come
    std::vector<Part> due(double now)
    {
        std::vector<Part> out;
        if (now - m_last < kGap)
            return out;
        for (size_t i = 0; i < m_open.size();) {
            if (now - m_open[i].since >= gather_of(m_open[i].part.cause)) {
                out.push_back(m_open[i].part);
                m_open.erase(m_open.begin() + i);
            } else
                i++;
        }
        if (!out.empty())
            m_last = now;
        return out;
    }
    void clear() { m_open.clear(); }
    size_t open() const { return m_open.size(); }

private:
    struct Open {
        Part part;
        double since;
    };
    std::vector<Open> m_open;
    double m_last = -1e9;
};

// the notice's text; `who`: the source player's name, `mine`: the source is this player
inline std::string notice_text(const Part& p, const std::string& who, bool mine)
{
    int n = (int)(p.amount + 0.5f);
    std::string amt = "+" + std::to_string(n) + " DNA";
    switch (p.cause) {
    case Cause::Hunt: return amt + (mine ? " from your hunt (paid by the pack)" : " from " + who + "'s hunt");
    case Cause::Earned: return amt + " earned by " + who;
    case Cause::Owed: return amt + " your partners earned meanwhile";
    case Cause::Host: return amt + " shared by " + who + "'s pack"; // its hunts, meals, friends
    case Cause::Meal:
        return "+" + std::to_string(n) + " food (DNA) from " + (p.player == kPartners ? std::string("your partners'") : who + "'s") +
               " meals";
    }
    return amt;
}

// whether this machine shows a notice for a part. The host: a guest's hunt (a guest's own gain and
// the owed DNA have their lines already, coop.cpp). A guest: every partner's gain and its own kill
// (its game shows no reward for a kill the host's game paid), not its own forwarded gain.
inline bool worth_a_notice(const Part& p, uint8_t me)
{
    if (!(p.amount >= 1))
        return false;
    if (me == 0)
        return (p.cause == Cause::Hunt || p.cause == Cause::Meal) && p.player != 0;
    if (p.player == me)
        return p.cause == Cause::Hunt;
    return true;
}

} // namespace dna_ledger
