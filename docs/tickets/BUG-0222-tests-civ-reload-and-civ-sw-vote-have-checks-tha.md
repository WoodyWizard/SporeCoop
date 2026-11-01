---
id: BUG-0222
title: Tests: civ_reload and civ_sw_vote have checks that pass without their fix or only read the mod's own variables
status: fixed
kind: test
stage: All
reported: 2026-10-28
source: GitHub #156 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.2
branch: fix/w9-tests
commits: ff34761 2db65c9 da65c35
tests: civ_reload, civ_sw_vote
signature:
docs: docs/re/civ.md#civ-superweapon-2024, docs/06-testing.md#testing-civ-checks
related:
---

## Report
From GitHub issue #156: https://github.com/WoodyWizard/SporeCoop/issues/156 (the full analysis, links and the scenario are there).

**Summary**
Two lab tests added for the Civ fixes of release 0.7.3.0 do not prove what their docstrings say.
- In `coop_civ_reload.py` (BUG-0040, BUG-0059), two of the checks cannot fail on the code without the fix, and the speed the boost writes into the game is never read.
- `coop_civ_sw_vote.py` (BUG-0117) checks a mod variable that is set just before the game call, not what the game fired. The host's own superweapon path, which the fix also changed, has no test.

Severity: low (tests only). A later regression in these paths would pass the lab (CLAUDE.md 3: "a test you never saw fail proves nothing", "check what the other player sees").

**Where**
- `coop_civ_reload.py`, the checks after the load and after the leave: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260fa

## Root cause
Observed (reading + runs): the checks named in the report read mod variables (`g_sw_fired`, the
`tables` counts) or could only fail on an address reuse that never happened in the lab. Step 0 on
GOG 2024: the boost speed +0x218 stays at 1.5x for 30 s without the session-end restore (the game's
RecalcStats does not bring it back), so it is a valid FAIL criterion; `unlocked[12]` +0x484 and the
money +0x98 are right on GOG 2024 (docs/re/civ.md#civ-superweapon-2024).

## Fix
Tests only, plus test commands in civ.cpp `debug()`:
- civ_sw_vote: the weapons are made usable the game's way (`unlocked[type]` via mem.write, shared
  money 20000), the guest case asks for type 3 (2000) then type 1 (16000) during the vote, the new
  host case asks for 9 then 1; the assertion is the game's own money (+0x98) on the host and the
  guest: spent 2000, not 16000. `coop.civ super <type> [x y z]` always passes a target (the game
  reads it without a null check); `coop.civ sw` prints the player civ.
- civ_reload: the boost is a precondition (>= 1 vehicle, its +0x218 at 1.5x); after the leave the
  game's speed of each boosted vehicle is back to its own within 2 s (`coop.civ boosts`); BUG-0059's
  pol compare is forced with the new host-only `coop.civ stale_hv <id> <owner>` (an NPC vehicle's
  entry with a player owner and another pol): the guest selects and orders that vehicle, and the
  host's game must not take the order (`coop.civ` prints the order's place `opos`, element +4 of
  v+0xB68).
Not done from the issue's design: the cooldown timers (+0x490) as the primary assertion (money tells
the types apart and needs no layout), a targeted launch (which city launched).

## Verification
FAIL without the fix: civ_sw_vote `SPORE_FIX_OFF=civ.sw_vote` lab 20261030-001854 (guest case spent
16000; host case spent 0: type 1 already on cooldown) and 20261030-001330 (earlier version);
civ_reload `SPORE_FIX_OFF=civ.world` lab 20261030-001854 (the NPC vehicle took the guest's order:
opos = the guest's target), `SPORE_FIX_OFF=civ.boost` lab 20261030-002050 (speed 1.5x 2 s, 10 s and
30 s after the leave).
PASS with it: civ_sw_vote lab 20261030-002053 (2 of 2); civ_reload lab 20261030-001626 and
20261030-002245 (2 of 2).

## History
- 2026-10-28: opened.
- 2026-10-28: fix/w5-world did not reach it (budget went to BUG-0217, BUG-0223, BUG-0224, BUG-0255, BUG-0254). Still open: step 0 (the +0x218 boost speed without boost_tick, the GOG 2024 superweapon offsets +0x484 / +0x490), `coop.civ stale_hv`, the boost precondition and speed read in civ_reload, the cooldown and money checks and the host-request case in civ_sw_vote. Usable now: `cCinematicManager::PlayCinematic` is ported to GOG 2024 (BUG-0224); `coop.civ nulllocal` exists (BUG-0223).
- 2026-10-30: fix/w9-tests: both tests check the game's state (money, speed, the order's place); FAIL with each switch off, PASS with it. Fixed.
- 2026-11-01: released in 0.7.3.2.
