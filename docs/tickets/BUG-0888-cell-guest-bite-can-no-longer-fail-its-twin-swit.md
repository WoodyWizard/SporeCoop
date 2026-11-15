---
id: BUG-0888
title: cell_guest_bite can no longer fail: its twin switches pool.guestbite_type and guestbite are dead code since cell.guest_hit_game (BUG-0714)
status: fixed
kind: test
stage: Cell
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-small
commits: b78132b3
tests: cell_guest_bite
signature:
docs: docs/06-testing.md#testing-p1-g1
related: BUG-0240, BUG-0078, BUG-0714, BUG-0428
---

## Report
No player symptom today, but the regression test of BUG-0078 and BUG-0240 no longer proves anything: `SPORE_FIX_OFF=pool.guestbite_type` (named in the test's docstring as the run that must FAIL at step 6) and `SPORE_CELL_FIX=0` (`coop.pool fix guestbite 0`) both switch a code path the default build never reaches, so a regression in the fallback gate would pass the lab unseen. docs/known-issues.md#issue-cell-guest-bite-lock (the hurt-lock part left open by BUG-0240) is probably out of date too: BUG-0714 now runs the game's own Bite / DamageCell, which sets the hurt marks.

Severity: medium. Confidence: high.

## Root cause
* Observed (code): mods/coop/cellsync.cpp:4107 runs `guest_attack(...)` (cell.guest_hit_game, on by default) and returns before the BUG-0078 / BUG-0240 gate at cellsync.cpp:4117-4126. guest_attack (cellsync.cpp:1517-1556) replays the hit through the game's DamageCell (1536), whose own IsDamageable spares rocks and meat.
* Observed (code): `g_fix_guestbite_type` and `g_fix_guestbite` are read only in that fallback (4118, 4119, 4130), which runs only when cell.guest_hit_game is off or the game functions are missing.
* Observed (docs): tools/tests/coop_cell_guest_bite.py:17 and :24 still name the old twin runs; tools/tests/suite.py:105 says "SPORE_CELL_FIX=0 fails". BUG-0714 ran cell_guest_bite only as a PASS.
* Inferred (high, not run): a fresh run with `SPORE_FIX_OFF=pool.guestbite_type` passes.

## Fix idea
Make the test's FAIL twin `SPORE_FIX_OFF=cell.guest_hit_game,pool.guestbite_type` (and `...,guestbite` for BUG-0078) so the fallback gate stays covered; or remove the fallback gate and its switches and make cell_guest_bite a check of guest_attack, with `cell.guest_hit_game off` as the FAIL run. Update the docstring and the suite comment; close or correct issue-cell-guest-bite-lock once a host bite and a replayed guest bite are measured to set the same lock bytes.

## How to test
Run cell_guest_bite three ways: (1) `SPORE_FIX_OFF=pool.guestbite_type` alone: expected PASS today (the twin is dead); (2) `SPORE_FIX_OFF=cell.guest_hit_game,pool.guestbite_type`: must FAIL (rock / meat hp 60 -> 54 in the host's game after the guest's bite); (3) all on: PASS. Every check reads the victim's hp in the host's game.

## Fix
No mod change (a test fix). Confirmed: `SPORE_FIX_OFF=pool.guestbite_type` alone passes (the gate is read on the hand-made path only, cell.guest_hit_game replays a guest's bite through the game's Bite / DamageCell whose IsDamageable spares rocks and meat by itself). The test's twins now name the live path: `SPORE_FIX_OFF=cell.guest_hit_game,pool.guestbite_type` (BUG-0240's rock / meat) and SPORE_CELL_FIX=0, which now also switches cell.guest_hit_game off on the host (BUG-0078's spare / invulnerable steps; restored at the end so a warm lane does not keep it). Docstring and suite comment say so. The default path (the game's own DamageCell) is what the all-on run checks. Not done: the hurt-lock bytes of a host bite against a replayed guest bite (docs/known-issues.md#issue-cell-guest-bite-lock stays as it is).

## Verification
cell_guest_bite (every check reads the victim's hp in the host's game):
* the dead twin: SPORE_FIX_OFF=pool.guestbite_type alone PASSES: out/lab/20261112-115921 (as the ticket said).
* FAIL with SPORE_FIX_OFF=cell.guest_hit_game,pool.guestbite_type: out/lab/20261112-115922 (rock 60 -> 54, meat 60 -> 54 in the host's game after the guest's bite).
* PASS: out/lab/20261112-123409, out/lab/20261112-124831.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0240. Severity medium, confidence high.
- 2026-11-12 (fix/w48-small): the twin that fails named in the test and the suite; SPORE_CELL_FIX=0 made live.
- 2026-11-12: released in 0.7.13.
