// Every player's own stage history (BUG-9263): the consequence card each finished stage gives (Cell carnivore /
// herbivore / omnivore, Creature aggressive / social / mixed, Tribe, Civ, the Space archetype; cPlayer +0x10FC..+0x110C)
// and the Creature trait progress (+0x1278 social, +0x127C combat) that decides the Creature card, kept per party seat.
//
// Every game has one cPlayer, loaded from the host's save: before this every guest's History showed the host's cards
// (and the wsync row "stages" kept writing them), and every kill or social win of a guest counted for the host's
// Creature card (BUG-9264). Now:
//   * the host keeps each seat's history with the party (founders::PartySeat, PTY2 in <planet>.spo.duoparty);
//     seat 0 is the host's own cPlayer;
//   * a guest's kill (the lethal hit the host lands as its avatar's, npc.cpp) and social win (social.cpp) add their
//     trait progress to that guest's seat instead of the host's cPlayer (the hook on cPlayer::AddTraitProgress);
//   * when the host's world has a stage done that a seat has not, the seat gets its own card for it: Cell = what the
//     guest's own game decides from its own diet (Cell_GetConsequenceTrait, reported by the guest), Creature = the
//     game's own decision (Creature_GetConsequenceTrait) run with the seat's progress, Tribe and Civ = the host's
//     (one shared tribe / nation in this build: the same play), Space = the game's archetype of the seat's own four
//     cards (SpaceArchetype_FromTraits). A stage the host's world has not done is not done for any seat either;
//   * the host sends each guest its seat's record (MSG_OWN_CARDS); the guest's game writes it into its own cPlayer
//     while it plays the host's world, and the "stages" row no longer overwrites it. The guest's game then shows its
//     own History and builds its own consequence powers from its own cards (editor requests carry cPlayer's cards).
// docs/re/creations.md#creations-own-cards. Switch `coop.fix party.own_cards off`: one shared history (0.7.15).
#pragma once

#include <cstdint>
#include <string>

#include "net/buffer.h"
#include "net/session.h"
#include "sporekit.h"

namespace cards {

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);
// guest: its own history arrived and is in force (the wsync row "stages" leaves this game's cPlayer alone)
bool guest_owns();
// host: the trait progress the game adds while this is alive goes to `player`'s seat (a guest's kill or social win)
struct Actor {
    explicit Actor(uint8_t player);
    ~Actor();
    uint8_t prev;
};
// host: add trait progress to a player's seat (social: true = social, false = combat); false: not credited
bool credit(uint8_t player, bool social, float amount);
std::string status();
int command(int argc, const char* const* argv, std::string& out);

} // namespace cards
