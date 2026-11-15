---
id: BUG-0971
title: Tribe: tribe_hold_only: the mirror misses a host's hold in the change animation 0x02C39200 (0.5-0.7 s), seen twice
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-12
source: rel/0.7.13 integration lab 20261112-155526 (tribe_hold_only); fix/w48-rest 20261112-143651
role: guest
versions: 0.7.13 (unreleased)
fixed_in: 0.7.13
branch: fix/w48-last
commits: 148fc439
tests: tribe_hold_guest, tribe_hold_only
signature:
docs: docs/re/tribe.md#tribe-hold-missed
related: BUG-0962, BUG-0968, BUG-0914
---

## Report
tribe_hold_only failed in the rel/0.7.13 integration run 20261112-155526: "the host member's holds are the mirror's
too (3 shared, missed [(103.1, 0.68, '02C39200')])"; the B1c agent saw the same once (0.54 s, 20261112-143651). In
the host's game the chief holds its item in the change animation 0x02C39200, the guest's copy does not.

## Root cause
Observed (lab, `coop.tribe holdtrace`): the change animation 0x02C39200 is where a member takes an item into its hand
(the animated change 0xC255E0 attaches it at the animation's event). The relayed start of 0x02C39200 reached the
mirror 0.4 s before the host's hold start, but when the start came the mirror's hand item +0xFFC was still 0 (the
replicated item field comes later): hold_try needs an item and returned at every start (`t 02C39200 FFFFFFFF`) until
the host's end (0.45-0.56 s holds missed; lab 20261112-163629 #2, 20261112-163629-2 #1). The second way the mirror
missed such a hold (the mirror in another animation) is BUG-0968. Not BUG-0962's 0.35 s cut: no cut (`C`) of those
holds in the traces.

## Fix
The host sends hold starts as MSG_NPC_ANIM kind 12 (kind 11's entries + u8 its member's item +0xFFC; npc.cpp passes
kind 12 to tribe.cpp); hold_try attaches the host's item when the mirror's differs or is 0. Switch: `coop.fix
tribe.hold_item off` (kind 11, the mirror's own item).

## Verification
Tests `tribe_hold_only` (the host's chief) and `tribe_hold_guest` (the guest's chief), both games sampled.
FAIL (before the fix, b31902a8): out/lab/20261112-163629 (#2: `t 02C39200 FFFFFFFF`, 0.56 s missed),
out/lab/20261112-163629-2; FAIL (SPORE_FIX_OFF=tribe.hold_item,tribe.hold_anim): out/lab/20261112-170252.
PASS: out/lab/20261112-171257 (tribe_hold_only 2 of 2), out/lab/20261112-171257-2 (tribe_hold_guest 2 of 2): 12
change holds, all shared. The change is rare (0.5-1 per 2 min): a dedicated test that waited for 5 changes ran out of
time (lab 20261112-164945), so the evidence is the two tests' rate (4 misses of ~20 change holds before, 0 of 12 after).

## History
- 2026-11-12: opened.
- 2026-11-12: cause found (the mirror's item still 0 at the host's start), tribe.hold_item, fix/w48-last.
- 2026-11-12: released in 0.7.13.
