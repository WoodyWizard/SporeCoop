// Census of the shared objects (tests; docs/06-testing.md#testing-census).
//
// `coop.census` prints, on the host and on a guest, the objects the co-op shares in the current mode
// as the game has them: creatures (Creature stage: the avatar, NPCs of the host with their net id,
// proxies of a guest with the host's net id, puppets, the guest's own NPCs) and civilization
// vehicles (net id, owner). tools/tests/census.py parses the dump, matches the host's objects with
// the guest's by net id and lists what differs, and tells which objects moved between two dumps of
// one machine (a frozen proxy, BUG-0205). Read-only.
//
// Line format (one object per line):
//   census v1 role=<host|guest|none> me=<id> mode=<name> modeid=<hex> world=<hex> t=<s>
//   obj <kind><id> key=<hex> alive=<0|1> hp=<f> ai=<0|1|-1> own=<who> owner=<player|-1> pos=(x y z)
//   end objects=<n>
// kind: c creature, v vehicle; id: the net id (-1 none: the avatar, a local object).
// own (creatures): avatar, npc (host), proxy (guest), puppet (a player's), local (no net id).
//
// `coop.census all [r]` (the divergence detector, docs/sync-architecture-plan.md, tools/tests/worlddiff.py)
// adds, after the v1 rows:
//   me pos=(x y z) ok=<0|1>                       the own player's position (the compare area's centre)
//   obj g-1 key=... pos=(x y z) noun=<hex> ser=<n> scale=<f>   every other placed game object (cGameData
//       with a cSpatialObject) within r m of the own player (default 250), at most 6000
//   obj l<id> key=<model> alive= hp= ai= own=<npc|proxy|local> owner=-1 pos=(...) scale=<size>  Cell: the
//       foreground cells (host: its pool, id = the cell index; guest: mirrors with the host's index, its
//       own cells as local)
//   nouns <hex>=<count>@<vt> ...                  every noun in the game's list (placed or not, any distance), with
//       the Ghidra address of one object's vtable (no RTTI in the exe: the class is found from it)
// and `scale=` / `noun=` on the c rows. The v1 parsers ignore what they do not know.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace census {

struct Row {
    char kind = '?';
    int id = -1;
    uint32_t key = 0;
    int alive = 1;
    float hp = -1;
    int ai = -1;
    const char* own = "";
    int owner = -1;
    net::Vec3 pos{};
    uint32_t noun = 0;
    uint32_t serial = 0;
    float scale = -1;
};

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(uint32_t& mode, uint32_t& world)> place; // g_mode, g_world
    std::function<bool(net::Vec3& pos)> self_pos;               // the own player (false: none)
};

void init(const SkHost* host, SkMod* mod, Ctx ctx); // registers coop.census

} // namespace census

namespace civ {
void census_rows(std::vector<census::Row>& out); // the vehicles (civ.cpp)
}
namespace cellsync {
void census_rows(std::vector<census::Row>& out); // the foreground cells (cellsync.cpp)
}
