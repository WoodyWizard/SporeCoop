---
id: BUG-0256
title: Creature: a guest that rejoins and loads the host's world saved while its puppet existed faults in Herd_Update (cCreatureAnimal::Update+0xAA2, caught) after the stray removal
status: fixed
kind: crash
stage: Creature
reported: 2026-10-28
source: fix/w5-core test run (coop_vote_drop_editor.py, lane 4)
role: guest
versions: main b423f56 (0.7.3.0)
fixed_in: 0.7.3.2
branch: fix/w9-creature
commits: bbdeb14 83e9c20 e4f980c d5279f6
tests: tools/tests/coop_rejoin_saved_puppet.py (suite rejoin_saved_puppet)
signature: 0x00C0B8D2 Creature_IsAdult (labelled cCreatureAnimal::Update+0xAA2) from Herd_Update+0x61, data 0xB34
docs: docs/re/creature.md#creature-herd-null-member, docs/06-testing.md#testing-w9-creature
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
Observed (2026-10-30, fix/w9-creature, lane 9, host and guest logs): the fault is
`Creature_IsAdult` 0xC0B8D0 on a **null member** of a herd's list, called by Herd_Update. The host
forgot the guest's puppet when it entered its evolution editor (the world stays through the editor);
back in the stage it removed it as a left-over puppet while the evolution cinematic played, so the
destroy was deferred; the rejoining guest's new puppet went into the same (reused) herd; the share
save stripped the new puppet (a null in the herd's list) and kept the herd for the left-over
creature (no longer a puppet). The guest loaded that herd and faulted every frame
(docs/re/creature.md#creature-herd-null-member). The stray removal on the guest is not the cause.

Earlier reading (2026-10-28): Inferred: the save the host shares holds the guest's puppet (savestrip, BUG-0020,
missed it; see BUG-0255 for its gaps); the guest's `remove_saved_strays` destroys that creature, and
its herd still updates a member pointer to it (null +0xB34). Guest logs: out/lab/20261028-235900-w5-core-twins/
vde_nofix1_guest.log, vde_nofix2_guest.log (fix/w5-core worktree).

## Fix
* `savestrip.pending`: a creature waiting for its deferred destroy counts as ours in the save filter
  (stripped, and its herd with it when nothing of the game is in it).
* `npc.herd_nulls`: a hook on Herd_Update takes null members out of the herd's list before the
  update (also for saves already written by older versions); `coop.npc herdnulls` counts them.

## Verification
`coop_rejoin_saved_puppet.py`: a passed vote, the guest leaves (own editor offline), the host leaves
its editor, the guest joins again and plays 20 s in the shared world; no fault on either game.
* FAIL without the fixes: out/lab/20261030-003045 (before savestrip.pending existed,
  `SPORE_FIX_OFF=npc.herd_nulls`: 1229 faults), out/lab/20261030-005130
  (`SPORE_FIX_OFF=npc.herd_nulls,savestrip.pending`: 1285 faults).
* PASS with both: out/lab/20261030-003904 (2/2; herd_nulls only, it took 1 null out).
* PASS on the final code (both fixes): out/lab/20261030-011525 (no null member reached the guest).
* PASS with savestrip.pending alone (`SPORE_FIX_OFF=npc.herd_nulls`): out/lab/20261030-004853 (no
  null member, no fault).

## History
- 2026-10-28: opened.
- 2026-10-29: looked at on fix/w5-world (BUG-0255): not covered. BUG-0255 strips empty herds and rebuilds the object list per save; the fault needs a guest puppet (species 7C329711) in the host's share save, and that save is written by the host, whose game log is not in the run folder, so why the filter missed the puppet is not known (candidates by reading: a puppet in the avatar's herd, the fallback for an unknown creation, which savestrip leaves on purpose; a puppet held by the deferred destroy during the evolution cinematic). Separately the guest side: `remove_saved_strays` destroys the creature while its herd still lists it (Herd_Update reads the freed member, data 0xB34): the stray removal should take it out of its herd first. Next: rerun the rejoin with the host log kept and `coop.savestrip` / `coop.npc herds` printed before the share save.
- 2026-10-30: reproduced and fixed on fix/w9-creature (savestrip.pending, npc.herd_nulls).
- 2026-11-01: released in 0.7.3.2.
