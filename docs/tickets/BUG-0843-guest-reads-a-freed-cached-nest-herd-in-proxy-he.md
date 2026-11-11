---
id: BUG-0843
title: Guest reads a freed cached nest herd in proxy_herd and coop.npc list (object_alive on a stored pointer)
status: open
kind: crash
stage: Creature
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9008
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: fork: coop.dll game::noun_id read 00000020, called by support describe and npc::list
docs:
related: BUG-0426, BUG-0170
---

## Report
A guest can crash when it makes a proxy for a host animal after the game freed the nest herd the mod cached, and
the support/test dump `coop.npc list` crashes the same way. The fork's guest crashed in game::noun_id (read 0x20)
from npc::list, called by the sync checker's description after the host's population died.

## Root cause
* Observed (code): npc.cpp proxy_herd (1622) reuses `g_local_herds[hk]` when `game::object_alive(h)` (npc.cpp:1637).
  object_alive reads +0x21 of the stored pointer; it does not prove the allocation is alive.
* Observed: npc::list prints every g_local_herds entry with object_alive(h) and game::noun_id(h) (a virtual call on
  the stored pointer, npc.cpp:2850).
* Inferred: confidence high for the dump path (the fork's crash), medium for proxy_herd in normal play. Part of
  BUG-0426.

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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: the same object_alive-on-a-cached-herd pattern was in 0.7.2.4 (npc.cpp:329 of that tag); a candidate writer of BUG-0170's guest heap corruption (spawning a proxy into a freed herd writes into freed memory). Linked.
