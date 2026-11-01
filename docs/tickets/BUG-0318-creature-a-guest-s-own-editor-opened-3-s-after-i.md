---
id: BUG-0318
title: Creature: a guest's own editor opened 3 s after it loaded the host's world stays black and never closes
status: fixed
kind: bug
stage: Creature
reported: 2026-10-30
source: fix/w13-creature lab 20261030-141448 legless_guest_cancel
role: guest
versions: 
fixed_in: 0.7.3.2
branch: fix/w14-creature
commits: 74e58b2, 103a369
tests: tools/tests/coop_editor_after_swap.py (suite editor_after_swap)
signature:
docs: docs/re/creations.md#creations-editor-after-swap
related: BUG-0317, BUG-0278
---

## Report
`coop_legless_guest.py --guest cancel` (lab 20261030-141448, lane 4, and one direct run before it):
after the guest's rejoin (Creature -> galaxy menu -> load -> Creature at 15:17:41) the test calls
`coop.vote editor` on the guest 2 s later; its own editor opened at 15:17:44 (stage line "Creature
-> Editor") and stayed in mode Editor for the rest of the test (5 min): the screen black (no editor
UI, screenshot legless_guest_next_editor_cancel.png), "nothing controlled", 13-17 fps, hitches of
200-360 ms. The test's checks "the guest saves its edit" and those after it fail.
Reproduce: `tools/lab.py run legless_guest_cancel --lane N`: 2 of 3 runs on 2026-10-30 (lane 4; the third,
out/lab/20261030-142629, opened and saved the next editor normally).
Not known: whether it needs the rejoin right before (a world loaded 3 s earlier) or also happens in
`--guest new`; whether a player can leave such an editor (Esc, the red cross) at all.

## Root cause
Observed (lane 4, 2026-10-30, guest at 6.7 fps with `frame.limit 150`):
* After a rejoin the guest loads the host's world and the mod gives its avatar its own creature
  (`SetSpecies`, log "creations: own avatar is now ..."). An editor request 0.3 s after that line:
  the editor's windows exist (69 lines in `coop.ui tree 8`, as in a working editor) but the screen
  stays black (mean brightness 0.2) for minutes, also after the frame rate went back to 23 fps, and
  the red cross / Quit / Esc do not leave it. 2 of 2 such runs, and the test's case (2 of 3 lab runs
  of legless_guest_cancel at 2-17 fps).
* Not without the swap: no rejoin at 6.7 fps (the editor drew), the swap done at normal fps 8 s
  before slowing down (drew), the rejoin at 6.7 fps but the editor entered at normal fps (drew), the
  request 60 s after the swap at 6.7 fps (drew). At 15 fps 3 s after the swap: drew.
* The same without a swap: the guest playing the world's own creature, rejoin, editor 1.2-1.7 s
  after the load (lab 20261030-162204 legless_guest_cancel at 23 fps; 20261030-170223 at 6.7 fps).
* Inferred: the editor takes the avatar's creature while its body (a new avatar of a loaded world,
  or SetSpecies) is still being built over the next frames; at 60 fps that is done long before
  anyone clicks.

## Fix
`creations.editor_after_swap`: editor requests (the game's own entry through `h_Submit`, the vote's own
editor in `own_editor_tick`) wait until the last avatar swap and the current avatar (a new one after
every load) are 5 s **and** 300 frames old; the
held request goes out from `submit_tick` (at most 90 s). The player is told "your editor opens in a
moment". At 60 fps the wait is 5 s after a swap; at 6.7 fps about 45 s.
`coop.fix creations.editor_after_swap off`: no wait (0.7.3.1).

## Verification
`coop_editor_after_swap.py` (suite editor_after_swap): guest at 6.7 fps, rejoin, editor request 3 s
later; the editor must draw (screenshot not black) and one red-cross click must bring it back.
* FAIL with `SPORE_FIX_OFF=creations.editor_after_swap`: out/lab/20261030-161437 (brightness 0.2 for
  60 s, the red cross did nothing).
* PASS: out/lab/20261030-161801 (2 runs: "the editor waits until the avatar's new body is built",
  opened 45-47 s later, brightness 88, left with one click).
`--no-swap` (suite editor_after_load, the world's own creature, no swap after the load):
* FAIL with the switch off: out/lab/20261030-170223. PASS: out/lab/20261030-170230, 20261030-170657 (with
  editor_after_swap again); legless_guest_cancel OK in 20261030-170648 (it failed on the first version of
  the fix, 20261030-162204: no swap after its load).

## History
- 2026-10-30: opened.
- 2026-10-30: opened on fix/w13-creature (not analysed, outside the batch time).
- 2026-10-30: fix/w14-creature: reproduced at 6.7 fps after a rejoin (an editor during the avatar swap), fixed by creations.editor_after_swap.
- 2026-11-01: released in 0.7.3.2.
