// Decision 5S A spike (alpha): another era's vehicles and buildings shown in a Creature game (era_visitors.cpp); BUG-9287:
// the players of a planet's other eras shown look-only (the galaxy VISIT stream, docs/12-server-and-players.md#server-era-visitors-built).
#pragma once

#include <cstdint>
#include <string>

#include "net/interp.h"
#include "sporekit.h"

namespace era_visitors {

void init(const SkHost* host, SkMod* mod);
void tick(double now);
std::string spawn(uint32_t instance, uint32_t type, uint32_t group, bool drive, float lift = 0, int axes = 0, bool prop = true);
std::string clear();
std::string move_last(const net::Vec3& at); // tests (BUG-9291)
std::string spawn_at(const net::Vec3& at);  // tests (BUG-9291)
std::string status();
void overlay(int screen_w, int screen_h); // BUG-9289: the visitors' name tags (SK_EV_OVERLAY only)

} // namespace era_visitors
