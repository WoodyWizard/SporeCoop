---
id: BUG-1144
title: Space: a caught fault in the game's cEmpire_MakeContact+0x3C7 (PlanetPopulate) when a guest loads its own save after a co-op session (space_allies, once in 4 runs)
status: open
kind: crash
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x00C32817 SporeCoop::cEmpire_MakeContact+0x3C7 (data 0x000000B8), called from SporeCoop::PlanetPopulate_ForStage+0x8BE
docs:
related: BUG-1133
---

## Report
space_allies on feat/w51-space, run 20261115-030136 (lane 6, cold): after the host left, the guest went to the galaxy
menu and loaded its own save (Game1/Madas); during the planet populate of that load the game faulted (caught, the test
went on and every check passed; the lab fails it on the fault). Guest log: exception 0xC0000005 at 0x00C32817
SporeCoop::cEmpire_MakeContact+0x3C7, ecx=esi=edi=ebp=0, called from PlanetPopulate_ForStage+0x8BE, tid main.

## Root cause
Not analysed. Not reproduced: the same test passed on feat/w51-space in run 20261115-032648 and on rel/0.7.15 d0cdc8ea
(worktree feat/w51-space-base) in run 20261115-032050. Signature not seen in any earlier lab log of the worktrees.
Candidates: the allies mirror empire (800021CE) undone at the session end and a contact record left pointing at it
(space_diplomacy's h_Contact hook is on the path: it calls the original first).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
