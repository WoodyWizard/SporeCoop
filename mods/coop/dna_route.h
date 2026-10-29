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
