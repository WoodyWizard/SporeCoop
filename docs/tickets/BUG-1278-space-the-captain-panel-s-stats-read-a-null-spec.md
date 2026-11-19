---
id: BUG-1278
title: Space: the captain panel's stats read a null species profile at 0xC02A14 for a captain this game has no species of
status: fixed
kind: crash
stage: Space
reported: 2026-11-18
source: static RE for GitHub issue #203
role: guest
versions: 0.7.15.2
fixed_in: 0.7.15.3
branch: fix/w55-captain
commits: 045af236, e8cb2c3c, 98ba8407
tests: space_captain, space_captain_none
signature: 0xC0000005 at 0x00C02A14 (read of 0x57C), 0xC02A10 <- 0xFE718F (SporeCoop::CaptainPanel_Stats1) <- 0xFE7980 (SporeCoop::CaptainPanel_Show)
docs: docs/re/space.md#space-captain-items-null
related: BUG-0304, BUG-1111
---

## Report
Found while fixing BUG-0304 (GitHub #203), static. The captain panel 0xFE7560 (the rank badge, an accepted adventure
mission) fills its stats after the items palette with three helpers (0xFE7140, 0xFE7250, 0xFE7330), each reading
`cSpeciesManager::GetSpeciesProfile(player empire's captain)` without a null check. The lab saw the handled fault at
0xC02A14 for captain 0 in 2026-11-14 (BUG-1111: "0xFE7140 ... a handled fault at 0xC02A14 for captain 0"). With
BUG-0304's guard the panel no longer stops at 0x5971D4, so a captain without a species profile reaches these reads;
on Windows such a fault ends the game (as 0x5971D4 did for #203).

## Root cause
Observed (Ghidra GOG 2024): 0xFE7140 passes the profile to 0xC02A10, which reads profile+0x57C (0xC02A14), then reads
+0x56C / +0x66C; 0xFE7250 reads +0x644 / +0x640; 0xFE7330 reads +0x648 / +0x660 / +0x664 / +0x668. Each is cdecl void()
with one caller (0xFE797B / 0xFE7982 / 0xFE7989 in 0xFE7560). Inferred: the profile is null for captain 0 (BUG-1111's
"Name Your Captain" without a copyable species) and for a captain no database of this game has.

## Fix
fix/w55-captain (0.7.15.3): gamefixes.cpp `game.captain_stats` hooks SporeCoop::CaptainPanel_Stats1..3: when
`GetSpeciesProfile(captain)` (the lookup the helper makes next; cached after it) is null the helper is skipped (its
windows keep their text), a warning names the captain. `coop.fix game.captain_stats off`: the game's code (0.7.15.2).

## Verification
* Lab 2026-11-19 (lane 3, fix/w55-captain): FAIL `SPORE_LAB_FIXOFF="space_captain=game.captain_stats"`
  20261119-112459 (the guest's caught fault `0x00C02A14`, data 0x57C, after Accept and after the rank badge); PASS
  space_captain 2x 20261119-111406 (`stats: skipped` +3 per opening).
* CORRECTION (2026-11-19): the twin is space_captain, not space_captain_none. The `space` fixture's captain 2905CC0C
  has no species profile in the guest's game, so every opening of the full panel reaches the stats helpers with a
  null profile. Captain 0 (space_captain_none) reaches neither guarded fault now (no guard warning; with
  `game.captain_stats` off it passed, 20261119-112022): the profile of key 0 is found. The old test also allowed the
  fault while its guard was off, so that twin could never fail; it allows no handled fault now.

## History
- 2026-11-18: opened while fixing BUG-0304 (GitHub #203); guard game.captain_stats, space_captain_none allows the
  fault only with the guard off (fix/w55-captain).
- 2026-11-19: lab verified (PASS 20261119-111406, FAIL 20261119-112459 on space_captain); the twin moved from
  space_captain_none to space_captain (fix/w55-captain).
- 2026-11-19: released in 0.7.15.3.
