---
id: BUG-1517
title: Effects: a guest's pool-3 one-shot is not relayed when the host has no live puppet of that guest (3-4 players) (UIFX-6)
status: open
kind: bug
stage: Creature
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w82-creature
commits: e44b4ff0, 9a4c9bc3
tests: tools/tests/coop_effects_party.py --oneshot (effects_party_oneshot, twin effects_party_oneshot_off; 3 players)
signature:
docs: docs/re/creature.md#creature-meter-hold
related: BUG-0451
switch: effects.bone_local
---

## Report
External static audit of 0.7.15.5, item UIFX-6 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-C1 (fix/w82-creature). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (effects.cpp), confirmed by the audit's verify row: `on_message` ran `valid_message`
before the host's relay, and for pool 3 it checked the bone index against the RECEIVER's puppet of the sender
(`partner_puppet(player)` + `creature_bone_ok`). A host with no live puppet of guest 1 (not made yet, the host
elsewhere) dropped guest 1's pool-3 one-shots before relaying them, so guests 2-3, who have that puppet, never got
them. The own_only / foreign_id half of the audit item is FALSE as a bug: those are receiver-side checks every guest
runs again.

## Fix
effects.cpp: with the fix the wire checks (pool, id, transform) stay before the relay; the bone of a pool-3 start is
checked against the puppet that plays it, right before the game's CreateEffect, on every game (the host too). Counted:
`relayed_unchecked=` (host), the drop stays `bone=`. Tests: `coop.effects nopuppet <player> [0|1]` makes this game
treat the player as having no puppet. Switch `effects.bone_local` (off: checked before the relay, 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: effects_party_oneshot, effects_party_oneshot_off; one 3-player
run each).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run effects_party_oneshot --lane N --retry 0 --no-build` (guest 2's game makes guest 1's mating call
  E1B24DDB on its puppet of guest 1: `pool=3 game=1`; key frame oneshot_relayed).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run effects_party_oneshot_off --lane N --retry 0 --no-build` (nothing reaches guest 2).
* Regression: effects_party, creature_effects.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-C1), branch fix/w82-creature.
- 2026-11-23: fix and tests on fix/w82-creature (e44b4ff0); waiting for the night lab run.
