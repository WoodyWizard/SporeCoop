---
id: BUG-0820
title: Space finale: the galactic core waits forever for a partner who already entered and flew away
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-space
commits: fff29d37
tests: space_finale_gone
signature:
docs: docs/re/space.md#space-s1-0713
related: BUG-0198
---

## Report
A player reaching the core is told it 'waits for your partners: A' although A went in earlier; the wait never ends, only F9 'Enter now' gets it in. Found by reading the code.

## Root cause
Observed in the code, confidence high. space_finale.cpp:268-279 missing() lists every partner in Space whose zone is not Z_GATE; it ignores F_ENTERED and g_reached_by. h_Enter (:294-309) blocks while the list is not empty. BUG-0198 lists 'a partner who never comes', not this. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
In missing() skip the partners with F_ENTERED or an entry in g_reached_by.

## How to test
Host `coop.space finale goto core enter`, `coop.space finale alone`, `coop.space finale enter`, then fly away; guest `coop.space finale goto core enter`, `coop.space finale enter`: FAIL = status waiting=1 entered=0 for 60 s; PASS = the guest enters.

## Fix
missing() (the partners the core waits for) skips a partner whose state has the entered flag (F_ENTERED, the game's
kFlagEnteredCore on its machine) or whose K_REACHED came in this session (g_reached_by). Root cause confirmed at
runtime: with the switch off the guest at the core stayed `waiting=1 entered=0` while the host (entered, then at a
star in the Grox zone, zone 2) was listed as missing. Switch `coop.fix space.finale_entered off` (0.7.12).
`coop.space finale` shows `not_waited=` (frames in which an entered partner was not waited for).

## Verification
`coop_space_finale_gone.py` (suite `space_finale_gone`): both get the drive, the host enters the core alone
(`finale alone`, `finale enter`), presses through the cinematic, flies away (`finale goto near`); the guest flies to
the core and zooms in (`finale enter`).
* FAIL with `SPORE_FIX_OFF=space.finale_entered`: lab 20261112-033047 (the guest `entered=0 waiting=1`).
* PASS: lab 20261112-033051 (the guest enters the core).
* PASS again: lab 20261112-035922; the area's older tests pass on the branch (lab 20261112-041200, 20261112-041012).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (an entered partner is not waited for; switch space.finale_entered).
- 2026-11-12: released in 0.7.13.
