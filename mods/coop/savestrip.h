// Our objects never go into a save (CLAUDE.md 2, BUG-0020, docs/re/creature.md#creature-save-strip).
//
// Every save of the game writes the world through the generic COM serializer: the roots (the noun
// manager's list of every live noun, the simulator subsystems) and then everything they refer to,
// each reference through COMSerializer::Write 0x6A05F0. The hook writes a null reference instead for
// objects of ours (partners' puppets, NPC proxies, the herds made for them), so they are never
// queued and never written; the world itself is not touched (nothing to restore after the save). A
// null entry in the noun list is skipped on load (cGameNounManager::Read 0xB1FBD0).
#pragma once

#include <cstdint>
#include <string>

#include "sporekit.h"

namespace savestrip {

bool init(const SkHost* host, SkMod* mod);
void tick(); // once per frame, on the main thread
// a mode's predicate: `obj` (a live game noun, `noun` its GetNounID) is one of ours and must not
// be saved. Asked once per save for every noun of the noun manager.
using Filter = bool (*)(void* obj, uint32_t noun);
void add_filter(Filter fn);
void set_enabled(bool on); // `coop.savestrip 0|1` (tests: 0 = the old behaviour)
bool enabled();
std::string status();
// BUG-0357: `f` runs when a save starts writing the world (main thread, once per serializer and
// frame), whether or not the strip is on: world.cpp notes where a Space save was really written
// (a save is posted as a message and written frames later).
void set_write_watch(void (*f)());

} // namespace savestrip
