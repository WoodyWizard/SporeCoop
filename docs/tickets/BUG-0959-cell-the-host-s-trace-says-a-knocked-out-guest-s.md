---
id: BUG-0959
title: Cell: the host's trace says a knocked-out guest's kill was applied before it refuses it
status: fixed
kind: bug
stage: Cell
reported: 2026-11-12
source: fix/w48-cell lab 2026-11-12 (coop_cell_kill_refused.py)
role: host
versions: 
fixed_in: 0.7.13
branch: fix/w48-pass
commits: a52c61de
tests: cell_kill_refused, cell_tests (kill_word)
signature:
docs: docs/re/cell.md#cell-c1-0713
related: BUG-0821 BUG-0714
---

## Report
The host's trace line for a guest's kill (MSG_CELL_EAT) says "eat: from 1 kill c<key> (creature): applied" for a kill
it refuses in the next statement because the guest is knocked out ("bite: from 1 kill c<key> refused: that player is
knocked out", lab 20261112-065711 and 20261112-070304): a support log reads as if the creature had been killed.

## Root cause
Code: cellsync.cpp MSG_CELL_EAT handler, the trace::ev for a kill decides its word from `again` / `alive` before the
knockout check (`guest_knocked_out(from)`) that refuses it.

## Fix
cellsync.cpp `cell.ko_trace`: the knockout refusal is decided before the trace line (`ko`), and the line's word comes from
`cell_logic::kill_word(again, alive, knocked_out)`: "refused (knocked out)", reason `from kill ko`. The refusal itself
is unchanged. Switch `coop.fix cell.ko_trace off` (0.7.12 wording).

## Verification
`coop_cell_kill_refused.py` step 2 now reads the host's log lines for that kill: none may end in ": applied", one must
say "refused (knocked out)". Unit: tests/cell_tests.cpp `kill_word_ko` (make test).
* FAIL with `SPORE_FIX_OFF=cell.ko_trace`: lab 20261112-130646 ("eat: from 1 kill c2134179892 (creature): applied").
* PASS: lab 20261112-130504 ("... : refused (knocked out)").

## How to test
`coop_cell_kill_refused.py` (suite `cell_kill_refused`).

## History
- 2026-11-12: opened.
- 2026-11-12 (fix/w48-cell): opened.
- 2026-11-12 (fix/w48-pass): fixed (cell.ko_trace), FAIL and PASS runs in Verification.
- 2026-11-12: released in 0.7.13.
