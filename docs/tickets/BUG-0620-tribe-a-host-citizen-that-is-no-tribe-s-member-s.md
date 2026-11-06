---
id: BUG-0620
title: Tribe: a host citizen that is no tribe's member stands near the guest on the host only (detector unmatched:citizen)
status: open
kind: desync
stage: Tribe
reported: 2026-11-06
source: detector, play_tribe (fix/w45-placement)
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tribe_member_place (coop_tribe_member_place.py; --attach over play_tribe)
signature:
docs:
related: BUG-0488, BUG-0481
---

## Report
Split from BUG-0488 (its members' row). The detector's `Tribe:unmatched:citizen` in play_tribe: "N on the host, N-1 on
the guest, 1 host / 0 guest without a partner within 15 m", 3-6 of 31 samples in the runs of 2026-11-05/06 (often 2-4
samples in a row), 3 of 32 in lab 20261106-070213, 1 of 31 in lab 20261106-073013 (this branch). Not in a quiet session
of the tribe fixture (tribe_member_place: 0 in 18 samples, lab 20261106-062912).
Observed with `coop_tribe_member_place.py --attach` over play_tribe (out/w45/watch_tribe2.log of fix/w45-placement,
play_tribe lab 20261106-071429): the host's lone citizen (noun 0x018EB4B7) stands still for 10-20 s
(ser 1823 at (403 -48 304) for 20 s, ser 3267 at (393 -91 313) for 10 s), and no member the mod tracks (`coop.sync` t<id>:
every member of every tribe's member list, tribe.cpp) is within 17-100 m of it on the host: it is a citizen object in
no tribe's member list, so the mod never mirrors it. Not a killed NPC member (tribe_dead_member, lab 20261106-072628:
the victim stays in its tribe, knocked out, mirrored) and not the bodies of a beaten NPC tribe (tribe_dead_tribe,
lab 20261106-072755: none left on the host).
Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_tribe --lane N` and, 75 s later,
`SPORE_LANE=N python3 tools/tests/coop_tribe_member_place.py --attach --minutes 11`: it prints the object's pointer,
vtable, age byte (+0xB34) and dead flag (+0xB5E) of every lone host citizen (not caught in lab 20261106-073013).

## Root cause
Not known. Candidates (inferred): a baby hatched from an egg before it joins its tribe (`Tribe:host_only:egg` rows in
the same runs), a member a player's tribe recruits or trains, an animal companion; the watcher's next sighting names it.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
