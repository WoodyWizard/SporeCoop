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
branch:
commits:
tests:
signature:
docs:
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
Not analysed (found while setting up the BUG-9306 superweapon test). Inferred: `party.reuse_offline_seat` (BUG-9259)
hands the party seat over, but the Civ founder roster (`g_founder_nations`, identity per seat, read by
`party::founder_seat_of`) keeps the old identity, so Civ code sees no seat for the newcomer.
Gameplay question for the owner: should a newcomer taking an offline seat take over that seat's nation (cities,
money) or found a new one?

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
