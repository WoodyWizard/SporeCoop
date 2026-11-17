---
id: BUG-9249
title: Creature: the host's pack stays full (summoned count left in the save), recruits refused
status: fixed
kind: bug
stage: Creature
reported: 2026-11-16
source: QA TITAN 2026-11-16
role: host
versions: 0.7.14.1-lineages63-dev (a count left by older builds, kept in saves)
fixed_in: unreleased
branch: feat/lineages-47
commits: 5455e4a5
tests: manual Windows QA with tools/qa_win/qa.py (TITAN, Samuth.spo, host 9)
signature:
docs: docs/re/creature.md#creature-summon-pool
related: BUG-9233, BUG-9245, BUG-9248
---

## Report
TITAN QA 2026-11-16 (Samuth.spo): `coop.npc recruit` on the host: "no such npc or it cannot join" with nobody in the host's pack (`coop.gpack`: host pack list 0) and no summon running. The owner's earlier report of 2026-11-15, "I'm unable to add creatures to my Pack" (BUG-9233), is the same symptom.

## Root cause
Observed (runtime): the posse simulator (cPosseSimulator_Get) read +0x18 = 3 and +0x1C = 3; +0x1C is the count of summoned creatures (BUG-9233: 0 -> 4 at a flock), and the pack takes no member while its members plus that count reach +0x18 (the pack size). With no summon running the count stayed 3; it was 3 at the start of every session in that world (the guest_pack log: "summoned count 3 -> 6" at the first summon), so it comes with the save. Writing 0 there: the same recruit joined at once.
Inferred: left by a flock of an older build (before BUG-9233's put-back) whose count never went down; any world played with those builds can keep it.

## Fix
creature.posse_count_heal (guest_pack.cpp heal_posse_count): on the host (and alone) in the Creature stage, every 2 s, when no summon event ran its update in the last 2 s (drama::summon_running), a summoned count above 0 is cleared and logged. The game counts a flock up at its start and down at its end, so the count is 0 whenever no summon runs. `coop.fix creature.posse_count_heal off`: kept.

## Verification
FAIL (count 3 of 3, before the fix, 22:06): `coop.npc recruit 71` refused; after writing 0: "joined the pack".
PASS (22:09, switch off then on): the count written back to 3 stayed 3 with the switch off; 2 s after switching it on: "the pack's summoned count 3 with no summon running: cleared (pack size 3, 1 member(s), BUG-9249)".
no-fail-run: Windows QA by hand (no lab on this PC); the recruit after the automatic clear was not repeated (the session had dropped after the reload).

## History
- 2026-11-16: opened and fixed during the pack attack QA (feat/lineages-47, unreleased).
