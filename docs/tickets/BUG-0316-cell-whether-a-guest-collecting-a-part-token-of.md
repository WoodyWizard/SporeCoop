---
id: BUG-0316
title: Cell: whether a guest collecting a part token of the host's (a mirror) gets the mating-call button S+0x69 (split from BUG-0178 item 3)
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-10-30
source: BUG-0178
role: guest
versions: 
fixed_in:
branch: fix/w45-cell
commits: 50e20f64
tests: cell_part_token
signature:
docs: docs/re/cell.md#cell-part-token
related: BUG-0178
---

## Report
Split from BUG-0178 item 3: the guest's mating-call button needs a collected part (`S+0x69`,
docs/re/cell.md 12, set by 0xE5D7B0 when the own cell touches a part token). Not checked whether a
guest touching a part token the host's game dropped (a mirror on the guest) runs that path; the
tests set the flag by console (`coop.pool mate button`).
To check: Cell fixture, the host kills a cell that drops a part token (`coop.pool loot` lists the
host's drops), the guest swims onto its mirror (`coop.pool swimto c<key> tp`), `coop.pool mate` on
the guest (mate_button=1?). The fixture's view had no part token (2026-10-30, lane 5,
`coop.pool see 2000` on both games).

## Root cause
Not a bug: a guest's touch of the host's part token works. Observed (Ghidra, 0xE7B630 from 0xE7E7F0): the
collect runs in the touching player's own game on whatever cell it touched (on a guest the host's mirror):
CollectPart (the part's state 2, the mating-call button S+0x69 = 1) and DestroyCell(token, 1); h_Destroy sends
the touched mirror's removal to the host (ACT_GONE) and parts.shared_cell (BUG-0467) gives the host the part.
Observed at runtime (coop_cell_part_token.py, cell fixture, the guest's button hidden first): in every run where
a token could be collected (5: lab 20261106-010242, 20261106-010456 #1 #2, 20261106-011422 #2, 20261106-012210
#3) the guest's game showed the button, had the part (state 0 -> 2), the token was gone in the host's game and
the host's game had the part. In the other tries (about 15) the host's own touch of the same token failed as
well (both games alike; why some tokens are not collected at that moment was not found: not the token's level,
not its graphics object). The fixture has no part token at level 1; they appear after 1-3 level changes.

## Fix
None needed. Test commands added: `coop.pool tokens [to <id> [c<key>]]`, `coop.pool mate nobutton`.

## Verification
no-fail-run: not a bug (the guest's collect works; there is no fix to switch off). cell_part_token PASS
lab 20261106-010242, 20261106-010456 #1 #2, 20261106-011422 #2, 20261106-012210 #3; the failed runs
(20261106-010456 #3 #4, 20261106-011422 #1 #3, 20261106-012210 #1 #2, 20261106-013445 x3) found no token
either game could collect (the host's control touch failed too) or lost the token before the move. The test
is in 'extended' for that setup flakiness.

* Re-check 2026-11-08 (fix/w46-n2-desyncs, N2 of 0.7.11): cell_part_token OK out/lab/20261108-030127 and out/lab/20261108-032648 (SPORE_NET=players, guest
  20 fps): the guest collects the host's token, has the part and the button, the token is gone on the host.

## History
- 2026-10-30: opened.
- 2026-11-06 (fix/w45-cell): checked in the game: the guest gets the part and the button (not a bug), test cell_part_token.
- 2026-11-07 (fix/w46-part-token): cell_part_token failed on rel/0.7.9 (20261106-231404, 20261107-004042): not a regression (no Cell change since its passes; 7 of 12 fix/w45-cell runs failed too). The test's own food steps started the game's Goldie's demise tutorial, which left both avatars' collect locks set (+0x178; lab 20261107-013013, own_locks 0010 before every touch; BUG-0666, docs/re/cell.md#cell-goldie). The test now marks the tutorial played (`coop.pool mate nogoldie`), counts a collect only with a part state change and skips a token culled before the touch: PASS 3/3 out/lab/20261107-021412 (the guest collects part 7, the host has it, the token is gone on the host).
- 2026-11-08 (fix/w46-n2-desyncs, N2): cell_part_token PASS twice more (plain; players' network at 20 fps); not a bug, stays closed.
