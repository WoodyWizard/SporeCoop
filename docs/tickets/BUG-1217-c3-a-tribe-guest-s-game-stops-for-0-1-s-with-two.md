---
id: BUG-1217
title: C3: a Tribe guest's game stops for 0.1 s with two cards ("game stands" / "goes on") on every join and host save: the host's own save was told as a caption stand
status: open
kind: bug
stage: Tribe
reported: 2026-11-16
source: plan
role: 
versions: unreleased
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Key frame review of the 0.7.15 final check (lab 20261116-025154, rel/0.7.15 11114769): a guest in Tribe gets two cards on
every (re)join, "Alice's game stands: yours waits until it goes on" and 0.1 s later "Alice (host) goes on: so does your
game", and its game time stops for that 0.1 s. 836 such lines in 150 guest logs of the run, all `why 0060 mode
01654C02` (Tribe), 573 of them in the first 6 s of a session (tribe_outfit.guest.game.log 07:09:02.287 / .388; the key
frames S/112 tribe_outfit, S/099 tribe_guest_raid_goal). A regression of C3 (BUG-1142), new in 0.7.15. The host's
autosave (E2, BUG-0864) does the same: every timer or milestone save of a Tribe host (campaign_tc2 03:06:18).

## Root cause
Observed (host logs): the share's save for a joining guest (and every autosave) stands the host's game for 1-2 s (the
write, then its "game saved" box; `perf: hitch ... (Tribe, saving)`). busy_model::classify saw a time stand without an
explicit pause reason and called it the game's own stand (kWhyCaption | kWhyStill = why 0060); the Announcer tells a
caption stand after it stood 1 s, and the save's frames are so slow that the first frame after the write is already
past that second: `host busy: none (why 0060) told the guests` comes at the END of the save, and 0.1 s later (the box
confirmed) `why 0000`. Tribe's row waits on a caption (BUG-1086's behaviour), so a Tribe guest paused its time for those
0.1 s with both cards. The other stages do not wait on a caption (the message was sent there too, without an effect).
Not every 0060 is a save: a Tribe village's cinematic (BUG-0531) is a real caption stand of 7-9 s, and Tribe guests
wait for it as designed; those are untouched.

## Fix
Owner decision 1 is about the host's explicit pause only: a save is no pause to announce. host_busy.cpp fills
`Reading.saving` (world::save_busy(): a share's save until its files are written, or world::saved_box_up(): the box of
the mod's last save up); busy_model::classify then keeps only the explicit pause bits (the pause menu, P, the
Sporepedia: the pause menu's own Save is still the host's pause) and drops the caption, modal and still bits: the
state stays `none (why 0000)`, nothing is told, no guest waits. The 0.1 s the time still stands after the box closed is
shorter than the Announcer's 1 s hold, so it is not told either. Switch `core.host_busy_save` (off: the save's stand is
classified as before). Unit test tests/core_tests.cpp busy_classify (a save's stand with and without the modal reason,
the pause menu during a save).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
