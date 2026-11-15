---
id: BUG-1142
title: C3: one host-busy model (busy_model.h): the host's explicit pause makes guests wait in every stage
status: fixed
kind: bug
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-ccore, feat/w51-core2
commits: 423016ca, 546746af, 8a24f582, 8e9aa765, 10cb551e, 5e0f356c
tests: tests/core_tests.cpp, host_pause_cell, host_pause_creature, host_pause_tribe, host_pause_civ, host_pause_space, host_pause_rivals, host_pause_guest
signature:
docs: docs/01-architecture.md#arch-busy-model, docs/01-architecture.md#arch-c2c3-wiring, docs/re/host-centric.md#host-centric-pause-reasons, docs/re/host-centric.md#host-centric-cell-hold, docs/06-testing.md#testing-host-pause
related: BUG-1086, BUG-0960, BUG-1101, BUG-0151, BUG-0693, BUG-1141
---

## Report
Plan item C3 of docs/plan-0.7.15.md (audit STG-4; owner decision 1: the host's explicit pause makes the guests wait in every stage with "<host> paused the game", not in Rivals mode, a guest's pause stops nobody, host away / editor stays its own state). Today only Tribe relays the host's pause (MSG_NPC_ANIM kind 21, BUG-1086); away is computed per module (local_play copies, npc's shadow g_host_away, cellsync's pool rule) and the held commands are wired twice.

## Root cause
Structure; and a behaviour gap (docs/03-gameplay.md 3.3 "host: pauses for everyone" holds in Tribe only).

## Fix
Portable core (feat/w51-ccore): mods/coop/busy_model.h: `classify(Reading)` (Away / Editor / Paused / None from the pause reasons: pause menu 0x4BF38A4, P 0x4BF38A8, Sporepedia 0x4BF38A6 while no editor is coming, the UI-modal 0x4BF38A7 with a window; Cell: the pause menu's window), `Announcer` (the state once, a pause after 1 s), the MSG_HOST_BUSY codec (14 bytes v1, golden bytes), `HostView` (host id 0 only, seq, the 0.7.14 fallback and waits, edges), per-stage rows and `act(stage, edge, rivals)`, `note()`, `HeldPump` (one held-command pump with the settle and Civ's note throttle). RE: the pause reasons per stage measured on lane 4 (tools/tests/coop_pause_reasons.py, docs/re/host-centric.md#host-centric-pause-reasons). Message spec, migration, per-stage switches (`core.host_busy`, `cell.busy_model`, `npc.busy_model`, `tribe.busy_model`, `civ.busy_model`, `space.busy_model`) and the class test per stage: docs/01-architecture.md#arch-busy-model. MSG_HOST_BUSY is not in the net tables yet (the manifest's). Stays open until the stages adopted it (a behaviour change: FAIL/PASS twin per stage, CHANGELOG line).

The wiring (feat/w51-core2, docs/01-architecture.md#arch-c2c3-wiring):
* MSG_HOST_BUSY = type 70 (messages.def row, Host -> guests, reliable, Session class, route HostBusy, guests only;
  session family 1.0 -> 1.1, lock rewritten; tests/validate_tests.cpp lists it as added after 0.7.14). A 0.7.14 guest
  ignores it (B6); a 0.7.15 guest of a 0.7.14 host stays on the fallback (Tribe's kind 21 only, as 0.7.14).
* host_busy.cpp (a seventh core file): host classify (mode, game_busy, the pause reasons, a modal window, vote.cpp
  `editor_coming`, in Cell the pause menu's window from coop.cpp's 5 Hz menu check) + Announcer (every change, the
  state to a guest that joins); guest HostView (id 0 only), `busy_model::act(stage, edge, spaceext::rivals())`; Wait,
  while the guest plays the host's world (same mode, not elsewhere / another Space save), holds the game: one
  `cGameTimeManager::Pause(0x4BF38A7)`; **Cell**: the cell step SimUpdate is not run (modes.cpp
  `set_cell_hold`), as under the game's own pause menu (the Cell stage has no game-time pause: answered in
  docs/re/host-centric.md#host-centric-cell-hold). Lines: "<host> paused the game: yours waits until <host> goes on",
  "<host> (host) goes on: so does your game". Switches `core.host_busy`, `cell.busy_model`, `npc.busy_model`
  (Creature), `tribe.busy_model`, `civ.busy_model`, `space.busy_model` (they stay: the FAIL twins).
* Tribe: a guest that heard MSG_HOST_BUSY ignores kind 21's "up" (the host still sends kind 21 / 20 for 0.7.14 guests;
  kind 20, the game over, stays Tribe's), the pause net (BUG-1101) counts the wait as held and never releases it.
* One `busy_model::HeldPump` in Tribe and in Civ replaces the two HeldCommands wirings (Civ's 5 s "your order waits"
  throttle is `say_due`).
* Away / editor stay coop.cpp host_away_tick and the modules' local play (decision 1: "its own state"); the HostView's
  away edges are computed but drive nothing yet (`C.host_away` was not replaced by `act() == Local`).
* `coop.status` "host busy:" line. CHANGELOG line (Added).

## Verification
Unit tests tests/core_tests.cpp (busy_classify, busy_codec, busy_announcer, busy_host_view, busy_rows (every stage, Rivals), busy_pump); mutations of the host-only rule and the Rivals rule fail them. RE probe runs on lane 4 (2026-11-15): all five stages, then cell and creature again (output in the RE section).
Lab (tools/tests/coop_host_pause.py, the host's Esc; the guest's game stands, its toast says "Alice paused the game",
it runs after Resume Game):
* PASS (switch-on build) 20261115-161010: host_pause_cell, host_pause_creature, host_pause_tribe, host_pause_civ,
  host_pause_space. Rivals mode (the guest does not wait) and the guest's pause (the host runs on): PASS 20261115-163718
  (host_pause_rivals, host_pause_guest).
* FAIL with the stage's switch off: cell.busy_model 20261115-163459, npc.busy_model 20261115-163626,
  tribe.busy_model,tribe.host_pause_wait 20261115-163744, civ.busy_model 20261115-163459-2, space.busy_model
  20261115-163601 (each: the guest's game plays on, no line).
* The away / held-command tests on the final build: tribe_host_pause, tribe_gameover_wait, cell_away, cell_pause
  (20261115-164230), civ_away, civ_away_buy, tribe_away (20261115-164240) PASS.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-ccore): the portable core and its unit tests; no stage file changed. Open until the stage adoptions land.
- 2026-11-15 (feat/w51-core2): wired into every stage (MSG_HOST_BUSY, the guest's wait, Cell's held step, one HeldPump); fixed (unreleased).
