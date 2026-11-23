---
id: BUG-1516
title: Mirror FX: the rate limit drops stops too, and a dropped stop is never sent again (a stuck effect on a mirror) (UIFX-4)
status: open
kind: bug
stage: Creature, Tribe
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w82-creature
commits: e44b4ff0, 9a4c9bc3
tests: tools/tests/coop_mirror_fx_keys.py --burst (mirror_fx_burst, twin mirror_fx_burst_off)
signature:
docs: docs/re/creature.md#creature-meter-hold
related: BUG-1252, BUG-1049, BUG-0600
switch: mirror.fx_stops
---

## Report
External static audit of 0.7.15.5, item UIFX-4 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-C1 (fix/w82-creature). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (mirror_fx.cpp), confirmed by the audit's verify row: `send()` dropped every kind when the
60/s bucket was empty (`if (!take_token()) { g_dropped++; return; }`), stops too, and `on_creature_effect` removes the
key from the table (`key_take`) before the send, so a dropped stop was never sent again: a looped effect (and, through
the same send, a sneak's alpha or the sprint dust after a dropped off edge of K_VOICE / K_SPRINT) stayed on the guest's
mirror of a host NPC or tribe member. The eviction stop of BUG-1049 went through the same limiter. Same class as
BUG-1252, whose fix covered only effects.cpp (a player's own effects).

## Fix
mirror_fx.cpp `send(kind, w, reliable, end)`: an end (a keyed stop, an evicted key's stop, the off edge of K_VOICE
and K_SPRINT) costs a token when there is one and goes anyway (counted `ends_unlimited=` in `coop.mirrorfx`); at most
one end per start that went out, so no flood. Tests: `coop.mirrorfx drain <s>` holds the bucket empty for s seconds
(a long fight). Switch `mirror.fx_stops` (off: dropped like a start, 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: mirror_fx_burst, mirror_fx_burst_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run mirror_fx_burst --lane N --retry 0 --no-build` (the guest's game gets the stop of the member's
  keyed effect sent inside a 4 s empty limit; key frame fx_burst_stop).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run mirror_fx_burst_off --lane N --retry 0 --no-build` (the stop dropped, never sent).
* Regression: mirror_fx_keys, tribe_fight_sounds.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-C1), branch fix/w82-creature.
- 2026-11-23: fix and tests on fix/w82-creature (e44b4ff0); waiting for the night lab run.
