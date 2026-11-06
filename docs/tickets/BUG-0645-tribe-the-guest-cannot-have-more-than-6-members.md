---
id: BUG-0645
title: Tribe: the guest cannot have more than 6 members and its village shows 12/6
status: open
kind: bug
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-14
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0422, BUG-0418
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-14 (the guest, s 5519): "I can't spawn more than 6 members,
and I see my village as guest 12/6 (over the 6 limit)".

## Root cause
Observed (logs): the host's `tribe(host): members=` between 4 and 36 during Tribe (both players' members); the guest
`local_tribes=6`, `ours=69/5`, no rejection lines for the guest's commands near the mark (host `rejected=5` flat since
18:39); one "M0ng0r gave 1 tribe member to Sterben" at 19:04:50. docs/re/tribe.md: cTribe::SpawnMember returns 0 at the
population cap (0xC8EC60).
Inferred, not determinable from the logs: the guest's village shows the shared or mirrored member count (12) against
its own village's cap (6), and its own spawn is refused at that cap. Class BUG-0422 (per-player state) or BUG-0418.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: tribe fixture, the guest buys members up to the cap; read the guest's tribe member count (+0x340 list), its
     population cap and the HUD count against the host's. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
