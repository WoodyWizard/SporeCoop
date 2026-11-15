---
id: BUG-0771
title: Civ: the city-alarm speed boost stacks (1.5x per alarm at the boost's end), vehicles drive faster and faster during sieges
status: fixed
kind: bug
stage: Civ
reported: 2026-11-10
source: GitHub #199
role: host
versions: 0.7.10.1
fixed_in: 0.7.13
branch: fix/w48-civ-speed
commits: 69eecd6d, 942fa887, 1342d697
tests: civ_boost_stack
signature: a vehicle +0x218 over 1.5x its own speed after alarms; civ(host) boosts= high in long sieges
docs: docs/re/civ.md#civ-boost-stacking, docs/re/civ.md#civ-boost-measured, docs/06-testing.md#testing-civ-speed
related: BUG-0040, BUG-0222, BUG-0773, BUG-0186
---

## Report
GitHub #199 (0.7.10.1, Steam, Windows, both players, Civilization, "sometimes"): with time the vehicles start to move
"super fast, like flash". The attached report (host, 62 min of Civ, one rejoin): sieges all the time ("... is under
attack: help!" 14, 12 and 10 times for three cities), the host's `civ(host)` lines show `boosts=18..30` of 34..43
vehicles for many minutes, the trace's `drawn_v` of the partner up to 423, sync-check `v: N apart` of 50..600 m.

## Root cause
Inferred from the code, confidence medium-high (not yet seen in the lab):
* `mods/coop/civ.cpp:1877`: the alarm of a city fires at most once per 20 s (`g_alarm_at`).
* `civ.cpp:1897-1899` (h_OnAttacked): every vehicle of the other players within 600 m gets a 20 s boost; when the
  entry's boost has run out (`b.until < now`) it reads `b.orig = F<float>(v, 0x218)` again.
* `civ.cpp:1903-1923` (boost_tick, once per frame in host_tick): writes `orig * 1.5` while the boost runs, puts
  `orig` back and erases the entry only when `now > until`.
* The next alarm of a city under siege comes exactly when the last boost ends (both 20 s), and OnAttacked runs on
  every hit (many per frame in a siege; docs/re/civ.md: the building's "attacked by" calls OnAttacked). A hit in the
  game update after `until` but before this frame's boost_tick finds the entry expired and not yet restored: `orig`
  is read as the boosted 1.5x speed. boost_tick then "restores" 1.5x, the next boost keeps 2.25x, and so on: x1.5
  every ~40 s of a continuous siege. H->time_seconds() is the wall clock (diag_stats.cpp Timer uses it), so the
  order inside the frame decides.
* "Both players": the host simulates every vehicle (the guests' too); a guest's `civ.vel_cap` (`civ.cpp:3786`, 2 x its
  own +0x218 + 10) takes the fast steps for jumps.
The second leak path (an entry erased without a restore) is BUG-0773.

## Fix idea
Keep the vehicle's own speed once per vehicle: `try_emplace` and set `orig` only for a new entry; an entry lives until
boost_tick restores it (re-arming only moves `until`). Or keep the base speed in its own table, independent of the
boost window. Switch `civ.boost_base` (off = 0.7.11). Optionally a cap: never write more than 1.5 x the base.

## How to test
New `coop_civ_boost_stack.py`: host in Civ with a player vehicle near city 0, read its +0x218 as `base`. Six times:
raise an alarm, then let the boost expire with a hit in the gap. Needs a test command, e.g. `coop.civ alarm 0 expire`
(sets every boost's `until` to now - 0.001 and calls h_OnAttacked in the same call, i.e. before boost_tick).
Assert on the game field: +0x218 <= 1.5 x base + eps all the time, and == base 25 s after the last alarm. Without the
fix (`coop.fix civ.boost_base off`, or main) the speed reaches base x 1.5^6: FAIL. A slower variant without the
hook: an NPC siege of 3-5 min (as in coop_civ_siege_npc) with the same assertion and the guest's vehicle speed from
positions <= 1.5 x top speed.

## Fix
civ.cpp h_OnAttacked: the boost entry is made with `try_emplace`; the vehicle's own speed (and its nation) is read
only when the entry is new. Re-arming an entry only moves `until`, also an entry whose boost has just run out and that
boost_tick has not restored yet. Switch `coop.fix civ.boost_base off` = 0.7.12 (re-read when `until < now`).

## Verification
Test `civ_boost_stack` (tools/tests/coop_civ_boost_stack.py) step 2: five alarms at the boost's end
(`coop.civ alarm <city> expire`), the game's +0x218 read with mem.read, then the host orders the vehicle 400 m away
and the guest's copy's speed is measured from its positions on the guest's game.
* FAIL: out/lab/20261112-003101 (`SPORE_FIX_OFF=civ.boost_base`, lane 2): +0x218 11.39x its own after six alarms,
  the guest saw the vehicle at 225.2 m/s (limit 39.3), 22 s after the last alarm it kept 145.17 (own 19.12).
* PASS: out/lab/20261112-002848, out/lab/20261112-003341, out/lab/20261112-003838 (1.50x, 29-30 m/s on the guest,
  back to 19.12).
Proven: the mechanism (an alarm in that order stacks x1.5 each time and the speed stays). Inferred, not measured: that
a real siege hits that order (cooldown and boost both 20 s, OnAttacked runs on every hit in the game's update before
the mod's frame tick); no NPC-army siege was run for it.

## History
- 2026-11-10: opened.
- 2026-11-12: fixed on fix/w48-civ-speed (civ.boost_base), FAIL 20261112-003101, PASS 20261112-002848 / -003341 / -003838.
- 2026-11-12: released in 0.7.13.
