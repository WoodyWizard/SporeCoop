// BUG-9275 (ported from Lineages BUG-9111, roadmap 43): Lineages cells. "When you go up in the Cell stage, others like
// you start to appear": the players' own cells appear in the Cell stage as NPC species, as the game's own species do.
// About one in six spawns of a native creature species takes a player cell's model (docs/re/cell.md#cell-lineage-cells,
// docs/01-architecture.md#architecture-lineage-cells).
//  - which cells: the host's own My Creations cells and the cells every guest sent from its My Creations
//    (creations::guest_cell_out) in this session; never a cell a player plays now (by its model bytes). Lineages'
//    Cache and Foreign Creations are not ported (no Lineages library here).
//  - the native species keeps its behaviour (.cell AI): a cell goes only onto a native species with the same diet (the
//    carnivore / herbivore flags of the model), so a carnivore hunts and a herbivore grazes
//  - the host decides; every guest gets the cell's records (creations::host_cell_out) 8 s before the host uses it
//  - the session setting cell.lineage_cells (default on, coop.ini lineage_cells); `coop.fix cell.lineage_cells off`
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace lineage_cells {

constexpr unsigned kSharePercent = 17; // of a native creature species' spawns (Lineages' player decision 2026-11-11: 1/6)

void init(const SkHost* host, SkMod* mod, net::Session* session);
void tick(double now, bool in_cell); // main thread, every frame
bool enabled();                      // the session setting cell.lineage_cells (the host's table on a guest)
unsigned share_percent();            // kSharePercent (tests: coop.lineage share <n>)
// host: a player cell whose diet fits (carnivore / herbivore flags of the native species' model) and that every guest
// got 8 s ago; key = {instance, type, group}
bool pick(bool carnivore, bool herbivore, uint32_t key[3]);
bool is_candidate(uint32_t instance); // host: a cell model of the pool's player cells (coop.lineage)
std::string debug(const std::vector<std::string>& args);

} // namespace lineage_cells
