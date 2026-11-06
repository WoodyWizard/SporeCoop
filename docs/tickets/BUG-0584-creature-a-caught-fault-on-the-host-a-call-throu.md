---
id: BUG-0584
title: Creature: a caught fault on the host, a call through null from 0xB097C4 (after 44 min of a long session)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-04
source: long_creature (D0), lab 20261104-214938
role: host
versions: 0.7.6
fixed_in: 0.7.8
branch: fix/w44-crashes
commits: b37ea975, f3b238dc, 0570e27d, 09461e63
tests: crash_fruit_eat (tools/tests/coop_crash_guards.py --case fruit_pool)
signature: 0xC0000005 at 0x00000000, calls 00B097C4 00B023CC 00B04B24 00B0586D 00B05893 00B09B3D 00B09F6D 004732BE
docs: docs/re/creature.md#creature-fruit-pool, docs/re/creature.md#creature-fruit-pool-guard
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

* Observed (runtime, fix/w44-crashes, lab 20261105-100942, no planted state): **root cause**. The mod's fruit_eat
  (worldobj.cpp: the host applying a guest's "took fruit", a guest applying the host's eaten fruit) set
  cFruit_SetState(5) alone on a fruit of the save (+0x148 bit 0). The game's eat node destroys such a fruit
  (DestroyInstance) before its SetState(5); without it the flora retired it into its free fruit with **one reference
  left** (`coop.fruitpool`: 19 -> 1). The next falling fruit's take (Flora_TakeFruitForDrop 0xB08DF0, free path) pops
  it, drops the pool's reference and its own: the fruit is freed inside the take, and the drop maker 0xB09790 calls
  its AddRef at 0xB097C2 through the freed memory: the same chain as the long session (00B097C4 00B023CC 00B04B24
  00B0586D 00B05893 00B09B3D). The long session's two `took fruit ...: done here too` 5 s before the fault fit.
* Observed: the eaten fruit's other references vary (1 in 20261105-100942 and in 20261105-100353: the fault; 3 in
  20261105-101857 and 20261105-102129: no fault yet, the freed-later state), so the test checks the pool state.
* CORRECTION of the analysis above: no reference is released once too often; the take's pop is the game's own and
  is right for every fruit the game itself retires.

## Fix
* `wobj.fruit_saved` (worldobj.cpp fruit_eat): a fruit of the save is destroyed (DestroyInstance, new
  game::destroy_instance) before SetState(5), as the game's eat node does. Off: SetState(5) alone (0.7.7).
* `game.fruit_pool` (gamefixes.cpp, a backstop): before each take, free fruit with one reference (the take would
  free them) and entries that are not a cFruit (vtable) leave the pool without a Release (kept alive). Not seen
  firing in the lab with wobj.fruit_saved on (the state no longer happens); its condition is the observed r1 state.

## Verification
Reproduced (fix/w44-crashes): crash_fruit_eat, the host eats a fruit of the save the way a guest's request does
(`coop.wobj eat`), then a fruit falls from a tree (`coop.wobj drop`):
- FAIL without the fix: lab 20261105-100942 (the build had no refcount check and no wobj.fruit_saved: the host
  faulted at 0xB097C2 with the long session's chain), lab 20261105-102129 (`SPORE_FIX_OFF=wobj.fruit_saved,game.fruit_pool`:
  the eaten fruit of the save lies in the free fruit, 3 references).
- PASS with it: lab 20261105-102226, lab 20261105-101523 (the eaten fruit is destroyed, not in the free fruit; the
  fruit falls; no fault).
- Not run: the 45 min long_creature.

## History
- 2026-11-04: opened.
- 2026-11-05: analysed (fruit pool RE), not reproduced, no fix (fix/w43-crashes).
- 2026-11-05 (fix/w44-crashes, X2): reproduced (crash_fruit_eat), root cause the mod's fruit_eat on a fruit of the save; fixed (wobj.fruit_saved) with a pool guard (game.fruit_pool); status fixed.
- 2026-11-05: released in 0.7.8.
