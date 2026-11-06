---
id: BUG-0569
title: Tribe: the guest sees no animations
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.8
branch: fix/w44-tribe-guest
commits: acc37291
tests: tribe_mirror_anims (tools/tests/coop_tribe_mirror_anims.py)
signature:
docs: docs/re/tribe.md#tribe-guest-a1
related:
---

## Report
The guest (3 reports): it does not see any animations in Tribe.
Reports: SporeCoop-report-2026-11-04 191438 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s6375 / s6403: the host sees its members and chiefs fighting in the village; the guest's screen of the same village shows no members (s6403) or creatures standing still (s6375).
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
Observed (host log): the host's game went Tribe -> Loading -> Tribe at s 5850, 6391 and 7320 (19:06:05, 19:15:06, 19:30:35: a game over and the game's reload of the last save; at 19:06 its members 25 -> 3) while the guest's game stayed in its world (no stage change in its log, only its mirrors 25 -> 3). Observed (lab): tribe_bounce_world out/lab/20261104-202045 FAIL: after the host's game over and reload the guest's game keeps the 3 NPC tribes the host's reloaded world does not have (emptied to 0 members by BUG-0483, but the tribe, its hut and map mark stay). Nothing re-aligns a guest's planet after the host's reload (W2).
* s 6375 / 6403 are after the second reload (s 6391): the guest's village lacks the members the host fights with.
2026-11-04 (fix/w43-realign): inferred, not reproduced on its own: s 6375 / 6403 came right after the host's second reload (s 6391); the guest's village at that spot was a village of its old world (kept, emptied by BUG-0483: no members) or one the member sync had just mapped to the host's new tribe and whose members it had removed at the cycle's end before spawning the host's (frozen creatures: members not yet bound). Covered by the whole-planet re-alignment (realign.villages: the old villages go, tribe.npc_tribes makes the host's); the members' own sync is tribe.cpp's (not changed).

2026-11-05 (fix/w44-tribe-guest, A1 of 0.7.8): **observed** in the lab (lane 9, GOG 2024, tribe fixture): the
host's members play the animations their behaviour trees start (24 kinds in 45 s of a fight and the villages' own
work: 0x591B056, 0x2C39003, 0x67378DD, ...; `coop.tribe anims`: the game's current animation, Anim::AnimatedCreature
GetCurrentAnimation, and the last one its tree started through SporeCoop::CreaturePlayAnim); the guest's mirrors of
the same members played none of them (0 of 646 samples, out/lab/20261105-112215). A mirror runs no behaviour tree
(tribe.cpp h_BTTick skips it: the host's member decides), and nothing relayed what the trees start: the mirrors
showed only their locomotion's idle and walk, as on the report's screenshot (members of the fight standing still).
The animals (npc.anims, BUG-0464) had this relay since 0.7.4; the members not. The reloads of the host (above) are a
second cause of an empty village (W2, BUG-0564), not of standing members.

## Fix
`tribe.mirror_anims` (tribe.cpp, npc.cpp): the host reports every behaviour animation of a member it tracks (game.cpp's
listener of SporeCoop::CreaturePlayAnim / PlayAnimation outside the idle refresh) and sends them once a state cycle to
the guests (MSG_NPC_ANIM kind 2), and once a second the animation each member plays now when its tree started it
(kind 3, for a mirror made later or a lost message); a guest plays the same call on the mirror. Older guests ignore
the new kinds. `coop.fix tribe.mirror_anims off`: nothing sent, nothing played (0.7.7).

## Verification
tribe_mirror_anims: PASS out/lab/20261105-112029 (the guest's mirror plays the host's behaviour animation in 570 of
605 samples, 94 %); FAIL with SPORE_FIX_OFF=tribe.mirror_anims out/lab/20261105-112215 (0 of 646). Threshold 80 %.
Same mechanism as BUG-0564 (tribe_bounce_world out/lab/20261104-202045 FAIL). Package W2.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), the class reproduced by tribe_bounce_world.
- 2026-11-04 (fix/w43-realign): analysed; covered by the W2 re-alignment (BUG-0564), not verified on its own: stays open until a test shows a village's members after a reload (coop_realign_planet.py checks villages, not their members).
- 2026-11-05 (fix/w44-tribe-guest, A1 of 0.7.8): root cause observed (mirrors play no behaviour animation), fixed by tribe.mirror_anims.
- 2026-11-05: released in 0.7.8.
