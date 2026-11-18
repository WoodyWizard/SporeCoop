// BUG-9111 (roadmap 43): Lineages cells. Player cells appear in the Cell stage as NPC species, about one
// in four spawns of a native creature species takes a player cell's model (docs/re/cell.md#cell-lineage-cells).
//  - which cells: the host's own My Creations and Lineages Cache cells authored by a player in this
//    session, and with Foreign Creations on (BUG-9112) any other Cache cell; never a player's current cell
//  - the native species keeps its behaviour (.cell AI): a cell goes only onto a native species with the
//    same diet (its mouth), so a carnivore hunts and a herbivore grazes
//  - the host decides; every guest gets the cell's records before the host uses it (creations::host_cell_out)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace lineage_cells {

constexpr unsigned kSharePercent = 17; // of a native creature species' spawns (player decision 2A: 1/4; 2026-11-11: 1/6)

void init(const SkHost* host, SkMod* mod, net::Session* session);
void tick(double now, bool in_cell); // main thread, every frame
bool enabled();                      // the host setting (coop.ini [cell] lineage_cells, default on)
void set_enabled(bool on);
// a player cell whose diet fits (carnivore / herbivore flags of the native species' model) and that every
// guest already has; key = {instance, type, group}
bool pick(bool carnivore, bool herbivore, uint32_t key[3]);
std::string debug(const std::vector<std::string>& args);

} // namespace lineage_cells
