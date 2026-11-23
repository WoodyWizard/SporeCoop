---
id: BUG-1523
title: Creature: a guest in the host's Creature world sees the host's NPCs but not the host's creature at first (the host's puppet hidden)
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-23
source: discord
role: guest
versions: 0.7.15.5
fixed_in:
branch: fix/w82-creature
commits:
tests: none (not reproduced)
signature:
docs: docs/re/creature.md#creature-meter-hold
related: BUG-1434
---

## Report
Player report on Discord 2026-11-22 (coop 0.7.15.5, Steam; out/research/dc-report-20261122-world-switch.md section 5, the guest's own marks after the world switch): mark 1: "I can see the host's AI creatures but not the host's creature". No matching ticket found with `tickets.py find`. Next: fix-player-report (reproduce with a test that fails first).

## Root cause
Not found. The guest's log (the report's last zip) has the host's puppet made from the host's creation and shown from
the reload on (`puppet=shown d=2 ... puppet_hp=10/10` at 14:14:11, 2-10 m from the guest until the mark at 14:15:11),
`npc: the puppet ... of player 0 had no archetype tuning: the player's given (BUG-0722)` and no failure line about its
creation or model. Every mark's screenshot has the co-op window over the middle of the screen (the mark is written in
it), so the shots do not show the place. The session ran on a co-op copy of the other player's old planet after a role
swap (BUG-1434). The host's own log of that session is not in the reports.

## Fix
None.

## Verification
* no-fail-run: not reproduced; the guest's log says the puppet was made and shown next to it, no screenshot shows the
  place and no other report since names it. Reopen with a report whose shot (F9 with the window closed) shows it.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-C1), branch fix/w82-creature.
- 2026-11-23: analysed on fix/w82-creature (the report's logs and shots): not reproduced, closed (no-fail-run).
