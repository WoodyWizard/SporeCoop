---
id: BUG-1334
title: Tests: cell_kin_species apart half fails when the guest is knocked out alone in its fresh area and respawns next to the host
status: open
kind: test
stage: Cell
reported: 2026-11-20
source: lab
role: 
versions: 0.7.16
fixed_in:
branch: fix/w59-cell
commits: 505778b1
tests: cell_kin_species
signature: cell_kin_species "apart: the kin near the guest in the host's pool are of the guest's cell E219E5D2 ({'4DA56CA5': 1}"
docs: docs/06-testing.md#testing-arrival-vote-kin
related: BUG-1257, BUG-1309
---

## Report
0.7.16 final check out/lab/20261119-214823 (lane 2): cell_kin_species FAILED its "apart" half (no kin of the guest's
cell near the guest, AI of its kin n=0, "both at the guest's place" none) while it passed on fix/w57-cellkin merged
with rel (e10cac3d, 20261119-123540). The triage suspected a later merge, fix/w57-cellslow first.

## Root cause
Not a merge (the diff e10cac3d..f6c16890 changes nothing of the kin filter, the model hook or creations; cellslow
only paces what the host sends, while the host's OWN pool lacked the guest's kin). Observed: the guest, alone at level 10
in its fresh area 121 m from the host, lost hp 6 -> 3 (s 80) and was knocked out at s 84.6 (guest log `ko: own cell
state 1`, `bites_in=0`: its own game, although `coop.pool invulnfor 900` was set), then `respawned small next to Alice`
at s 94.6. The "apart" key frame is at 23:30:09 after 45 s of sampling: the sampling began at ~23:29:24, 2 s after
the respawn next to the host. Every kin "near the guest" was then the host's; the 81 kin the host had made of the
guest's cell (`partner=81`) were culled with the area the guest left. Knockouts are frequent in this test: 9 of 15
runs of it had one (fix/w57-cellkin 111537 .. 123540, rel 095414, 214823); the passing 20261119-123540 had its
knockout at s 64 next to the host (revived at once), before the players parted. Why invulnfor does not hold there is
not followed further (inferred: a damage path without the +0x111 check; docs/re/cell.md#cell-kin-follow).

## Fix
Test only (the check is unchanged): coop_cell_kin_species.py samples "apart" with the guard `apart_from`: a sample with
the guest within half the apart distance of the host stops the sampling without a verdict ('lost'), the players are
parted again (invulnfor again on both) and sampled anew, at most twice; the third try is checked as before.

## Verification
Lab not run yet (orchestrator: no lab until "lab ok", the final check runs on other lanes); commands below.
* FAIL run: out/lab/20261119-214823 cell_kin_species (lane 2).
* PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_kin_species --lane N --retry 0 --no-build`
* twin (BUG-1257's), FAIL expected: `SPORE_FIX_OFF=cell.kin_species SPORE_DETECTOR=20 python3 tools/lab.py run
  cell_kin_species --lane M --retry 0 --no-build`
* to split from cellslow as the triage asked: `SPORE_FIX_OFF=cell.slow_link SPORE_DETECTOR=20 python3 tools/lab.py run
  cell_kin_species --lane N --retry 0 --no-build` (PASS expected too).

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-cell): the night plan of the second final check ran the "split from cellslow" line as `SPORE_LAB_FIXOFF="cell_slow_link=cell.slow_link"` on cell_slow_link (PASS expected) instead of on cell_kin_species: that is cell_slow_link_off's case and it FAILED as it must (lab 20261120-133208: median 0.31, `c: 158 missing`; the twin 0.28 in 20261120-130608). The command meant: `SPORE_LAB_FIXOFF="cell_kin_species=cell.slow_link" SPORE_DETECTOR=20 python3 tools/lab.py run cell_kin_species --lane N --retry 0 --no-build` (PASS expected).
