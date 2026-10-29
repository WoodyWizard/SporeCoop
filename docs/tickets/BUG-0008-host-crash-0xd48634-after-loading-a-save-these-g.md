---
id: BUG-0008
title: Host crash 0xD48634 after loading a save; "these guys appeared"
status: fixed
kind: crash
stage: Creature
reported: 2026-10-25
source: Spore Exception Reports (host, 14:12 / 14:14 / 14:21, 0.7.2.1) + screenshots
role: host
versions: 0.7.2.1
fixed_in: 0.7.2.3
branch: fix/creature-e84
commits: 54f04c1
tests: creature_e84
signature: dc04aee5:0xD48634 read 0x32C (CreatureStageEventHandler, null +0xE84)
docs: docs/re/creature.md 12
related:
switch: npc.strays
---

## Report
After a reload extra creatures of the players' species stood in the world; attacking them crashed the host (3 times).

## Root cause
A host save made while the guest's puppet existed kept it and its herd; after a restart it came back as an NPC without archetype tuning (+0xE84); the attack message handler read its tuning.

## Fix
h_StageMsg gives any creature without tuning a blank one during the handler; remove_saved_strays removes such creatures in our kind of herd; puppet spawns reuse a herd.

## Verification
coop_creature_e84.py FAIL 3/3 before (crash at 0xD48634), 4/4 after. The cause at the source (puppets in saves) is BUG-0020.

## History
- 2026-10-25: reported (Spore Exception Reports (host, 14:12 / 14:14 / 14:21, 0.7.2.1) + screenshots).
- 2026-10-25: fixed on fix/creature-e84, released in 0.7.2.3.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: npc.strays (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
