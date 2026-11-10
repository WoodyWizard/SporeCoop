---
id: BUG-0824
title: Tribe: a member the host buys within 4 s of a guest's purchase is given to the guest (the pairing takes the host's own baby)
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Host and guest both buy a tribe member at about the same time: the host's new member belongs to the guest; with 3-4 players members change hands more often, and a lost relay makes one member for two payments. Found by reading the code.

## Root cause
Observed in the code, confidence medium (depends on where the game places the BabyBorn position). tribe.cpp:1877-1878 (h_BabyBorn path): every baby the host's game makes without a pending ask goes into g_baby_made, the host's own purchases too. baby_on_host (:1882-1893) gives the first g_baby_made entry within kBabyNear (6 m) and kBabyPair (4 s) to the asking guest; the tribe has one hut, so the positions are nearly always 'near'. The guest's own baby, made later by its relayed effect, finds no ask and goes to the next owner in turn. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Do not put babies of the host's own click (BabyBorn not called from the relayed effect) into g_baby_made, or pair only with babies made by a relayed effect.

## How to test
Two instances in Tribe with food; host and guest click their hut's buy within about 1 s: FAIL = the host's new member has the guest as owner (`coop.tribe` member list, babies_paired 1 too early); PASS = each buyer owns its own member.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
