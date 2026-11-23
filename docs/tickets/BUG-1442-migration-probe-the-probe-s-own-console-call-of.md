---
id: BUG-1442
title: migration_probe: the probe's own console call of CreatureGame_NestHerdFor in GalaxyGameEntry (the guest reloading the host's world) crashed the guest (dc04aee5:SporeApp.exe SporeCoop::Herd_NestPosition)
status: fixed
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w65-night
commits: e06d3a16
tests: tools/tests/coop_migration_probe.py (migration_probe), tools/tests/coop_migration.py new_nest (also migration, nest_follow, nest_target_probe)
signature: dc04aee5:SporeApp.exe SporeCoop::Herd_NestPosition (0xC0000005 at 0x00C6AE60, read 0x160) from CreatureGame_NestHerdFor+0x47 <- sporekit c_call (the console `call`)
docs:
related: BUG-0018, BUG-0640
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, lane 6, rel/0.7.16 5376d388; the build still says coop
0.7.15.5): migration_probe "FAIL lab: new report crash-20261122-233253-i12.dmp / .txt" (instance 12, the guest). The
probe then died on "console closed the connection".

## Root cause
Test, not the mod. `tools/crash_triage.py`: GOG 2024, mode GalaxyGameEntry, 0xC0000005 at 0x00C6AE60
SporeCoop::Herd_NestPosition, read of 0x160 (ecx=0), chain `CreatureGame_NestHerdFor+0x47 <- sporekit.dll+0x47B4`
(skg::cdecl_call inlined into sk::c_call) `<- +0xBEA3` (sk::run_cmd) `<- +0xDDB1` (sk::start_next, the console): the
game function was called by the console's `call`, i.e. by the test. The probe prints a row every 4 s with
`new_nest()` = `call SporeCoop::CreatureGame_NestHerdFor cdecl 1`, whatever the mode. Guest log: `23:32:53.098 world:
Creature -> none (closed, BUG-1141)`, `stage: Creature -> Galaxy menu` (the host shared its world after the editors:
host `23:32:58.119 back to the world: sending it to Bob`), crash report at 23:32:53.495: the next row's call ran with
no creature game and the function read its null herd. The mod's own call of the function (wsync.cpp target_nest) runs
only after home_nest(), which checks `game::mode_id() == modes::kCreature` in the same frame. Same address as BUG-0018
(a player crash, fixed in 0.7.2.4) but not its path (there the cinematic read a destroyed creature's herd).
Not new in 0.7.16: the probe is an "extended" test (0 of 2 failed before); the share after the editors is old.

## Fix
coop_migration.py `new_nest()`: (None, None) unless `game.mode` is Creature right before the call (all its users:
migration, migration_probe, nest_follow, nest_target_probe). A world closed in the one frame between the mode read and
the call is not covered (the console has no guarded call); the probe's 4 s step makes that rare.

## Verification
Was waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run migration_probe --lane N --retry 0
--no-build` PASS with no crash report (its rows print `nestherd=0x0 nest=None` while the guest is out of Creature).
no-fail-run: a test fix; the crash before is 20261122-194210 migration_probe.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): migration_probe PASS in 20261123-015558, no crash report. Closed.

## History
- 2026-11-22: opened.
- 2026-11-23: fix/w65-night: the crash triaged (crash_triage: the console's call); new_nest() checks the mode first.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
