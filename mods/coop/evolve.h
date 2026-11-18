// BUG-9160 (owner decisions 2D, 2E, 2026-11-12): personal evolving in Creature with the game's own cocoon.
// The game ships a cocoon for creatures (PatchData / Spore_Game; ids are FNV-1 of the lower-case names): the
// animations (type EE17C6AD) gen_evolve_cocoon (curl in), gen_evolve_cocoon_hold (inside), gen_evolve_emerge (out;
// its event crg_cocoon_hatch) and the effects crg_cocoon_create (the sac forms), crg_cocoon_hold (a translucent glowing
// sac), crg_cocoon_hatch (it bursts); runtime 2026-11-12 (`coop.evolve fx`: made / none for a name).
// A player who goes into their own editor stays where they stood on every partner's game: their creature wraps into
// the cocoon and holds there while they edit, and comes out with the new body when they are back. docs/re/creature.md
// #creature-cocoon.
#pragma once

#include <cstdint>
#include "net/protocol.h"
#include <string>
#include "sporekit.h"

namespace evolve {

constexpr uint32_t kCocoonEnter = 0x9C26F705u; // gen_evolve_cocoon
constexpr uint32_t kCocoonHold = 0x4E0D3CBFu;  // gen_evolve_cocoon_hold
constexpr uint32_t kEmerge = 0xD4F48A35u;      // gen_evolve_emerge
constexpr uint32_t kFxCreate = 0x59A93586u;    // crg_cocoon_create
constexpr uint32_t kFxHold = 0x6F02369Du;      // crg_cocoon_hold
constexpr uint32_t kFxHatch = 0xD033D3ACu;     // crg_cocoon_hatch

// a partner's cocoon as this game shows it (kept with the partner's Remote)
struct Cocoon {
    int state = 0;      // 0 none, 1 wrapping in, 2 holding, 3 back in the world: holding until the new body is there
    double at = 0;      // when the state began
    double played = 0;  // the last play of the hold animation
    void* fx = nullptr;   // the sac (crg_cocoon_hold), ours to stop and release
    double fx_at = 0;     // when the glow (crg_cocoon_hold) was made (it fades after a few seconds: made again)
    double shell_at = 0;  // when the woven shell (crg_cocoon_create, ~2 s each) was laid last
    void* puppet = nullptr; // the puppet the cocoon was put on (a new body is a new puppet: it goes in too)
    uint32_t key = 0;     // the partner's creature here when it went in (a new one: the new design came)
    float size = 0;       // BUG-9181: its scale before the cocoon (restored when it comes out; 0: not shrunk)
    float radius = 0;     // its bounding radius then: the shell keeps the size of the creature it holds
    net::Vec3 spot;       // BUG-9194: where it went in: every new shell there (shells of two places overlapped as two)
    bool spot_set = false;
};

void init(const SkHost* host, SkMod* mod);
bool cocoon_on();          // `coop.fix creature.cocoon`
bool personal_editor();    // `coop.fix creature.personal_editor`: the host's own editor in Creature, no vote

// partners' view (coop.cpp drive_puppet): partner `player` is in an editor of their own; keeps and animates the puppet
void puppet_editing(uint8_t player, void* puppet, Cocoon& c, double now);
// partner `player` is back in the world: the puppet stays in the cocoon until it wears the new design (the partner's
// creature here changed and the puppet has it: seen 2026-11-12, the body came 1.2 s after the partner's state and the
// puppet was made again), then comes out; with no new design (left without saving) after kNoDesignWait
void puppet_back(uint8_t player, void* puppet, Cocoon& c, double now);
// the puppet went: the sac goes; `keep_state`: a new puppet of the partner still comes out of the cocoon later
void drop(Cocoon& c, bool keep_state = false);

// the own creature: back from the own editor in Creature, it comes out of the cocoon once the game is free
void own_tick(uint32_t mode, void* avatar, bool busy, double now);

// BUG-9161: after a mating the egg scene (cinematic CRG_LaidEgg) never ended by itself in co-op (2026-11-12, the owner
// and the MSI, also alone in a session); the game finishes it only from its key handler (Escape / T: 0xE45760 ->
// 0xE37920, the flag +0x371 and a 500 ms timer, then 0xE45E0F posts 0x4471C60, the evolution request). Past
// kEggWatch seconds the mod presses Escape for the player, once per scene. `coop.fix creature.egg_watch`.
void egg_tick(uint32_t mode, double now);

// BUG-9175: the mating finished by the mod. The game's pairing needs the mate's own AI to answer and walk in (0xD6D360);
// a guest's mate is a copy of the host's member, run by the host, so a guest's creature stood in the mating action for
// ever (the MSI, 2026-11-13; it never worked: before decision 2E a guest evolved through the host's vote). The mating
// action of the own creature (its controller +0x1D8: 02D852F0 / D23FE7CA, started by the game's own click on the mate)
// now ends in what the pairing ends in: the cocoon, then `request` (coop.cpp submit_evolution_request, the one the join
// editor and the migration use). On a guest after kDirectAfter s of hearts; on the host only when the game's pairing
// has not reached its egg scene after kStallAfter s (a stalled pairing). `coop.fix creature.mating_direct`.
using Request = int (*)();
void mating_tick(uint32_t mode, void* avatar, bool guest, bool busy, double now, Request request);

// BUG-9161 (cause): the own creature wears the player's own species (creations.cpp apply_own_avatar, BUG-0517's key
// after an editor) while its nest family and the herd kept the world's older species (seen 2026-11-13: avatar
// 2920607A, herd and family 2919A821, one nestmate 2920607A): the mate showed the ally face instead of the mate's, and
// the egg scene after a mating never ended. In the game the family is always the player's species. Every 2 s the own
// herd's species (+0xA4) and its living members follow the own creature's (SetSpecies, their age kept).
// `coop.fix creature.family_species`.
void family_tick(uint32_t mode, void* avatar, bool busy, double now);

std::string debug();
int command(int argc, const char* const* argv, std::string& out); // coop.evolve [anim <hex> [hold]|release]

} // namespace evolve
