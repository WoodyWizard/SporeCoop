// "Is this guest in the host's world right now?" for the modules (O1 of 0.7.13, the children of BUG-0427:
// host state and guest rules applied while a guest plays another world). One place answers it, coop.cpp, on top of
// host_world.h (the rule, unit-tested); each module that must not apply the host's world, or must not hold a guest
// to the host's rules, asks here and keeps its own `coop.fix` switch.
//
//   guest side: elsewhere()      a gated stage (Cell, Creature, Tribe, Civ): another world of the host's stage, or
//                                not known yet (host_world::classify Other; core.same_place off: never)
//               space_other()    Space: another Space save than the host's, both known (host_world::space_other;
//                                not switched by res.space_save, which is BUG-0709's money switch)
//               host_left()      a gated stage whose host plays another stage (Creature / Space while we are still
//                                in Cell), for at least 2 s (a passing step of a stage switch is not one)
//   host side:  peer_here(id)    guest `id`'s latest state is this machine's stage and world
//               peer_elsewhere(id) guest `id` plays another world of this machine's stage (what its own
//                                elsewhere() says: it ignores the host's votes and gated messages, BUG-0702)
#pragma once

#include <cstdint>

namespace own_world {

bool elsewhere();
bool space_other();
bool host_left();
bool peer_here(uint8_t id);
bool peer_elsewhere(uint8_t id);

} // namespace own_world
