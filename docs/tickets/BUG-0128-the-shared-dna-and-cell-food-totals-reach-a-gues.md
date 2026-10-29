---
id: BUG-0128
title: the shared DNA and Cell food totals reach a guest only when they change, and are lost while it loads
status: fixed
kind: desync
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#107
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-cell
commits: e7ab004, 595e12f, b6039e6, 55b3c2a, b5ebdb2
tests: tools/tests/coop_cell_sync.py (suite cell_sync), tools/tests/coop_dna_resend.py (suite dna_resend)
signature:
docs: docs/re/cell.md#cell-sync-robustness
related: #107
switch: pool.resend
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/107 (full text, discussion).

The host sends the shared DNA total (`MSG_DNA`) and the Cell stage food total (`MSG_CELL_FOOD`) only when the value changes. Guests drop both while they are not in the stage with an avatar: loading, in an editor, receiving the world. Neither is ever resent, so the guest shows a stale total until the host's value changes again. A guest that joins during the Cell stage gets no food total at all until the host's food changes. Severity: wrong behaviour (low).

### Where
- DNA, sent only on a change: [mods/coop/coop.cpp#L1110-L1120](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1110-L1120)
- DNA, reset when a player joins: [mods/coop/coop.cpp#L1511](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1511)
- DNA, the guest drops it without an avatar: [mods/coop/coop.cpp#L1543-L1552](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1543-L1552); `game::avatar()` is null outside the creature modes: [mods/coop/game.cpp#L338-L351](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L338-L351)
- Food, sent only on a change: [mods/coop/cellsync.cpp#L919-L931](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L919-L931). `g_food_sent` is reset only by a late eat and at the session end, never on a join: [L101](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L101), [L1302](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1302), [L1351](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1351)
- Food, every cell message is dropped outside the Cell stage or without an avatar: [mods/coop/cellsync.cpp#L1209-L1212](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1209-L1212); the guest applies the total at [L1316-L1334](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1316-L1334)
- For comparison, `MSG_RES` (tribe food, money) is also resent every 2 s: [mods/coop/shared.cpp#L157-L168](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/shared.cpp#L157-L168)

### How to reproduce
1. Cell stage, two lanes. The host eats until food is 40 and keeps eating while a guest joins and loads.
2. When the guest is in the pool, the host stops eating.
3. The guest's food (`coop.cell` status, `+0x1C` of the cell game) is lower than the host's and stays so until the host eats again.

The same with DNA in the Creature stage (host gains DNA during the guest's load, then stands still).

### Expected
A guest that enters the stage gets the host's current totals within about a second.

## Root cause
**What happens**

DNA:
1. A guest joins: `g_dna_sent = -1`, so the host sends its total at once.
2. The guest is still in the menu or loading, has no avatar, and the message is dropped:
   ```cpp
   if (r.ok() && game::avatar() && !g_host_away) { ... }
   ```
3. The guest's world is the host's save from the start of the transfer. DNA the host earned during the transfer and load is missing on the guest until the host's total changes again.
4. The same happens after an editor: the host leaves its editor first and its new total is sent while the guest is still in its own editor.

Food (Cell):
1. The host plays Cell, so `g_food_sent` equals its food.
2. A guest joins. Nothing resets `g_food_sent`, so no `MSG_CELL_FOOD` is sent until the host eats again. Food the host gained after the share save is missing on the guest.
3. The same happens after any time the guest spent outside the Cell stage: totals sent in that window were dropped by the `!in_cell() || !avatar()` gate and are not repeated.

It heals at the next change of the host's total. A guest eating also triggers one, through `MSG_CELL_FOOD`/`MSG_DNA_ADD` to the host and its new total back. While the host stands still, the guest's progress bar and DNA stay wrong.

### Confidence
CONFIRMED by reading; checked independently by a second reviewer (low). Not run in the game.

## Fix
Fixed on fix/w2-cell (e7ab004, b5ebdb2). The host sends the Cell food total to each guest when it
(or that guest's meal acknowledgement) changed, at once to a guest it has not sent to yet (a
newcomer), and again every 2 s; the DNA total (coop.cpp) also every 2 s. A guest that dropped a
total while loading or in an editor has the host's within 2 s. The per-guest bookkeeping goes with
the player (`cellsync::on_player_leave`) and the session. Old behaviour: `coop.pool fix resend 0`
(works in every stage). Not touched: the shared money pool (fix/w2-civ owns BUG-0049/0106).

The review suggested:

- Resend both totals periodically (every 2 s, like `MSG_RES`), or send them when a guest's state first shows the stage mode.
- Reset `g_food_sent` in `player_join` as `g_dna_sent` is.

## Verification
Food: `coop_cell_sync.py` changes the guest's own total by 1 point (`coop.pool foodlocal`, as a total
it missed) and waits 4 s while the host's total stays the same. FAIL with `SPORE_CELL_FIX=0`
(out/lab/20261027-231147): guest 1, host 0. PASS (out/lab/20261027-231004, out/lab/20261027-231523).
DNA: `coop_dna_resend.py` (Creature) adds 7 DNA to the guest's own total (`coop.dna local 7`).
FAIL with `SPORE_CELL_FIX=0` (out/lab/20261027-232647); PASS (out/lab/20261027-232430).

The planned test was:

`coop_cell_*` on two lanes:
1. The host changes its food while the guest loads (`coop.res add`-style debug, or eating), then stands still.
2. Assert within 3 s, on the guest, that the game's food value equals the host's.

It FAILS on main and PASSES with the fix. The same for DNA in Creature.

### Related
#29 (guest food total and level lines), #27 (offline money and food), #39 (other one-shot cell messages dropped outside the Cell stage).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #107.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-cell (bug wave 2); test FAIL without the fix, PASS with it.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: pool.resend (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
