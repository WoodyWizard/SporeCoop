---
id: BUG-9313
title: Civ: a party player's global superweapon (types 2, 8) also hits the other party nations' cities (decision 7P A: spare them)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-21
source: owner decision 7P A, 2026-11-21
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-decide
commits: 2d52f56a
tests: manual two-game live test (docs/06-testing.md#testing-global-sw-newcomer); no lab test yet
signature:
docs: docs/re/civ.md#civ-global-sw-party
related: BUG-9306, BUG-9310, BUG-9312, BUG-9155
---

## Report
Owner decision 7P A (2026-11-21): a global superweapon spares the party's cities. Before, the global types fired by any
party player (the host's own, or a partner's from its own nation since BUG-9306) hit every city that was not the firing
nation's, the other founder nations' cities included (docs/re/civ.md#civ-guest-superweapons, "?" line).

## Root cause
Static (Ghidra, GOG 2024): the launch Simulator::cCity::LaunchSuperweapon 0xBE5D30 runs 0xBE4300 (type 2) or 0xBE4550
(type 8), which fire the type's tool at every city whose political id differs from the launching city's and set that
city's instant-capture flag; 0xBE4300 also applies the "used a superweapon" relationship event 0x5776D99 between the
firing nation and every other nation. The game has no ally exception (an ally is hit too). Type 5 (0xBDC4A0) hits no
city at its launch. docs/re/civ.md#civ-global-sw-party.

## Fix
`civ.global_sw_spares_party` (`coop.fix civ.global_sw_spares_party off`: the game's rule). Host, types 2 / 8 launched
by a party founder nation (whoever fires): for the launch call every city of another party founder nation carries the
firing nation's political id (+0x30, through the swap ledger, put back after the call), so the game's own exclusion
("the launching city's own nation") skips it, and the relationship event 0x5776D99 between two party nations is not
applied inside the call (ApplyRelationship hook, active only then). NPC nations are hit as before. Not changed: the cost
of a global type (12000 x the cities not the nation's, party cities included) and type 5.

## Verification
Live, TITAN QA copy (build fix/l47-decide), host DuoQACivHost / Abber (instance 9; `.duo-seats` byte 4 patched 0x10 -> 4 in a backed-up folder, every QA save folder restored as found afterwards), guest DuoQACivGuest (instance 10), 2026-11-21 09:33-09:47. Logs kept in the run's scratch; screenshots in out/qa-shots/ of the worktree (not in git).
Abber had no NPC nation at first: `coop.civ test_outsider <faction>` (BUG-9155 test tool) counted one founder nation as
outside the party; a native NPC nation (21CE) appeared during the run.
- PASS guest-fired (seat 4, the BUG-9309 newcomer's nation 21D0): outsider 21CD, `coop.civ swgrant 4 2 100000`, guest
  `coop.civ super 2`: host "global superweapon 2 of nation 000021D0: 3 party cities spared, 3 party relationship
  events spared"; 15 s later on both games the outsider's city (167 -479 -3) pol=000021D0, the host's capital, 21CC
  and 21CB unchanged (hp, ruins). out/qa-shots/bug9313_pass_guest_sw2_montage_host_left_guest_right.png.
- PASS host-fired: outsider 21CC, `coop.civ swgrant 0 8 100000`, host `coop.civ super 8`: "3 party cities spared";
  on both games 21CC's city and the NPC nation 21CE's city now the host's, 21CB and both 21D0 cities unchanged.
  out/qa-shots/bug9313_pass_host_sw8_montage_host_left_guest_right.png.
- FAIL (`coop.fix civ.global_sw_spares_party off`, outsider none, the host's +0x4CC cleared with mem.write): host
  `coop.civ super 2`: 3 aims, on both games the party cities of 21CB (77 -301 398, an offline founder's) and 21D0 (the
  newcomer's, 167 -479 -3) taken by the host. out/qa-shots/bug9313_fail_off_host_sw2_*.png.
- No crash report on either game.

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-decide (civ.global_sw_spares_party), live FAIL / PASS on TITAN.
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
