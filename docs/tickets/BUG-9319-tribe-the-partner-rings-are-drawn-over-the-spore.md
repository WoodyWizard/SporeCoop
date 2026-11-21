---
id: BUG-9319
title: Tribe: the partner rings are drawn over the Spore Guide window
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-21
source: QA 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev (fix/l47-tribe 3e6d1b0a)
fixed_in: unreleased
branch: feat/l47-rings
commits: d19ed484
tests: tools/qa_win (manual, two TITAN QA games, 2026-11-21)
signature:
docs: docs/08-ui.md#ui-spore-guide-window
related: BUG-9302, BUG-9321, BUG-9322
---

## Report
Live QA 2026-11-21 (MSI two games, host MSITribeHost instance 2, guest MSIGuest instance 1, Tribe from Vandh): the
host pressed H (the Spore Guide window opened over the whole screen) and the blue partner rings under the partner's
villagers stayed drawn on top of the guide's text (out/qa-shots/rings_over_guide_host.png). `coop.status` at that
moment: `labels: menu_up=0 window=0 ... partner_rings=3`.
Repro: Tribe with a partner's villagers on screen, press H (or the pause menu's Spore Guide button).

## Root cause
Observed in the code (coop.cpp menu_check_task): the world labels (tags, rings, arrows) are hidden only for the pause
menu (0x0615B50D), the co-op window, a game message box (0x06318E78) and a cinematic that holds the game; the Spore
Guide window is none of these, so `labels` stays true. The guide window's control id is not known yet (the pause
menu's button that opens it is 0x0447060A, docs/08-ui.md).

## Fix
Switch `ui.cover_labels` (coop.cpp, on): menu_check_task also reads whether the guide's frame window 0x53C5B392 is
shown (it exists only while the guide is open; docs/08-ui.md#ui-spore-guide-window); while it is, the overlay draws no
world label (rings, name tags, arrows). A party ring whose centre is on one of the game's HUD windows (the boxes the
labels keep off, pfind::over_hud) is left out too (seen in Civ: a partner's vehicle under the bottom bar drew its ring
over the bar). Off: both drawn (67-dev). Readout: `coop.status` `labels: ... cover_up= cover_frames= rings_on_hud=`.

## Verification
- Live 2026-11-21 (TITAN QA copy, guest screen, 1024 x 768): Civ (host DuoQACivHost / Abber, guest DuoQACivGuest),
  the guest's camera on the host's vehicle, H: FAIL with `coop.fix ui.cover_labels off` (the ring and the vehicle's
  name tag over the guide's text, cover_up=0, partner_rings=1: civ_bug9319_guide_FAIL_switch_off.png); PASS on
  (cover_up=1, partner_rings=0, name_tags=0, nothing over the guide: civ_bug9319_guide_PASS_switch_on.png).
  Tribe (host DuoQAJiffFresh, guest DuoQAStageGuest), the camera on the host's chief, H: FAIL off (the ring over the
  guide: tribe_bug9319_guide_FAIL_switch_off.png), PASS on (tribe_bug9319_guide_PASS_switch_on.png). Screenshots in the
  worktree's out/qa-shots (not in git). The HUD part: `rings_on_hud` counted 393 frames of a vehicle under the bottom
  bar with no ring drawn there.

## History
- 2026-11-21: opened (fix/l47-tribe live QA).
- 2026-11-21: fixed on feat/l47-rings (d19ed484), FAIL / PASS live in Civ and Tribe.
