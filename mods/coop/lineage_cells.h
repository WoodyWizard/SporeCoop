// BUG-9275 (ported from Lineages BUG-9111, roadmap 43): Lineages cells. "When you go up in the Cell stage, others like
// you start to appear": the players' own cells appear in the Cell stage as NPC species, as the game's own species do.
// About one in six spawns of a native creature species takes a player cell's model (docs/re/cell.md#cell-lineage-cells,
// docs/01-architecture.md#architecture-lineage-cells).
//  - which cells (BUG-9285, cell.lineage_current on): every partner's current cell, on spawns of the player species
//    (current_only below). Off (0.7.15.94): the host's own My Creations cells and the cells every guest sent from its
//    My Creations (creations::guest_cell_out) in this session; never a cell a player plays now (by its model bytes).
//    Lineages' Cache and Foreign Creations are not ported (no Lineages library here).
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
// BUG-9285 (the owner's test 2026-11-20: "many giants in the Cell stage"): the host's eight My Creations cells took the
// look of about one in six spawns of EVERY native species, the big ones of the levels above included, so player cells
// swam about as giants. Now (cell.lineage_current, on) only the players' CURRENT cells appear, each player's own species:
// a spawn of the player species' native spread (the game puts 6-8 cells of the avatar's own cell at the avatar's level
// around it, docs/re/cell.md#cell-own-species) takes a partner's current cell; in a partner's own tile pass (the host
// populates every partner's area, cellsync_tiles.cpp) that partner's cell, in the host's pass any player's (the host's
// own stays native). Normal sizes and amounts, never another species' size. Off: the My Creations cells on every
// native species (0.7.15.94).
bool current_only();
// host: the current cell of player `player` (-1: any player's, the host's own counting as "keep the native"); false: keep it
bool pick_player(int player, uint32_t key[3]);
std::string debug(const std::vector<std::string>& args);

} // namespace lineage_cells
