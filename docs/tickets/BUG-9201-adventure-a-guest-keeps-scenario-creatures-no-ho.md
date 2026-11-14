---
id: BUG-9201
title: Adventure: a guest keeps scenario creatures no host creature drives, standing without AI at 0 hp
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-14
source: roadmap
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 6b282874
tests: tools/qa_win (two TITAN games by hand: City Assault with adventure.enemies on, the switch off and on)
signature:
docs: docs/re/space.md#space-adventure-guest-leftovers
related: BUG-9114 BUG-9200 BUG-0015
---

## Report
BUG-9114 (roadmap phase 7, decision 3O A): with `adventure.enemies` on, a guest's game kept scenario creatures that no host creature drives (117 after ~10 minutes, 81 of them standing at 0 hp), and the guest's test hit landed on one of them, so its hits on the host's creatures could not be checked.

## Root cause
Observed (City Assault, 2026-11-14): two causes. (1) The guest's scenario spawns its own waves from markers; the host's pairing takes as many as the host has of that id, the rest have no AI (skipped while the host drives them) and nothing removes them. (2) A creature the host killed only got 0 hp on the guest: a creature dies in its behaviour tree (the death node calls Die, vt 0x8C, docs/re/creature.md 14.1), which the guest skips for these; the copy stood at 0 hp, not dead, and when the host's corpse went its key timed out and it became an unpaired leftover too. With the guest's lethal hit: host `hp=0.0 dead=1`, guest `hp=0.0 dead=0` (same key).

## Fix
adventure.guest_extras (on): a guest's scenario creature (a corpse too) that no host creature drives for 5 s while the host's states arrive (for more than 3 s) is removed (game::destroy_creature); a paired creature whose host creature died gets the game's Die (game::play_death, as BUG-0015 does for Creature-stage proxies). `coop.adventure` shows `extras=` and `extras_removed=`. The test hit `coop.adventure hit <class>` on a guest picks a living creature the host drives and prints its key. With this and BUG-9200, adventure.enemies is on by default.

## Verification
Live, two TITAN QA games (2026-11-14, DuoQAAdvHost + DuoQAAdvGuest, City Assault, adventure.enemies on). Leftovers: FAIL (`coop.fix adventure.guest_extras off` on the guest from the start): after ~3 minutes the guest had 166 creatures to the host's 84, 80 living without a host key, 56 at 0 hp; PASS (switched on): 12 s later 96, 92 removed; a later run with everything at its defaults: the guest 85-98 to the host's 78-89 over 4 minutes, none at 0 hp, the rest within the 5 s grace. Deaths: FAIL (off): the guest's lethal hit (`coop.adventure hit 1083 200`) killed the host's creature (hits_in 1, hits_applied 1, host dead=1) and the guest's copy stood at 0 hp dead=0; PASS: switched on, the same copy dead=1 3 s later; the next lethal hit dead on both. A light hit (5): the host 22 -> 17 hp and the guest's copy 17. The host's creatures still strike the guest's body (7 hits in 40 s). Both missions ran to the end (play state 5 on both games, the guest by the host's outcome), 0 faults. no-fail-run: not a lab run (TITAN QA games, by hand).

## History
- 2026-11-14: opened.
- 2026-11-14: fixed (leftovers removed, the host's kills die on the guest), live-tested with two games; adventure.enemies on by default.
