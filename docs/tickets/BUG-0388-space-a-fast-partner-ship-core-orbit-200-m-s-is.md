---
id: BUG-0388
title: space: a fast partner ship (core orbit ~200 m/s) is drawn up to ~30 m off its own path for ~0.2 s when it turns or stops
status: fixed
kind: bug
stage: Space
reported: 2026-11-01
source: coop_space_finale_wait.py coop.fleet track, fix/w31-space-gate
role: 
versions: 
fixed_in: 0.7.6
branch: fix/w42-space-life
commits: 003267c3
tests: space_finale_wait
signature:
docs: docs/re/space.md#space-fast-ship-track
related:
---

## Report
lab 20261101-100032 (fix/w31-space-gate, lane 10, coop_space_finale_wait.py with `coop.fleet track`): in
the galactic core's system the guest's ship (at the gate, the core's orbit moves it at up to ~210 m/s)
is drawn on the host 20-31 m from every point of its own path over the 0.5 s before, for ~0.2 s
(s 65.707-65.914, 11 of 84 rows), while the guest's ship stopped (own x -191 -> -218.3, then still):
the drawn ship came from y -211 to -184 while the own ship stayed at y ~-178. Every other row was
within 0.1 m of the path. In the run before (20261101-095432) the same direction was within 0.1 m at
207-235 m/s. Inferred, not checked: the partner interpolation (SnapshotBuffer, extrapolation up to
0.25 s with the last velocity) overshooting on a sudden stop or turn, or snapshots arriving late
during the host's cinematic. Reproduce: `lab.py run space_finale_wait`, line `core: the guest ship
on the host` (printed only, not checked).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (lab 20261104-100758 #1 and #2, 20261104-101851: reproduced in 3 of 3 runs, worst rows 27.0, 16.8 and
  12.5 m): with the puppet's track rows extended by the pose the mod set (`coop.fleet track`: set, extrap, pin)
  every off row was pinned, interpolated (no extrapolation) and drawn exactly where it was set. So the host drew
  the interpolated state; the "off path" was in the measurement.
* Observed: the guest's own rows had a hole of ~1.5 s every ~0.7 s (20261104-101851: 499 of ~1500 rows read).
  SporeKit's `out_printf` (and `outf`) formatted into an 8192-byte buffer and cut the rest without a word: a
  `coop.fleet track dump <i> 300` answer held the first 97 rows. Across a hole `coop_space_drawn.path_dist`
  interpolates the guest's path along the straight chord of its orbit (radius 276 m), and the drawn ship on the
  real arc was up to 30 m from it. Where the own path had no hole (98 rows of that run) the drawn ship was within
  0.17 m of it (offline check of the saved tracks).
* Inferred: the 0.7.4 observation (lab 20261101-100032) is the same artifact (the old 10-column rows: ~110 of 400
  per call); the interpolation and the 0.25 s extrapolation were not involved (`extrap` 0 in every off row).
* The same cut hit every long dump read in pages (coop.track by the Creature / Tribe walk tests, coop.npctrack):
  those tests compared over the rows they got.

## Fix
SporeKit `kit.fix longout` (sporekit/commands.cpp `out_vappend`, used by `out_printf` and `outf`): one printf may
give up to 1 MB, a longer one ends with a line `[output cut at 1 MB]`. Off: the 8 KB cut (SporeKit 0.3.8).
twoinst takes `SPORE_FIX_OFF=kit.<name>` for SporeKit switches. space_finale_wait checks the guest's ship on the
host now (it was printed only) and also while it is fast (>= 100 m/s: 98 % of those rows within 3 m of its path);
`coop_space_drawn.dump` says when it read fewer rows than the track holds, and space_finale_wait checks that
both dumps are complete (a path with holes proves nothing). Puppet track rows carry the pose set,
its extrapolation and the pin (diagnostics).

## Verification
* Reproduced before the fix: out/lab/20261104-100758 (#1 27.0 m, #2 16.8 m), out/lab/20261104-101851 (12.5 m)
  (the check was printed only then).
* FAIL (`SPORE_FIX_OFF=kit.longout`): out/lab/20261104-104458 (406-407 of 1024 rows read, 10 of 22 fast rows beyond
  3 m); also 20261104-103258 #2 and 20261104-103851 #2 (the fast-row check alone, before the completeness check:
  it caught 2 of 4 runs, the others had under 10 fast rows left).
* PASS: out/lab/20261104-105026 (every row read, 88 fast rows, max 0.2 m), 20261104-102729 #1 and #2 (142 and
  158 fast rows at up to 217 m/s, max 0.1 m).

## History
- 2026-11-01: opened.
- 2026-11-04: root cause found on fix/w42-space-life (the console cut answers at 8 KB, a measuring gap, not a drawing error); SporeKit kit.fix longout; space_finale_wait checks the fast ship.
- 2026-11-04: released in 0.7.6.
