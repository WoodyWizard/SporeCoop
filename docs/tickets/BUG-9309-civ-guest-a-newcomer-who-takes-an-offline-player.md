---
id: BUG-9309
title: Civ (guest): a newcomer who takes an offline player's party seat gets no nation of its own (every founder seat taken)
status: open
kind: bug
stage: Civ
reported: 2026-11-21
source: live QA 2026-11-21 (TITAN, DuoQACivHost Abber + DuoQACivGuest)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-decide
commits: 2d52f56a
tests: tests/civ_seats_tests.cpp (seat limit cases); manual two-game live test (docs/06-testing.md#testing-global-sw-newcomer)
signature:
docs: docs/re/civ.md#civ-global-sw-party
related: BUG-9259, BUG-9306, BUG-9135
---

## Report
Live QA 2026-11-21 (QA lead, TITAN QA copy, build fix/l47-qa on aeaedfc3): host DuoQACivHost loads Abber (Civ, 4 founder
seats, seats 1-3 bound to three earlier partners' identities, none of them online), guest DuoQACivGuest (another
identity) joins. Host log: "party: CivGuest takes seat 3 of Fourth, who is not here (all 4 seats were taken, BUG-9259)";
the guest is sent into its late-join creature editor ("Founding your capital ... Catching up"), then the host logs
"civ late capital: CivGuest (player 1) finds every founder seat taken" / "CivGuest gets no capital of their own: every
founder seat is taken." `coop.status` founders: every partner seat `player=-1`. A superweapon request of that guest
(`coop.civ super 9`) is refused "superweapon: you command no nation of the party".
Reproduce: the setup above (any Civ save whose partner seats are all bound to players who are not online).

## Root cause
Observed: party.reuse_offline_seat (BUG-9259) hands the party seat over, but a Civilization founder seat belongs to its
player's identity (`.duonations` CIV2), so the newcomer had no nation seat (`nation_seat=-1`), and the late capital
(civ_seats::late_status) refused a fifth nation: `late_seat(nations) >= founders::kPlaySeats` -> SeatsFull. The founder
data held founders::kMaxSeats = 8 seats; only the gameplay rules (civ_seats::play_seats, late_status, bind_guest) capped
the nations at kPlaySeats = 4.
Owner decision 7S B (2026-11-21): the newcomer founds a new nation of its own; the absent player keeps theirs.

## Fix
`civ.newcomer_nation` (`coop.fix civ.newcomer_nation off`: at most 4 nations, the old "every founder seat taken").
civ_seats::late_status / late_capital take a seat limit (kMaxSeats with the fix): the newcomer gets the next founder
seat (4..7) through the existing late-capital founding (a native site, the catch-up to the host's capital, the roster
written with its identity, the world shared). civ_seats::play_seats covers every founded seat (up to kMaxSeats), so the
seat's player commands it (host owner tables, the guest's own_seat, superweapons, purchases, captures); the absent
players' seats keep their identities, cities and money (the game runs them while they are away) and are theirs again on
return (the seat is their identity's). All eight taken: no capital, the log says "all 8 founder seats have a nation".
Every per-seat table, mask and side file was checked for seats 4..7 (docs/re/civ.md#civ-global-sw-party).

## Verification
Live, TITAN QA copy (build fix/l47-decide), host DuoQACivHost / Abber (instance 9; `.duo-seats` byte 4 patched 0x10 -> 4 in a backed-up folder, every QA save folder restored as found afterwards), guest DuoQACivGuest (instance 10), 2026-11-21 09:33-09:47. Logs kept in the run's scratch; screenshots in out/qa-shots/ of the worktree (not in git).
- FAIL (`coop.fix civ.newcomer_nation off` on the host before the load): "party: CivGuest takes seat 3 of Fourth, who is
  not here", then (after the guest chose its own creature) 09:35:24 "civ late capital: CivGuest (player 1) finds every
  founder seat taken (the first 4; civ.newcomer_nation is off, BUG-9309)"; `.duonations` 4 seats.
  out/qa-shots/bug9309_fail_switch_off_guest.png.
- PASS (switched on in the same session): 09:35:52 "founding requested for seat 4 (CivGuest)", 09:35:57 "seat 4
  (CivGuest) capital 000021D0 ... roster written", "caught up with the host's capital: 4 of 4 buildings, 2 of 2
  turrets"; `.duonations` CIV2 5 seats, seat 4 = the guest's identity, seats 0..3 byte for byte as before; guest
  `coop.status` "founders: ... seats=5 local_seat=4". Seats 1..3 kept their cities and money.
  out/qa-shots/bug9309_pass_guest_after_founding.png.
- PASS after save and reload: host `coop.save`, both `coop.leave`, host `coop.load Game0 Abber`, host and join again:
  host `coop.civ late` "player 1 party_seat=3 ... nation_seat=4: has a nation", host owner table seat 4 = player 1, guest
  local_seat=4. The guest then fired superweapons from its own seat-4 nation (BUG-9312 / BUG-9313 runs: "CivGuest's
  superweapon 3 nation=2B1648C8 ...: fires", that nation's money -2000). No crash report on either game.
- Unit: tests/civ_seats_tests.cpp (a newcomer founds seat 4; without the limit SeatsFull; eight nations: SeatsFull;
  seats 4..7 recordable; own_seat finds seat 4; the returning absent player keeps seat 3): 0 failures.
- Not run: a returning absent player live (covered by identity: their seat is theirs), all eight seats taken live.

## History
- 2026-11-21: opened.
- 2026-11-21: owner decision 7S B (a newcomer founds a nation of its own); fixed on fix/l47-decide (civ.newcomer_nation), live FAIL / PASS on TITAN.
