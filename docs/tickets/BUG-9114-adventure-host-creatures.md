---
id: BUG-9114
title: Adventure creatures move differently in each game; the host should simulate them for everyone
status: fixed
kind: feature
stage: Adventure
reported: 2026-11-10
source: owner request (44-dev)
role: host guest
versions: 0.7.3.6-lineages44-dev
fixed_in: unreleased
branch: feat/adventure-bodies
commits: 1e547391 6b282874
tests: tests/adventure_sync_tests.cpp (hits, actor batches); tools/qa_win (QA compare of `coop.adventure actors` on both games)
signature: enemies' positions differ between games; host creatures pick a partner body but never strike it
docs: docs/re/space.md#space-adventure-host-creatures
related: BUG-9089 BUG-9113 BUG-0016
switch: adventure.enemies
---

## Report

Combat adventures (BUG-9089) share deaths, credits and the end, but every game moves its own enemies,
allies and characters. An enemy chasing one player stands still on the other's screen until it dies.

## Implementation (44, behind `adventure.enemies`, off by default)

* Host: every 0.1 s the states of the scenario's creatures (key = host object serial, id = scenario
  object id, position, facing, velocity, health, dead) that changed, all of them once a second,
  unreliable (MSG_ADVENTURE_ACTORS, 20 per packet).
* Guest: its scenario creatures have no AI tick (`game::set_ai_skip`); each host creature is paired
  with the nearest local creature of the same id and driven like a partner puppet (walk on the path,
  else the driver), with the host's health. A death of a paired creature comes by key (killing "any
  one of that id" killed living lookalikes: soldiers from one marker share an id); its corpse is put
  where the host's lies. The guest captain's hits on them go to the host (Packet Hit) and are applied
  there with the guest's body as the attacker; other damage to them is dropped.
* Host: npc `ai_target` also runs in an Adventure, so a creature nearer to a partner's body ticks
  with that body as the player.

## Verification so far

City Assault, QA instances 9 + 8, sync on from the mission's start, 40 s of the allied army's battle:
living creatures paired by key 75, p50 0.38 m, p90 1.32 m, max 2.80 m, health equal on 74 of 75
(`compare.py`); before deaths by key a run had 30 extra living creatures on the guest. 0 faults.

## Open (why it is off)

The host's creatures picked the guest's body (npc `partner_picks` 23-34 in a minute) but never struck
it: no "1 hit by" damage line on the host, guest `damage_in` 0. With the switch on, the guest's own
creatures have no AI, so the guest was hurt only by buildings (a turret's 30 every 3 s): combat would be
trivial for guests. Next: how scenario creatures choose an attack target (the scenario's combatant /
political target system rather than the behaviour tree's avatar, docs/re/creature.md 14); the body's
political id; whether the body needs the captain's combatant registration. The guest-hit path (Hit ->
host) was not exercised before the mission ended. Not tested: two PCs, 3-4 players.

## 2026-11-14 (feat/lineages-47, roadmap 3N A step 3)

* Fixed the strike: BUG-9200 (the body had political id -1; now the captain's). The host's creatures hit a guest's
  body and the damage reaches the guest (5-8 hits in 40-45 s among the defenders; 0 with the fix off).
* Still open, why `adventure.enemies` stays off:
  * The guest keeps scenario creatures the host does not have: 117 unpaired on the guest after ~10 minutes, 81 of
    them standing at 0 hp (`coop.adventure actors 600` on the guest: `hp=0.0 dead=0 key=00000000`). They have no AI
    (skipped) and nothing kills or removes them: the guest's own waves (armies spawned from markers) and creatures
    whose host copy died while unpaired. Next: a guest's unpaired scenario creature is hidden or removed after a
    grace time, and a paired one whose host copy is gone dies there.
  * The guest captain's hit on a host creature (Packet Hit -> host) is still not exercised: `coop.adventure hit
    <class>` on the guest picks the nearest creature of the class, which was a stale 0-hp copy (hits_out 0). Next:
    the test hit picks a paired creature.

## 2026-11-14 (feat/lineages-47, roadmap 3O A): on by default

* The two open points are fixed in BUG-9201: a guest's leftover creatures are removed after 5 s, and a creature
  the host killed dies on the guest (the game's Die). The guest captain's hits go to the host and are applied
  there (hits_in = hits_applied, the host's health comes back to the guest; a lethal hit kills on both).
* `adventure.enemies` is on by default (6b282874). Live: two City Assault missions to the end on TITAN, the guest
  following the host's creatures (85-98 creatures to the host's 78-89), 0 faults.
* Not tested yet: two PCs, 3-4 players, other adventures than City Assault.

## Verification

Live, two TITAN QA games, City Assault (2026-11-14): BUG-9200 (the strike: 0 hits with its switch off, 5-8 with it
on) and BUG-9201 (leftovers and deaths: FAIL with its switch off, PASS on). no-fail-run: not a lab run (TITAN QA
games, by hand; the FAIL/PASS pairs are in BUG-9200 and BUG-9201).
