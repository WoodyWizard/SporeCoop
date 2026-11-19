// BUG-1292 (found by the Lineages fork, PR #196, its BUG-9157): when does the game's base max hit points
// (cCreatureAnimal_BaseMaxHitPoints 0xC04170, creature vt 0x7C) read the species profile (+0xB20) +0x56C, so that a
// creature without a species faults there (0xC04252)? Kept apart from the game so `make test` checks it
// (tests/npc_tests.cpp). The branches, from the disassembly of GOG 2024 (docs/re/creature.md#creature-max-hp-null-species):
//   * an adventure (game mode 0x1654C10): LevelHealth(vt 0xD8()) + species +0x56C (0xC0419D), no check;
//   * species == cSpeciesManager PlayerSpecies (a null species when PlayerSpecies is null too): + species +0x56C via
//     0xC04252, no check;
//   * other species: herd (+0x1674) +0x154 if > 0.0 (0xC0422E), else tuning (+0xE84) +0x340 when there is a tuning
//     (0xC04242): neither reads the species; else [0x150C890] + species +0x56C at 0xC04252, no check.
#pragma once

#include <cmath>

namespace max_hp_rule {

// true: the native base reads species +0x56C for a creature with these fields (a null species faults there)
inline bool base_reads_species(bool adventure, bool is_player_species, bool has_herd, float herd_hp, bool has_tuning)
{
    if (adventure || is_player_species)
        return true;
    if (has_herd && herd_hp > 0.f)
        return false;
    return !has_tuning;
}

// true: the hook answers without the native (`on`: the switch creature.max_hp_species)
inline bool guard(bool on, bool species_null, bool adventure, bool player_species_null, bool has_herd, float herd_hp,
                  bool has_tuning)
{
    // a null species is "the player species" only when PlayerSpecies is null as well
    return on && species_null &&
           base_reads_species(adventure, player_species_null, has_herd, herd_hp, has_tuning);
}

// what a creature without a species gets instead: the max health it has (+0x5D4), at least 1
inline float fallback(float kept_max)
{
    return kept_max > 0.f && std::isfinite(kept_max) ? kept_max : 1.f;
}

} // namespace max_hp_rule
