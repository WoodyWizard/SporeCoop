---
id: BUG-0878
title: Civ: while the host is away a guest's capture shows "won" on its screen but is never made (suspected)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 
fixed_in: 0.7.13
branch: fix/w48-captures
commits: 70334b34, 5e81941b, 3ec69627
tests: civ_away_capture
signature:
docs: docs/re/civ.md#civ-capture-keeps
related: BUG-0775
---

## Report
While the host is in an editor, a menu or a load (local play), a guest that besieges a city sees its capture progress fill and the win, but the city is not captured; the host's state undoes it when it is back.

## Root cause
* Observed (lab 20261112-003556, civ.away_capture off): with the host in the main menu the guest's own game filled
  the capture meter and its CheckMilitaryCapture called Capture (refused on a guest); after the host's reload the city
  was the NPC's on both games.
* Cause (code): local play lets the guest's game besiege (cosmetic_hit off, its AI runs); h_Capture refused and sent
  nothing.

Earlier reading (2026-11-10):
Inferred, confidence low-medium: in local play `cosmetic_hit` returns false (`mods/coop/civ.cpp:1085`), so the guest's game accumulates capture progress and shows the result, while Capture itself is blocked on a guest (`civ.cpp:1685`).

## Fix idea
In local play either keep the capture meter from finishing (hold it below the threshold with a notice "the capture waits for the host") or queue the capture and send it to the host when it is back.

## How to test
Host `coop.civ away 1` (or a held editor), the guest besieges an NPC city to the end; then the host returns: the city's owner must be the same on both games and match what the guest was shown.

## Fix
civ.cpp h_Capture: on a guest in local play a capture for the player civilization sends CMD_CAPTURE (7: city index,
type) once per city and absence; the host holds it (civ.cmd_hold, BUG-0693) and runs h_Capture for that guest when it is
back. Switch `coop.fix civ.away_capture off` (0.7.12).

## Verification
`coop_civ_away_capture.py` (suite `civ_away_capture`, civ_npc): host `coop.menu`; the guest's game fills the nearest
NPC city's meter (`coop.civ siege <city> 1 150 pc`, the game's AddCaptureDamage); the host loads the same save; both
games then show the city as the player civilization's and the guest's (owner 1).
* FAIL with `SPORE_FIX_OFF=civ.away_capture`: lab 20261112-003556.
* PASS: lab 20261112-003112, 20261112-004830 (lane 4).

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
- 2026-11-12 (fix/w48-captures): fixed, FAIL and PASS runs in Verification.
- 2026-11-12: released in 0.7.13.
