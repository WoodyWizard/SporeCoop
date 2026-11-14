---
id: BUG-1082
title: Tribe: a guest's copy of a member 12-114 m off the host's (sync-check MISMATCH) during the campaign's raids
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: 0.7.13 and earlier
fixed_in: 0.7.14
branch: fix/w49-raid2
commits: 22c48f58
tests: tribe_member_hang, campaign_tribe_end (lab gate sync-check)
signature:
docs: docs/re/tribe.md#tribe-member-hang
related:
---

## Report
fix/w49-camp, campaign_tribe_end lab 20261113-125221 (lane 5, rel/0.7.14 + fix/w49-camp): the campaign itself reached Civ
the players' way (RESULT: OK), the lab failed it on the sync-check gate: at s 1389 (13:15:57, during the Tribe end's raids,
four seconds after the shortcut `coop.tribe spawn 6`) the host and Carol (extra1) disagreed on one NPC village member:
host `#45 2AD674C0 owner=254 hp=102 pos=(-98.9 -557.5 247.5) bt=04625F64`, Carol's mirror `#45 23A57C50 ours=1
pos=(-80.7 -454.7 201.9) bt=FFFFFFFF host_speed=1.2`, 113.9 m apart, on two checks 3 s apart. The other two guests
matched. Seen once in 3 campaign_tribe_end runs of fix/w49-camp.
Again in campaign_tribe_end 20261113-160426-2 (lane 5) at 16:18:20, during the raiders' run to a village 281 m away
(`coop.tribe order` 50 s before): one of Dave's own members, on the host `#15 24A45EA0 owner=3 pos=(56.2 -514.4 21.5)
speed=12.9 bt=0485FD10`, on Dave's game `#15 0EF90080 ours=1 pos=(54.8 -502.6 20.9) speed=0.0 host_speed=13.7
bt=FFFFFFFF`: 11.9 m apart on two checks 3 s apart (the host and Dave only). The Tribe end itself reached Civ in
12 min in that run.
Reproduce: `lab.py run campaign_tribe_end` and look for "sync-check: MISMATCH ... t: 1 apart" in the game logs.

## Root cause
Observed (logs): both mismatches were a member in the air on the host and its mirror on the guest's ground: the two
positions lie on one radial line (direction equal to 4 decimals), radius 617.9 vs 504.0 (NPC member #45, 114 m up,
node 0x04625F64) and 518.0 vs 506.2 (Dave's #15, 12-14 m up, node 0x0485FD10, 12.9 m/s). Node 0x0485FD10 is the
citizen's airborne node (static: its tick 0xDA7AE0 runs while the grounded byte is 0, anim 0x0493840C, the one in the
mismatch); the same campaigns logged members in it 3-15 m up with speed 0 for 7-28 s.
Observed (runtime, tribe fixture, `coop.tribe hang` holding host members 12/60/110 m up): the guests' mirror stood on
its terrain for three reasons in tribe.mirror_air (BUG-0946): (1) heights over drive::g_agl_max 30 m are never placed;
(2) the guest's physics update puts a creature off the guest's screen onto the plain terrain without setting the
grounded byte, so the hold's reassert never acts (lab 20261113-182546: list received 5 times a second, mirror placed
every frame, on its ground in 12 of 14 reads); (3) the list's 0.35 s freshness against a slow host (frames 0.2-0.4 s
apart in the 4-player campaigns) and 64 members a list.
Not found (inferred open): why the host's game holds a member up there with speed 0. Not the mod (no hold on host
members), not reproduced in 3 raid probes with the co-op epic (one host-only). The game's own state; the fix makes
every guest show it as the host has it.

## Fix
`tribe.mirror_hang` (tribe.cpp send_member_air / guest_tick): any height (2000 m), the air list held 1 s, sent in lists
of 64 as often as needed, and a landing (a member of the last list on its ground) sent once with height 0.
`game.air_culled` (game.cpp after_physics): after the game's physics update a creature the mod holds in the air that
is more than 0.5 m under its hold is put back (not grounded, the hold's velocity). Off: `coop.fix tribe.mirror_hang
off`, `coop.fix game.air_culled off` (0.7.13). Diagnostics: `coop.tribe airprobe`, `coop.tribe hang`, the
`culled_lifts=` / `landed=` counters in `coop.tribe anims`.

## Verification
tribe_member_hang (new, host at 10 fps over the player network): SPORE_FIX_OFF=tribe.mirror_hang,game.air_culled FAIL
out/lab/20261113-183101 (mirror within 3 m in 4/14, 1/14, 3/13 reads, on its ground); PASS out/lab/20261113-182858 and
out/lab/20261113-183101-2 (14/14, 14/14, 14/14; culled_lifts every frame). The campaign itself was not rerun (80 min);
final check: campaign_tribe_end's sync-check gate.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-raid2 (tribe.mirror_hang, game.air_culled; test tribe_member_hang).
- 2026-11-14: released in 0.7.14.
