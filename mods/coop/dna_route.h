// Where DNA a guest earned while the host was away goes (BUG-0151, BUG-0205, BUG-0233), kept
// apart from the game so `make test` can check it (tests/validate_tests.cpp).
//
// A guest keeps what it earns while the host is away (g_solo_dna) and the host keeps a guest's
// gain that arrives while it has no creature (g_dna_owed). Both belong to the stage and the world
// they were earned in: Creature DNA has no use in Tribe or Space, and a world nobody plays any
// more must not pay it into the next one (0.7.3.0 paid it into the next Creature save the host
// loaded, and sent a guest's solo DNA to a host already in Tribe with "is back").
#pragma once

#include <cstdint>

namespace dna_route {

// The decision table (GitHub #165 owns rows 1-9 and 12, #167 rows 10, 11, 13-15). A switch that
// is off makes its input false. "elsewhere" is host_world.h's Other (coop.cpp guest_elsewhere()).

// rows 1-5: a guest's own gain (on_dna, d > 0)
enum class Gain {
    Local,   // 1: not a connected guest: the game applies it
    Own,     // 2: the guest plays another world (core.world_dna): applied here, nothing sent; the
             //    host's total replaces it once the guest plays the host's world (row 6)
    Solo,    // 3: the host is away (core.away_dna): applied here, kept as solo DNA (row 9)
    Forward, // 4-5: a goal card's reward (K_GOAL) or MSG_DNA_ADD, not applied here
};

inline Gain guest_gain(bool connected_guest, bool positive, bool elsewhere, bool away)
{
    if (!connected_guest || !positive)
        return Gain::Local;
    if (elsewhere)
        return Gain::Own;
    if (away)
        return Gain::Solo;
    return Gain::Forward;
}

// rows 6-8: the guest gets the host's total (MSG_DNA)
enum class Total {
    Pending, // 6: elsewhere: kept as g_dna_pending, applied once the guest plays the host's world
    Drop,    // 7: the host away or no avatar: dropped (the host resends it every 2 s, BUG-0128)
    Apply,   // 8: the difference is applied through the game
};

inline Total host_total(bool elsewhere, bool away, bool avatar)
{
    if (elsewhere)
        return Total::Pending;
    if (away || !avatar)
        return Total::Drop;
    return Total::Apply;
}

// row 12 (13: #167 owes it, tagged with the world): the host gets a guest's MSG_DNA_ADD
enum class Add { Pay, Owe, Drop };

inline Add host_add(bool avatar, bool owe_fix)
{
    if (avatar)
        return Add::Pay;
    return owe_fix ? Add::Owe : Add::Drop;
}

// the guest's "host away" ended: what happens to its solo DNA
enum class SoloEnd {
    SendBack, // the host is back in the guest's world: sent to the pack, "is back" notice
    Send,     // the host plays the stage the DNA was earned in (another save of it, the guest
              // follows: BUG-0151): sent, no "is back"
    Drop,     // the host plays another stage (or nothing): dropped, logged
};

inline SoloEnd solo_end(bool fix, bool host_here, bool host_in_world, uint32_t host_mode, uint32_t solo_mode)
{
    if (!fix || host_here)
        return SoloEnd::SendBack; // fix off: sent on any "back", with the notice (0.7.3.0)
    if (host_in_world && solo_mode && host_mode == solo_mode)
        return SoloEnd::Send;
    return SoloEnd::Drop;
}

// the host's owed DNA (guest gains that arrived without a creature), every frame
enum class Owed { Keep, Pay, Clear };

// mode / world / stage: the host's game now; owed_world: the stage world it was owed in
inline Owed owed(bool fix, uint32_t mode, bool stage, bool avatar, uint32_t world, uint32_t owed_world,
                 uint32_t creature_mode)
{
    if (!fix) // any stage with a creature, any world (0.7.3.0)
        return stage && avatar ? Owed::Pay : Owed::Keep;
    if (!stage)
        return Owed::Keep; // menu, editor, load: wait
    if (mode != creature_mode || world != owed_world)
        return Owed::Clear; // another stage or another world: that DNA belongs to no game any more
    return avatar ? Owed::Pay : Owed::Keep;
}

// the stages whose modules play on locally while the host is in an editor or a load (npc in
// Creature, tribe / civ local_play): only there an editor or a load counts as "away"
inline bool away_stage(uint32_t mode, const uint32_t* stages, int n)
{
    for (int i = 0; i < n; i++)
        if (stages[i] == mode)
            return true;
    return false;
}

} // namespace dna_route
