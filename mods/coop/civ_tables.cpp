// The Civ vehicle tables (mirror::Registry, C1 of 0.7.15) and the guest's snapshot receiver (C4); moved out of
// civ.cpp (C8, BUG-1164). docs/01-architecture.md#arch-civ-registry.
#include "civ_internal.h"

namespace civ {

// the guest's liveness rule of a mirror's vehicle: its memory says it is not destroyed and (civ.mirror_lifetime) it is
// the object bound (its serial). The serial in the vehicle's own memory, not the object list: a vehicle the mod just
// made may join the list later, and prune_gone (the game's vehicle list, every Civ frame) drops one the game removed
// before it is freed; what is left is another vehicle at the address, which has another serial. The one object_alive
// the registry keeps (docs/01-architecture.md#arch-mirror-registry). Off (civ.mirror_lifetime): object_alive alone.
static bool gv_alive(const mirror::Ref& r)
{
    if (!r.p || !game::object_alive(r.p))
        return false;
    return !g_veh_lifetime_fix || game::object_serial(r.p) == r.serial;
}
// ---------------- the vehicle tables (C1 of 0.7.15, BUG-1140) ----------------
// The host's table (net id <-> vehicle of the game's list), the guest's mirrors (host id <-> the local vehicle that
// copies it) and the guest's removals waiting for the planner or a cinematic (BUG-0493) are mirror::Registry objects
// (mirror_registry.h, docs/01-architecture.md#arch-mirror-registry): the reverse index is the registry's, so the two
// directions cannot drift (BUG-0951's `g_gv_of[v] == id` checks of the hand-made maps up to 0.7.14). What stays here
// is Civ's own: the political id check (BUG-0059) and the serial check (BUG-0951) of the host's table pass, the owners
// (BUG-0031) and E1's claims (owner_claims.h), the bind candidate (same nation / purpose / locomotion within 25 m) and
// the guest's liveness rule (gv_alive: the serial in the vehicle's own memory, because a vehicle the mod just made joins
// the game's list later; prune_gone drops one the game took out of its list before it is freed). The lab passed with
// the registry and the hand-made maps were removed (switch civ.registry, 0.7.15; run dirs in BUG-1140).
static bool hv_alive(const mirror::Ref& r) { return game::alive(game::LiveRef{r.p, r.serial, r.noun}); }
mirror::Registry<uint16_t, HV> g_hvr("civ.hv", hv_alive);
mirror::Registry<uint16_t, GV> g_gvr("civ.gv", gv_alive);

HvRef hv_find(void* v)
{
    uint16_t id;
    if (!v || !g_hvr.id_of(v, id))
        return {};
    auto* e = g_hvr.find(id);
    return e ? HvRef{id, e->ref.p, e->ref.serial, &e->data} : HvRef{};
}
HvRef hv_by_id(uint16_t id)
{
    auto* e = g_hvr.find(id);
    return e && e->ref.p ? HvRef{id, e->ref.p, e->ref.serial, &e->data} : HvRef{};
}
size_t hv_count() { return g_hvr.size(); }
struct HvIds { // net::alloc_id's "in use" for the registry
    const mirror::Registry<uint16_t, HV>& r;
    size_t count(uint16_t id) const { return r.has(id) ? 1 : 0; }
};
// host: a vehicle of the game's list gets a net id (BUG-0154: never a live id, never 0); 0 when none is free
uint16_t hv_add(void* v, uint8_t owner)
{
    uint16_t id = 0;
    g_hvr.adopt(mirror::Ref{v, game::object_serial(v), kVehicleNoun}, [] { return net::alloc_id(g_next_id, HvIds{g_hvr}); },
                [](uint16_t) {}, id);
    if (auto* e = id ? g_hvr.find(id) : nullptr)
        e->data = {owner, pol_of(v)};
    return id;
}
void hv_drop(uint16_t id) { g_hvr.erase(id); }
void hv_clear() { g_hvr.clear(); }

GvRef gv_find(uint16_t id)
{
    auto* e = g_gvr.find(id);
    return e ? GvRef{id, &e->data, e->ref.p, e->ref.serial} : GvRef{};
}
GvRef gv_at(uint16_t id)
{
    auto& e = g_gvr.at(id);
    return {id, &e.data, e.ref.p, e.ref.serial};
}
// the id of the mirror whose vehicle is v (a pointer compare: safe on a freed one; no liveness check)
bool gv_id_of(void* v, uint16_t& id) { return v && g_gvr.id_of(v, id); }
GvRef gv_of(void* v)
{
    uint16_t id;
    return gv_id_of(v, id) ? gv_find(id) : GvRef{};
}
bool gv_bound(void* v) { return v && g_gvr.bound(v); }
bool gv_ok(const GvRef& x) { return x.v && gv_alive(mirror::Ref{x.v, x.serial, kVehicleNoun}); } // (BUG-0951)
// the mirror's vehicle while it is the object bound (gv_ok), else null
void* gv_live(uint16_t id)
{
    GvRef x = gv_find(id);
    return gv_ok(x) ? x.v : nullptr;
}
size_t gv_count() { return g_gvr.size(); }
// mirror id's vehicle is v from now on (made: CreateVehicle, not a local vehicle bound). An address still bound to
// another mirror holds a new vehicle now (CreateVehicle gave the block of a freed one): that mirror loses it.
void gv_bind(uint16_t id, void* v, bool made)
{
    uint16_t other;
    if (g_gvr.id_of(v, other) && other != id)
        g_gvr.detach(other);
    g_gvr.bind(id, mirror::Ref{v, game::object_serial(v), kVehicleNoun}, made);
    g_gvr.at(id).data.listed = false;
}
// mirror id has no vehicle (forgotten, no game call; the reverse index goes only when it named this mirror)
void gv_unbind(uint16_t id)
{
    g_gvr.detach(id);
    if (auto* e = g_gvr.find(id))
        e->data.listed = false;
}
void gv_clear() { g_gvr.clear(); } // (also its waiting removals)
// BUG-0493: the removals waiting (the planner moves the vehicle, a cinematic plays): the registry's deferred list
bool kill_waiting(void* v) { return v && g_gvr.deferred(v); }
void kill_wait(void* v, uint32_t serial) { g_gvr.defer(mirror::Ref{v, serial, kVehicleNoun}, false); }
size_t kill_waiting_count() { return g_gvr.deferred_count(); }

// C4 of 0.7.15 (BUG-1143): the chunked receive of the host's snapshot is net::SnapshotReceiver (net/snapshot_rx.h,
// docs/01-architecture.md#arch-snapshot-rx) with Tribe's policy: at most 255 chunks, a late chunk of an older cycle
// (within half the u16 range) dropped while net.cycle_order is on (BUG-0612), only the last chunk completes a cycle and
// only once (a duplicate of it does not sweep again), the sweep hands the cycle's ids to the mirror registry. It
// replaced the CycleTracker code copied from tribe.cpp (switch civ.snapshot_rx, 0.7.15; run dirs in BUG-1143).
net::SnapshotReceiver<uint16_t> g_rx(net::SnapshotPolicy{255, true, 0x8000, true, true});

} // namespace civ
