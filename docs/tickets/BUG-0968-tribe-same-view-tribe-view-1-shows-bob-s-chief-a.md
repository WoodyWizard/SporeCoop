---
id: BUG-0968
title: Tribe: same_view_tribe view 1 shows Bob's chief (a guest-owned member) with its torch raised in the hand on the host and not on the guest
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-rest final check, same_view_tribe lab 20261112-145017
role: 
versions: 0.7.13 (unreleased)
fixed_in: 0.7.13
branch: fix/w48-last
commits: 148fc439 a54a52bf
tests: tribe_hold_guest, tribe_hold_only, same_view_tribe
signature:
docs: docs/re/tribe.md#tribe-hold-missed
related: BUG-0962, BUG-0971, BUG-0914
---

## Report
Key frame view1 of same_view_tribe in out/lab/20261112-145017 (fix/w48-rest, with tribe.mirror_hold_only on): the
host's game draws "Bob's chief" (the guest's chief, simulated by the host) with its torch raised in the hand, the
guest's copy of it stands with the torch in the resting grip. Alice's chief (the host's) holds its staff the same way
on both in all 4 views. The other direction of BUG-0962 (a host's hold the mirror lacks), or a still between the
host's hold start and the mirror's (0.0-0.07 s measured for the host's chief only). Seen once.

## Root cause
Observed (lab, `coop.tribe holdtrace` on the guest, the guest's chief sampled on both games at ~10 Hz by
tribe_hold_only --who guest): the host's hold could not bind on the mirror while the mirror played another animation.
hold_try binds a host's hold only when the mirror plays the hold's animation; when the host's member went from its
gesture 0x04C6CB02 straight into the change 0x02C39200 (or the idle fix had just started the host's gesture on the
mirror), no relayed start of the hold's animation reached the mirror before the hold, and each start (again every
second) found the mirror in the other animation (`t 02C39200 04C6CB02`) until the host's end: the host's member held
its item (torch or staff raised in the hand), the mirror did not, in another pose. Misses in 2 of 2 tribe_hold_guest
runs before the fix (lab 20261112-163629-2) and in the integration run's tribe_hold_only (20261112-155526). The key
frame of the report (host: the chief rearing with the torch raised, guest: another pose without it) matches this; the
exact moment of that still was not traced (inferred).
A first version of the fix played the animation and the relayed start of the same animation came in the same moment
and started it again: the hold bound to the first start ended and no later start bound (2.7 s missed, lab
20261112-170115); the relay now skips that second start.

## Fix
mods/coop/tribe.cpp hold_try: a fresh host start (under 0.5 s) whose animation the mirror does not play starts it on
the mirror (play_behaviour_anim, at most once a second, counted as a relayed start for the idle fix) and binds the
hold to it; the relay (kind 2) does not start that animation again within 0.5 s while the mirror plays it. Switch:
`coop.fix tribe.hold_anim off` (waits for the relay as before). Diagnostics: `coop.tribe holdtrace [id]`.

## Verification
Test `tribe_hold_guest` (coop_tribe_hold_only.py --who guest --windows 2, tribe fixture): the guest's chief on both
games for 2 x 120 s; every hold of the host's member over 0.3 s is the mirror's too.
FAIL (before the fix, b31902a8): out/lab/20261112-163629-2 (2 of 2: 02C39200 holds missed, the mirror in 04C6CB02);
FAIL (SPORE_FIX_OFF=tribe.hold_item,tribe.hold_anim): out/lab/20261112-170252 (1 of 2, `t 02C39200 04C6CB02`).
PASS: out/lab/20261112-171257-2 (2 of 2, every host hold shared, 8 of them change holds), tribe_hold_only
out/lab/20261112-171257 (2 of 2).

## History
- 2026-11-12: opened.
- 2026-11-12: cause found (the mirror in another animation at the host's hold start), tribe.hold_anim, fix/w48-last.
- 2026-11-12: released in 0.7.13.
