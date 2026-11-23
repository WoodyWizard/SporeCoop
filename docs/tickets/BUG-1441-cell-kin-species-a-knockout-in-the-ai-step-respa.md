---
id: BUG-1441
title: cell_kin_species: a knockout in the AI step respawned the host next to the guest; its way back to its own place was measured from there, the host's place check ran at the guest's place (empty kin sets)
status: open
kind: test
stage: Cell
reported: 2026-11-22
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-night
commits: 68d86794
tests: tools/tests/coop_cell_kin_species.py (cell_kin_species --editor)
signature:
docs:
related: BUG-1407, BUG-1334, BUG-1257
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, lane 3, rel/0.7.16 5376d388): cell_kin_species "FAIL both
players at the host's place: the kin there are the host's kin on both games ({}; before the guest came [], drawn by the
guest as [])". All other checks passed (apart, AI, guest's place, after the editor, the save).

## Root cause
Test, BUG-1407's class at a step its fix (4c7f2925) did not guard. Host game log: s130 pos=(13.6 3.0) hp 6/6; s140
pos=(38.1 27.0) hp=3/6 (in the 20 s AI step, after key frame 02 at 22:22:59.6); `22:23:12.638 ko: own cell state 1`,
`Alice is knocked out`; `22:23:22.652 Alice respawned small next to Bob`; s160 partner d=7, size=0.24, pool=103 (was
284). The test then took "the host's place" (`h_at`) from where the host was at that moment, next to the guest, so
its way back led to the guest's place: key frame 03 at_guest_place host (84.6 0.0) guest (72.3 1.7), key frame 04
"at_host_place" host (73.0 1.0) guest (81.1 2.8). One sample 3 s after the move found no kin there on the host
(before = {}) and one 8 s after the guest's tp none on the guest ({}). Run 2 (20261120-133703): no knockout, the host's
place 147 m from the guest's, OK. Not 0.7.16 (21025f40..5376d388 changes only a log line of the kin code). The
knockout in spite of invulnfor stays BUG-1334 / BUG-1407 (mod side, not this ticket).
Not INCONCLUSIVE: the knockout and the respawn next to the partner are mod paths.

## Fix
coop_cell_kin_species.py: the host's place is taken before the AI step (`home`); the way back starts only once the host
is up (wait_up, invulnfor again) and is measured from the guest's position now; if that place lies within 2 r of the
guest (this run: 42 m) the host goes `dist` away instead (a place of its own); a check that the host is back more than
r from the guest (tried twice, with ko_diag). The kin there are sampled every 3 s for up to 45 s (the area the host
left was culled; one sample can hold none), the host's pool until it has a kin there and the guest's mirrors until a
kin of the host's cell is near; every sample counts for "only the host's kin" (union of before and kept), so nothing
is loosened.

## Verification
Open, waiting for the lab run:
- PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_kin_species --lane N --retry 0 --no-build`.
- FAIL expected (the BUG-1257 switch off): `SPORE_LAB_FIXOFF="cell_kin_species=cell.kin_species" SPORE_DETECTOR=20
  python3 tools/lab.py run cell_kin_species --lane M --retry 0 --no-build`.
no-fail-run: the knockout cannot be forced; the FAIL before is 20261122-194210 cell_kin_species.

## History
- 2026-11-22: opened.
- 2026-11-22: fix/w65-night: cause from the host's ko lines and the key frames' places; the way back is measured anew.
