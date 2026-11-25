---
id: BUG-1562
title: Rivals: isolate designs.cpp outfit/design sync and block Space Jump to non-allied rivals
status: open
kind: bug
stage: All
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in:
branch: w081/rivals-designs-jump
commits:
tests: rivals_designs_jump, rivals_designs_jump_off
signature:
docs: docs/11-rivals.md
related: BUG-0072, BUG-1156, BUG-1548, BUG-1550
---

## Report
1. `mods/coop/designs.cpp` has zero Rivals awareness: in Tribe Rivals, editing the host's outfit in Tribal Outfitter runs `apply_outfit()` onto `player_tribe()` on the guest, which after perspective swap is the guest's own rival tribe (overwriting the rival's clothing). Similarly, host Civ/Space designs should not overwrite a rival's designs in Rivals mode.
2. `mods/coop/spacecoop.cpp` (`pick_jump_target` / `can_jump` / `jump`) does not check `spaceext::rivals()` or player relations: in Space Rivals, a player can click Jump To and teleport directly into a Neutral or War rival's star system, bypassing drive range and exploration.

## Root cause
- `designs.cpp` never checks `tribe_rivals::active()` / `civ_rivals::active()` / `spacerivals::active()` before applying host outfit/design updates onto a guest.
- `spacecoop.cpp` treats every connected partner as a valid jump target regardless of Rivals diplomacy state.

## Fix
- Gate `designs.cpp` host-to-guest outfit/design overwrite off in active Rivals stages behind `COOP_FIX("rivals.designs_isolate", "BUG-1562")`.
- Gate `spacecoop.cpp` (`pick_jump_target` / `can_jump` / `jump` + pure logic in `space_logic.h`) behind `COOP_FIX("rivals.space_jump_ally", "BUG-1562")` so in Space Rivals `Jump to` only targets `Relation::Ally` partners and refuses Neutral/War rivals with a clear notice.

## Verification
Pending implementation and Wine lab ON/OFF twin verification.
