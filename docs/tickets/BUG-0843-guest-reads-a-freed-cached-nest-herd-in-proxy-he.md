---
id: BUG-0843
title: Guest reads a freed cached nest herd in proxy_herd and coop.npc list (object_alive on a stored pointer)
status: not-reproduced
kind: crash
stage: Creature
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9008
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch: fix/w48-freed
commits: 570ee491
tests: npc_herd_identity
signature: fork: coop.dll game::noun_id read 00000020, called by support describe and npc::list
docs: docs/re/creature.md#creature-freed-block-reuse
related: BUG-0426, BUG-0170
---

## Report
A guest can crash when it makes a proxy for a host animal after the game freed the nest herd the mod cached, and
the support/test dump `coop.npc list` crashes the same way. The fork's guest crashed in game::noun_id (read 0x20)
from npc::list, called by the sync checker's description after the host's population died.

## Root cause
Checked at runtime (fix/w48-freed, 2026-11-12):
* The dump half (npc::list calling noun_id on a stored herd) was fixed by BUG-0917 in 0.7.12 (listed_now()): not reachable any more.
* The proxy_herd half: a herd destroyed with DestroyInstance gives its block to a new herd of another species within 1-63 creations (6 of 8 hunts), so the address does come back. But in every run the guest's cache entry for the species was not asked with the stale address: for a nest herd the update filter refreshed it from the guest's own NPCs of the species before the next proxy was made (6 hunts); for a herd the guest made itself (g_herds) the destroyed herd took the proxy's creature with it and the proxy was not made again at all in the test's 9 s, with and without the fix (BUG-0952). With `npc.herd_identity` off the test never saw a proxy in another species' herd (lab 20261112-010327, 20261112-010824, 20261112-011341-2, 20261112-012235, 20261112-012736, 20261112-013804).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9008); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* proxy_herd: the cached herd is never dereferenced directly. The fork keeps it only as a "preferred" pointer and
  runs the existing for_each_object search over the noun list (noun kHerdNoun, object_alive, herd_species == the
  species, herd_nest set). If the preferred pointer is met in the list it wins, else the first match is used; if no
  listed herd matches, the cache entry is erased before falling back to g_herds / create_herd.
* npc::list: builds an unordered_set of the listed herds (for_each_object, kHerdNoun) first and prints alive =
  membership in that set and the noun only for listed ones, never noun_id on an unlisted pointer.
* No switch in the fork for this one. Their test: duo_tribe_survival.py plus a 2 min soak with 107 rounds of NPC and
  Tribe listings after a host population loss, zero faults.

## How to test
Lab test: creature fixture, guest joined; kill or despawn a whole nest species on the host (or let a migration
remove it), then on the guest run `coop.npc list` in a loop for 2 min while the host's animals respawn. Assert no
fault on either game and that every proxy has a herd that is in the noun list. FAIL on main (expected access
violation or garbage noun), PASS with the fix. A unit-like check: `coop.npc list` right after the guest's world
reload.

## Fix
Kept as hardening, not proven: mods/coop/npc.cpp proxy_herd uses a cached herd (g_local_herds, g_herds) only while it is the same object in the game's list (kHerdNoun, its serial recorded when cached) and of the species (herd +0xA4), else it searches the list again or makes a new herd. Switch: `coop.fix npc.herd_identity off`. `coop.npc list` prints `herd_sp=` (the proxy's herd is a listed herd of its own species) and `fam=`.

## Verification
no-fail-run: not reproducible: the stale cache entry was never asked in 6 runs with the switch off (the runs above); the test `npc_herd_identity` (tools/tests/coop_npc_herd_identity.py) passes on its herd checks in both; its last check (the g_herds proxy made again) fails in both for BUG-0952.

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: the same object_alive-on-a-cached-herd pattern was in 0.7.2.4 (npc.cpp:329 of that tag); a candidate writer of BUG-0170's guest heap corruption (spawning a proxy into a freed herd writes into freed memory). Linked.
- 2026-11-12: not reproduced on fix/w48-freed (npc.herd_identity kept as hardening; the list half fixed by BUG-0917); BUG-0952 opened from the test.
