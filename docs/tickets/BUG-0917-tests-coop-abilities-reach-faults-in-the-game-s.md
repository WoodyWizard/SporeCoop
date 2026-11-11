---
id: BUG-0917
title: tests: a caught fault in the game's Ability_TargetGap (0xC0000005 at 0x3F800000) on a guest in the campaign's Creature play; the campaign fails on it
status: open
kind: test
stage: Creature
reported: 2026-11-11
source: fix/w47-lab-memory lab out/lab/20261111-002733 (campaign, lane 9)
role: 
versions: 
fixed_in:
branch:
commits:
tests: campaign, long_creature
signature: 0xC0000005 at 0x3F800000, calls 00D99A4A (Ability_TargetGap+0x2A)
docs:
related: BUG-0618, BUG-0916
---

## Report
Found while measuring the lab's memory (BUG-0916): the whole campaign alone on lane 9 (fix/w47-lab-memory = rel/0.7.12
1e37ae95 plus test tooling only, lab out/lab/20261111-002733, `SPORE_DETECTOR=20`) ended `RESULT: FAIL` on one
finding: the first guest's game (Bob, instance 18) caught one fault 18 minutes into the session, in the Creature
play with four players (the play bots hunting):

    exception 0xC0000005 at 0x3F800000 (data 0x3F800000) tid=308 (main) eax=3F800000 ecx=22B794E0 esi=22B799C8
    calls: 00D99A4A 00A20B2E 00A35243 00A35140 00A35152 0075F074 011F8178 0076072C 0092A6A1 007E61AF
    called from: SporeCoop::Ability_TargetGap+0x2A < ...

The game went on (the frame was cut off, BUG-0667's handler); nothing else failed in the 81 minutes to Space
(the other two findings are `info`). The final check of 0.7.11 (rel/0.7.11 lab 20261108-114435) had no such fault.

A second time the same night: `long_creature` on lane 10 (lab out/lab/20261111-014656), the host's game 27 minutes
into the session, 1.4 s after `+18 DNA from Bob's hunt`:

    exception 0xC0000005 at 0x00D99AA1 SporeCoop::Ability_TargetGap+0x81 (data 0x0000002C) tid=356 edx=00000000
    calls: 00A20B2E 00A35243 00A35140 00A35152 0075F074 011F8178 0076072C 0092A6A1 007E61AF 00F47A6A

The first one came 1 s after a `relationship event ... killed` line of the same game. No game log under
rel/0.7.11, rel/0.7.12 or fix/w47-play-bots out/lab has a fault in this function: both sightings are with the play
bot of fix/w47-play-bots (merged into rel/0.7.12 at ef22410d), which asks `coop.abilities reach` more often and
keeps asking around a kill. Two long tests of the final check (campaign, long_creature) failed on it in one night.

## Root cause
Not analysed (outside the task that found it). Observed: the jump target is 0x3F800000 (the float 1.0) taken from
eax inside `Ability_TargetGap` (0x00D99A20, cdecl float(spatial* a, spatial* b, bool clamp0): it calls the spatials'
vt 0x74 for the radii), so one of the two spatials was not a live object any more (its vtable slot held data): a
stale target. Inferred, not checked: the target is the avatar's combat target (avatar +0xE7C), which is a creature
that was destroyed (a proxy of an NPC the host removed, or a dead animal) and still stood in that field.
Who called it is not determined: the play bot's test command `coop.abilities reach` calls this function on
avatar +0xE7C after only a `readable` check (mods/coop/debug.cpp), and the bots of fix/w47-play-bots ask it several
times a second; but the game's own ability use (`Ability_Use` 0x00D1E930, a key press) calls it on the same field,
and the fault's call list shows no address outside the game. If it is the game's own path, a player can hit it
(a combat target destroyed by the mod without the avatar's target being cleared).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Seen twice in about 3.5 hours of Creature play bots: `SPORE_DETECTOR=20 tools/lab.py run campaign --lane 9`
(out/lab/20261111-002733, the test `campaign`: campaign.guest.game.log at 00:45:35, campaign.log finding 1) and
`tools/lab.py run long_creature --lane 10` (out/lab/20261111-014656, long_creature.host.game.log at 02:14:27).
Not in the 10-minute play_creature of lab 20261111-002757. Next steps: whether the avatar's +0xE7C still points
at a destroyed proxy after `npc::destroy_proxy` (clear it there), and a liveness check of the target in
`coop.abilities reach` before it calls the game.

## History
- 2026-11-11: opened.
