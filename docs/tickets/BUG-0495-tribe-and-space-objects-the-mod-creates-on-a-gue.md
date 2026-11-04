---
id: BUG-0495
title: Tribe and Space objects the mod creates on a guest (tribe mirrors, UFO mirrors, puppets) go into the guest's saves
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-03
source: split from BUG-0470 (its Civ half fixed on fix/w40-civ-crashes)
role: guest
versions: 
fixed_in: 0.7.5
branch: fix/w41-tribe-world
commits: 629fbd74
tests: tribe_save_strip
signature:
docs: docs/re/tribe.md#tribe-guest-world
related: BUG-0470, BUG-0020, BUG-0522
---

## Report
Split from BUG-0470, whose Civ half (mirror vehicles) is fixed on fix/w40-civ-crashes (`civ.save_strip`,
test `civ_save_strip`). Still open: the Tribe (`SpawnNpcTribe` tribes and their members' mirrors) and Space
(UFO mirrors and puppets, the fallback puppet in the host's pack herd: modes.cpp) objects the mod creates
on a guest go into its saves. The Civ fix shows the pattern: a savestrip filter, and the lists that hold
the objects emptied of them while the save is written (savestrip::add_save_hooks), since a stripped
reference is read back as a null (BUG-0470: civ vectors faulted at 0xBF2BF2 on load). Each stage needs
its own save + reload test.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (lab 20261103-234621, `tribe_save_strip` with SPORE_FIX_OFF=tribe.save_strip): the guest (Carol) had 7
  members in its player tribe, 5 of them mirrors its game spawned for the host's (`spawn_member`), and 3 NPC tribes
  the mod made for the host's (SpawnNpcTribe, tribe.npc_tribes); its save, loaded alone, had all 7 members and the
  made tribe at the host's place (3 members, now an ordinary tribe of that world).
* Observed (code): the savestrip filters knew creatures, herds and Civ mirror vehicles only; a citizen (noun
  0x18EB4B7), a tribe and its hut / tools / food mat were written like the game's own.

## Fix
`tribe.save_strip` (tribe.cpp): the guest notes the members it spawns and the tribes it makes (pointer + serial);
a savestrip filter writes a null reference for them, and save hooks (savestrip::add_save_hooks) take the members out
of every tribe's member vector +0x340 and selectable vector +0x354 while the save is written (BUG-0470's null rule)
and put them back on the next frame. A made tribe goes with its hut (+0x368), tools (+0x36C, +0x380) and food mat
(the 0x629BAFE object holding the tribe, at +0x2C). Members of the guest's own game bound as mirrors stay.
The Space half (UFO puppets) moved to BUG-0522: not done here (needs its own save + reload test).

## Verification
* FAIL (`SPORE_FIX_OFF=tribe.save_strip`): out/lab/20261103-234621: the reloaded player tribe has 7 members (own 2,
  5 mirrors), a tribe stands where the made one did.
* PASS: out/lab/20261103-234419: the reloaded player tribe has the 2 own members, no made tribe, no fault; the
  mirrors were back in the session after the save.

## History
- 2026-11-03: opened.
- 2026-11-03: Tribe half fixed on fix/w41-tribe-world (tribe.save_strip); Space half split into BUG-0522.
- 2026-11-04: released in 0.7.5.
