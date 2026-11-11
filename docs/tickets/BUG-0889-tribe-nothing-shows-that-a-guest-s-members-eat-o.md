---
id: BUG-0889
title: Tribe: nothing shows that a guest's members eat on their own; BUG-0573's cause (the guest's hunger AI) cannot hold, a guest's mirrors run no behaviour tree
status: open
kind: test
stage: Tribe
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0573. Severity medium, confidence medium.
