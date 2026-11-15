---
id: BUG-1142
title: C3: one host-busy model (busy_model.h): the host's explicit pause makes guests wait in every stage
status: open
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-ccore
commits: 423016ca
tests: tests/core_tests.cpp
signature:
docs: docs/01-architecture.md#arch-busy-model, docs/re/host-centric.md#host-centric-pause-reasons
related: BUG-1086, BUG-0960, BUG-1101, BUG-0151, BUG-0693, BUG-1141
---

## Report
Plan item C3 of docs/plan-0.7.15.md (audit STG-4; owner decision 1: the host's explicit pause makes the guests wait in every stage with "<host> paused the game", not in Rivals mode, a guest's pause stops nobody, host away / editor stays its own state). Today only Tribe relays the host's pause (MSG_NPC_ANIM kind 21, BUG-1086); away is computed per module (local_play copies, npc's shadow g_host_away, cellsync's pool rule) and the held commands are wired twice.

## Root cause
Structure; and a behaviour gap (docs/03-gameplay.md 3.3 "host: pauses for everyone" holds in Tribe only).

## Fix
Portable core (feat/w51-ccore): mods/coop/busy_model.h: `classify(Reading)` (Away / Editor / Paused / None from the pause reasons: pause menu 0x4BF38A4, P 0x4BF38A8, Sporepedia 0x4BF38A6 while no editor is coming, the UI-modal 0x4BF38A7 with a window; Cell: the pause menu's window), `Announcer` (the state once, a pause after 1 s), the MSG_HOST_BUSY codec (14 bytes v1, golden bytes), `HostView` (host id 0 only, seq, the 0.7.14 fallback and waits, edges), per-stage rows and `act(stage, edge, rivals)`, `note()`, `HeldPump` (one held-command pump with the settle and Civ's note throttle). RE: the pause reasons per stage measured on lane 4 (tools/tests/coop_pause_reasons.py, docs/re/host-centric.md#host-centric-pause-reasons). Message spec, migration, per-stage switches (`core.host_busy`, `cell.busy_model`, `npc.busy_model`, `tribe.busy_model`, `civ.busy_model`, `space.busy_model`) and the class test per stage: docs/01-architecture.md#arch-busy-model. MSG_HOST_BUSY is not in the net tables yet (the manifest's). Stays open until the stages adopted it (a behaviour change: FAIL/PASS twin per stage, CHANGELOG line).

## Verification
Unit tests tests/core_tests.cpp (busy_classify, busy_codec, busy_announcer, busy_host_view, busy_rows (every stage, Rivals), busy_pump); mutations of the host-only rule and the Rivals rule fail them. RE probe runs on lane 4 (2026-11-15): all five stages, then cell and creature again (output in the RE section).

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-ccore): the portable core and its unit tests; no stage file changed. Open until the stage adoptions land.
