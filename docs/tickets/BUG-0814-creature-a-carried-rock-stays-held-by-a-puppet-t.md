---
id: BUG-0814
title: Creature: a carried rock stays held by a puppet that was destroyed (frozen copy, possible read of the freed socket) (suspected)
status: not-reproduced
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch: fix/w48-freed
commits: be40ef44
tests: carry_holder
signature:
docs: docs/re/creature.md#creature-freed-block-reuse
related: BUG-0458 BUG-0426
---

## Report
No player report. When a partner who carries a stick or rock dies, hides, enters an editor or leaves, the copy in its puppet's hand is never released: at least a rock frozen in the air; at worst a crash when the object follows the freed creature's socket. Suspected.

## Root cause
Not reproduced (fix/w48-freed, 2026-11-12). The open question of the ticket, answered at runtime: the game's DestroyInstance of a puppet that holds the copy of a rock (Creature_HoldObject, +0x71 = 1) releases the hold slot itself: +0x71 is 0 within 1.5 s and the copy falls or rolls away (about 5 m in 5 s) with the 0.7.12 code (lab 20261112-011434, 20261112-012019-2). A later K_PUT that names the new puppet calls Release for an object it does not hold: Creature_ReleaseObject finds no slot (docs/re/creature.md#creature-hold-slots), nothing happens.

## Fix
None: a fix written first (release before every despawn, the holder remembered) made no difference in the test and was taken out (be40ef44). The diagnostics stay: `coop.rules carry` prints the last copy held for each player with the game's own fields (held +0x71, physics +0x1F0, position).

## Fix idea
Release the copy (Creature_ReleaseObject on the old puppet) before every puppet despawn / forget, and clear g_carried per player (PLAYER_STATE); RE first what DestroyInstance does with hold slots.

## How to test
Guest picks up a stick (`coop.rules carry` shows hold_rx/done on the host), then `coop.team die` on the guest. On the host the rock's +0x71 must be 0 and its physics normal within 1 s; no fault. Main: held flag stays 1 at the old spot.

## Verification
no-fail-run: not reproducible: the game lets the copy go by itself. Test `carry_holder` (tools/tests/coop_carry_holder.py) stays as a regression check: the 0.7.12 behaviour passes it (out/lab/20261112-012019-2, out/lab/20261112-012019); an early run (out/lab/20261112-011434) read the ground radius under the fixture's rock as 4.4 m below it (a mound the terrain query does not see) and was a false FAIL; the test judges by the copy's movement now.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12: not reproduced on fix/w48-freed: DestroyInstance releases the hold slot and the copy falls; the fix written for it removed, the test kept.
