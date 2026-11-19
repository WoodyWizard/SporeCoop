// WP-K of 0.7.16 (BUG-1303, owner approval 2026-11-19): a partner in an editor of its own, seen from every other game
// of the session, in the Creature stage. The partner's puppet wraps into the game's own cocoon where it stood and holds
// in it while the partner edits (the host's mating editor, a guest's "Evolve now" editor or its later editor at the
// nest, docs/09-coop-design.md 9.3); when the partner is back the puppet stays in the cocoon until it wears the new
// design, then comes out (the game's emerge animation and the cocoon's burst). Before: the puppet disappeared while the
// partner edited and came back with its new body.
//
// Ported (the visual part only) from the Lineages fork by PandizeLive (PR #196, MIT; its BUG-9160, 9181, 9194:
// mods/coop/evolve.cpp there): the game's cocoon is data (docs/re/creature.md#creature-lineages-cocoon-mate,
// #creature-cocoon): the animations gen_evolve_cocoon / gen_evolve_cocoon_hold / gen_evolve_emerge and the world effects
// crg_cocoon_create (the woven shell, ~2 s each: laid again in time), crg_cocoon_hold (the glow, fades: made again) and
// crg_cocoon_hatch (the burst). The creature is shrunk inside (nothing shows through the shell), every shell of one
// cocoon at the spot it went in (no doubled shells), sized by the creature's bounding radius, its scale restored when
// it comes out.
//
// How a game learns that a partner is in its editor: the partner's own player state (mode Editor, the same world)
// that every SporeCoop sends 20 times a second; no new message (a 0.7.15.2 partner in its editor is shown in the
// cocoon too; a 0.7.15.2 game hides the partner as before).
// Lifetime (CLAUDE.md 2): no game object of ours outlives the puppet: the effects are stopped and released with it, and
// only while this game is still in the mode and world they were made in (after a stage or world change the game tore
// them down: the pointers are dropped without a call, as effects.cpp does, BUG-0451 review item 7); the held pose leaves
// with the puppet (game::puppet_unregister). Nothing of this goes into a save (world effects and the puppet's scale).
// Switch: `coop.fix creature.cocoon off`: the puppet is hidden while the partner edits (0.7.15).
#pragma once

#include <cstdint>
#include <string>

#include "net/protocol.h"
#include "sporekit.h"

namespace cocoon {

constexpr uint32_t kAnimEnter = 0x9C26F705u;  // gen_evolve_cocoon (curl in)
constexpr uint32_t kAnimHold = 0x4E0D3CBFu;   // gen_evolve_cocoon_hold
constexpr uint32_t kAnimEmerge = 0xD4F48A35u; // gen_evolve_emerge
constexpr uint32_t kFxShell = 0x59A93586u;    // crg_cocoon_create
constexpr uint32_t kFxGlow = 0x6F02369Du;     // crg_cocoon_hold
constexpr uint32_t kFxHatch = 0xD033D3ACu;    // crg_cocoon_hatch

void init(const SkHost* host, SkMod* mod);
bool on(); // creature.cocoon

// puppets.cpp drive_puppet, this game in the Creature stage with partner `id`'s creature puppet `puppet` in the world
// it was made in. `editing`: the partner's newest state says it is in an editor in this world (or, once in the cocoon,
// not back in it yet). True: the puppet is held in the cocoon this frame (no driving, kept shown).
bool editing(uint8_t id, void* puppet, double now);
// the partner plays in this world again (its puppet shown, maybe a new body): true while the puppet stays in the cocoon
// or comes out of it (no driving this frame). `reported`: the species the partner's own game reports (PlayerState).
bool back(uint8_t id, void* puppet, uint32_t reported, double now);
// partner `id` is in a cocoon here (any phase)
bool active(uint8_t id);
// the puppet goes (despawn, forget_puppets). keep: the same partner's new body comes next (its new look): it goes into
// the same cocoon; else the cocoon of this partner ends here
void puppet_gone(uint8_t id, void* puppet, bool keep);
// once per frame (coop.cpp frame_drive): the shells and bursts that ended are released
void tick(double now);
// coop.cocoon: the switch, the counters, every partner's cocoon as the game shows it (its animation, the glow's state)
std::string status(uint8_t only = 0xFF);

} // namespace cocoon
