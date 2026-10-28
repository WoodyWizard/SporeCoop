---
id: BUG-0072
title: with 3 or more players the "Jump to" button fails, and the jump message names the wrong player
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#50
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-space
commits: 66d7a30, 4c74ae4
tests: space_map_input, unit (tests/space_tests.cpp)
signature:
docs: docs/re/space.md#space-w3-trips-clicks-defenders
related: #50
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/50 (full text, discussion).

The co-op window's "Jump to <name>" button shows one partner but jumps to another. With three or more players, when the first partner in the list is already in my system, the button reads "Jump to B" and every press answers "can't jump: already in the partner's system". Also, after a successful button jump the notice always says "jumped to <player 0>'s star system": that is the host's name for a guest, and the host's own name when the host jumps. Severity: wrong behaviour.

### Where
- `can_jump`, which picks the name the button shows: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L254-L269
- `jump`, which picks the star: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L271-L295
- The button wiring (`can_jump` for the label, `jump(0xFF)` for the action): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2515 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2527-L2537
- The label: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L662-L664

### How to reproduce
1. Three players in Space: host (0), guest A (1), guest B (2). All have the interstellar drive.
2. Host and guest A in the same star system. Guest B in another system.
3. On guest A, open the co-op window: the button reads "Jump to B".
4. Press it: "can't jump: already in the partner's system". Nothing happens, every time.
5. Name bug, 2 players are enough: the host is in another system than the guest, the host presses "Jump to <guest>". After the jump the notice reads "jumped to <host's own name>'s star system".

### Expected
The button jumps to the partner it names. The notice names that partner.

## Root cause
**What happens**

1. `can_jump` walks `C.partners()` (ordered by player id, `g_remotes` is a `std::map`) and returns the first partner whose star is known **and differs from mine** (`s && s != here`). The button shows that partner's name.
2. The button calls `jump(0xFF)`. That loop takes the first partner with **any** known star:
   ```cpp
   if (p.id == to || to == 0xFF) {
       star = star_of(p.place);
       ...
       if (star)
           break;          // no check against my own star
   }
   ...
   if (star == star_of(C.my_place ? C.my_place() : 0))
       return "already in the partner's system";
   ```
   If that first partner is in my system, the jump is refused, even though the button promised another partner.
3. On success the notice is `"jumped to " + name(to == 0xFF ? 0 : to) + "'s star system"`. With `to == 0xFF` it always names player 0, whoever the jump went to. For the host, player 0 is the host itself.

### Confidence
CONFIRMED by reading the full path (label from `can_jump`, action `jump(0xFF)`, the two different loop conditions). Not run with 3 instances.

## Fix
Fixed on fix/w3-space (66d7a30): one pick for the button label (`can_jump`) and `jump(0xFF)`: `spacelogic::pick_jump_target` (mods/coop/space_logic.h), the first partner with a known star that is not mine; the notice names the partner the jump went to. `coop.space jumpfix 0`: the old pick and notice (tests).

Suggested in the review:

- One helper `pick_jump_target(uint8_t& id, uint32_t& star)` used by both `can_jump` and `jump(0xFF)`: first partner with a known star that is not mine. `jump(0xFF)` then uses that id.
- Use the chosen partner's id in the notice: `name(chosen_id)`.
- Or better, let the button pass the partner id that `can_jump` showed (the UI keeps it).

## Verification
Done (2026-10-28, lane 6). 3 players need a second lane (not this agent's), so the pick is a unit test: tests/space_tests.cpp `test_jump_target` (make test) checks the 3-player case (button and jump both pick partner 2; the old pick gives partner 0, in my own system). The notice: `coop_space_map_input.py`, the host jumps with `coop.space jump` (any partner). `--without-fix` FAILS: "jumped to Alice's star system" (the host's own name, /tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w3-space/map_input_nofix.log); with the fix PASS: "jumped to Bob's star system" (/tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w3-space/map_input_pass1.log). Not run: a real 3-instance click on the co-op window's button.

Planned in the review: Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_jump3.py`, three players on two lanes (the way `tools/tests/coop_npc_multi.py` runs three or four players), fixture `space`:
1. Host and guest A in the home system, guest B jumps to another star (`coop.space jumpto <StarID>`).
2. On guest A, press the co-op window's Jump button (the virtual cursor, as `coop_space_page.py` does).
3. Assert on guest A's game: the active star record (`coop.space star`, the game's `GetActiveStarRecord`) is guest B's star.
4. Assert the chat log line names B.
5. Fails on main (active star unchanged, "already in the partner's system"), passes with the fix.

### Related
- `squad_rows` (the disabled-partner row) and space_map's rows pass the partner id explicitly and are not affected.
- docs/re/space.md 7.3 (the co-op jump).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #50.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-space (66d7a30): shared target pick, notice names the target; unit test + space_map_input FAIL without / PASS with.
- 2026-10-28: fix/w3-space lab runs on lane 6: 20261028-010933 (14/14: the four new wave 3 Space tests and space_map, space_stale, space_fleet, space_puppet, space_rivalry, space_contracts, space_jump, space_guest_ui, space_allies, space_diplomacy), smoke 20261028-013951 (13/13; `ui` skipped: exclusive, needs a lane 1-5).
- 2026-10-28: released in 0.7.3.0.
