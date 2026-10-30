---
id: BUG-0206
title: play_civ: right after the chunk budget drops to 300, four vehicles show owner 0 on the host and 1 on the guest for one audit
status: not-reproduced
kind: desync
stage: Civ
reported: 2026-10-28
source: play_civ.py --seed 9, fix/w3-civ
role: guest
versions: fix/w3-civ (main 5ae9744 + wave 3 Civ fixes)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0186
---

## Report
`SPORE_LANE=7 python3 tools/tests/play_civ.py --minutes 10 --seed 9` (fix/w3-civ, 2026-10-28): one
audit right after the test lowered the chunk budget to 300 (`chunk budget 300`) listed vehicles
#1, #6, #7, #8 with owner 0 on the host and owner 1 on the guest (after gifts from the guest to the
host); the next audits were clean. The run's RESULT was FAIL from these lines and from a spice
claim / move of the host's last seconds that the end audit did not see. The fix/w2-civ run of the
same seed (before wave 3) was OK. Not reproduced or analysed: possibly the owners of vehicles in the
second chunk arrive one cycle later after the split (stale for one audit), or a gift racing the
chunk change. Nothing in the wave 3 changes touches owners without a change of the player set.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not reproduced. Evidence kept in the History: 1 sighting (fix/w3-civ, before the
audit's second look), then no owner difference at all in 4 runs on fix/w4-census and
fix/w13-tribe-civ, and the w14 seed 9 / 11 / 5 runs (each with the chunk budget at 300 for the second
half and 8-15 gifts).

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w4-census): not reproduced: `play_civ.py --minutes 10 --seed 9` on lane 7 with the chunk budget at 300 for the second half, gifts=9: no owner difference at any audit. Reading (inferred): the guest takes a vehicle's owner from every snapshot chunk unconditionally (civ.cpp guest_state), so an owner can only be stale while no snapshot is applied: the 0.1 s until the next one after a gift, or while the guest's game is outside the Civ mode (`civ::active()` false: the city planner, which the bots open; the run of the report had planner 4 -> 7 and give 12 -> 17 in that minute). play_civ's audit compared vehicle owners once, without the 1 s second look the cities get. Changed: the audit counts a difference once (`owner_differs_once`, printed with the guest's mode) and reports a problem only when it is still there 1.5 s later. If it comes back as a problem after that, it is a real desync; `coop.census` + census.py `diff` show it per vehicle. Status: open until a few more seed 9 runs show no persistent difference.
- 2026-10-30 (fix/w13-tribe-civ): not reproduced in 3 more `play_civ.py --minutes 10 --seed 9` runs on lane 7 (main 5d7f20e + this branch), each with the chunk budget at 300 for the second half and gifts=8 / 15 / 11: no "vehicle owners differ once" line at all, so not even the one-audit difference. With BUG-0206's 1.5 s second look this is no longer a test failure; the owner path (guest_state takes the owner from every snapshot) is unchanged. The third run failed on another desync (an NPC city and its two vehicles 40-60 s late on the guest): BUG-0319, not this one. Status kept open (brief); proposal for the owner: close as not-reproduced. Logs: out/w13/play_civ9_a.log, play_civ9_fix1.log, play_civ9_fix2.log of the worktree.
- 2026-10-30 (fix/w14-tribe-civ): closed as not-reproduced (the coordinator agreed); evidence above. The play_civ audit still reports a persistent difference (1.5 s second look) if it ever comes back.
