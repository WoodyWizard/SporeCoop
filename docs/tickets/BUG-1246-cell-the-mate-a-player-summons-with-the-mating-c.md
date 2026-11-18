---
id: BUG-1246
title: Cell: the mate a player summons with the mating call is not in the other player's game
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in: unreleased
branch: fix/w53-cellvote
commits: 9b1dca20 37042f38 39048c26
tests: evo_screens_cell_mate
signature: guest `coop.pool mate` alive=1 while the host's `coop.pool near <its pos> 5` lists no cell of the mate's model
docs: docs/re/cell.md#cell-partner-mate
related: BUG-1245, BUG-1186
switch: cell.partner_mate
---

## Report
A players' video: the guest calls a mate ("do you see the male? on the left"), the host swims right next to it and sees nobody. Suggested cause: the guest's summoned mate is made locally (mate_idx G+0x51D4, cellsync.cpp) and the cell pool only goes host -> guests (MSG_CELL_STATE).

## Root cause
Observed (lab 20261118-123948, `cell.partner_mate` off): the suggested cause holds. The mating call 0xE760F0 makes the
mate in the caller's own pool (G+0x51D4); the Cell pool goes only host -> guests (MSG_CELL_STATE), so a guest's mate
is in the guest's game alone: with the host's cell moved 3 m from the guest's, the host's game had no cell of the
mate's model near the guest's mate in 6 of 6 looks. (The host's own mate is a host cell and reaches the guests as a
mirror.)

## Fix
`cell.partner_mate` (cellsync_mate.cpp, new message MSG_CELL_MATE 72, cell family minor 1.1): a guest sends its mate's
pose 5 times a second (and "gone" when it is); the host keeps a passive copy in its pool (AI lock, keep-alive,
invulnerable, never the host's own mate G+0x51D4) at that pose, removes it on "gone", after 2 s without a pose, when
the player leaves or the stage ends, and sends it to the other guests as one of its cells but never back to its owner.
Mate copies stay out of the sync-check and the census. The meeting and the editor stay the owner's game's. `coop.pool
mate` prints the copies; `coop.pool near x y r` lists a game's cells at a place. Off: the mate is the caller's only.

## Verification
Test `evo_screens_cell_mate` (cell fixture; real clicks on the guest's mating-call button; `coop.pool tp 3 3` puts the
host's cell next to the guest's; then the guest swims to its mate): the host's game has a drawn cell of the guest's
mate's model within 5 m of where the guest's game has the mate, the guest no second copy; the copy goes when the guest
meets its mate.
* FAIL: out/lab/20261118-123948 (`SPORE_FIX_OFF=cell.partner_mate`, lane 2): 0 of 6 looks.
* PASS: out/lab/20261118-124008 (lane 3: 6 of 6, d 0.0-0.1 m, gfx 1; the copy removed 2 s after the guest's editor
  opened; the lab's sync-check gate then counted the copy as "missing on the guest", fixed in 37042f38) and
  out/lab/20261118-124955 (lane 3, all checks and gates OK). Frame 01_host_sees_guest_mate_host: the guest's mate
  (its hearts) next to Bob in the host's game.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellvote): reproduced, fixed with cell.partner_mate (a passive copy of a guest's mate in the host's pool).
