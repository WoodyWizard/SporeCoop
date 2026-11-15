---
id: BUG-0917
title: tests: a caught fault in the game's Ability_TargetGap (0xC0000005 at 0x3F800000) on a guest in the campaign's Creature play; the campaign fails on it
status: fixed
kind: test
stage: Creature
reported: 2026-11-11
source: fix/w47-lab-memory lab out/lab/20261111-002733 (campaign, lane 9)
role: 
versions: 
fixed_in: 0.7.12
branch: fix/w47-target-gap
commits: bb0c7737, 5dc9698c, 0d586e59
tests: avatar_stale_target, play_creature, long_creature_short, campaign, long_creature
signature: 0xC0000005 at 0x3F800000, calls 00D99A4A (Ability_TargetGap+0x2A)
docs: docs/re/creature.md#creature-avatar-stale-target, docs/06-testing.md#testing-commands-object-list
related: BUG-0618, BUG-0916, BUG-0918, BUG-0919
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
(2026-11-11, fix/w47-lab-memory, before the analysis:)
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

(2026-11-11, fix/w47-target-gap:)
**The caller was the test command, in both faults.** Observed:
* The second fault's report in the test log has the stack words from esp (0x0012D0A0): `21B6A800 7BE9E5FC 0012D0F8
  21B6A258 00000000 0012D0F8 0000001C 0012D0C0 0000001C 0012D178 70C286D4 C2A7E904`. `Ability_TargetGap`'s frame
  (`sub esp,0x1C; push ebx; push edi`, and `push esi` on the path of the fault at +0x81) puts its return address at
  esp+0x28: **0x70C286D4**, outside the exe. With coop.dll's load base 0x70B20000 (the only 64 KB-aligned base of
  the 49 its text section allows for which addr2line names a function) it is `c_abilities`, mods/coop/debug.cpp:250
  of that build: the line after the command's call of the gap function. The first word is the saved esi = the
  target's combatant (0x21B6A800 = 0x21B6A258 + 0x5A8), the last is the x of the avatar's position that the
  function stores over its first argument (-83.9, the trace's position).
* The game calls `Ability_TargetGap` from 36 places (all direct calls, no pointer to it in the image); none of their
  return addresses is in either fault's `calls:` list, and both lists are the same chain (0xA20B2E 0xA35243 ...
  0x7E61AF: the frame above the console's dispatch). That list holds exe addresses only, which is why the ticket
  saw "no address outside the game".
* The command read the avatar's target field (+0xE7C), took it for a creature's combatant (minus 0x5A8) and called
  the game after `skg::readable`. The game itself (Ability_Use at 0xD1EDB4) casts the field through the combatant's
  vt 0x5C first. In the faults the creature's memory was not a creature any more: its spatial's vtable was a heap
  pointer (slot 0x74 = 1.0f, the jump to 0x3F800000) and null (the read at +0x2C).
* Why the field is stale (lab, coop_avatar_stale_target.py): the game does not clear the avatar's target when the
  creature it names is destroyed (it does for NPCs). With the partner's puppet as the host's target (a click), the
  field named the removed puppet for the whole 1.5 s watched, 8 of 8 rounds (lab 20261111-030815, -031056), and the
  puppet's address was the address of a later puppet: freed memory. In play_creature (lab 20261111-025527, the
  guarded command) the host's bot had the partner's puppet as its target when the partner died: the field named
  the destroyed puppet 0.2 s later (the command's new log line).
Inferred: which creature the two original faults named (the logs of those runs do not have the pointers): a proxy
the host's game removed (the guest, campaign) and an animal or the puppet (the host, long_creature). The stale
reads fault only when the freed block was given to something else, which is why it took 18 and 27 minutes.
The game's own path reads the same field: BUG-0918.

## Fix
* `coop.abilities reach` (mods/coop/debug.cpp): the target is looked up in the game's object list by pointer
  compares (`target_kind`); the game's gap function is called only for the combatant of a live creature of the list.
  Anything else prints `target none (the game's target ... is no live creature / no object of the world's list)`
  (the bots read it as no target) and one log line per such target. Switch `coop.fix test.reach_target` (unsafe:
  off is the old readable check).
* `coop.npc list` (mods/coop/npc.cpp): the host's NPCs, the guest's proxies and herds are used only when they are
  live objects of the list in this call (it made virtual calls on them: body_print, noun_id). Found by an audit of
  the commands the bots poll; the rest of the audit is BUG-0919 (open).
* The cause of the stale field is fixed in the game path: BUG-0918 (`game.avatar_stale_target`).

## Verification
Seen twice in about 3.5 hours of Creature play bots: `SPORE_DETECTOR=20 tools/lab.py run campaign --lane 9`
(out/lab/20261111-002733 of fix/w47-lab-memory, the test `campaign`: campaign.guest.game.log at 00:45:35) and
`tools/lab.py run long_creature --lane 10` (out/lab/20261111-014656, long_creature.host.game.log at 02:14:27).

Test `avatar_stale_target` (tools/tests/coop_avatar_stale_target.py, lane 9, fix/w47-target-gap):
* FAIL: out/lab/20261111-030815 (`SPORE_FIX_OFF=test.reach_target`, before BUG-0918's fix existed): "coop.abilities
  reach never took the removed puppet for its target (80 times)": the old command called the game on the removed
  puppet in every ask. No fault in the 86 s (the freed block kept its content).
* With the guard on and the game fix off (out/lab/20261111-031056): reach used the removed puppet 0 times of 96 asks
  while the field named it (the run fails on BUG-0918's check only).
* PASS: out/lab/20261111-031225, out/lab/20261111-031354.
Play runs with both fixes (fix/w47-target-gap, `SPORE_DETECTOR=20`, no fault in any game log): play_creature x3 on
lane 9: out/lab/20261111-031649, out/lab/20261111-032734, out/lab/20261111-033823 (all OK; the avatar's target was
cleared at a destroy 1, 3 and 2 times, the reach command met no stale target any more); long_creature_short x2 on
lane 10: out/lab/20261111-031658 (both OK, 4 clears). Before the game fix, play_creature out/lab/20261111-025527
(the guarded command only): OK, one stale target met and reported as no target.
The full long_creature (lane 10, 45.9 min, the test that failed): out/lab/20261111-034946 OK, no fault, 3 clears.
Related tests on lane 9 (all OK): creature_kills out/lab/20261111-035019, npc_lost_target out/lab/20261111-035136,
team out/lab/20261111-035316, avatar_stale_target out/lab/20261111-035408. The campaign was not rerun.
`make test` passes.

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-target-gap: the caller was the test command; the command and `coop.npc list` check the object list; BUG-0918 (the game path) and BUG-0919 (the audit's rest) opened.
- 2026-11-11: released in 0.7.12.
