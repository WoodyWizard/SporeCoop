// The editors of the end of Tribe (BUG-0588, BUG-0849, BUG-1280): the City Hall editor, then one vehicle editor chosen
// by the player's consequence archetype (cPlayer +0x1104). Kept apart from the game so `make test` checks it
// (tests/tribe_tests.cpp). Disassembly of GOG 2024 at 0xCD2308..0xCD233B: C2CA9495 -> F670AA43; 60A78928 -> 9AD7D4AA;
// any other -> (neg/sbb mask & E26B6B03) + 9AD7D4AA = 7D433FAD. The game data has the three editor setups (type
// 0x00B1B104, group 0x40600100, instances F670AA43, 9AD7D4AA, 7D433FAD in Spore_Game.package and PatchData.package),
// and no 7CB21BAD anywhere (docs/re/tribe.md#tribe-end-editors).
#pragma once

#include <cstdint>

namespace tribe_editors {

constexpr uint32_t kCityHall = 0x99E92F05u;
constexpr uint32_t kArchetypeA = 0xC2CA9495u, kArchetypeB = 0x60A78928u; // "tribe_mixed", "tribe_social"
constexpr uint32_t kArchetypeAggressive = 0x0CAC124Du;              // "tribe_aggressive" (the exe's archetype table 0x156ACD0: name, hash)
constexpr uint32_t kVehicleA = 0xF670AA43u, kVehicleB = 0x9AD7D4AAu, kVehicleOther = 0x7D433FADu;
constexpr uint32_t kWrongVehicle = 0x7CB21BADu; // BUG-1280: the misread id the check had before (no such editor)

// the vehicle editor the game opens after the City Hall editor for a consequence archetype (0xCD2308)
inline uint32_t vehicle_editor(uint32_t archetype)
{
    if (archetype == kArchetypeA)
        return kVehicleA;
    uint32_t mask = archetype - kArchetypeB ? 0xFFFFFFFFu : 0u; // sub; neg; sbb
    return (mask & 0xE26B6B03u) + kVehicleB;
}

// an editor of the end of Tribe (the City Hall's or a vehicle's). `fix` false: the list before BUG-1280
inline bool end_editor(uint32_t id, bool fix = true)
{
    return id == kCityHall || id == kVehicleA || id == kVehicleB || id == (fix ? kVehicleOther : kWrongVehicle);
}

} // namespace tribe_editors
