---
id: BUG-0963
title: Adventures: two players in the same adventure never see each other's captain (each play has its own scenario key, so the place ids differ)
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-12
source: fix/w48-small lab 2026-11-12 (adventure_duo) and Discord #russian community report (2026-11-27)
role: both
versions: 0.7.13 0.8.2.4
fixed_in: 0.8.2.5
branch: main
commits: 9d2045fe
tests: make test, live Wine Quick Play Adventure Town (lane 2)
signature: adventure_place differs across two Quick Play instances of the same adventure
switch: adventure.content_identity
docs: docs/re/adventure.md#adventure-two-captains
related: BUG-0845 BUG-0793 BUG-1607
---

## Report
Found while testing BUG-0845 and re-reported in Discord `#russian` (2026-11-27) during Galactic Adventures Quick Play. Two players in a session who both play the same adventure (e.g. a Maxis adventure or two local copies of the same scenario) never see each other: each game's trace says "partner in another world" and no captain puppet is made, because each play has its own local scenario DBPF instance ID.

## Root cause
Observed (runtime, lab lane 2, out/lab/20261112-122759, -123202, -124436): the Adventure adapter's place id (`modes.cpp` `adventure_place`) hashed the scenario resource key (`cScenarioMode +0x74 -> +0x10`: instance +8, group +0x10), which differs across local copies or separate Quick Play runs of the same scenario.

## Fix
- Added `adventure_logic::content_identity_hash` (`mods/coop/adventure_logic.h`) and `modes::adventure_content_key()` (`mods/coop/modes.cpp`) guarded by `COOP_FIX("adventure.content_identity", "BUG-0963", ...)`.
- Computes an order-independent blueprint hash of `cScenarioResource` (`num_acts`, `num_classes`, quantized avatar spawn position at `+0x150`, and live marker definitions at `+0x2C10` stride `0x238`) in Play Mode (`sub_mode != 1`), so two copies of the same adventure with different local DBPF instance IDs derive the exact same `adventure_place()` and `adventure_mission_key()` automatically without `coop.adventure place`.
- Keeps raw scenario keys in Edit Mode (`sub_mode == 1`) so Adventure Editor draft syncing (`adv_editor.cpp`) remains isolated per draft.

## Verification
- Verified in unit tests (`make test`, `adventure_tests` covering order independence, marker/act sensitivity, and Edit Mode exclusion).
- Verified live on Wine lane 2 (Host and Guest entering Quick Play `Adventure Town` with zero `coop.adventure place` overrides): both matched `run=F4E4ECC9`, `paired=6 stream=1`, and saw each other's captains (`err=0.04m / 0.12m`, 0 faults).
- no-fail-run: reproduced historically in `out/lab/20261112-122759`, `out/lab/20261112-123202`, and `out/lab/20261112-124436`.

## Fix idea
Implemented via content-based blueprint hashing of `cScenarioResource` in Play Mode.

## How to test
Launch the same Galactic Adventures Quick Play mission on Host and Guest without `coop.adventure place`; both clients must derive identical `adventure_place()` / `run=` IDs and render each other's captain puppets.

## History
- 2026-11-12: opened while testing BUG-0845.
- 2026-11-12 (rel/0.7.13): closed as duplicate of BUG-0793.
- 2026-11-27 (0.8.2.5, 9d2045fe): reopened and fixed via `adventure.content_identity` blueprint content hashing (`BUG-0963`) alongside 1-click Quick Play Join Adventure invite (`BUG-1607`).
