---
id: BUG-0824
title: Tribe: a member the host buys within 4 s of a guest's purchase is given to the guest (the pairing takes the host's own baby)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-tribe-civ
commits: fea09459
tests: tribe_baby_pair
signature:
docs: docs/re/tribe.md#tribe-baby-buyer
related: BUG-0671
---

## Report
Host and guest both buy a tribe member at about the same time: the host's new member belongs to the guest; with 3-4 players members change hands more often, and a lost relay makes one member for two payments. Found by reading the code.

## Root cause
Observed (lab 20261112-083858, the code before the fix): worse than the audit thought. Every game makes a baby for
every hut purchase: a click on the own hut starts the world effect 0xDB6F2233, the world effects relay plays it in the
partners' games, and its data sends the stage's message 0x63BBC42 5.2 s later in every game that plays it (5.16-5.20 s
after the start, the own and the relayed one alike: lab 20261112-084158 with `coop.effects log on`). So a host's
purchase reached the guest's game as a relayed effect, the guest's BabyBorn (tribe.guest_baby) took it for its own
purchase and sent CMD_BABY, and the host paired its own baby with that ask: **every member the host bought went to
the guest** (part A: one click of the host, the new member owner 1, the guest sent one ask). With both buying within
a second both new members were the guest's. The pairing (baby_on_host / h_BabyBorn) had no way to tell whose
purchase a baby was: position (one hut) and time (4 s) only.

## Fix idea
Do not put babies of the host's own click (BabyBorn not called from the relayed effect) into g_baby_made, or pair only with babies made by a relayed effect.

## How to test
Two instances in Tribe with food; host and guest click their hut's buy within about 1 s: FAIL = the host's new member has the guest as owner (`coop.tribe` member list, babies_paired 1 too early); PASS = each buyer owns its own member.

## Fix
tribe.cpp `tribe.baby_pair` (with effects.cpp's new world-start observer): every game notes each hut effect 0xDB6F2233
that starts in it, its own (this player's id) or a relayed one (the player who made it), in start order, at most 12 s.
A baby of the hut's message (return address SporeCoop::TribeStage_BabyBornMsg_Ret 0xCD8ED3) takes the oldest noted
purchase within 25 m: on the host its own purchase is the host's (no pairing), a guest's relayed one is that guest's
and pairs only with that guest's ask (before or after); on a guest a baby of another player's purchase is dropped
without an ask, its own goes to the host as before (BUG-0671). A baby without a noted purchase goes the old way. The
same effect reported by two creator hooks in one frame is noted once. Switch `coop.fix tribe.baby_pair off` (0.7.12).

## Verification
`coop_tribe_baby_pair.py` (suite `tribe_baby_pair`, tribe fixture, ~1.5 min): members are spawned until the turn
split's next unclaimed member is the guest's; part A: the host's player clicks its hut (real click): one new member,
the host's, and the guest asked for none; part B: host and guest click within 0.5 s: two new members, one each.
* FAIL with `SPORE_FIX_OFF=tribe.baby_pair`: lab 20261112-084503-2 (A: {10: 1}, the guest asked once; B: {12: 1, 13: 1});
  on the code before the fix 20261112-083858 and 20261112-084158 the same.
* PASS: lab 20261112-084704, 20261112-084851-2.

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12 (fix/w48-tribe-civ): reproduced (every host purchase went to the guest), fixed (tribe.baby_pair), FAIL and PASS runs in Verification.
