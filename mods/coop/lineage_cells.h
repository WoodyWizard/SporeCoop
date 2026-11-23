// Players' old cells as wild species in the Cell stage (BUG-1477; idea and RE of PR #211 by IltonBJSilva). The rules
// are in lineage_cells_logic.h (portable, unit-tested); docs/01-architecture.md#arch-lineage-cells,
// docs/re/cell.md#cell-lineage-cells-0717.
//  - the session setting cell.lineage_cells (off by default) and, for a guest's own cells, its consent: the co-op
//    window's box (never ticked by default, not kept across sessions)
//  - each player's offered cells go to every game as line bundles (MSG_CREATION kind 12, creations 1.2) and are
//    registered there under one shared key (0x7B......); the host's population gives some native spawns that key
//  - `coop.fix cell.lineage_cells off`: nothing offered, nothing swapped (0.7.16.1)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lineage_cells {

// creations.cpp: kind 12 between the games (owner = the cell's player)
void on_bundle(uint8_t owner, uint8_t from, uint32_t id, std::vector<uint8_t>&& data);
void on_withdraw(uint8_t owner, uint8_t from, uint32_t id);
void tick(double now); // main thread (creations::tick)
void on_player_join(uint8_t id);
void on_player_leave(uint8_t id);
void on_session_end();

// the co-op window's box (a guest): -1 not shown, 0 not ticked, 1 ticked
int box();
void toggle();

// host, inside the population's GetModelKeyForCellResource (cellsync_kin.cpp): a native spawn's model `native` may
// become a pool cell; diet_of(key) answers a model's mouths (bit 0 carnivore, bit 1 herbivore, -1 unknown)
bool active(); // the host swaps now (the setting, the switch, every peer reads kind 12, a cell in the pool)
bool pick(bool any_diet, bool native_carn, bool native_herb, int (*diet_of)(uint32_t key), uint32_t out[3]);
bool is_lineage(uint32_t instance); // a shared key of the pool (tests, coop.pool)
void cell_left();                   // the Cell stage was left: its preloads went with it
void count_swap(bool swapped);      // tests: a spawn considered (cellsync_kin.cpp)

std::string status();
std::string command(const std::vector<std::string>& args);

} // namespace lineage_cells
