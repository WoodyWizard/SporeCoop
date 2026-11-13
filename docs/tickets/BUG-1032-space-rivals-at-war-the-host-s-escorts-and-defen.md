---
id: BUG-1032
title: Space: Rivals at war: the host's escorts and Defenders and a guest at war with the host cannot damage each other (hits dropped as friendly)
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-3
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-war-escort-hits
related: BUG-0192, BUG-0420, BUG-0705
---

## Report
Code audit for 0.7.14, finding SPC-3. Not seen at runtime, found by code reading.
In Rivals with a guest at War with the host, the host's escort ships and the Defenders of its systems attack the guest's puppet (its political id is -1, an enemy to the game), but the guest's real ship never takes the damage; the guest's shots at the host's escorts (their mirrors on the guest) do nothing on the host either. The player sees the host's escorts shooting at it without effect and cannot destroy them.

## Root cause
Observed in code (main efd2e0ba), confidence high:
* space_rivalry.cpp:640-656 puppets_tick gives the puppet of a player at war with us political id kEnemyPol (-1): the host's own NPC ships of its empire now target it.
* squadron.cpp:279 (host h_TakeDamage): `own = ... && friendly(att)`; friendly() (squadron.cpp:153-159) is true for any attacker with the host's empire id, so the escort's hit on a War puppet is dropped and no MSG_UFO_DAMAGE goes to the guest. BUG-0192 documents the opposite: NPC ships of the host hit an enemy puppet and the hit is relayed to its owner.
* squadron.cpp:1107 (host, MSG_UFO_HIT): a guest's shot at an escort of the host's empire is dropped by friendly() without asking rivals()/relation; and :1116 applies the hit with the host's own empire id (PlayerEmpireID) as the attacker's political id, so even without the filter the game skips damage within one empire (BUG-0705's runtime finding).
The BUG-0420 filter (space.friendly_hits) was written for Allies, where the game ignores such hits anyway; in Rivals at War it removes real hits. The audit's word "asymmetric" is only half right: both directions are dropped; the visible asymmetry is that the host's escorts fire at the guest at all.

## Fix idea
friendly(att) and the MSG_UFO_HIT filter: not friendly when spaceext::rivals() and the hit's other side is a player at War with the empire's owner (the host); MSG_UFO_HIT from a War guest applies the hit with kEnemyPol as the attacker's political id (or the guest puppet's own). Owner decision first: should the host's escorts and Defenders fight a guest at war (BUG-0192 says NPC ships of the host do)? Switch: `coop.fix space.war_npc_hits off`.

## How to test
New coop_space_war_escort.py (Space fixture, Rivals): host and guest at War, the host spawns an escort of its empire (`coop.fleet spawn own`), the escort is made to hit the guest's puppet (`coop.fleet hit` with the escort as the attacker) and the guest shoots the escort mirror; assert the guest's ship hp (its game) and the escort's hp on the host go down. Main: FAIL (friendly_dropped grows, hp unchanged).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
