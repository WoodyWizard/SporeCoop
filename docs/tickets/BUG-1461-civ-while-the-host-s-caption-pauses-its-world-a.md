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
branch: feat/w68-wallet
commits: 162b29f0 ebe0e45a
tests: wsync_civ (tools/tests/coop_wsync_civ.py), tools/tests/coop_civ_money_pause.py (civ_money_pause, twin civ_money_pause_off)
signature:
docs: docs/re/civ.md#civ-own-money
related: BUG-0205, BUG-1468
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, mod code 5376d388): wsync_civ "FAIL the guest's game: the nation has the
host's money (3 s)": `civmoney 000021CC: 0 -> 1500`, `3 s later: ... host 1500.0, guest 0.0`; the 25 s check passed.

## Root cause
Old behaviour (unchanged since before 0.7.16); the mechanism is now known from the disassembly (objdump 2026-11-23,
docs/re/civ.md#civ-own-money): an NPC nation's AI update (Civilization_AI 0xBFBD30, every second, at 0xBFBF72) calls
CivAI_WealthToBudgets 0xBF7370, which moves all of a positive +0x98 into the domestic / expansion budgets +0x4AC /
+0x4B0 and sets +0x98 to 0. On the host the AI stands while its caption pauses the world; on the guest it runs: the
1500 the row civ_money wrote was in the budgets within a second, the row wrote it again every 0.5 s (applied=9
reapplied=8), and a single read at 3 s fell on a 0. (The guest's blocked AI spawns, BUG-0188, then spend the budgets:
the purchases pay after the spawn without looking at its result.)

## Fix
`coop.fix civ.npc_money_hold` (on): civ.cpp hooks CivAI_WealthToBudgets (.sym verified: fastcall, plain ret 0xBF74CA)
and, on a guest in the host's Civ world, skips it for NPC nations while the host's world stands
(host_busy::host_stands(): the host's last MSG_HOST_BUSY is paused / editor / away for this guest's stage and world).
The row's money then stays where it put it until the host's AI runs again. Off: 0.7.16.1 (`coop.civ` money:
npc_money_held=).

## Verification
Lab: pending night run: civ_money_pause (a capture caption on the host, 1500 to a nation: 8 reads in 4 s, every
guest read the host's) and twin civ_money_pause_off (SPORE_FIX_OFF=civ.npc_money_hold: reads of 0, must FAIL);
regressions wsync_civ (repeat 3), civ_caption, civ_ai_spawn.

## History
- 2026-11-23: opened (fix/w65-tests triage).
- 2026-11-23: implemented on feat/w68-wallet (commits 162b29f0, ebe0e45a and after); unit tests pass; lab tests and twins written for the night run.
