---
id: BUG-9262
title: Alpha: pausing is off in a session (P and the pause menu do not stop the world)
status: fixed
kind: feature
stage: All
reported: 2026-11-19
source: owner 2026-11-18
role: both
versions: alpha
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: d18b3b8b
tests: tools/qa_win/qa.py (manual: MSI pair, P and Esc on the host, coop.pauses on both)
signature:
docs:
related: BUG-9227, BUG-9260, BUG-1101, BUG-1086
---

## Report
Owner 2026-11-18: "I think pausing should be disabled when we move to the MMO / Alpha build." (ROADMAP decision 6T.)

## Root cause
Not a bug: in a shared world one player's pause stopped everyone (live: the host's pause stands every guest's game,
BUG-9260). An MMO world runs on.

## Fix
coop.cpp (alpha only) no_pause_tick: in a session (joined, or hosting with a player in it) and a stage, every 0.2 s the
player's own pause reasons are resumed: P (0x4BF38A8) always, 0x4BF38A4 while the pause menu is up (the same reason is
the stage card, left alone). A reason the game resumes later would count under 0, so it is paused back to 0 (BUG-1101's
way). The host sends no PF_PAUSED in a session, so no guest stands for it and no "Paused by host" cover shows. The
game's own stops (editors, captions, cinematics, its message boxes, the host's History screen) still stop that
player's own game. Switch: `coop.fix core.no_pause off` (pausing as on live).

## Verification
no-fail-run: two PCs, a feature. Manual 2026-11-19 on the MSI pair (alpha build, host Samuth, a guest joined): the
host's P key and `coop.pauses add 4BF38A8` were released within a second (host `coop.pauses`: none; log "P in a
session: the world runs on"); the Esc menu stayed open with the host's time running ("the pause menu in a session"),
closing it left every count at 0; the guest never paused. With `coop.fix core.no_pause off` the host's P paused it
(04BF38A8=1), the guest followed (04BF38A7=1), and both ran again after the second P (the live behaviour).

## History
- 2026-11-19: opened.
- 2026-11-19: built and checked on feat/lineages-alpha (d18b3b8b).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
