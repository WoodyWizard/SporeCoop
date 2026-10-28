---
id: BUG-0070
title: the knockout state is not reset on disconnect or stage leave (cell stays invulnerable or translucent)
status: fixed
kind: bug
stage: Cell
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#48
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-cell
commits: e7ab004, 595e12f, b6039e6, 55b3c2a
tests: tools/tests/coop_cell_sync.py (suite cell_sync)
signature:
docs: docs/re/cell.md#cell-sync-robustness
related: #48
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/48 (full text, discussion).

The local knockout state (`g_downed`, `g_invuln_until`, the avatar's invulnerability `+0x111` and opacity `+0xA4`) is only partly reset when the session ends, and not at all when the player leaves the Cell stage. A player whose host quits within 2-3 s after a revive or respawn keeps an invulnerable cell for the rest of the stage. A player who was knocked out when the session ended keeps a translucent cell with 1 hp. A player knocked out when the stage changed is moved and respawned small in the next Cell game. Severity: wrong behaviour.

### Where
- `on_session_end`: [mods/coop/cellsync.cpp#L1338-L1353](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1338-L1353)
- Stage leave handling in `tick`, and the early return while disconnected: [mods/coop/cellsync.cpp#L1190-L1207](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1190-L1207)
- Invulnerability cleared only by the tick: [mods/coop/cellsync.cpp#L1144-L1147](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1144-L1147)
- Knockout, revive and respawn set it: [mods/coop/cellsync.cpp#L270-L339](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L270-L339)

### How to reproduce
Case A (stuck invulnerability):
1. Host and guest in the Cell stage, same pool. Knock out the guest next to the host and let the host revive it (as in `tools/tests/coop_cell.py`).
2. Within 2 s, quit the host's game (or `tools/spore.sh kill` its lane).
3. On the guest, `coop.pool` keeps showing `invuln=1` for the avatar (`coop.pool prey` prints it). Let an NPC bite the guest: its hp does not drop.

Case B (translucent): knock out the guest, then kill the host within the 10 s. The guest's `coop.pool` shows `avatar_opacity=0.35` from then on.

Case C (stage change): knock out the guest, then have the host load another Cell save within 10 s so the guest follows. In the new game the guest's cell jumps to the old position and respawns small.

### Expected
When the session ends or the Cell stage is left, the knockout ends: the cell is opaque, vulnerable and keeps its hp and position. A new Cell game starts with no knockout.

## Root cause
**What happens**

1. A revive sets `+0x111 = 1` and `g_invuln_until = now + 2`; a respawn sets `now + 3`. The flag is cleared only in `symbiosis_tick`:
   ```cpp
   if (g_invuln_until > 0 && now > g_invuln_until) {
       F<uint8_t>(a, kInvuln) = 0;
   ```
2. `tick` returns early when `!C.sess->connected()`, so after a disconnect `symbiosis_tick` never runs again.
3. `on_session_end` clears the flag only while knocked out:
   ```cpp
   if (g_downed) {
       g_downed = false;
       if (uint8_t* a = in_cell() ? avatar() : nullptr)
           F<uint8_t>(a, kInvuln) = 0;
   }
   ```
   If the session ends inside the 2-3 s window after a revive or respawn, `g_downed` is already false, so `+0x111` stays 1 and nothing clears it. The player's cell takes no damage for the rest of the stage, playing alone.
4. While knocked out, `downed_tick` sets opacity `+0xA4` to 0.35 and hp to 1 every tick. `on_session_end` resets neither, so after a disconnect mid-knockout the cell stays translucent (and at 1 hp).
5. The stage-leave block in `tick` (`!cell && was_in_cell`) clears the mirrors and puppets but not `g_downed`, `g_down_since`, `g_down_pos` or `g_invuln_until`. If the player was knocked out when the stage changed (the host loads another save, the player quits to the menu), the next Cell game in the same session runs `downed_tick` on the new avatar: it is moved to `g_down_pos` from the old game, set to 1 hp and invulnerable. Because more than 10 s have passed, it then respawns at once as a small cell with "respawned small" sent to everybody.

PR #5 (BUG-0028) adds an `on_session_end` call to `do_leave`, but `on_session_end` has the gaps above, so that PR does not fix this.

### Confidence
CONFIRMED by reading the full path. Not run in the game. Not checked: whether the game itself resets `+0x111` or `+0xA4` on the avatar later (a level change or rebuild may).

## Fix
Fixed on fix/w2-cell (e7ab004). `reset_knockout()`: in the Cell stage with an avatar, invulnerability
`+0x111` cleared (when knocked out or within the post-revive window), opacity `+0xA4` 1 and hp at
least 1 after a knockout; then `g_downed`, `g_hold`, `g_down_since`, `g_invuln_until` and `g_ko`
are cleared. Called from `on_session_end` and from the stage-leave block of `tick` (the state only,
the pool is gone there). Old behaviour: `coop.pool fix koreset 0`.

The review suggested:

Add `reset_knockout()`: if in the Cell stage and the avatar exists, set `+0x111 = 0`, `+0xA4 = 1`, and hp to at least 1 (keep the current hp when it is higher). Then clear `g_downed`, `g_hold`, `g_invuln_until` and the `KO_OUT` entries in `g_ko`. Call it from `on_session_end` and from the stage-leave block in `tick` (before the pool is gone, or skip the avatar writes when `in_cell()` is false). Keep the old behaviour behind `coop.fix koreset off`.

## Verification
`coop_cell_sync.py`: case A, the host revives the guest and the guest leaves at once (`coop.leave`,
the same session-end path as the host quitting); 4 s later the avatar's `+0x111`. Case B, the guest
leaves 1 s into a knockout; its avatar's `+0xA4`. FAIL with `SPORE_CELL_FIX=0` (out/lab/20261027-231147): invuln=1,
then opacity 0.35. PASS (out/lab/20261027-231004, out/lab/20261027-231523): invuln 0, opacity 1.00, hp 1. Case C (a stage change
during a knockout) is covered by the reset in the stage-leave block, not run in the game.

The planned test was:

`tools/tests/coop_cell_ko_reset.py` on `twoinst.start`: a revive next to the host, then the host's game is killed within 1 s. On the guest, wait 5 s and assert the game's own state through `coop.pool`: `+0x111` (invuln) is 0 and `+0xA4` (avatar_opacity) is 1.0. Then let a host-free bite through `coop.pool kill` on the guest's own cell, or `coop.pool hp`, and check that hp really drops. A second case kills the host during the knockout and checks opacity 1.0. Both fail today and pass with the fix. The lab must ignore the host's crash report for the deliberately killed game only.

### Related
- #39 (the remote side of the same state is not cleared when a player leaves)
- PR #5 / BUG-0028 (do_leave skips `cellsync::on_session_end`)
- Respawn recipe: [docs/re/cell.md#L229-L233](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L229-L233)
- After the Cell stage the pool is gone (section 13): [docs/re/cell.md#L529](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L529)

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #48.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-cell (bug wave 2); test FAIL without the fix, PASS with it.
- 2026-10-28: released in 0.7.3.0.
