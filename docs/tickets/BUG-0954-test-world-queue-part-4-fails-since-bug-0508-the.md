---
id: BUG-0954
title: Test world_queue part 4 fails since BUG-0508: the host's evolution goes in place for a guest in its editor, no world is sent for the BUG-0074 check
status: fixed
kind: test
stage: Creature
reported: 2026-11-12
source: lab (fix/w48-saves area run)
role: 
versions: 
fixed_in: 0.7.13
branch: fix/w48-new
commits: fc9436b4
tests: world_queue
signature:
docs: docs/06-testing.md#testing-modal-list-owners
related: BUG-0508, BUG-0074
---

## Report
Lab 20261112-024053 (fix/w48-saves, lane 3): world_queue part 4 FAIL "the host's world waits for the guest to
leave its editor" and "the guest stays in its Creature game after leaving the session". The last passing runs are of
2026-10-30, before BUG-0508. Reproduce: `tools/lab.py run world_queue`.

## Root cause
Observed in the host log: after the host's editor "evolved in place: Bob keeps its world" (BUG-0508: a guest in its
own editor counts as in the same world), so no world is sent and the BUG-0074 check (a received world waits while
the guest is in its editor, then is dropped at its Leave) never gets a world. Inferred: a stale test, not a bug of the
mod; the test needs a share that is not in place (for example `SPORE_FIX_OFF=world.evolve_in_place` inside the test
for that part, or a guest in another world). Not caused by fix/w48-saves (the in-place decision is untouched).
Confirmed: with `SPORE_FIX_OFF=world.evolve_in_place` world_queue passes on fix/w48-saves (lab 20261112-030856, lane 2).

## Fix
Test only (fix/w48-new): part 4 now checks what is true since BUG-0508: after the host's editor no world is sent to a
guest still in its own editor (the evolution goes in place). For the BUG-0074 check the host then shares its world
with `coop.share` while the guest is still in its editor (as at a load or a stage change of the host): the received
world waits for the editor and replaces the coop.load of part 3; the guest leaves, closes its editor and stays in its
Creature game for 20 s. The test now also reads `SPORE_FIX_OFF=world.<name>` as `--without <name>` (the test sets its
switches itself, so the lab's twin switch was undone before). No mod change.

## Verification
* Old part 4 on fix/w48-new: FAIL lab 20261112-064427-2 (lane 5, both part 4 checks).
* New part 4: PASS lab 20261112-065043 (lane 5).
* It still catches BUG-0074: `SPORE_FIX_OFF=world.world_reset` FAIL lab 20261112-065338 (lane 5: "the guest stays in its
  Creature game after leaving the session").

## History
- 2026-11-12: opened.
- 2026-11-12: part 4 rewritten on fix/w48-new (in-place evolution checked, the BUG-0074 share by coop.share).
- 2026-11-12: released in 0.7.13.
