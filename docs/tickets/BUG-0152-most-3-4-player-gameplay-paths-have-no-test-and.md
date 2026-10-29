---
id: BUG-0152
title: most 3-4 player gameplay paths have no test, and coop_multi runs in no lab suite
status: open
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#131
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch: fix/w4-tests
commits:
tests:
signature:
docs:
related: #131
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/131 (full text, discussion).

The project rule is "every feature gets an automated test", and the mod supports up to 4 players. But the only multi-player tests are `coop_multi.py` and `coop_npc_multi.py`, and neither checks gameplay between guests:
- `coop_multi.py` runs chaos and checks sync, coverage, shared totals and freezes.
- `coop_npc_multi.py` checks NPC positions and ground height.

coop_multi is in no lab suite, so it does not run before a merge. The bugs found in this review with 3 or 4 players (#91, #96, #110, #120) are all in paths with no test.

### Where
- [tools/tests/coop_multi.py](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_multi.py) and [tools/tests/coop_npc_multi.py](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_npc_multi.py)
- The lab suites: [tools/tests/suite.py](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/suite.py)

### How to reproduce
`grep -l start_extra tools/tests/*.py` lists only the two files above. No other test starts a third game.

### Expected
Each multi-player feature has a test with 3 players (host plus two guests) that checks the game's own state on the *other guest's* machine, not mod counters. coop_multi (or a short 3-player smoke) runs in a lab suite.

## Root cause
**What happens**

These 3-4 player paths are never exercised by any test:
- **World share:** the world going to several guests at once (a stage change or evolution with 2+ guests, two joins within seconds); see #91.
- **Evolution vote:** ready counting with 3+ players, a player joining or leaving during the vote.
- **Cell:**
  - knockout and revive between two guests;
  - two players knocked out at once (#110);
  - a guest's KO relayed by the host to the other guests.
- **Creature:**
  - two guests impressing the same NPC;
  - joint hunt by two guests;
  - an NPC choosing between two partner puppets as its target.
- **Tribe:**
  - the re-split of members when a third player joins or leaves (gifts and orders change owner);
  - chiefs per owner with 4 players;
  - gifts from guest to guest.
- **Civ:**
  - city and vehicle ownership with 3-4 players, including the shared capital while cities < players;
  - roles of two guests, trade pairs;
  - superweapon requests from two guests (#96);
  - gifts from guest to guest.
- **Space:**
  - rescue by or of a second guest (#110);
  - spice and tool gifts and sales from guest to guest;
  - rivals relations among 3 players;
  - allies mirrors of two partners;
  - a contract crew without the host;
  - the finale waiting for two partners.
- **Names:** two players with the same name (#120).
- **Leaves:** a guest leaving while another guest is mid world transfer or mid creation transfer.

### Confidence
CONFIRMED by reading the test folder and the suites. Verified by a second reviewer, who noted that coop_multi is in no suite.

## Fix
Not fixed yet. Suggested in the review:

- Add a `multi` suite to `suite.py` with a short `coop_multi.py --players 3 --minutes 2` run, plus one focused 3-player test per stage on top of `twoinst.start` and `coop_multi.start_extra` (lane A plus lane B).
- Start with the paths of #91, #96, #110 and #120, whose tests must FAIL on main and PASS with their fixes.
- Remove the Enter press in `coop_multi.wait_all` (it can hide a stuck "game saved" box, #91), or count every time it presses Enter as a failure.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

This issue is about tests: done when the suite above exists and its tests fail on main where the linked bugs are still open.

### Related
#91, #96, #110, #120, #125 (found in this review); #20 and #51 (tests that pass on bookkeeping or on retry).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #131.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fix/w4-tests: partly done: coop_multi (3 players, creature, 3 min) and coop_npc_multi (3 players) run in the lab now (suite extended, in nightly; out/lab/20261028-030927 passed), a third player runs in the test's own lane (instances 20+N). Left: one focused 3-player test per feature that asserts the game state on the other guest's machine (the paths of #91 BUG-0112, #96 BUG-0117, #110 and #120), 4 players, and the Enter press in coop_multi.wait_all that can hide a stuck save box. Stays open.
- 2026-10-29: dev/campaign: `coop_campaign.py` (suite `campaign`, docs/06-testing.md#testing-campaign) plays a whole game with four players (lane N's two games plus instances 20+N / 30+N), every stage change through the vote, the editors and the world share to three guests at once, 4-player play per stage with the stage bots, rejoins, and checks every 30 s on every machine (stage, together, sync per kind, shared resources, census, faults, freezes, crash reports). It found BUG-0258 (after the live Creature -> Tribe change no guest is with the host). Its rejoins no longer press Enter on the host's save box. Still open here: focused 3-player tests that assert the game state on the other guest's machine per feature.
