---
id: BUG-1527
title: Creature: a guest's kill raises the extinction count by one, then it goes back to zero
status: open
kind: bug
stage: Creature
reported: 2026-11-23
source: discord
role: guest
versions: 0.7.15.5
fixed_in:
branch: fix/w82-creature
commits: 2649938f, 9a4c9bc3
tests: tools/tests/coop_creature_solo_kills.py (creature_solo_kills, twin creature_solo_kills_off), tests/npc_tests.cpp (test_social_logic)
signature:
docs: docs/re/creature.md#creature-meter-hold, docs/04-netcode.md#netcode-social-solo
related: BUG-0420, BUG-0205
switch: social.solo_table
---

## Report
Player report on Discord 2026-11-22 (coop 0.7.15.5, Steam; out/research/dc-report-20261122-world-switch.md section 5, the guest's own marks after the world switch): mark 5: "when I kill a creature (guest) the extinction count goes up one, then back to zero". No matching ticket found with `tickets.py find`. Next: fix-player-report (reproduce with a test that fails first).

## Root cause
Observed in the report's log: the host sat in its editor (`host_away=1` for 3 minutes); the guest killed its own game's
animals: `social: relationship event 0 species 24E813CD ... (npc -1): killed 0 -> 1` four times, each time from 0.
social.cpp's guest tick wrote the host's last species table into the game every 0.5 s (enforce_table, gated only for a
guest in another world, BUG-0420), also while the host was away: the guest's own kill went back to the host's 0 (its log
line is capped at 400 lines, so the resets were silent). The host never learned of the kill either.

## Fix
social_logic.h + social.cpp: while the host is away the guest does not write the host's table and keeps its own game's
events 0 / 3 / 5 (SoloEvents, at most 32 species x event, 50 each); when the host is back it sends them (MSG_SOCIAL
K_SOLO = 6, only to a host of creature 1.3: `kSoloMinor`, else dropped as in 0.7.16.1) and takes the host's table
again once a newer one came (or 3 s later). The host replays each event through its game's ApplyEvent with no
creature (the extinction, the alliance and the cards follow); bounds checked (`refused=`). `coop.social solo`.
Switch `social.solo_table` (off: the host's table every 0.5 s, nothing sent, 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: creature_solo_kills, creature_solo_kills_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_solo_kills --lane N --retry 0 --no-build` (the guest's game record keeps killed k0 + 1
  while the host is away; the host's game has k0 + 1 after its return; key frame solo_kill).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_solo_kills_off --lane N --retry 0 --no-build` (back to k0 within 0.5 s).
* Unit: tests/npc_tests.cpp test_social_logic.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-C1), branch fix/w82-creature.
- 2026-11-23: fix and tests on fix/w82-creature (2649938f); waiting for the night lab run.
