---
id: BUG-0534
title: Civ: a guest's copy of a host's shot without a target faults in the weapon update
status: open
kind: crash
stage: civ
reported: 2026-11-04
source: lab final 0.7.5
role: guest
versions: 0.7.5-dev
fixed_in:
branch: rel/0.7.5
commits:
tests: play_civ
signature: 0xC0000005 at SporeApp.exe+0x8B609C (GOG 2024) data 0x50; then cResourceProjectile::LaunchProjectile+0x5B5 data 0
docs:
related: BUG-0477
---

## Report
In the 0.7.5 final check (out/lab/20261104-061516) play_civ's guest wrote 7981 access violations at
SporeApp.exe+0x8B609C (data 0x50) in one 2 s hitch during a fight, then one in
cResourceProjectile::LaunchProjectile+0x5B5 (ecx 0). The game lived on. Not seen on 0.7.4 (the shot relay is new).

## Root cause
Observed (disassembly, GOG 2024): 0xCB5FA0 (thiscall, ret 8, called from 0xCC4266) takes this+0x11c, reads its
+0x114 (source) and +0x118 (target) through vt 0xB8 (cast 0x1186577) and then reads `[target+0x50]` with no
null check (0xCB609C). Inferred: the guest's copy of a host shot (BUG-0477, civ.cpp guest_fire) called
cToolManager::FireTool with a null target when the host's target was not found on the guest (or was an object
the host could not name), and a weapon whose update needs the target faulted each frame.

## Fix
The host marks a shot whose target it cannot name (tkind 2); the guest fires a shot that had a target only when it
finds that target in its own game (counter `no_target` in `coop.civ status` shots line). `coop.fix civ.shot_target off`: 0.7.5-dev behaviour.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
