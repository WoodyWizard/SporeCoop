---
id: BUG-0067
title: chiefs stay 1.3x big after the session ends, and the epic creature state survives a leave or a load
status: fixed
kind: bug
stage: Tribe
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#45
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-civ
commits: ba9dc46, 421bfe0
tests: tribe_epic, tribe_chief_removed
signature:
docs: docs/re/tribe.md#tribe-lifetime, docs/re/tribe.md#tribe-epic-lifetime
related: #45
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/45 (full text, discussion).

When a co-op session ends in the Tribe stage, every player's chief keeps the 1.3x size the mod gave it, in the solo game that goes on (and in saves made then). The epic creature pointer (`g_epic`), its hitters and the pincer table are never reset either. After a leave or a load, the host can read a freed creature, award "epic creature down: +30 food" in a different world, or never spawn an epic again. Severity: wrong behaviour (minor), possible read of freed memory.

### Where
- `on_session_end` / `on_world_change` clear only the member tables: [tribe.cpp#L1116-L1130](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1116-L1130)
- The chief scale is applied and restored only inside `chiefs_tick`, which only runs while `active()`: [tribe.cpp#L776-L815](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L776-L815), [tribe.cpp#L1087-L1097](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1087-L1097)
- Epic state: [tribe.cpp#L711-L719](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L711-L719), `epic_tick`: [tribe.cpp#L1014-L1031](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L1014-L1031)
- `do_leave` calls neither `tribe::on_session_end` nor `civ::on_session_end`: [coop.cpp#L1761-L1771](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1761-L1771) (only the disconnect callback does: [coop.cpp#L1488-L1508](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1488-L1508))

### How to reproduce
Chief scale:
1. Tribe stage, host + 1 guest (fixture of `coop_tribe.py`). Wait until `coop.tribe` status shows `chiefs= 0:#a 1:#b`; the two chiefs are visibly bigger.
2. The guest runs `coop.leave` (or the host ends the session).
3. On either machine, the chiefs are still 1.3x. Read the spatial scale of member `#a` (spatial `+0xC0` object, scale field) and compare it with the citizen's desired scale (citizen vt 0x80 with arg 0): the ratio is 1.3 and stays so.

Epic:
1. Tribe stage, host + 1 guest. `coop.tribe epic` on the host spawns it (`epic=<ptr>`).
2. The host loads the tribe fixture again while connected.
3. Watch the host's notices and `coop.tribe` status. On main you get "epic creature down: +30 food" (`epic_kills` +1, food +30) without any fight, or `epic` stays non-null and `epics` never grows again.

### Expected
When a session ends, every chief gets its own scale back. On a session end or a world change, all Tribe co-op tables are cleared: `g_chief`, `g_chief_scale`, `g_chief_saved`, `g_epic`, `g_epic_hitters`, `g_epic_next`, `g_pincer`, `g_pincer_told`.

## Root cause
**What happens**

Chief scale:
1. `chiefs_tick` picks each player's chief (lowest-id adult). It stores the game's desired scale in `g_chief_scale` and sets `SetScale(desired * 1.3)` (spatial vt 0x40).
2. The scale is put back only when a chief stops being a chief, inside the next `chiefs_tick`.
3. When the session ends (disconnect, `coop.leave`, or the host closing), `tick()` returns at once (`!active()`), so `chiefs_tick` never runs again. No session-end function restores the scale or clears `g_chief` / `g_chief_scale`.
4. The chiefs stay 1.3x for the rest of the solo game. A save made then has the oversized citizens.

Epic:
1. `spawn_epic` stores the creature in `g_epic`. `epic_tick` later tests `!game::object_alive(g_epic) || game::is_dead(g_epic)`.
2. Neither `on_session_end` nor `on_world_change` resets `g_epic`, `g_epic_hitters` or `g_pincer`.
3. After a load (or `coop.leave`, a load, then hosting again), `g_epic` points into the old world. `object_alive` only reads byte `+0x21` of that memory:
   - If the old destroyed flag is still there, the host awards `bonus_food(30)` to the new world's player tribe with the notice "epic creature down: +30 food".
   - If the address now holds another live object, `g_epic` stays non-null and `spawn_epic` refuses (`|| g_epic`), so no epic ever comes again in that session. `is_dead` keeps reading a creature field of whatever object is there.
4. `g_pincer` / `g_pincer_told` / `g_chief_saved` keep growing with keys from old worlds. They are only compared, so this is a small leak, not a crash.

The general "one session-end helper that every leave path calls" is filed separately by the core review. This issue is about what Tribe's own reset must cover.

### Confidence
- Chief scale: CONFIRMED by reading the full path (no code restores it outside `chiefs_tick`).
- Epic: CONFIRMED by reading that the state survives. The exact outcome (food award or no more epics) depends on what the freed memory holds: PLAUSIBLE.
- Not run on the lab.

## Fix
Mostly fixed already by BUG-0081 (fix/w1-tribe-civ, ba9dc46): `tribe::on_session_end` restores the
chiefs' size and clears the chief, pincer and epic tables; `on_world_change` clears them;
`end_session_modules` (BUG-0033) runs it on every way a session ends, `coop.leave` included.
Left over and fixed on fix/w3-civ (421bfe0): `g_epic_next` is reset (the first epic of the next
session comes after 8 minutes again), and the live co-op epic is destroyed at the session end
(BUG-0121) instead of staying in the solo game.

## Verification
`coop_tribe_epic.py` (suite `tribe_epic`), part 3: both chiefs are 1.3x in the game (spatial scale /
the citizen's desired scale, `coop.tribe scales`); the guest leaves, then the host ends its session.
FAIL with `--nofix` (`coop.tribe fix chiefs 0`: ratio 1.30 on both after the leave), PASS with the fix
(no member above 1.15), lane 7, 2 of 2. Note: the host keeps its chief while it hosts alone (its
session goes on), which is right.

### Related
- docs/re/tribe.md, section "Co-op features (pincer, duet, healers, chief, epic)".
- Same missing-reset pattern in Civ (`g_boost` and others, separate issues).
- The chief scale restore uses `object_alive` only (separate issue).
- Saves holding our objects: BUG-0020.

Run evidence (2026-10-28, BUG-0215, found afterwards in the lab folders and agent logs): FAIL with the fix off: `epic_nofix.log`, `epic_nofix2.log` (fix/w3-civ, chiefs off: max ratio 1.30), a standalone test run whose log stayed in the agent's scratch folder (not kept in out/lab). PASS: lab run out/lab/20261028-014643 on fix/w3-civ (tribe_epic ok); later full runs on main pass too.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #45.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: re-checked on fix/w3-civ: the chief part was fixed by BUG-0081 (ba9dc46); epic timer and the live epic at the session end fixed (421bfe0), test tribe_epic.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: Verification names the FAIL log and the PASS lab run (BUG-0215).
