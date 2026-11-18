---
id: BUG-9100
title: Campaign: Difficulty Scaling changes only progress; enemy health, damage, hunger, takeover and raids ignore it
status: fixed
kind: bug
stage: all
reported: 2026-11-09
source: player report (Lineages 40-dev)
role: host
versions: 0.7.3.6-lineages36-dev to 0.7.3.6-lineages40-dev
fixed_in: 0.7.3.6-lineages41-dev
branch: fix/fov-stuck-wide-lens
commits: 39cdca69 f733c8b1
tests: win_tests (founders block), QA coop.rules and coop.hp on two instances, see Verification
signature: coop.rules model=0 on a new campaign; enemy_hp the same at every Difficulty Scaling
docs: data/config/coop.ini, README.md roadmap
related:
---

## Report

"Changing the difficulty scaling does not increase certain things, like HP and
Damage of enemy creatures. It should affect all difficulty settings we've
created." The player also asked to lower the progress requirement per extra
seat to 60%.

## Findings

`founders::CampaignRules` (founders.h): `difficulty_percent` fed only
`workload_percent()` (progress, Cell food credit, rival targets, Civ military
interval and fleet limit). Enemy health (+15%/seat), damage (+5%), hunger
(+2.5%), city takeover (+5%) and the Space raid size (+1 raider/seat,
space_allies.cpp) used fixed per-seat values from coop.ini.

## Fix

- `CampaignRules::model` (0 = campaigns before 41-dev, 1 = new campaigns).
  Model 1: progress adds 60% per extra seat at 100%, and the slider multiplies
  every per-seat extra (`extra_total`, rounded once over all seats).
- Rules file FST7 (24 bytes) stores the model; FST1-FST6 read as model 0, so
  saved campaigns keep their numbers. `MSG_CAMPAIGN_RULES` carries the model.
- `do_host` and the lobby preview use model 1. Space raids scale too.
- `coop.rules` prints the rules in effect and every derived value.

## Verification

- win_tests: ALL OK. New checks for model 1 at 50/100/200%, one seat, 0%,
  FST6 -> model 0, FST7 round trip, model byte 2 rejected. The old-model
  checks are unchanged and pass.
- QA (Steam 3010a413, `out/release36/native`, profiles LineagesDiff41QA host /
  LineagesDiff41Guest, 640x480 secondary monitor, 5 FPS, muted):
  - Old save Samuth (FST6, 2 seats, 100%): `coop.rules` model=0 progress=200%
    enemy_hp=115% before and after hosting; the file stayed 23 bytes.
  - Same save with an FST7 model 1 file (4 seats, 200%) on host and guest:
    progress=460% enemy_hp=190% enemy_dmg=130% hunger=1.15 takeover=130%
    raid_extra=6 civ_interval_ms=25600.
  - In the world: NPC #12 native max 50 -> 95 (x1.9) once in combat
    (`coop.npc hosthit 12 1`); a neutral NPC of the same species stayed 50
    (by design: neutral species scale when they fight). `exc` 0 faults on both.
- Not covered: a whole new campaign created through the lobby (the write path
  is the unit-tested `reserve`), Tribe/Civ/Space combat in game.

## History
- 2026-11-09: reported by the player; fixed on fix/fov-stuck-wide-lens.
- 2026-11-09: released in 0.7.3.6-lineages41-dev.
