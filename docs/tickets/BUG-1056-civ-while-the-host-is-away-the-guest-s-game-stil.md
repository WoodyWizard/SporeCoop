---
id: BUG-1056
title: Civ: while the host is away the guest's game still skips the ground check of its mirror vehicles, which its own AI drives then
status: open
kind: bug
stage: civ
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-14
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0186, BUG-0151
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-14 (the only part that holds). Not seen at runtime, found by code reading.
While the host is away (main menu, editor), a guest's Civ vehicles are run by its own game's AI (BUG-0151,
civ.away). The guest's game still skips Vehicle_KeepOnValidGround for every mirror vehicle (BUG-0186), which was made
for vehicles that stand where the host's vehicle stands. With the AI driving them, a land or sea vehicle could end up
on ground its locomotion does not allow (in water, on a spice node) and stay there.

## Root cause
Observed in code (main efd2e0ba), the player effect inferred (confidence medium, not tried):
mods/coop/civ.cpp:468 h_KeepGround: `if (g_mirror_place_fix && !g_applying && is_guest() && game::mode_id() ==
modes::kCiv && g_gv_of.count(v)) return;` has no `!local_play()` (civ.cpp:78). The other two WLD-14 claims are not
bugs: drop_foreign_selection (civ.cpp:2812, tribe.cpp:4983) keeps the ownership split in local play (a guest only
commands its own units, also while the host is away); on_member_anims (tribe.cpp:2257) has nothing to receive while
the host is away (the host sends no Tribe messages outside Tribe).

## Fix idea
Add `!local_play()` to the skip. Switch idea `coop.fix civ.away_ground off`.

## How to test
civ fixture twoinst test: the host goes to the main menu, the guest orders its sea (or land) mirror vehicle across a
coast line its locomotion does not allow; assert on the guest's game that the vehicle's cell is valid for its
locomotion (the game's own planet cell flags) after the drive. Main: it stands on invalid ground: FAIL (if the AI's own
path can produce it at all; otherwise close as not reproduced with the run).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
