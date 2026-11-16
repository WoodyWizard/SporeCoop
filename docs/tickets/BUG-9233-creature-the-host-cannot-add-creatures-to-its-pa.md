---
id: BUG-9233
title: Creature: the host cannot add creatures to its pack while a guest's flock is out
status: fixed
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15 (59-dev)
role: host
versions: 0.7.14.1-lineages59-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: e1ce9e69
tests: manual Windows QA: the MSI, two games, coop.gpack summon + coop.npc recruit
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (59-dev): "I'm unable to add creatures to my Pack." Host log: the guest BigTexas used Summon Flock (18:24 and later; a second one at 18:38 made nothing).

## Root cause
Observed (runtime, the MSI, 2026-11-15): the posse simulator's dword +0x1C counts the summoned flock (0 -> 4 when a guest's flock is made); the game lowers it when the flock leaves its own way, but guest_pack takes the members out of the host's pack list by hand, so it stayed at 4 and the host's pack took no recruit (3 recruits join with no flock; none after a guest's flock; the host's own summon does not block them). Ruled out: cPlayer +0x1250 mTempPosseCount (0 before and after), the summon event still running (ending it changed nothing).

## Fix
- After a guest's flock is handed over, the posse simulator's summoned count (+0x1C) is put back to its value before the summon; members found back in the host's pack list leave it again; the partner's summon event ends at once (drama::end_flock_event). Switch creature.guest_flock_out_of_pack.
- With BUG-9229 the event runs with the guest's creature as its leader (drama::summon_for), so the flock follows the guest.
- Tests: `coop.gpack` prints the posse simulator's 0x58 bytes and the host's pack list length.

## Verification
FAIL (the MSI, two games, 59-dev code paths): after `coop.gpack summon 1` the posse count +0x1C went 0 -> 4 and `coop.npc recruit` refused every NPC; 3 recruits in a row joined with no flock.
PASS (the fix): count put back 0 -> 0 after the flock of 5; the next recruit "joined the pack"; the flock stayed 3-8 m from the guest.
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-15: opened.
- 2026-11-15: found and fixed.
