---
id: BUG-0633
title: Cell: the host cannot collect the part (card) a kill dropped
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-02
role: host
versions: 0.7.8
fixed_in:
branch: fix/w46-cell-p0
commits:
tests: cell_token_probe (coop_cell_once.py --case token), coop_cell_once.py --case drop
signature:
docs: docs/re/cell.md#cell-token-collect
related: BUG-0467, BUG-0316, BUG-0632
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/Sterben-02 (the host, mark at s 211.6): "can't take a card after
a kill, Cell stage". The host's shot shows the goal "Collect / Unlock Cell Parts 3/6"; the guest's shot at s 212 shows
several silver part tokens and meat chunks.

## Root cause
Observed (the host's log): `[s 193.958] eat: from 1 gone c2935292748 (part) ... applied` (the guest swallowed a part
token of the host's pool), `[s 195.293] parts: 1 Cell part(s) of the party collected here (found by player 1)`, the
party's parts 0E2C -> 0F2C. Guest kills at s 206.3 and 206.9 (each applied twice, BUG-0632), refused at 207.5. No
part-token, collect or loot line from the host's own game between s 196 and the mark.
Inferred, weak: the token of that kill exists in one game only, or the guest took it first and the host was left with
one it cannot collect; or a side effect of the doubled break (BUG-0632).
Related: BUG-0467 (Cell parts shared, fixed 0.7.4), BUG-0316 (a guest collecting a part token of the host's mirror, not
reproduced).

## Fix
None of its own. The one co-op cause found near the report is BUG-0632 (fixed, `cell.kill_once`): the kills at
s 206.3 / 206.9 were broken twice in the host's world, each break with its own loot drop, so the host's screen held
two drops of that kill (two tokens of one part when the creature carried one). Nothing found blocks the host's
collect of a token of its own world.

## Verification
Dedicated attempt (docs/re/cell.md#cell-token-collect):
* The collect (0xE7B630, Ghidra) depends on the avatar's +0x112 / +0x113 / +0x178 / +0x17F and the token's +0x17B
  only. A guest's kill next to the host set none of the host's locks (lab 20261106-215829, 4 kills, 4 s each).
* The host collected 11 of 14 part tokens of its pool, levels 2-8 (labs 20261106-220209 and its retry); the 3
  misses had been pushed 10-20 m away (1.5-6 m/s) before the touch, which matches "cannot take it" only if the token
  keeps getting pushed off; none had a lock (`lock17b=0`).
* The player's case (a token dropped by a guest's kill) could not be made: 32 guest kills next to the host at levels
  1-4 dropped no part token, with the kill applied 1-4 times (`SPORE_FIX_OFF=cell.kill_once`, lab 20261106-222600)
  and once (lab 20261106-222027).
* The report's logs: no collect, token or loot line of the host's own game between s 196 and the mark; the party's
  parts went 0E2C -> 0F2C from the guest's collect at s 193.958 (shared, BUG-0467).
## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-06: not reproduced in fix/w46-cell-p0: the host collects tokens of its pool, a guest's kill sets no lock on it; the double loot of the doubled kill is BUG-0632 (fixed).
