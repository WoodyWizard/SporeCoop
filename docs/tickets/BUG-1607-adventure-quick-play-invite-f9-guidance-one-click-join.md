---
id: BUG-1607
title: Galactic Adventures Quick Play: misleading world-wait notice and missing 1-click Join Adventure invite for guests
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-27
source: Discord #russian community report (peaceduke, v0.8.2.4)
role: both
versions: 0.8.2.4
fixed_in: 0.8.2.5
branch: main
commits: 9d2045fe
tests: make test, live Wine Quick Play Adventure Town (lane 2)
signature: Host in Quick Play shows "a player is waiting for the world: finish or quit the adventure" while Guest stays stuck in Galaxy Menu
switch: adventure.quick_join
docs: docs/re/adventure.md#adventure-two-captains
related: BUG-0963
---

## Report
When a host starts a Galactic Adventures Quick Play mission while a guest is in the Galaxy Menu, the host received the misleading corner notice `"a player is waiting for the world: finish or quit the adventure, then share the world"` while the guest's F9 lobby showed `"Waiting for the host's world..."` with no indication of which adventure the host was playing and no way to join directly.

## Root cause
Galactic Adventures Quick Play does not create a save-game world (`MSG_WORLD`), so the world-share wait loop (`world.cpp`) treated the host's adventure as an obstacle to sharing a planet save instead of broadcasting a Quick Play adventure invite, and `coop.cpp` / `coop_ui.cpp` had no guest-side Quick Play status line or launch button.

## Fix
- Added `adventure.quick_join` (`COOP_FIX("adventure.quick_join", "BUG-1607", ...)`) and wire message `K_HOST_ADV = 9` (wire family `creature 1.6`).
- Host reads the live adventure title directly from `cScenarioData +0x4C` (`eastl::string16 mName`, verified in `0x00F3F9DB` / `0x00F44B2B`) with fallback to `creations::creation_name` (`0x030BDEE3`), and broadcasts `K_HOST_ADV` on adventure enter, leave, and player join (`adventure::send_invite_to`).
- Suppressed the misleading `"a player is waiting for the world: finish or quit the adventure..."` notice during Quick Play in `mods/coop/world.cpp`.
- Replaced `"Waiting for the host's world..."` in `coop.cpp` `lobby_world_line()` with localized Quick Play guidance naming the host's adventure (5 languages: EN/RU/HU/ES/ZH).
- Added a 1-click `"Join Adventure"` button (`D.share_btn` reused on Guest in `mods/coop/coop_ui.cpp`) and `coop.adventure join` launching Play Mode natively via `ScenarioRequest_Reset` (`subMode=2, entryKind=1`) and `IStateManager::vt+0x28(0x21851EBE)`.

## Verification
- Verified in unit tests (`make test`, `adventure_tests`, `msg_manifest`, `static_guard_lint`, `ratchet_lint`).
- Verified live on Wine lane 2: Host entered Quick Play `Adventure Town` (`mission=69D33EE1/F7B14965`), Guest in Galaxy Menu received `K_HOST_ADV` (`title='Adventure Town'`), F9 displayed the `"Join Adventure"` button and hint, and `coop.adventure join` launched `Adventure Town` Play Mode with 0 faults, auto-matched `run=F4E4ECC9`, and rendered both captains (`err=0.04m / 0.12m`).
- no-fail-run: community UX report in Discord `#russian` on `0.8.2.4`.
