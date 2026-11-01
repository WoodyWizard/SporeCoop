---
id: BUG-0330
title: Creature: team test 'pack regen next to the partner: hp 10.0 -> 10.0' (the guest's avatar 14 m from the host at the regen step)
status: fixed
kind: test
stage: Creature
reported: 2026-10-30
source: final smoke lab 20261030-205830 lane 2
role: guest
versions: 
fixed_in: 0.7.3.2
branch: fix/w17-creature
commits: 813d4e3
tests: team (tools/tests/coop_team.py)
signature:
docs:
related: BUG-0333, BUG-0281, BUG-0318
---

## Report
Final verification smoke (out/lab/20261030-205830, lane 2, idle machine, cold) failed `team`: "FAIL pack
regen next to the partner: hp 10.0 -> 10.0". `team` had 2 flaky failures in 27 runs before, with
other symptoms.

## Root cause
Two separate things (fix/w17-creature, lanes 4 and 5, 2026-10-30):
1. **The smoke failure (observed):** at the regen check the guest's own status said "Alice near", not
   "close (regen)". The guest's avatar was ~14 m from the host (guest trace (-158.5 -471.6 -86.4),
   host (-172.1 -467.8 -83.5)), so the pack regen (10 m) rightly did nothing. The same happened in
   one of my runs with `npc.kinematic` off (out/lab/20261030-230659 team#5: 0.9 m apart before the
   hunt, 9.8 m before the regen check, the guest at the same spot (-159.0 -471.4 -86.5)). So it is not
   `npc.kinematic` (BUG-0281) and not the editor wait (BUG-0318: no editor in this test). The cause
   of the move was not found: the test moves neither avatar, a debug hit on proxies 14-37 m away
   did not move the guest (probe, 0.0 m), and only 2 of ~60 team runs in all lab history show the
   guest at that spot. Opened as BUG-0333.
2. **The "other symptoms" (observed, a mod bug):** every *warm* `team` run (the same games reused,
   the guest rejoins) failed "guest survived a lethal hit: hp 16.0 -> 0.0", "rescue counted (1 -> 1)"
   (out/lab/20261030-231727 #2/#4, 20261030-231732 #2/#4: 4 of 4 warm runs). The pack's rescue
   cooldown (`kRescueCooldown`, 60 s) of the previous session was still running in the new one:
   `teamplay::reset` (session end, world change) cleared the hunt table but not `g_last_rescue`.
   A player who left and joined again (or loaded) within a minute of a rescue had no rescue.

## Fix
* `team.rescue_reset` (teamplay.cpp): `teamplay::reset()` also makes the rescue ready again (a new
  session or world). `coop.fix team.rescue_reset off`: the cooldown runs on (0.7.3.x).
* Test (`coop_team.py`): before the regen check the guest is put 1.5 m from the host and its own
  status must say "close (regen)" (the regen is what is checked; the drift is BUG-0333). The test
  prints both positions before the hunt, after the guest's hit and before the regen check.

## Verification
* FAIL (code without the fix, main be725db, warm runs): out/lab/20261030-231727 (team#2, #4) and
  out/lab/20261030-231732 (team#2, #4): rescue (1 -> 1), the guest died of the lethal hit. The regen
  drift: out/lab/20261030-205830 (smoke), out/lab/20261030-230659 team#5 (npc.kinematic off).
* PASS: out/lab/20261030-232216 (6/6, lane 4) and out/lab/20261030-232221 (6/6, lane 5): 12 of 12, 10
  of them warm.
A twin FAIL run with `SPORE_FIX_OFF=team.rescue_reset` is not possible in the warm order the bug needs:
since BUG-0312 a run with a switch off is not reused warm. The FAIL runs above are the same code
without the change.

## History
- 2026-10-30: opened.
- 2026-10-30: fix/w17-creature: two causes: the regen drift (not npc.kinematic; BUG-0333, test made robust) and the rescue cooldown surviving a rejoin (fixed, team.rescue_reset); team 12/12.
- 2026-11-01: released in 0.7.3.2.
