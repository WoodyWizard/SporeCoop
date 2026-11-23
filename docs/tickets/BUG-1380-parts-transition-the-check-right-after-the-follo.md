---
id: BUG-1380
title: parts_transition: the check right after the follow phase races with the world the host sends after all after its first creature editor (guest in GalaxyGameEntry)
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: final check 20261120-133703 (parts_transition)
role: guest
versions: 
fixed_in: 0.7.16
branch: fix/w61-creature-a
commits: c4261908
tests: parts_transition
signature: FAIL the guest plays Creature with an avatar (0x02CCD1D2 GalaxyGameEntry)
docs: docs/06-testing.md#testing-creature-after-worlds-reset
related: BUG-1215, BUG-0508, BUG-0441
---

## Report
Final check of 0.7.16 (lab 20261120-133703, lane 6) and the run before it (20261119-214823): parts_transition "FAIL the
guest plays Creature with an avatar (0x02CCD1D2 GalaxyGameEntry)" right after "all guests play creature together with
the host"; every later check of the test passed. It passed in 20261120-041304.

## Root cause
Observed (guest game logs): the host's first creature editor keeps the guest in place (BUG-0508) and 30 s later sends
the world after all (BUG-1215, host log of 20261119-214823: "Bob still plays 2929AD56 after 30 s: sending the world").
In 20261120-133703 the guest got it at 18:24:31.3 ("receiving world from Alice", the stage left for the galaxy menu at
18:24:32.9, Creature again at 18:24:36.6); the campaign's follow phase reported the guest in Creature at 18:24:32 and
the test's check came at once: GalaxyGameEntry. In the passing run 20261120-041304 the same share arrived 13 s after
the follow phase (06:28:51 vs 06:28:38). A race of the test with the designed path (BUG-1215 already let the test wait
for it before its editor vote), not a mod change.

## Fix
coop_parts_shared.py --from-cell: the check waits up to 60 s for the guest to play Creature with an avatar and prints
the modes it saw (GalaxyGameEntry / Loading on the reload); a guest that does not come back to Creature still fails.
Not loosened: the same state is required, only its moment is no longer the one frame after the follow phase.

## Verification
Was waiting for the lab run (no lab runs today, owner rule):
`SPORE_DETECTOR=20 python3 tools/lab.py run parts_transition --lane N --retry 0 --no-build` must PASS (its log names
the modes seen when the share lands at the check). FAIL runs of the old test: 20261119-214823, 20261120-133703.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): parts_transition PASS in 20261121-003202, 20261122-194210, 20261123-023643. Closed.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-creature-a): cause by the logs; the test waits for the state.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
