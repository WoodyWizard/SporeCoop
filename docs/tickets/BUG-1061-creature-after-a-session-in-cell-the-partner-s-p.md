---
id: BUG-1061
title: Creature: after a session in Cell, the partner's puppet shows its own species' full health instead of the partner's for 10+ s after the next Creature join
status: open
kind: desync
stage: Creature
reported: 2026-11-13
source: fix/w49-life lab (coop_lifecycle_reset.py, monitor puppet_hp)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen by the invariant monitor (rule puppet_hp) in 3 of 4 runs of coop_lifecycle_reset.py on fix/w49-life (lane 4):
the two games played Cell (fixture cell), both left the session, the host loaded the creature fixture with
`coop.load`, hosted again and the guest joined (it loaded the host's world). At the 30 s mark the monitor saw
`guest shows host's creature at 10/10, host's own game 6/6` (out/lab/20261113-010644-2, 20261113-011601) and
`host shows guest's creature at 18/18, guest's own game 6/6` (out/lab/20261113-011935): the puppet carries its own
full health, not the partner's (creature.partner_hp, BUG-0373), on two lines in a row. Also with
`core.lifecycle_reset off` (011601), so not from the lifecycle registry. Not seen in a Creature session that did not
follow a Cell one (the cold-start creature tests, the lifecycle test with Creature first: 20261113-011223).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed. Inferred: creature_hp's drive_puppet erases the puppet's entry while the partner's state reports
max_health <= 0 (h_GetMaxHp then answers the puppet's own maximum), or a value of the Cell game is used for the
first Creature states (the player species, the level health).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
