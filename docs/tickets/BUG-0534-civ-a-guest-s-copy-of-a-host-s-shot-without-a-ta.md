---
id: BUG-0534
title: Civ: a guest's copy of a host's shot without a target faults in the weapon update
status: fixed
kind: crash
stage: civ
reported: 2026-11-04
source: lab final 0.7.5
role: guest
versions: 0.7.5-dev
fixed_in: 0.7.5
branch: rel/0.7.5
commits: e201eb30
tests: play_civ, civ_shots
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
FAIL: out/lab/20261104-061516 (play_civ: 7981 guest faults at SporeApp.exe+0x8B609C).
PASS: out/lab/20261104-075207 (play_civ x1, civ_shots: 0 faults on both games).
(A play run, not a twin: the fault needs a fight's random target; civ_shots guards the shots themselves.)

## History
- 2026-11-04: opened.
- 2026-11-04: fixed in e201eb30 on rel/0.7.5.
- 2026-11-04: released in 0.7.5.
