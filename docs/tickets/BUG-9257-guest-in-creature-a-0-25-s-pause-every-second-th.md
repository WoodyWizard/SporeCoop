---
id: BUG-9257
title: "Guest in Creature: a ~0.25 s pause every second (the parts census asks Windows about every item)"
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: player report (SporeCoop-report-20261118-211933.zip)
role: guest
versions: 0.7.14.1-lineages64-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: ab032cd3
tests: tools/qa_win/qa.py (manual: TITAN host + guest in Creature, the frame profiler's prof lines)
signature: "a pause of about 0.25 s after every second, only while connected; microstutter about 34 %"
docs: docs/06-testing.md#testing-frame-profiler
related: BUG-0401, BUG-0441, BUG-0791
---

## Report
A guest (64-dev, Windows 10, RX 6700 XT, Steam and Discord overlays, the booster pack installed) stutters only while
connected to the host: "It pauses for about .25s after every second", a microstutter rate of 34 %. Seen before
(ROADMAP 4V: "BigTexas' stutter (needs their log)"). His log: present interval p50 9.7 ms, p95 46.5 ms in Creature.

## Root cause
Observed (2026-11-18, TITAN, a host and a guest in Creature with a frame profiler added to on_frame): on the guest
`parts::tick` took 54-106 ms once a second (8 of the 9 calls over 50 ms in the first minute were it).

`parts::guest_tick` (BUG-0401, the shared creature parts) runs every second on a guest in Creature and made the parts
census twice (its own and the one in `unlock_missing`). A census walks every collectable item of the game (thousands,
more with the booster pack) and called `readable()` for every bucket and every node: one VirtualQuery system call
each. Inferred: on the player's PC (Windows 10, two overlays hooking the process) the same walk costs about 0.25 s.
The host's `parts::host_tick` and the trace line's census (every 10 s) walk the same way.

## Fix
parts.cpp: a walk asks Windows once per memory region (the nodes lie in a few heap regions; a region found committed
and readable is not asked again within the walk), and a guest's poll makes one census (its locked list goes to
`unlock_missing`). Switch: `coop.fix parts.census_cache off` (every node asked as before). Also on this branch: the
frame profiler (coop.cpp `fprof`, `[diag] frame_prof`), which found it.

## Verification
no-fail-run: no automated test (a timing on a player's PC). Manual, 2026-11-18 on TITAN (QA 9 host, QA 8 guest, Samuth,
Creature): before the fix the guest's profiler logged parts::tick over 50 ms 8 times in its first minute (54, 54, 56,
58, 68, 106 ms ...); with it, once. The second run was disturbed (the owner's own game ran on the same PC, every
section was slower), so the player's confirmation in a release is still the real check.

## History
- 2026-11-18: opened from the player's report and fixed on feat/lineages-47.
