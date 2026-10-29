---
id: BUG-0224
title: with guests in the session the host's cinematics are cut off after 8 seconds, not only the capture caption
status: fixed
kind: bug
stage: Civ
reported: 2026-10-28
source: GitHub #158 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-world
commits: ad7d76d 976af47
tests: civ_caption (tools/tests/coop_civ_caption.py, new step 0)
signature:
docs: docs/re/civ.md#civ-world-epoch-cinematics, docs/06-testing.md#testing-w5-world
related:
switch: civ.caption_only
---

## Report
From GitHub issue #158: https://github.com/WoodyWizard/SporeCoop/issues/158 (the full analysis, links and the scenario are there).

**Summary**
The BUG-0187 fix ends the host's cinematic in Civ after 8 s whenever 2 or more players are in the session, so that a capture caption waiting for OK does not stop everybody's game. It ends any cinematic of the game's cinematic manager, not only that caption. Other Civ cinematics, such as the start of a city purchase, the camera cinematic when an NPC city appears, or superweapon cinematics, are cut short on the host, and a cinematic that ends a game step may leave that step half done. Severity: low (wrong behaviour). Introduced by f0c5f0a (BUG-0187) in release 0.7.3.0.

**Where**
- `movie_playing()` is true for every cinematic (cCinematicManager +0x2C): https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/mods/coop/civ.cpp#L1060-L1064
- `movie_ti

## Root cause
Observed (lane 5): `movie_tick` ended any cinematic of the host after 8 s with 2+ players: a camera cinematic started with PlayCinematic ("CVG_PRE_NewCityAppears", which plays for more than 30 s) was closed after 8 s (`closed=1`).

## Fix
* `h_Capture` arms on every capture the host's game runs in Civ; `movie_tick` closes a cinematic only while armed, 8 s after the arm. Disarm when the cinematic ends after it was seen, when none came within 3 s, after the close, in `on_session_end` and `on_world_change`.
* `Simulator::cCinematicManager::PlayCinematic` ported to GOG 2024 (0xAE08B0, disassembly; GOG 2016 has it at 0xAE0880, so nothing for the frozen list).
* Switch: `coop.fix civ.caption_only off` (every cinematic closed, 0.7.3.0); `civ.caption` unchanged.
* Not done: step 0 for the purchase chain and the other capture routes (only the forced military capture was seen), and the purchase case of the test plan.

## Verification
* FAIL: lab run 20261029-003144 (lane 5, `SPORE_FIX_OFF=civ.caption_only`): civ_caption failed (the camera cinematic closed after 8 s).
* PASS: lab runs 20261028-232442, 20261028-235458 (after the merge of main with w5-tools) and 20261029-003648 (the final branch), lane 5.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-world (ad7d76d); test civ_caption: FAIL 20261029-003144 with civ.caption_only off, PASS 20261028-232442, 20261028-235458, 20261029-003648
