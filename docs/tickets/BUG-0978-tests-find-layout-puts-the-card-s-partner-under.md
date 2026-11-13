---
id: BUG-0978
title: tests: find_layout puts the card's partner under a second, older notice card that then expires (the guest's own creature notice)
status: fixed
kind: test
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447, reruns 20261112-205503
role: 
versions: 0.7.13-rc
fixed_in: unreleased
branch: fix/w48-fui
commits: ae79f2aa
tests: find_layout
signature:
docs:
related: BUG-0949
---

## Report
rel/0.7.13 final check (lab out/lab/20261112-175447) and both reruns (out/lab/20261112-205503, find_layout#1 and
#2): `FAIL the tag of the partner under the message card is not drawn over it`. It passed in the integration run
out/lab/20261112-135150 (rel/0.7.13 at 9b0785ef, lane 7).

## Root cause
Observed: the failing runs list two notice cards on the guest when the test reads them 2.5 s after the host's
message: [856, 228, 1416, 286] and [870, 162, 1430, 220]; the passing run one, [856, 162, 1416, 220]. The second,
older card is the guest's own notice "Bob plays its own creature '...'" (logged 3.8 s before the message): in the
failing runs the lane's worlds.ini had a record that the guest played its own creation in this world, so the
guest's game swapped to it right after the join (BUG-0928's swap); in the passing run (lane 7) it played the world's
species. The test took cards[0], the lower card at y 228 (the host's message, pushed down below the older
notice), and put the partner D under its centre (G206 at y 247-267). One second later, when the labels were read,
the older notice had expired and the host's card had moved up to y 162: G206 was no longer under any card, so the
mod drew it (correctly), and the test's "D hidden" check failed. The same pass found no label over any UI window.
The key frame of #1 shows G206 below the message card, over empty sky.
So no regression of the mod: none of the merges since 9b0785ef changed find_layout's code; the new factor is the
lane's state (a world record of the guest's own creature), which makes the extra card. Bisecting was not needed.

## Fix
tools/tests/coop_find_layout.py: before the host's message the test waits (up to 15 s) until no notice card is
up on the guest, so the host's card is the only one and stays where D is put.

## Verification
FAIL (the old test, the guest's own creature notice up): out/lab/20261112-205503 (find_layout#1, #2), the final
check out/lab/20261112-175447. PASS: out/lab/20261112-211719 (find_layout 2 of 2 on lane 4, the guest's own
creature notice 'Ms Spoffit' came up in both, the host's card alone at [856, 162], G206 hidden).

## History
- 2026-11-12: opened.
- 2026-11-12: cause (the test's card choice with two notice cards), test fixed; fix/w48-fui.
