---
id: BUG-1187
title: tribe_zero_fields fails on rel/0.7.15 3a7f4b25: the host member's hunger 0 does not reach the guest's mirror (stays 100)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-15
source: lab
role: 
versions: 0.7.14, 0.7.14.1, 0.7.15 dev
fixed_in: unreleased
branch: fix/w51-tribefields
commits: 930651a4, 5bb993ba, 39df71db
tests: tribe_zero_fields
signature:
docs: docs/re/tribe.md#tribe-full-clock, docs/06-testing.md#testing-tribe-full-clock
related: BUG-1043, BUG-0935, BUG-1140, BUG-1185, BUG-1112
---

## Report
Found by feat/w51-harness (D4, BUG-1185) in its lab sample on lanes 4/5: `tribe_zero_fields` fails its BUG-1043 check
"the guest's mirror is as starving as the host's member": after `coop.tribe hunger <id> 0` on the host (the host's row
reads `(0, 0, 0.0)`) the guest's mirror keeps hunger 100 for the 5 s of the wait (`(0, 0, 100.0)`). The tool steps
before it (tool 1, then tool 0: also a zero value) pass, and so does the statistics check after the leave.

Three runs, all on lane 5, build of feat/w51-harness (its C++ is rel/0.7.15 3a7f4b25 unchanged, only tests and tools
differ): 20261115-225537 and 20261115-230446 (the codemodded test), 20261115-230558 (the test's text of rel/0.7.15
3a7f4b25 put back for the run): the same FAIL each time. So it is not the codemod. The last PASS on record is
feat/w51-tribe 20261115-182010 (before the merges of C1 / BUG-1140 into rel/0.7.15); 2 of 8 runs since 2026-11-13
failed in the ledger (before: other steps, fix/w49-small 2026-11-13).

To reproduce: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_zero_fields --lane N --retry 0 --no-build` on a build
of rel/0.7.15 3a7f4b25.

## Root cause
Observed (objdump of 0xC0D5A0, runtime on lanes 4 / 5): a full member's hunger is held at 100 by the game. The hunger
update keeps calling the setter with 100 while the member's full clock (+0xB90, a simulator clock, +0x18 running) has
run less than its delay (+0xB8C, 120000 ms); an adult exactly at 100 with the clock stopped starts it. Every mirror the
guest's game spawns (a host member more than 6 m from every free member of the guest's loaded world) is full when it
is made, so for 2 minutes the BUG-0935 field's apply of the host's hunger was undone in the next frame
(`coop.fields`: tribe_member_hunger reapplied=74; the mirror read `full=1/410/120000`). Players saw a hungry or
starving host member with a full bar on the guest after a join or a new member. A product bug since BUG-0935 (0.7.14).

Why it showed up on rel/0.7.15 (bisect, 3 cold runs per point, the commit's own test and lab): v0.7.14.1 0 of 5
spawned the test's member (lane 4, 20261116-000418 in fix/w51-tribefields-v07141), ee9d9990 0 of 3 (bisA
out/lab/20261116-001523), d1fe9727 2 of 3 (bisB out/lab/20261116-001522), 9362bcdd (before every Tribe C++ change
of 0.7.15) 2 of 3 (bisA out/lab/20261116-001130), a2c1ef8a 2 of 3 (bisB out/lab/20261116-001130). The trigger is
d1fe9727 (A9, BUG-1112, lab start-up with condition waits): the guest joins ~2 s after the host is in Tribe instead
of ~8 s, the host's members walk away from where the shared save holds them during the guest's load, and the
test's member #3 got a spawned, full mirror. With SPORE_START_FIXED=1 (the old sleeps) it was bound in 3 of 3
(20261116-001814). Not the C1 registry / tribe split / fields relay: the ids map the same, the field arrives
(`received`), its apply is undone by the game. Step 4 also passed now and then without the fix: the engine applies
every 0.25 s and a poll caught the one frame before the game's update.

## Fix
tribe.cpp apply_hunger: a host value below 100 ends the mirror's full clock with the game's own clock stop
(`SporeCoop::Clock_Stop` 0xBC3280, what the update does at the delay) before the setter; a host value of 100 leaves
the mirror's clock (the host's member is full too). Switch `coop.fix tribe.mirror_hunger_full off`: the mirror's own
full time (0.7.14). Test command `coop.tribe hunger <id> [v [end]]`: reads the full clock (`full=running/elapsed/delay`),
`end` ends it first (the host's member just made or fed would be 100 again in its next frame).
Test: tribe_zero_fields step 5 (`coop.tribe spawn 1` on the host: the guest's new mirror is full, the host's member
starves, the mirror must read 0 within 5 s and keep it 1 s); both hunger steps hold for 1 s now.

## Verification
FAIL (SPORE_FIX_OFF=tribe.mirror_hunger_full): out/lab/20261116-000010 (lane 4: step 5 guest 100.0, `full=1/5671/120000`
after the host's 0), out/lab/20261116-000735 (lane 5, cold x3: step 5 3 of 3, step 4 2 of 3), out/lab/20261116-001814
(lane 4, SPORE_START_FIXED=1, cold x3: step 4 bound and ok, step 5 FAIL 3 of 3).
PASS: out/lab/20261116-000012 (lane 5), out/lab/20261116-001814-2 (lane 5, cold x3: the test's member's mirror was
full in all three, "member #3's full time ends as in the host's game", step 4 and 5 ok), out/lab/20261116-020859 (lane 5,
x2, the final tree). Tribe set (tribe_* and play_tribe --quick, lanes 4 / 5): out/lab/20261116-002240, 135 of 136
(tribe_zero_fields ok; tribe_duet FAIL twice, PASS on v0.7.14.1 lane 4 with another selection: BUG-1188).
Bisect run dirs in the worktrees fix/w51-tribefields-v07141, fix/w51-tribefields-bisA (lane 4), -bisB (lane 5).

## History
- 2026-11-15: opened.
- 2026-11-16: fixed on fix/w51-tribefields (tribe.mirror_hunger_full; trigger d1fe9727's faster lab start-up).
