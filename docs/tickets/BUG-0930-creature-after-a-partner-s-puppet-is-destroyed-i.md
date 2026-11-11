---
id: BUG-0930
title: Creature: after a partner's puppet is destroyed in a fight, NPCs still hold raw pointers into it (AI block +0x68C / +0x694 / +0x6D8, creature +0x354..+0x370) and one NPC keeps it alive as its last attacker (combatant +0x28)
status: open
kind: crash
stage: Creature
reported: 2026-11-11
source: found with coop.staleref while fixing BUG-0926 (lab lane 2, 2026-11-11)
role: host
versions: 0.7.x
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-stimulus-attacker
related: BUG-0926, BUG-0456, BUG-0918
---

## Report
No player report. `coop.staleref` (a scan of the host's memory for pointers into a destroyed creature, lab lane 2,
tools/tests/coop_stale_refs.py) lists what still names a partner's puppet after it is destroyed in a fight, besides
the stimulus fields fixed by BUG-0456 / BUG-0926 and the targets (BUG-0453, BUG-0918).

## Root cause
Observed (per removal, about a second of game time after the destroy; offsets of the holder):
* NPC AI block (creature +0xB4C) **+0x694** -> the puppet (+0), 2-8 NPCs, in every removal, also after the puppet's
  block was freed; **+0x6D8** -> the puppet, 1-4 NPCs, 5 of 6; **+0x68C** -> its combatant (+0x5A8), 1 NPC (the one
  that attacked it), gone a second later in the runs where the NPC lived.
* Creature **+0x354 .. +0x370** -> the puppet's +0xC0 sub-object, 1-4 creatures, changing from scan to scan (a list
  of neighbours that is rebuilt).
* The NPC the puppet hit last: **+0x5D0** (combatant +0x28) -> the puppet's combatant, with a reference: it keeps the
  destroyed puppet's block alive (vtables intact, destroyed flag set, count 1) until that NPC is hit by somebody
  else or goes. One creature's memory per such death stays that long.
Not known: what these fields are, which code reads them and whether it checks the destroyed flag. No fault or stop
was seen from them (6 + 6 + 8 removals with the block freed while they named it, lab lane 2).

## Fix
None yet. Next: name the fields (who writes AI +0x694 / +0x6D8 / +0x68C), then clear them in the destroy hook as the stimuli are.

## Verification
How to see it: `SPORE_LANE=2 python3 tools/tests/coop_stale_refs.py --rounds 6` (report only; `--kill-first --watch 6`
frees the block first; `--strict` fails on any holder).

## History
- 2026-11-11: opened.
