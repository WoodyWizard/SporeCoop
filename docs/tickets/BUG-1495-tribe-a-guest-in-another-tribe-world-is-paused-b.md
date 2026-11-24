---
id: BUG-1495
title: Tribe: a guest in another Tribe world is paused by the host's game-over caption and hears the host's music (part 2) (TR-9)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w79-tribe
commits: 70979dc4
tests: tribe_gameover_elsewhere, tribe_gameover_elsewhere_off
signature:
docs: docs/re/tribe.md#tribe-audit-wp-b1
switch: tribe.elsewhere_anims
related: 
---

## Report
External static audit of 0.7.15.5, item TR-9 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-cr-tr.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-B1 (fix/w79-tribe). A behaviour change gets a switch and a twin.

## Root cause
* Code (inferred, not run): MSG_TRIBE_ANIM (71) and MSG_NPC_ANIM's tribe kinds are class Module (not dropped at the
  dispatch for a guest elsewhere: "the module gates it per place"), and tribe::on_member_anims had no place check. Kind
  20 (the host's game-over caption up) paused a guest that plays its own Tribe world ("The tribe is out: waiting for
  <host>"); kinds 4-8 (theme and party music) played the host's music in the other world. Kind 21 is covered for a
  0.7.15+ host by host_busy (in_host_world).
* Part 1 (REAL-THEORETICAL): g_go_host_up / g_go_why are not reset on a world change; gameover_host_tick recomputes the
  reason and a stale "up" ends with the input lock (seconds). Not changed: a reset there would skip the "gone" a waiting
  guest needs (it would wait for the 10 s timeout).

## Fix
tribe.cpp on_member_anims: a guest with `own_world::elsewhere()` drops kinds 4, 5, 7 (plays) and a wait's start
(20 / 21 with a reason); stops (6, 8) and a wait's end pass (`tribe_logic::elsewhere_drops`, unit test). Counter
`tribe audit 0.7.17: elsewhere_anims dropped=`. Switch `coop.fix tribe.elsewhere_anims off`: 0.7.16.1.

## Verification
open, waiting for the lab run. pending night run: tribe_gameover_elsewhere, tribe_gameover_elsewhere_off.
`coop_tribe_gameover_elsewhere.py` (suite `tribe_gameover_elsewhere`, not warm): the guest plays Game9/Otherworld (a
copy of the fixture, as tribe_other_world), joins (elsewhere=1); the host knocks out its tribe until its game-over
caption; for 10 s the guest's game time runs (`busy:` without `paused`). Twin `tribe_gameover_elsewhere_off`: the guest
stands, must FAIL. The music half: unit test only (the guest's own trees play the same sounds in its world).
0.7.17 release lab: PASS: tribe_gameover_elsewhere (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-B1), branch fix/w79-tribe.
- 2026-11-23: fix on fix/w79-tribe (switch tribe.elsewhere_anims, tests tribe_gameover_elsewhere, tribe_gameover_elsewhere_off); lab run pending (no lab by day).
- 2026-11-24: released in 0.7.17.
