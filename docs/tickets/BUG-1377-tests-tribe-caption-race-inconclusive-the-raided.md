---
id: BUG-1377
title: Tests: tribe_caption_race INCONCLUSIVE: the raided village had no tribe mission (raids by coop.tribe order only), so no conquest caption
status: fixed
kind: test
stage: Tribe
reported: 2026-11-20
source: lab
role: 
versions: 
fixed_in: unreleased
branch: fix/w61-civ
commits: de99d975, 326d54e1
tests: tribe_caption_race (coop_tribe_conquest_guest.py --click-at-close)
signature: RESULT: INCONCLUSIVE neither game played the conquest caption (the host's game left it out, BUG-1205)
docs: docs/re/tribe.md#tribe-raid-end
related: BUG-1205, BUG-1101, BUG-1068
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, lane 2, warm run 2): tribe_caption_race `RESULT: INCONCLUSIVE
neither game played the conquest caption (the host's game left it out, BUG-1205)`; the coverage of 673f7a55 lists it
as not a pass. The ledger: 4 of 23 runs without the caption since 2026-11-14 (FAIL before BUG-1205's verdict).

## Root cause
Observed: the host's log has no `village cinematic ... plays on the host` after the hut's fall (state 2, the village
conquered on both games), and no TRG_Upgrade either; in the passing run 20261120-041304 both followed the fall.
Known (docs/re/tribe.md#tribe-raid-end, .sym TribeToTribeMission_Complete 0xC2F7F0, BUG-1068): the conquest caption
TRG_TotemPole_Conquer is played by the end of the village's cTribeToTribeMission, and the host's game makes that
mission (TribeMission_SetTargetTribe) only from a player's raid or gift click; the test raids with `coop.tribe order`
(DoAction), which makes none. So the caption came only when the game had made the village's mission by itself (lab
20261113-072313 saw a village formed during play count its fall without a click; how the game picks it is not known),
and in 4 runs of 23 it had not (inferred from the mechanism; the hut's damage before the test's cut, none or one or
two blows, does not tell the runs apart). Not a mod fault.

## Fix
Test only (coop_tribe_conquest_guest.py --click-at-close): the raid starts with the guest's own raid order of its
members (`coop.tribe act_obj 15 <hut>` on the guest: its order path, MSG_TRIBE_CMD, for which the host makes the
village's mission, tribe.guest_mission BUG-1068), then waits up to 10 s for the host's `guest_missions=` to rise and
prints it; the host's `coop.tribe order` raids follow as before. The INCONCLUSIVE rule of BUG-1205 stays for a run
whose host still plays no caption.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- INCONCLUSIVE (the precondition missing, before): 20261120-133703:tribe_caption_race.
- no-fail-run: a test precondition made certain (the checks are unchanged). Was waiting for the lab run:
  tribe_caption_race must PASS with `guest_missions 0 -> 1` printed (best several runs: `--repeat 4`).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tribe_caption_race PASS in 20261121-003202, 20261122-194210, 20261123-023643 (INCONCLUSIVE before: 20261120-133703). Closed.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-civ): cause from the logs and the docs; the guest's raid order makes the mission; no lab run (owner rule).
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
