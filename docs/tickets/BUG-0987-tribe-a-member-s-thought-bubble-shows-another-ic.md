---
id: BUG-0987
title: Tribe: a member's thought bubble shows another icon on the guest than on the host (tribe_mirror_idle rest, tribe_mirror_hold hold)
status: open
kind: desync
stage: Tribe
reported: 2026-11-12
source: 0.7.13 final check key frames, lab 20261112-175447
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Key frames of the 0.7.13 final check (out/lab/20261112-175447 in the rel/0.7.13 worktree, sheets
065_tribe_mirror_idle_01_rest and 063_tribe_mirror_hold_02_hold): the thought bubble over a tribe member shows drums
on the host and a bug (food) on the guest (065, the member behind the fire); in 063 the bubble at the left shows a
brain on the host and drums on the guest. Poses and places of the members agree in both frames. A player sees another
wish of the same member than the partner does.
Reproduce: `lab.py run tribe_mirror_idle tribe_mirror_hold` and compare the bubbles in the key frames.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed (fix/w48-ftribe, 2026-11-12). Inferred: the bubble is the game's own display of a member's current
need / behaviour (food, music, idea); a guest's mirrors run no behaviour tree and the mod relays no bubble, so the
guest's game shows what its own state of the mirror says (its hunger field from BUG-0935's relay, its idle). Next:
find what the bubble reads (string / effect of the bubble, the member's field) and relay it or set it on the mirror.
Not done now: a cosmetic difference with no effect on play, the mechanism is unknown (RE first), the release waits.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
- 2026-11-12: fix/w48-ftribe: seen in two key frames of the 0.7.13 final check; left for a later release (cosmetic, RE needed).
