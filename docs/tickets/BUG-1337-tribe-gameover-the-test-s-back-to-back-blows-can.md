---
id: BUG-1337
title: tribe_gameover: the test's back-to-back blows can fell two members in one behaviour tick, then the game shows no game over at all
status: fixed
kind: test
stage: Tribe
reported: 2026-11-20
source: lab
role: host
versions: 0.7.14.1,0.7.16
fixed_in: 0.7.16
branch: fix/w59-tribe
commits: f6daf30c c0792a81
tests: tribe_gameover, tribe_gameover_reload, tribe_gameover_reload2, tribe_gameover_slow (coop_tribe_gameover.py); twin tribe_gameover_reload2_sameframe
signature: tribe_gameover*: ui "Alas": no window with caption 'Alas', then FAIL after the bounce back the player tribe has members again (player tribe {<chief>: 0.0})
docs: docs/re/tribe.md#tribe-gameover-list
related: BUG-0172, BUG-1083
---

## Report
Lab 20261119-214823 (0.7.16 final check), tribe_gameover_reload2 on lane 3: every member of the player tribe out
(`player tribe now {1: 0.0}`), but the game's "Alas! Your population ..." caption never came (`no window with caption
'Alas'`), the host's input lock stayed 0 for 80 s, so no bounce back and `FAIL after the bounce back the player tribe
has members again`. The triage suspected 0.7.16 (BUG-1299 / BUG-1313 / BUG-1319 or the chief guards).

## Root cause
* ✔ Not 0.7.16: the same failure, word for word (`{1: 0.0}`, no caption, `chief_gone=1`, `spawns_blocked`), on
  v0.7.14.1 (tag/v0.7.14.1-w51tribe, lab 20261118-033459, tribe_gameover #3). 2 of the 60 runs of the four
  tribe_gameover tests in all worktrees' lab logs had no caption; every other run of reload2 passed (8 of 9, 3 of them
  cold starts like this one). No 0.7.16 line (species, births, kid rule) in the host's log of the run.
* ✔ (BUG-1083, docs/re/tribe.md#tribe-gameover-list) The game posts its game over at a member's fall only when fewer
  than 2 members are left in its up list; a fallen member leaves the list at its next behaviour tick, so two members
  falling in one tick both still count each other and no game over comes at all. The test's step 1 gave every member
  with health a blow of 300, back to back, every second: now and then the last two fell in one tick (the chief, at
  0.3 after the partner's rescue or the game's floor, and the last other member). BUG-1083 fixed this in
  coop_tribe_gameover_partner.py only.
* A bug of the test, not of the mod or the game's sync: the host's game alone decides (the guest waits under its
  caption, #tribe-gameover-wait).

## Fix
Test only (coop_tribe_gameover.py, every tribe_gameover* entry): step 1 gives one blow a second to one member at a
time, the tribe's leader last (a leader with others up stops at 0.3), so no two members fall in one tick; the checks are
unchanged. `--same-frame` (the twin `tribe_gameover_reload2_sameframe`): the last two killing blows in one call (one
frame), the old race made sure: must FAIL (no caption).

## Verification
Lab: not run then (the 0.7.16 final check holds the lab): tribe_gameover_reload2 (and tribe_gameover,
tribe_gameover_reload) must PASS, tribe_gameover_reload2_sameframe must FAIL with no caption. Natural FAIL runs of the
old blows: 20261118-033459 (tribe_gameover #3), 20261119-214823 (tribe_gameover_reload2).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tribe_gameover, tribe_gameover_reload, tribe_gameover_reload2 and tribe_gameover_slow PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643; the twin tribe_gameover_reload2_sameframe FAIL in 20261120-130608 and 20261122-191140 (no caption). Closed.

## History
- 2026-11-20: opened (fix/w59-tribe, triage package F of the 0.7.16 final check): a flake of the test's blows, not a
  0.7.16 regression.
- 2026-11-20: the test's blows one at a time on fix/w59-tribe; lab runs pending.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
