---
id: BUG-1577
title: World share: automatic share save during host Pause Menu / Save & Exit corrupts save slot (0x00BA7FA7 LoadGame crash)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-26
source: Discord (Vlaid18)
role: host
versions: 0.8.1.1
fixed_in: 0.8.1.3
branch: main
commits: 8021d561
tests: share_pause_guard, share_pause_guard_off, core_tests
signature: 0xC0000005 at 0x00BA7FA7 (Steam 0x00B17FA7) read 0x00001188
docs: docs/03-gameplay.md
related: BUG-0846, BUG-1142, BUG-1365
---

## Report
Discord (`Vlaid18`, `crash-20261126-190925.txt` + `sporekit-20261126-190925.log`): after saving & exiting Tribe save `Кана.spo` to the Galaxy Menu, reloading it 60 s later crashed at Steam `0x00B17FA7` (GOG `0x00BA7FA7`, `cStarManager::GetStarGridPosition+0xD57`, reading `[ebx+0x1188]` with `ebx=0`).

## Root cause
From `sporekit-20261126-190925.log` (`19:08:15..19:08:23`):
- Guest `DERMOGEN` exited to Galaxy Menu (`19:08:15.911`).
- Host `Vlaid18` opened the Pause Menu (`host busy: paused (why 0044)`) and clicked **Save & Exit to Main Menu** (`why 0054`, saving `Кана.spo` at `19:08:18.604`).
- Exactly 5.0 s after guest exited (`19:08:20.919`), `session_glue.cpp:follow_phase()` (`back to the world: sending it to DERMOGEN`) called `world::share_with(1, ..., true)` while `host_busy` was `paused (why 0054)`, posting a second concurrent `kSaveMsg` mid-teardown as the game transitioned `Tribe -> Galaxy menu` (`272 creatures destroyed`, `"the game went from Tribe to Galaxy menu during the save for player 1"`), corrupting `Кана.spo`.

## Fix
- Added portable `share_logic::share_pause_blocker(bool fix, uint16_t host_why)` in `mods/coop/share_logic.h` checking explicit UI pause/dialog bits (`kWhyPauseMenu`, `kWhyModal`, `kWhySporepedia`) without blocking implicit engine pauses or key-`P` pauses.
- Exposed `host_busy::local_ui_pause_why()` (`mods/coop/host_busy.h`, `mods/coop/host_busy.cpp`) classifying the host's live local UI pause state independent of `core.host_busy`.
- Added `COOP_FIX("world.share_not_paused", "BUG-1577")` (default ON) in `mods/coop/world.cpp` (`share_pause_block_reason()`, `note_pause_share_wait()`, and `PendingShare` save wait in `world::tick()`) and gated both `follow_phase()` share triggers in `mods/coop/session_glue.cpp`. Manual `coop.save` remains unblocked (`coop_saved_box.py --case question` unaffected).

## Verification
- Unit tests (`tests/core_tests.cpp`): `share_pause_blocker` matrix PASS.
- Two-game Wine lab ON/OFF twin (`out/lab/20261126-142431`, `coop_share_pause_guard.py`):
  - ON (`share_pause_guard`): **PASS** (45 s, host held auto-share while Pause Menu was open with `pause_waits 0 -> 1`, 0 share saves posted; once Pause Menu closed, held share fired automatically and guest returned to Tribe; 0 faults).
  - OFF (`share_pause_guard_off`, `SPORE_FIX_OFF=world.share_not_paused`): **FAIL** (host posted share save `share_saves 1 -> 2` and sent offer `1 -> 2` while Pause Menu was open).
- Regression (`coop_saved_box.py --case question`): **PASS** (0 faults).
