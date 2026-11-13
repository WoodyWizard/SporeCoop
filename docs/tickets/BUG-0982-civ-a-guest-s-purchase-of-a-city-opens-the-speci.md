---
id: BUG-0982
title: Civ: a guest's purchase of a city opens the specialty question (and the pause) on the host, the guest is never asked
status: fixed
kind: bug
stage: civ
reported: 2026-11-12
source: lab-final-0.7.13
role: host
versions: 0.7.13
fixed_in: unreleased
branch: fix/w49-civ
commits: 409f7c75
tests: civ_buy_city
signature:
docs: docs/known-issues.md#issue-civ-guest-buy-specialty, docs/re/civ.md#civ-specialty-buyer
related: BUG-0877, BUG-0311, BUG-0981
---

## Report
Key frame 026 (civ_buy_city) of the rel/0.7.13 final check, lab 20261112-175447: the guest bought an NPC city; the
host's screen shows the game's question "You've bought out this military city ... Choose the city's specialty"
(Military / Economic) and the host's game stands paused (`busy host: paused`, the frame waited 20 s); the guest's
screen shows no question. Player-visible: the host is stopped by a box about the guest's purchase, and the guest
cannot choose its city's specialty.

## Root cause
Inferred: civ.buy_city (BUG-0877) makes the guest's purchase in the host's game (`buys_done=2` on the host), and the
game's capture path asks the player civ's last captured city for its specialty when its capture type differs from
its specialty (0xCF7A30 -> box 0xBD9330 + Pause, docs/re/civ.md); the host's game is where that runs. Not traced.
In civ_owners_reload (key frames 029 / 030) a box the test left up on the host survived the quit to the galaxy menu
and the load of the same save.

## Fix
Observed (fix/w49-civ, Ghidra + lane 4): GameCiv_CaptureNeedsSpecialty 0xCF7A30 (cdecl bool()) runs in the host's game
at the end of the purchase's capture cinematic for player civ +0x44C, the guest's bought city; nothing sent it to the
guest. Fixed: `civ.specialty_buyer` (host hook of 0xCF7A30): a city whose owner is a guest gets its question sent to
that guest (MSG_CIV_CMD kind 8) and the host's game goes on (no box, no pause); the guest's game shows the game's own
box (cCity_ShowSpecialtyBox on its copy, capture type written to +0x29C) and its answer (cCity_MessageListener hook,
message 0x40FAFDF, 0x5107B1A = take) is applied there and sent back; the host's game calls cCity_SetSpecialty.
`coop.fix civ.specialty_buyer off`: the host's game asks (0.7.13).
Before (open, for 0.7.14 or later). Ideas: forward the question to the buyer and answer the host's box with its
choice, or keep the city's own specialty for a guest's purchase. The tests now answer such a box before their key
frame (BUG-0981) and civ_buy_city prints which game showed it.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
Test civ_buy_city now asserts: no question on the host, the question on the guest, the guest's answer (Economic) is
the city's specialty (2) in both games.
FAIL with SPORE_FIX_OFF=civ.specialty_buyer: out/lab/20261113-040400 (host Military | guest None).
PASS: out/lab/20261113-040703 and 20261113-041352 (host None | guest Economic, specialty 2 on both).

## History
- 2026-11-12: opened from the 0.7.13 final check's key frames (fix/w48-fmisc); not fixed.
- 2026-11-13 (fix/w49-civ): fixed for 0.7.14 (package V2): the question goes to the buyer.
