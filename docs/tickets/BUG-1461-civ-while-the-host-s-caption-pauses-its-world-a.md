---
id: BUG-1461
title: Civ: while the host's caption pauses its world, a guest's NPC nation AI spends the money the civ_money row copied; the row puts it back every 0.5 s (wsync_civ read 0 at 3 s)
status: open
kind: desync
stage: Civ
reported: 2026-11-23
source: lab
role: 
versions: 0.7.15, 0.7.16
fixed_in:
branch:
commits:
tests: wsync_civ (tools/tests/coop_wsync_civ.py)
signature:
docs:
related: BUG-0205
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, mod code 5376d388): wsync_civ "FAIL the guest's game: the nation has the
host's money (3 s)": `civmoney 000021CC: 0 -> 1500`, `3 s later: ... host 1500.0, guest 0.0`; the 25 s check passed.

## Root cause
Old behaviour, seen by a single read (a flake of the test). It is an NPC nation's money (the wsync row civ_money,
wsync.cpp, unchanged since before 0.7.16), not the player's money (#205, per-player money, is 0.7.17; the player's
money is still shared in this run: "Alice earned 40 money (shared)"). The test's capture puts the host into a caption:
the host's world is paused 04:56:51.919-04:56:59.921 (`busy=cinematic,input_lock,paused`), the guest's runs (no busy):
its copy of the NPC nation's AI spends the copied 1500 and the row puts it back every 0.5 s (civ_money applied=9
reapplied=8 in this run and in the passing ones). The one read at exactly 3 s fell on a 0. Passed in 20261122-194210,
20261121-003202, 20261120-133703 (1500 / 1500); 1 of 9 failed on rel/main. Not a 0.7.16 regression.

## Fix
None in 0.7.16 (low: the values agree within 0.5 s and by 25 s). Idea: the guest's NPC nation does not spend while
the host's world stands (or the row holds the value then). The test stays strict (a single read at 3 s); a test change
that waits for any agreeing read would hide this.

## Verification
Open.

## History
- 2026-11-23: opened (fix/w65-tests triage).
