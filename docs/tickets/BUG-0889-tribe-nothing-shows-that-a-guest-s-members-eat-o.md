---
id: BUG-0889
title: Tribe: nothing shows that a guest's members eat on their own; BUG-0573's cause (the guest's hunger AI) cannot hold, a guest's mirrors run no behaviour tree
status: fixed
kind: test
stage: Tribe
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-small
commits: aff0790d
tests: tribe_guest_eat
signature:
docs: docs/06-testing.md#testing-p1-g1, docs/re/tribe.md#tribe-guest-eat
related: BUG-0573, BUG-0572, BUG-0569, BUG-0667
---

## Report
The guest's report behind BUG-0573: "tribe members do not eat on their own, they have to be ordered to eat". BUG-0573 was closed as a consequence of BUG-0572 (food 0 on the guest) with no-fail-run; nobody checked whether a guest's members eat on their own.

Severity: medium. Confidence: medium.

## Root cause
* Observed (code): mods/coop/tribe.cpp:472-485 (h_BTTick) skips the behaviour tree of every citizen mirror on a guest in Tribe unless local_play (host away) or a test flag is set, so the guest's game runs no hunger or eat node for its members; tribe.cpp:213-219 says the same.
* Observed (ticket): BUG-0573's Fix rests on "the guest's tribe has the host's food bundles again, so its members' hunger AI finds food"; by the code above that AI does not run on a guest.
* Inferred (medium): eating on its own happens only in the host's game, on the host's copy of the guest-owned member, and the guest sees it only through the animation relay of BUG-0569 (tribe.mirror_anims, 0.7.8; the report was on 0.7.6). Either the bug was "the guest does not see its members eat" (fixed by BUG-0569, not BUG-0572), or the host's game does not run hunger for guest-owned members, which nothing checks.

## Fix idea
Reproduce first. If the host-side member eats, record BUG-0569 as the fix and close with a real test; if it does not, find what keeps the host's AI from feeding guest-owned members (owner rules, selection, order records) and fix it on the host.

## How to test
New coop_tribe_guest_eat.py (tribe fixture): make a guest-owned member hungry with food on the mat and give no order; within 60 s the host's game must run the eat node 0x0461300F on that member (its own tree) and its hunger must go down; the guest's mirror must play the eat animation (the game's current animation on the guest). FAIL twin: `SPORE_FIX_OFF=tribe.mirror_anims` (the guest sees no eating). PASS: all on.

## Fix
Reproduced the way the ticket asked; no mod change was needed. Proven (runtime): the HOST's game feeds a guest-owned member by itself: made hungry with the game's setter (`coop.tribe hunger <id> 15`, no order), its own behaviour tree ate within 15.6-17.3 s (hunger 15 -> 100). The guest's mirror runs no tree (tribe.cpp h_BTTick) and shows the meal only through the relays: the meal's animation (tribe.mirror_anims, BUG-0569) and the hunger (tribe.mirror_hunger, BUG-0935). So BUG-0573's cause ("the guest's hunger AI finds food again") could not hold; the report's "members do not eat on their own" on the guest was what the guest SAW: no eating animation before BUG-0569 (0.7.8, the report was on 0.7.6) and a hunger that ran down on the guest's own game before BUG-0935 (0.7.13). New test `tribe_guest_eat` asserts the games' own state on both sides; its twin is SPORE_FIX_OFF=tribe.mirror_anims,tribe.mirror_hunger.

## Verification
tribe_guest_eat (`coop_tribe_guest_eat.py`, tribe fixture, ~1.2 min): the host's game feeds the guest's member with no order (hunger +30 or more); the guest's mirror is as hungry before the meal (25 or less), as fed after it (within 5) and plays the meal's animation (the host's member's tree animation while it ate).
* FAIL with SPORE_FIX_OFF=tribe.mirror_anims,tribe.mirror_hunger: out/lab/20261112-122726 (the host's game fed the member, 15 -> 100, but the guest's mirror played none of the meal's animations 02796B38 / 060212CE; its hunger stayed its own).
* PASS: out/lab/20261112-124541, out/lab/20261112-124718 (the final test with the hunger-before check); earlier runs of the first version: 20261112-122032, 20261112-122839.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0573. Severity medium, confidence medium.
- 2026-11-12 (fix/w48-small): the host's game feeds guest-owned members itself; the guest sees it through the relays; test tribe_guest_eat (FAIL / PASS).
