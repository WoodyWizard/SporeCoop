---
id: BUG-0863
title: Feature idea: shared Adventure Editor (host plus up to 3 guests edit one adventure draft)
status: fixed
kind: feature
stage: Adventure
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor (editor preview 10, unreleased)
role: 
versions: 
fixed_in: 0.8.0
branch: w08/adv-editor
commits: 1d7c66e0, 93ed786b, 53d99b31, c1d632f1, be2719c4
tests: adv_editor, adv_editor_off, tests/adv_editor_tests.cpp
signature:
docs: docs/re/adventure.md#adventure-shared-editor
related: BUG-0793
switch: adventure.shared_editor
---

## Report
Feature idea, not a bug. Nothing in our roadmap covers the adventure editor in multiplayer. The GA fork built a shared
Adventure Editor on top of its co-op adventures (BUG-0793): unreleased private previews, latest
`0.7.5.0-ga-editor-preview.10` (their protocol 26, 28 after their 0.7.8 sync), branch `sync/upstream-0.7.8-editor`.

## Root cause
Not a bug. What the fork built (their docs, not verified by us; status partial):
* Host plus 1..3 guests edit one draft; the host orders and validates changes; each player keeps its own camera,
  selection and inspector; renewable leases per placement, cast class, act, crew and captain.
* Placements: create from the cast browser, clone, delete, move, rotate, uniform scale with live drag previews; cast
  inspector (behaviours, respawn, visibility, speech); acts (add, delete, reorder, timers, goals); crew and captain;
  terraform as an ordered stroke recipe (partial); "New / Edit asset" opens each player's own creation editor.
* Undo / Redo of own gestures (one gesture = one transaction, 128 gestures / 8 MiB), native Save (host Save As into a
  new local creation, guest local copy), a durable private project per profile (journal, checkpoints, content-addressed
  artifacts), recovery after host loss by manual re-host, Test Solo / Test Together on one immutable revision.
* Pending by their own docs: native acceptance of every terraform tool, cross-act editing, save / reopen, creation
  round trips, 4-player network / FPS / memory gates.
* Size: about 89 000 added lines over their gameplay branch (26 000 code in mods/coop editor_*, net/editor_*,
  package_admission*, 40 000 tests, Windows-native harnesses), 199 symbols in overrides_editor*_dc04aee5.sym and
  overrides_refpack_dc04aee5.sym (verified static), RE docs adventure-editor-save.md, adventure-complexity.md,
  adventure-editor-history.md, imported-package-admission.md, sporekit-lifecycle.md.
* Key game functions (dc04, from the fork, not verified by us): edit history 0x016065D8, StartHistory 0xF45810,
  CommitHistory 0xF45920, Undo 0xF464B0, Redo 0xF464C0; CreateMarker 0xF44210, CloneMarker 0xF44460, DeleteMarker
  0xF428A0; AddAct 0xF448E0, DeleteAct 0xF440B0; SetCaptainAsset 0xF42420; CanSave 0xF41110; NativePreview 0xEF12D0;
  complexity limit `cmp eax,400h` at 0xF44267 / 0xF444DE, MaxComplexity float 0x16C8944 (10.0).

## Fix idea
From the fork (jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor, editor preview 10); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
Too big to port (89k lines on a base 729 commits behind ours, own protocol numbers: MSG_EDITOR 66, MSG_ACTIVITY 55,
adventure 65 collide with ours, e.g. our 65 = MSG_NPC_MEAT; their ticket ids BUG-0606..0628 collide with ours). Realistic
only as a contribution led by the authors after co-op adventures (BUG-0793) exist in ours. Cheap now: copy the RE notes
and symbols with attribution (marked from a fork, not verified).

## How to test
Their model: four players, host places an actor, a guest moves it, Undo / Redo on both, host saves; reopen and compare the
placement on every game.

## Fix
Implemented a clean, SporeCoop-native, host-authoritative Shared Adventure Editor (WP-AE of 0.8.0) behind `COOP_FIX("adventure.shared_editor", "BUG-0863", ...)` on reserved `MSG_ADV_EDITOR = 82` (`Family::creature` 1.5):
- **Verified GOG 2024 RE (`docs/re/adventure.md#adventure-shared-editor`, `data/symbols/overrides_dc04aee5.sym`):** verified 32 Adventure Editor symbols (`cScenarioMode` sub-mode `+0xCC`, `ScenarioRequest_Reset` `0xEF1910`, `App::IStateManager::Get` `0x67DCE0`, `cScenarioData` `CreateMarker`/`CloneMarker`/`DeleteMarker`/`SyncMarkerTransform`/`AddAct`/`DeleteAct`/`SetCaptainAsset`/`CreateClass`/`CanSave`/`Save`/`StartHistory`/`CommitHistory`, `cScenarioEditHistory` `CanUndo`/`CanRedo`/`Undo`/`Redo`, and the `cScenarioResource +0x2C10` marker pool layout where `cScenarioMarker` starts at `slot +0x04`).
- **Portable logic & wire codec (`mods/coop/adv_editor_logic.h`, `mods/coop/wire/adv_editor.h`):** host-authoritative monotonic revision counter (`rev`), per-marker renewable leases with automatic 5 s inactivity expiry and disconnect cleanup, per-player Undo/Redo gesture stacks storing inverse deltas so each player undoes only their own gestures, chunked snapshot assembly (`<= 16` markers/chunk, up to 1024 markers), delta catch-up, and 10 Hz unreliable live drag previews (`K_DRAG_PREVIEW`).
- **Runtime module (`mods/coop/adv_editor.{h,cpp}`):** hooks native Adventure Editor mutations with `CALL_GUARD("adv_editor.applying")` echo suppression, queues all native scene updates outside the D3D9 frame (`H->run_outside_frame`), skips captain puppets/avatars while in Edit Mode (`modes.cpp` `scenario_local` / `scenario_spawn`), pauses Play-mode `adventure.cpp` while editing, and provides `coop.adveditor` (`status`, `enter`, `snapshot`, `markers`, `create`, `move`, `delete`, `undo`, `redo`, `act`, `captain`, `lease`).

## Verification
- Native unit tests (`tests/adv_editor_tests.cpp`, ASan + UBSan): 181 checks, 0 failed (`make unit-cpp`).
- Decoder fuzzer (`tests/fuzz_decoders.cpp`, ASan + UBSan): all 7 `wire::adv_editor` structs pass (`make unit-fuzz`).
- Wine lab (lane 6, GOG 2024 `dc04aee5`, run `20261125-031642`, commit `be2719c4`):
  - `adv_editor` (ON): **PASS** in 58 s (0 faults on host & guest, 0 sync-check mismatches): both instances open stock adventure `F63B6851` in native Edit Mode (441 markers), snapshot syncs across 28 chunks, host CreateMarker replicates to guest native `cScenarioResource`, guest Move with lease replicates to host, guest Undo reverts on both, host Delete rejected while guest holds lease, lease release + host Delete removes marker natively on both.
  - `adv_editor_off` (OFF-twin, `SPORE_FIX_OFF=adventure.shared_editor`): **FAIL** cleanly on checks 2, 3, and 4 (0 crashes) because edits remain local when the switch is off.

## History
- 2026-11-10: opened.
- 2026-11-25: implemented WP-AE Shared Adventure Editor on `w08/adv-editor` (`BUG-0863`), verified on GOG 2024 Wine lab lane 6 (`20261125-031642`: `adv_editor` PASS, `adv_editor_off` FAIL).
