---
id: BUG-0987
title: Tribe: a member's thought bubble shows another icon on the guest than on the host (tribe_mirror_idle rest, tribe_mirror_hold hold)
status: wontfix
kind: desync
stage: Tribe
reported: 2026-11-12
source: 0.7.13 final check key frames, lab 20261112-175447
role: guest
versions: 
fixed_in:
branch: fix/w49-mirror
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-bubble-icons, docs/known-issues.md#issue-tribe-bubble-icons
related: BUG-1043
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
Observed (fix/w49-mirror, `coop.effects log`, shots of both games, a member made to chat with its CreaturePlayAnim): the
bubble is the animation event effect 0x883816C2 of the chat animations 0x04FFA012..0x04FFA019, a cloud with a random
pictogram (chat, not a member's wish). The relay plays the animation on the mirror and the guest's game makes the same
effect at the same moment; the pictogram is the effect's own pick. Not BUG-1043's cause (no field involved).
Three ways to give both games the same pick failed (seed + restart of the effect; the effects' random generator set
before the effect is built; and before each of its updates): the pick is made deeper in the effect system
(docs/re/tribe.md#tribe-bubble-icons).

## Fix
Not fixed (wontfix): a random chat pictogram, cosmetic, no effect on play; a fix needs the Swarm particle component's
random source found first. The experiment code is not kept; known-issues#issue-tribe-bubble-icons.

## Verification
no-fail-run: not fixed (wontfix); reproduced in 5 of 6 forced chats (probe on lanes 6 / 7).

## History
- 2026-11-12: opened.
- 2026-11-12: fix/w48-ftribe: seen in two key frames of the 0.7.13 final check; left for a later release (cosmetic, RE needed).
- 2026-11-13: fix/w49-mirror: mechanism found (the chat effect 0x883816C2), three sync attempts failed; wontfix.
