---
id: BUG-0152
title: most 3-4 player gameplay paths have no test, and coop_multi runs in no lab suite
status: fixed
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#131
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.2
branch: fix/w4-tests, dev/campaign, fix/w9-tests
commits: 2db65c9 da65c35 4994b7a ab94194 96d1a2f 081b936
tests: multi3, npc_multi3, campaign, party_cell3, party_creature4, party_tribe4, party_civ3, party_space4, party_creature3_seq, party_cell_ko, party_creature_npc, party_tribe_split, party_civ_own, party_civ_trade, party_space_names
signature:
docs: docs/06-testing.md#testing-party
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
Tests (fix/w9-tests), built on `tools/tests/party.py` (docs/06-testing.md#testing-party), suite `multi`
(also extended / nightly), each 1-4 min; every path of the list above, checked in the game of the
other guest:
- World share to several guests at once, two joins within seconds: `party_<stage><n>` (all stages).
- Evolution vote with 3-4 players: ready counting on every window, a leaver not ready lets it pass,
  a player joining during the vote (counted, its window shows it): `party_*` (coop_party.py).
- Cell: KO relayed to the other guest, revive between two guests, two knocked out at once (#110):
  `party_cell_ko`.
- Creature: two guests impress one NPC, a joint hunt by two guests, an NPC noticing two partner
  puppets (its behaviour-tree stimuli): `party_creature_npc`.
- Tribe: the split with 4 players and after a leave / rejoin, chiefs per owner, a gift from guest to
  guest: `party_tribe_split`.
- Civ: ownership with 4 players and the shared capital, a purchase per guest, a gift from guest to
  guest, superweapon requests from two guests (#96), a leaver's vehicle: `party_civ_own`; three
  cities for three players, roles of two guests, trade pairs: `party_civ_trade`.
- Space: same names (#120), tool gifts and a spice sale from guest to guest, a contract crew without
  the host, allies mirrors of two partners, rivals relations among 3, rescue of / by a second guest
  (#110): `party_space_names`. The finale waiting for several partners: `campaign` (4 players).
- Leaves during another guest's world transfer and creation transfer: `party_creature_npc`.
Not as asked: "an NPC choosing between two partner puppets as its target" is checked as the NPC
noticing both puppets (the stimulus its target choice starts from), not as the chosen target; the
coop_multi Enter press is gone from the party helpers (Party never presses Enter), coop_multi.wait_all
keeps it for the chaos test.

## Verification
PASS: the `multi` suite, lab 20261030-014410 (12 of 13; party_tribe_split failed once: Carol's select of the gifted member right after the owner changed was not kept; it now selects up to 3 times and passed 3 of 3 at the first select in lab 20261030-015630), and the single runs named in docs/06-testing.md#testing-party. FAIL with a fix switched off, where the paths
have one: `party_cell_ko` with `pool.rescue` (20261030-010849), `party_creature_npc` with
`npc.play_death` (20261030-011708) and `npc.partners` (20261030-014139), `party_civ_own` with
`civ.sw_vote,civ.orphans` (20261030-011312), `party_space_names` with `core.unique_names,space.helper`
(20261030-011923). `party_creature4` still passes with `world.share_one_save` off (20261030-004236):
BUG-0112's one-save path is world_queue's. No switch exists for the tribe split, civ trade or the
vote paths.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #131.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fix/w4-tests: partly done: coop_multi (3 players, creature, 3 min) and coop_npc_multi (3 players) run in the lab now (suite extended, in nightly; out/lab/20261028-030927 passed), a third player runs in the test's own lane (instances 20+N). Left: one focused 3-player test per feature that asserts the game state on the other guest's machine (the paths of #91 BUG-0112, #96 BUG-0117, #110 and #120), 4 players, and the Enter press in coop_multi.wait_all that can hide a stuck save box. Stays open.
- 2026-10-29: dev/campaign: `coop_campaign.py` (suite `campaign`, docs/06-testing.md#testing-campaign) plays a whole game with four players (lane N's two games plus instances 20+N / 30+N), every stage change through the vote, the editors and the world share to three guests at once, 4-player play per stage with the stage bots, rejoins, and checks every 30 s on every machine (stage, together, sync per kind, shared resources, census, faults, freezes, crash reports). It found BUG-0258 (after the live Creature -> Tribe change no guest is with the host). Its rejoins no longer press Enter on the host's save box. Still open here: focused 3-player tests that assert the game state on the other guest's machine per feature.
- 2026-10-30: fix/w9-tests: `tools/tests/party.py` (Party: host + 1-3 guests in one lane, checks of each machine's own game: together, every guest sees every other player where it is, shared totals; join / leave / rejoin, a real "I'm ready" click, host away / back) and `coop_party.py` per stage in the new `multi` suite (also extended / nightly): two guests join at once, guest-to-guest views, a vote with 3-4 players (every window shows the same ready k/N, a leaver not ready lets it pass once), a rejoin of the 3rd / 4th player, the host in the menu with several guests. Lab 20261030-003549: 6 of 6 in 6.4 min on two lanes (2 min each). Negative: party_creature4 still passes with `SPORE_FIX_OFF=world.share_one_save` (lab 20261030-004236): it does not cover BUG-0112's one-save path (world_queue does). Still open: the per-stage gameplay between guests (KO / revive between guests, two guests and one NPC, tribe re-split and chiefs, civ ownership and superweapon requests from two guests, space rescue / gifts / rivals between guests, same names, a leave during another guest's transfer).
- 2026-10-30: fix/w9-tests: one party test per stage for the gameplay between guests (cell KO, creature NPC, tribe split, civ ownership and trade, space names / gifts / sale / crew / mirrors / rivals / rescue), join during a vote, leaves during transfers; FAIL with the fix switches, PASS in the multi suite. Fixed.
- 2026-11-01: released in 0.7.3.2.
