---
id: BUG-0846
title: Save watcher takes the captured-city specialty question (same window id 0x06318E78) for the saved box, presses Enter on it and shares an unwritten save
status: fixed
kind: desync
stage: Civ
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9057
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: 0.7.13
branch: fix/w48-saves
commits: cf952855
tests: saved_box_question
signature: window 0x06318E78 without OK button 0xFFFFFFF1; buttons 0x05107B19 / 0x05107B1A
docs: docs/re/creature.md#creature-saved-box-kinds, docs/re/civ.md#civ-shared-message-box, docs/06-testing.md#testing-saves-w48
related: BUG-0276, BUG-0599
---

## Report
In Civ, when the host captured a city that asks for its specialty and a share save runs at that time (a guest
joins, a stage share), the mod (inferred from code):
* takes the specialty question for the game's "saved" box and posts Enter to the game window once a second for up to
  30 s (an automatic answer or a stuck prompt);
* treats the save as complete and sends the slot to the guest before the new save is written (old or partial world;
  the fork saw capture counts 2 on the host, 1 on the guest).
Also an old saved box that is still visible makes a new save count as done.

## Root cause
* Observed (fork, UI tree): the native captured-city specialty prompt uses the same window id 0x06318E78 as the saved
  confirmation, with choice buttons 0x05107B19 (Religious) and 0x05107B1A (Military) and no OK 0xFFFFFFF1.
* Observed (code): world.cpp:356 box_task accepts any visible 0x06318E78, clicks OK if found, else posts VK_RETURN
  every second (world.cpp:388-391) while the 30 s watch runs; it sets g_box_seen_at. The share loop treats
  g_box_seen_at >= g_saved_at as "written" (world.cpp:1205, 1253) and skips BUG-0276's freshness check, which runs only
  when no box was seen (world.cpp:1230).
* Observed: save_game (world.cpp:442) does not refuse while a previous box is still visible, so a stale box seen
  right after the new save counts as its confirmation.
* Observed (our docs, docs/re/creature.md): the pause menu's save questions ("Do you want to save your game?",
  "Are you sure?") use the same container id 0x06318E78 too; inferred: a share save while such a question is up is
  also counted as written, and its OK may be clicked.
* Confidence: high for the misclassification (code + fork's UI ids), medium for how often players hit it.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9057, switch world.saved_box); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* world.cpp box_task: with the switch on, a found 0x06318E78 counts as the saved box only if it contains a visible
  OK 0xFFFFFFF1 and neither 0x05107B19 nor 0x05107B1A; otherwise box = nullptr (no click, no Enter). A visible saved
  box sets a flag g_saved_box_visible and extends the watch (watch_until = now + 30 s) while it stays up.
* Before the click the fork sends a `move x y` to the OK button centre, then `click` (native hover must follow the
  target; captions kept focus otherwise).
* save_game: refuses ("waiting for the previous save confirmation to close") while g_saved_box_visible.
* Their regression drives a full Civ capture progression where the driver answers captions and the specialty
  question only (never the save box); both automatic checkpoints converge. The isolated switch-off run is still
  pending in the fork (their ticket stays open).
* For us also consider: run the BUG-0276 file-freshness check also when a box was seen.

## How to test
Lab test (civ fixture, 2 games): host captures a city with a specialty question (cCity::Capture as in
coop_campaign; leave the question open), then the guest joins (share save). Assert: the specialty question is still
open after 10 s (the mod did not press it), the host's share waits until a real saved box or fresh files, and the
guest's capture count / city owners equal the host's after load. FAIL on main (Enter posted, share sent early), PASS
with the fix (switch off = FAIL).

## Fix
world.cpp box_task: a visible 0x06318E78 counts as the saved box only with a visible OK 0xFFFFFFF1 and none of the
choice buttons 0x05107B17..0x05107B1A (the pause menu's question: Don't Save / Cancel / Save; the Civ specialty
question: 0x05107B19 / 0x05107B1A); a question is left to the player (no click, no Enter, not counted as the save's
confirmation). The watch goes on while a saved box is up; save_game refuses while the previous save's box is up
("waiting for the previous save's ..."), so an old box never confirms a new save. Switch
`coop.fix world.saved_box_only off` = 0.7.12. The BUG-0276 freshness check was left as it is: with only the real box
counted, "box seen" is a written save again.

## Verification
`saved_box_question` (coop_saved_box.py --case question, creature): the pause menu's "Do you want to save your
game?" open, then coop.save.
* FAIL: out/lab/20261112-014220 (`SPORE_FIX_OFF=world.saved_box_only`, lane 2): the question was gone 0.03 s after the
  save (the mod's Enter answered it), the real saved box came 2 s later and stayed up (the game paused) for 12 s.
* PASS: out/lab/20261112-014019, out/lab/20261112-024053 (lane 3): the question still open after 12 s, the saved box confirmed, a
  second save under a held box refused.
Proven with the pause-menu question; the Civ specialty question is covered by the same rule (its ids from the fork,
not seen by us).

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12: fixed on fix/w48-saves (world.saved_box_only), FAIL 20261112-014220, PASS 20261112-014019.
- 2026-11-12: released in 0.7.13.
