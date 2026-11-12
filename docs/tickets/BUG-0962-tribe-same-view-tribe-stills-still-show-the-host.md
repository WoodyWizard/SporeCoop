---
id: BUG-0962
title: Tribe: same_view_tribe stills still show the host chief's staff held differently on the guest in 2 of 4 views with tribe.mirror_hold on (phase or hold timing)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-tribe-mirrors, same_view_tribe key frames lab 20261112-102606
role: guest
versions: 0.7.13 (unreleased)
fixed_in: unreleased
branch: fix/w48-rest
commits: c13be445
tests: tribe_hold_only, tribe_mirror_hold, same_view_tribe
signature:
docs: docs/re/tribe.md#tribe-hold-only
related: BUG-0914, BUG-0936, BUG-0569
---

## Report
Key-frame review of same_view_tribe in lab 20261112-102606 (fix/w48-tribe-mirrors, BUG-0914's tribe.mirror_hold and
BUG-0936's tribe.mirror_idle on): the places and the members' poses agree, but "Alice's chief" (the host's) holds its
staff forward / low in the hand on the host and upright on the guest in view 1 and view 3, the other way round in
view 4. tribe_mirror_hold (the chief's hold in its animation, 3 rounds) passes 3 of 3 and its key frame shows the staff
in the hand on both.

## Root cause
Measured (2026-11-12, fix/w48-rest, lab lane 5: the host's chief in `coop.tribe anims` on both games at ~20 Hz for
120 s, docs/re/tribe.md#tribe-hold-only): the host's holds reached the mirror 0.0-0.07 s later and ended within 0.02 s
(3 of 3); the hold state agreed in 1967 of 1990 samples. The rest: in a gesture 0x04C6CB02 that both games played the
mirror's own animation event took the staff while the host's member never did, for 1.66 s (BUG-0906's settle ends
such a hold after 1.5 s); a still in that window shows the staff in the hand on the guest only. Phase (relay delay)
is not the cause: under 0.1 s.
Was (inferred): Inferred: the chief's gestures (0x04C6CB02 / 05) are short holds; a still catches the host and the
mirror at different phases of the same animation (the relay starts it 0.1-0.3 s later) or one in the hold and the
other just out of it. Measure the time from the host's hold start / end to the mirror's (both games' `coop.tribe
anims` hold= at 20 Hz) before changing anything.

## Fix
mods/coop/tribe.cpp held_items_tick: a mirror's hold that is not bound to a host's hold, with none of the host's known
for that member, ends after 0.35 s (the host's start comes 0.0-0.07 s after the mirror's own one for a shared hold and
binds it). Switch: `coop.fix tribe.mirror_hold_only off`.

## Verification
Test `tribe_hold_only` (tools/tests/coop_tribe_hold_only.py, tribe fixture): the host's chief sampled on both games
(~20 Hz, up to 3 x 120 s until the mirror took its item by its own animation): such holds end within 0.8 s, the
host's holds are the mirror's too.
FAIL (SPORE_FIX_OFF=tribe.mirror_hold_only): out/lab/20261112-143201 (holds of the mirror's own of 1.52 and 1.8 s).
PASS: out/lab/20261112-143653, out/lab/20261112-144021 (such holds 0.54-0.71 s). A first PASS run, out/lab/20261112-143651, failed the test's side check on a host hold open at the window's start (57 s, not comparable) and on a 0.54 s change-animation hold (0x02C39200) the mirror's samples did not show (not seen in the two later runs; a sampling gap or a lost hold: not known). The test skips holds open at the window's start and counts overlapping holds since.

## History
- 2026-11-12: opened.
- 2026-11-12: measured (holds of the mirror's own, not phase), such holds end after 0.35 s (tribe.mirror_hold_only), fix/w48-rest.
