---
id: BUG-0342
title: Cell: a guest whose own cell is applied by the level rebuild at the world load is a ghost (cannot eat, bite or be hit)
status: open
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (host and guest marks)
role: guest
versions: 0.7.3.1
fixed_in:
branch: fix/w22-reports
commits: 2a4420a c8f91ff
tests: cell_own_load (tools/tests/coop_cell_own_load.py)
signature:
docs: docs/re/creations.md (4.2 Cell stage), docs/re/cell.md#cell-rebuilds-in-session
switch: cell.own_rebuild
related: BUG-0345, BUG-0341
---

## Report
Host on GOG 2024, guest on Steam (both Windows, 0.7.3.1), a new world in Cell. Marks of both
players in the same session: the host "the guest got its own cell from another world and is
basically a ghost which cannot interact with anything" (about 5 min after the load), later "my
friend is a ghost"; the guest "I was a ghost (go through everything, can't eat, can't attack or
get attacked)".

## Root cause
Observed (guest log, 10 s `coop: cell:` lines, `creations:` lines):
* Ghost periods are exactly the times after a world load at which the guest's own cell (from its
  choices.ini or its newest own creation, `creations.cpp apply_own_cell`) was applied with
  `own cell is now <key> (level rebuilt)`. That line comes in the same millisecond as
  `stage: Loading -> Cell`, i.e. RebuildLevel is requested (G+0x51D8 = 1) on the first frames of a
  freshly loaded Cell game.
  * load 1 (own cell applied by the rebuild): 10 minutes with `ate=0 bites_sent=0` in every 10 s
    line, also with plants 0-1 m away (`nearest plant=0..1`, `plants=43/61` on screen); sync-check
    MISMATCH twice (38 and 57 cells apart).
  * load 2 (the guest's cell key was already the one applied, so no rebuild, the game kept the
    world's model, `(DIFFERENT)` in the trace): normal play, eats and bites from the first minute.
  * load 3 (a new own cell from the guest's editor applied by the rebuild again): ghost again for
    about 4 minutes (the marks), no eat, no bite.
  * after the guest's own cell editor (the game's own RebuildLevel, same model): eats and bites
    again at once.
  * a later new world: the same rebuild at the load; the report was made 50 s later (too short to
    tell, `ate=0` meanwhile).
* The lab test `coop_creations_cell.py` chooses the own cell after the guest is in Cell, waits
  12 s and checks the keys and the mirror count only, not that the cell can eat, bite or be hit.

Inferred: the rebuild requested on the first Cell frames of a load leaves the new avatar without
its interaction (collision/abilities): the editor's own path refreshes the unlocked parts
(`S+0x34`, 0xE4FBA0) before RebuildLevel and is not run on the first frames of a load;
`apply_own_cell` does only `S+0x10` + PreloadModel + the flag. Not checked which of the two
(timing or the missing part refresh) is the cause.

CORRECTION (2026-10-31, fix/w22-reports, from fix/w22-reports2 out/w22-evidence.md): "ghost iff the own
cell is rebuilt at the load" is wrong: a rebuild at 19:15:59 (host clock, right after the guest's own
editor) was not a ghost (the guest ate and bit within seconds). Lead from that batch, not verified:
in the three ghost rebuilds the host's mirrors already filled the pool in the same frame
(pool=408/231/313); in the good one the pool was empty.

Reproduced in part (2026-10-31, GOG lanes 5/6, Wine, `coop_cell_own_load.py`: the guest's choices.ini
names premade cell BAF1AD40; "eats" = the guest swims into 3 different plants and its own game runs
AvatarEat, the plant gone on both machines):
* after a RELOAD of the world (the own cell re-applied, BUG-0349) with the rebuild on the first Cell
  frames: 0 of 3 plants eaten in 2 of 2 runs (lab 20261031-130455, 20261031-130741); with the rebuild
  5 s later (`cell.own_rebuild`): 3 of 3 in 2 of 2 (20261031-130258, 20261031-130756). Observed.
* after the FIRST load (the join): 0 of 3 in 2 runs and 3 of 3 in 2 runs, with the delay on as well
  (switching it off happens only after the join, so the first load always ran with it): not fixed,
  cause open. The lead of the second batch (mirrors already in the pool at the rebuild) is not
  checked; the join adds the first state rounds of the host's pool at the same time.
* Earlier runs where the eat probe found no food or retried the same plant were inconclusive (the
  probe now takes a new plant each time).

## Fix
Partly: `cell.own_rebuild` (on; off = 0.7.3.1): `apply_own_cell` waits until the loaded Cell world
has run 5 s and 150 frames before it asks for the rebuild. Fixes the reload case (above). The first
load (join) can still leave the cell unable to eat: open.

## Verification
Reload part: FAIL with `SPORE_FIX_OFF=cell.own_rebuild` lab 20261031-130455 and 20261031-130741
("after the reload the guest's game still eats plants (0 of 3)"); PASS lab 20261031-130258 and
20261031-130756 (that check 3 of 3). First load: reported by the test, not checked (open).

## History
- 2026-10-31: opened from the host's and guest's report zips (marks, cell trace lines).
- 2026-10-31: CORRECTION recorded; not reproduced on lanes 5/6 (coop_cell_own_load.py passes on main); the reload part is BUG-0349.
- 2026-10-31: reload case reproduced and fixed (cell.own_rebuild); first-load case reproduced in 2 of 4 runs, still open.
- 2026-10-31 (fix/w23-reports): first-load case measured, cause still open. `coop_cell_own_load.py`
  with diagnostics, lanes 5/6, 24 runs: the guest did not eat after the first load in 11 of 24
  (lane 6 more often: 3 of 3 in one batch), after the reload never (with cell.own_rebuild). In the
  failing runs, NOT the cause (observed): the cell model data (16 parts, radius 1.259 like the good
  runs), the broadphase (no pending, nothing removed or unsorted, pairs 60-120 of 4096, the avatar's
  object id 0 with its own four endpoints and bounds around its position, the plant's bounds inside
  the avatar's on both axes), the avatar's scale (+0x58 0.55) and rotation matrix (+0x5C, finite),
  flags +0x48 = 6 as in good runs, the level (-1 / plants 0, scale_diff 2 like the partner).
  Seen: `coop.pool` own_pairs (HandleCollisionPair calls with our cell, hook h_Pair) stayed 0 for 30
  frames while the plant overlapped; not checked whether own_pairs is non-zero in good runs, so it
  does not yet tell the broadphase pair from the narrow test in Collisions 0xE7F260 (GetIfNotDeleted
  of both, then the bounding spheres from 0xE6D790). The mirrors-in-pool lead (pool 340-700 at the
  rebuild in good and bad runs alike) does not separate the cases. Tools for the next step:
  `coop.pool sapme [c<key>]`, `own_pairs=` in `coop.pool`, `pairs=` / `pairs_full=` in `coop.pool sap`.
