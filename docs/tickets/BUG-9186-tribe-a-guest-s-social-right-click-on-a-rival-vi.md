---
id: BUG-9186
title: Tribe: a guest's social right-click on a rival village's hut gives no food gift
status: fixed
kind: bug
stage: tribe
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 21bc3c1f
tests: tools/qa_win (manual, TITAN host + the MSI guest, msi_input.ps1, 2026-11-13)
signature:
docs:
related: BUG-9033 BUG-9154
switch: tribe.rival_gift_click
---

## Report
Found in the 3A / 3B checks (2026-11-13, a fresh copy of the owner's Jiff save, the MSI as the guest, real clicks): social stance, the own villager selected and clicked next to a rival village, the game's hover card of the rival ("Cyan Village 3/12") shown, a right press on its hut: no order at all (the guest's "tribe last order" unchanged, nothing on the host). A right press on the ground still walked.

## Root cause
Observed: a guest's game offers no Gift on a village whose copy runs no game AI there (docs/re/tribe.md#tribe-food-share: the same for a party village, BUG-9154). The mod's right-press handler (party_gift_input) sent the gift order for party villages only.

## Fix
tribe.rival_gift_click (on): the same right press within 9 m of a rival village's hut (not defeated, not the own village) sends CMD_ACTION 9 on that village; the host checks it as a native gift (BUG-9033: a live recipient with a hut and food mat, the game's price, the sender's food) and runs the game's gift. Off: party villages only.

## Verification
no-fail-run: the PASS run was not reached live. FAIL: the press above (before the switch existed: the same as off). The PASS tries failed on the setup, not the order: the camera could not see a natural rival's hut behind a hill, and the test rival (made by `coop.tribe npc_tribe`) was at war after the raid test, so the MSI's villager kept running there and going down, and a party wipe reloaded the world. Built and on all three games, 0 faults. Code path: party_gift_input (live with BUG-9154) with the village filter widened; the host side is BUG-9033's.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on feat/lineages-47 (decision 3B, Claude's pick).
- 2026-11-14 (3Q A): still not seen live: the setup (the guest's villager about 220 m away from the nearest rival hut
  on the DuoQAJiffStand copy, the camera on that hut) was left to the owner's own test of the installed build.
