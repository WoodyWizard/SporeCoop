---
id: BUG-0041
title: after a small respawn, the next level-up makes the player's cell up to twice too big
status: fixed
kind: bug
stage: Cell
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#19
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-cell
commits: e7ab004, 595e12f, b6039e6, 55b3c2a
tests: tools/tests/coop_cell_sync.py (suite cell_sync), tests/cell_tests.cpp body()
signature:
docs: docs/re/cell.md#cell-sync-robustness
related: #19
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/19 (full text, discussion).

After a knockout ends in a small respawn (half size), the player's cell grows back by eating. If one of those meals crosses a level line (every 50 food points, so almost always on the way back), the cell ends up bigger than its normal size for the new level, up to twice as big, and stays that way for the whole level. Severity: wrong behaviour (size, mass and collisions differ from what the level should be).

### Where
- `h_AddFood`: [mods/coop/cellsync.cpp#L635-L653](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L635-L653) (the growth step at L642-L644)
- `set_body`: [mods/coop/cellsync.cpp#L187-L200](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L187-L200)
- The resize check that should catch a level change: [mods/coop/cellsync.cpp#L1122-L1123](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1122-L1123)
- The small respawn: [mods/coop/cellsync.cpp#L324-L339](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L324-L339)
- SetCellSize is called inside AddFoodPoints on a level change: [docs/re/cell.md#L219-L221](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L219-L221)

### How to reproduce
1. Host and guest in the Cell stage, same pool. On the guest, note the food total (`coop.status`) and the target size (`coop.pool`, `target=`).
2. Knock the guest out far from the host and let the 10 s pass (as `tools/tests/coop_cell.py` does): it respawns small, `body=0.50`.
3. On the guest, add food until the total crosses the next multiple of 50, for example `coop.pool food 20` repeated, or through the game's eat path (`coop.pool eat c<key>`).
4. After the level-up, `coop.pool` shows `target=` larger than the size a fresh cell of that level has; after 50 more points `body=1.00` and `target=` is about twice the normal size. Compare with the host's cell, which is at the same level.

### Expected
After the level-up, the cell is the size the game gave it for the new level (scaled by `g_body` while it is still growing back), and at `body=1.00` it is exactly the game's size for its level.

## Root cause
**What happens**

1. The respawn calls `set_body(a, kSmall)`: `g_body = 0.5`, the target size `+0xB4` is halved and `g_body_size` remembers it.
2. The player eats, so `h_AddFood` runs. It first calls the original:
   ```cpp
   o_AddFood(amount, plant);
   ...
   if (a && amount > 0 && g_body < 1.f)
       set_body(a, std::min(1.f, g_body + amount * 0.01f));
   ```
3. If the meal crosses a level line, the original AddFoodPoints (0xE7CE10) calls SetCellSize (0xE57190) for the avatar before returning. `+0xB4` is now the full size `N` of the new level (docs/re/cell.md section 6).
4. `set_body` assumes `+0xB4` is still the small size:
   ```cpp
   float full = g_body < 1.f ? cur / g_body : cur;   // N / 0.5 = 2N
   float want = full * factor;                        // 2N * 0.6 = 1.2N
   ```
   The target becomes 1.2N, mass is scaled to match, `g_body` becomes 0.6 and `g_body_size` becomes 1.2N.
5. `symbiosis_tick` compares `+0xB4` with `g_body_size` to notice the game resizing the cell. They now agree, so the reset to `g_body = 1` never happens.
6. Each further meal computes `full = 1.2N / 0.6 = 2N`. By the time `g_body` reaches 1, the cell's target size is 2N. It stays at 2N until the next level-up, which then happens with `g_body == 1`, so `set_body` is skipped and the game sets the right size again.

Thresholds and the regrowth both use 50 points, so unless the respawn happens exactly on a line, the way back always crosses one. Any regrowth starting below 1.0 overshoots by `1 / g_body` at the crossing.

### Confidence
CONFIRMED by reading the full path, based on the ✔ finding in docs/re/cell.md section 6 that AddFoodPoints calls SetCellSize for the avatar on a level change. Not run in the game. `coop_cell.py` checks only the `body` counter after `coop.pool food 20`, which does not cross a level line, so it does not see this.

## Fix
Fixed on fix/w2-cell (e7ab004). `h_AddFood` reads the avatar's target size `+0xB4` before the
original AddFoodPoints; when the call changed it (a level line crossed: the game sized the avatar
for the new level), the body factor becomes 1 and nothing is rescaled (`cell_logic::regrow`).
Old behaviour: `coop.pool fix bodylevel 0`.

The review suggested:

In `h_AddFood`, read `+0xB4` before calling `o_AddFood`. If it changed afterwards, the game resized the avatar for a new level: either set `g_body = 1` and skip `set_body` (same as the resize check does), or keep the regrowth and compute `full` from the new value instead of dividing it by `g_body`. Keep the old behaviour behind `coop.fix bodylevel off`.

## Verification
`coop_cell_sync.py`: the guest at 10 points over a line, `coop.pool small` (what the small respawn
does), then 41 points over the next line; the guest's target size must grow by the host's ratio.
FAIL with `SPORE_CELL_FIX=0` (out/lab/20261027-231147): host 0.55 -> 0.82, guest 0.55 -> 1.5 (body 0.91). PASS
(out/lab/20261027-231004, out/lab/20261027-231523): guest 0.82, body 1.0. Unit test `body()` (make test).

The planned test was:

Extend `tools/tests/coop_cell.py` (or a new `coop_cell_regrow.py`): after the small respawn, raise the food total across a level line with the game's AddFoodPoints, then up to the point where `body` is 1.0. Assert on the game's own fields: the guest's target size `+0xB4` (and `+0x58` once it has settled) is within 5 % of the host's cell at the same level. Read both through `coop.pool`, and also check the guest's size on the host's puppet, which shows what the partner sees. It fails today (about 2x) and passes with the fix.

### Related
- docs/re/cell.md section 6, "Avatar move / respawn, regen": [docs/re/cell.md#L205](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L205)
- Level table and AddFoodPoints: [docs/re/cell.md#L173-L177](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L173-L177)

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #19.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-cell (bug wave 2); test FAIL without the fix, PASS with it.
- 2026-10-28: released in 0.7.3.0.
