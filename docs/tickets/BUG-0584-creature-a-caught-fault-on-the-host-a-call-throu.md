---
id: BUG-0584
title: Creature: a caught fault on the host, a call through null from 0xB097C4 (after 44 min of a long session)
status: open
kind: crash
stage: Creature
reported: 2026-11-04
source: long_creature (D0), lab 20261104-214938
role: host
versions: 0.7.6
fixed_in:
branch: fix/w43-crashes
commits:
tests:
signature: 0xC0000005 at 0x00000000, calls 00B097C4 00B023CC 00B04B24 00B0586D 00B05893 00B09B3D 00B09F6D 004732BE
docs: docs/re/creature.md#creature-fruit-pool
related: BUG-0486
---

## Report
Seen by the long-session bot (`long_creature`, coop_long_session.py) on rel/0.7.7 (0.7.6 code), lab 20261104-214938,
lane 2, the host, s 2660 of the session (44 min, after death, editor, the guest's menu exit, its restart and a rejoin,
all back): `exc` total faults 1: `0xC0000005 x1 at 0x00000000 ... tid=356 (main)`, a call through a null pointer;
calls 00B097C4 (FUN_00B09790) <- 00B023CC (FUN_00B02390) <- 00B04B24 <- 00B0586D <- 00B05893 <- 00B09B3D <- 00B09F6D
<- 004732BE <- 0041DDF4 <- 0046DB49 (no names in the .sym files for 0xB02390..0xB09F6D). The game survived (caught).
Before it in the host's log: `drama: sea_monster event starts (host or alone)` 4 s earlier, and from s 2644 on hundreds
of `damage: 0 hit by none owner=- posse=0 amount=0.0` a second (643 in 10 s, the host's own creature hit for 0).
Not reproduced yet; one fault in three long sessions (2 x long_creature, 2 x long_tribe).
Reproduce: `SPORE_DETECTOR=20 tools/lab.py run long_creature --lane N` (45 min); the seed is in the log (782899617).

## Root cause
* Observed (log, Ghidra): the host's flora's timed fruit fall (0xB09B50 -> Flora_DropFruit 0xB09AE0 -> 0xB09790)
  got a fruit from the pool 0xB08DF0 whose memory was freed and reused (`[esi]` = 0x15B40, a call through
  [0x15B40] = 0); the pool's free and loose vectors hold references (docs/re/creature.md#creature-fruit-pool).
* Not found: the release that freed it. The mod's fruit kind only calls the game's cFruit_SetState(5) (two `took
  fruit ... done here too` 5 s before the fault); the eat node destroys a saved fruit (+0x148 bit 0) while it is
  still in the loose vector (the game's own path).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not reproduced (one fault in three long sessions); the analysis is in docs/re/creature.md#creature-fruit-pool.

## History
- 2026-11-04: opened.
- 2026-11-05: analysed (fruit pool RE), not reproduced, no fix (fix/w43-crashes).
