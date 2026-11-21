---
id: BUG-1347
title: Tribe: a guest's copy of the chief does not take the staff when the host's hold binds right after the mirror's animation start (hold off 1-1.4 s)
status: open
kind: desync
stage: Tribe
reported: 2026-11-20
source: lab
role: guest
versions: 0.7.15, rel/0.7.16
fixed_in:
branch: fix/w60-tribe
commits: a431469c
tests: tribe_chief_still, tribe_chief_still_retry_off (twin, probabilistic), tribe_mirror_hold
signature:
docs: docs/re/tribe.md#tribe-hold-retry
related: BUG-1069, BUG-0914, BUG-0968, BUG-0992, BUG-1335
switch: tribe.hold_retry
---

## Report
tribe_chief_still FAILs "no stretch over 1.0 s with the chief's item in the hand on one game only": lab 20261120-041304
(1.4 s at 30.9 s, rel/0.7.16 final check), 20261119-214823 (1.1 s), rel/0.7.15 20261117-154911 (1.1 s), fix/w49-tribe3
20261113-060910 (1.4 s and 1.1 s). `lab.py flaky`: 5 of 20 runs since 2026-11-13. The host's chief holds its staff in
its gesture 04C6CB02; the guest's copy has it in the resting grip for 1.1-1.4 s.

## Root cause
✔ (the guest's hold traces `coop.tribe holdtrace`, printed by the test, all five stretches) the same ending each time:
the guest's `hold_try` bound the host's hold with the attach (event T with hold 0: SetHandheldItem) in the same tick it
started the host's animation on the copy (P) or a relayed restart was played (R / D); that hold never started (hold 0)
until the host's once-a-second resend (send_member_holds `again`) bound it again 1.0 s later (T, then O 0.02 s after):
  20261120-041304: 31.29 S, P 04C6CB02 (from idle 02481DE5), T 1100 ... 32.32 S T 1100, 32.34 O.
  20261119-214823: 82.85 S, P (from walk 04866DAB), T 1100, D ... 83.90 S T 1100, 83.92 O.
  20261113-060910: 87.82 S T 1100 R ... 88.91 S T 1100, 88.95 O.
~ (inferred, BUG-0992's ✔ mechanism, docs/re/tribe.md#tribe-anim-queued-start) the attach 0xC240E0 binds the hold modifier
to the track of the current id looking at the pending tracks first; a start made in that frame is not yet the playing
track, the modifier's tick ends it while its track does not play. The second attach, a second later, finds the playing
track.
~ Open (not this fix): in three of the five (060910 first, 154911, 041304) the copy's gesture had ended for its idle
0.6 s after a resend (M: the bound animation no longer drawn, hold ended) while the host's chief still held it; the
retry shortens what follows (the next resend's P is followed by an attach 0.3 s later instead of 1.0 s), the early end
of the copy's track itself is not explained (the host's track loops / passes come with kind 15).

## Fix
`tribe.hold_retry` (tribe.cpp held_items_tick, guest): a hold hold_try bound with an attach (hold_set_at) whose modifier
has not started kHoldRetry 0.3 s later, while the copy still draws the bound animation and holds the item, is attached
again (the pending starts of that id ended first with the game's own call, as BUG-0992; the item scale put back as
hold_try does), at most 2 times per bind; hold event 'A', counter `coop.tribe anims` hold `retried=`. Off: it waits for
the host's resend (as before).
Tests: tribe_chief_still prints the guest's hold counters; twin tribe_chief_still_retry_off (360 s of samples,
SPORE_FIX_OFF=tribe.hold_retry): probabilistic, the case came in about one 90 s run in four.

## Verification
FAIL (before): lab 20261120-041304, 20261119-214823 (rel/0.7.16), 20261117-154911 (rel/0.7.15), 20261113-060910.
Open, waiting for the lab run: tribe_chief_still must PASS (and print `retried=` > 0 when the case came);
tribe_chief_still_retry_off is expected to FAIL (a PASS there is no proof: the case is timing); tribe_mirror_hold,
tribe_hold_only, tribe_hold_guest, same_view_tribe must still PASS.
- FAIL twin (the case came twice, retry off): lab 20261120-130608 tribe_chief_still_retry_off.log (1.7 s at 27.1 s,
  1.1 s at 188.3 s, retried=0) and .env1.log (1.4 s at 87.3 s); the lab listed the twin OK only because its rule for a
  game that did not start matched this switch's description and kept the third run's PASS (BUG-1379).

## History
- 2026-11-20: opened.
- 2026-11-20: cause and fix on fix/w60-tribe (no lab today, owner rule); lab run pending.
- 2026-11-20 (fix/w61-civ): the twin's runs of the second final check FAILed twice as wanted (BUG-1379 explains the
  OK in the twins' list).
