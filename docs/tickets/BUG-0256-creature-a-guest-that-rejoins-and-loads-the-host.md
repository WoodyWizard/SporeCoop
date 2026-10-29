---
id: BUG-0256
title: Creature: a guest that rejoins and loads the host's world saved while its puppet existed faults in Herd_Update (cCreatureAnimal::Update+0xAA2, caught) after the stray removal
status: open
kind: crash
stage: Creature
reported: 2026-10-28
source: fix/w5-core test run (coop_vote_drop_editor.py, lane 4)
role: guest
versions: main b423f56 (0.7.3.0)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen twice on lane 4 (GOG 2024, main b423f56 code plus fix/w5-core, Creature fixture), in the
first version of `coop_vote_drop_editor.py` that ran both cases in one session:
1. host and guest play; an evolution vote passes (the host in its editor), the guest leaves;
2. the host leaves its editor, the guest joins again; the host shares its world (a save made
   while the guest's puppet existed), the guest loads it;
3. on the guest: `removed a creature of species <guest species> without tuning in a herd like
   ours (from a save made while a partner's puppet existed)`, then 0.9 s later
   `exception 0xC0000005 at 0x00C0B8D2 Simulator::cCreatureAnimal::Update+0xAA2 (data 0x00000B34)`
   called from `SporeCoop::Herd_Update+0x61`, repeated every frame (961 and 1661 faults in the
   two runs; SporeKit caught them, the game went on).
Reproduce: `coop_vote_drop_editor.py` at commit c4f4981 of fix/w5-core with both cases in one run
(the rejoin step), or any rejoin into a host save written while the guest's puppet was in the world.

## Root cause
Not analysed. Inferred: the save the host shares holds the guest's puppet (savestrip, BUG-0020,
missed it; see BUG-0255 for its gaps); the guest's `remove_saved_strays` destroys that creature, and
its herd still updates a member pointer to it (null +0xB34). Guest logs: out/lab/20261028-235900-w5-core-twins/
vde_nofix1_guest.log, vde_nofix2_guest.log (fix/w5-core worktree).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
- 2026-10-29: looked at on fix/w5-world (BUG-0255): not covered. BUG-0255 strips empty herds and rebuilds the object list per save; the fault needs a guest puppet (species 7C329711) in the host's share save, and that save is written by the host, whose game log is not in the run folder, so why the filter missed the puppet is not known (candidates by reading: a puppet in the avatar's herd, the fallback for an unknown creation, which savestrip leaves on purpose; a puppet held by the deferred destroy during the evolution cinematic). Separately the guest side: `remove_saved_strays` destroys the creature while its herd still lists it (Herd_Update reads the freed member, data 0xB34): the stray removal should take it out of its herd first. Next: rerun the rejoin with the host log kept and `coop.savestrip` / `coop.npc herds` printed before the share save.
