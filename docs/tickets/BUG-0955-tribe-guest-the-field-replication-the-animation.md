---
id: BUG-0955
title: Tribe guest: the field replication, the animation relay and the effects relay still use a mirror's freed citizen (object_alive), BUG-0844's fix missed them
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-12
source: rel/0.7.13 lab 20261112-041619-2, 20261112-031338 (tribe_mirror_lifetime)
role: guest
versions: 0.7.12 .. rel/0.7.13
fixed_in: unreleased
branch: fix/w48-freed2
commits: 06f67869
tests: tribe_mirror_lifetime
signature: guest: coop+0x13E88F game::object_scale (game::cast vcall) then SporeCoop::CreaturePlayAnim+0x4F, same object; or a call into the freed block (UTFWin paint); frames cut off by the game handler
docs: docs/re/tribe.md#tribe-mirror-other-users
related: BUG-0844, BUG-0951
---

## Report
tribe_mirror_lifetime (BUG-0844's test) failed 1 of 3 on rel/0.7.13 with 6 guest faults (lab 20261112-041619-2 #3)
and once with 10 (20261112-031338); on fix/w48-freed it had passed. The guest's game caught the faults and went on
(`sporekit: frames: the frame N was cut off by a fault the game's handler caught`).

## Root cause
Observed (logs + addr2line on the run's coop.dll): `0xC0000005 at coop+0x13E88F` = `game::object_scale` ->
`game::cast` (vcall) on 1E1684C0, from the field replication of the Tribe members (`member_mirror`, fields.cpp reads
the scale); in the same frame `SporeCoop::CreaturePlayAnim+0x4F` with esi = the same object (the behaviour
animation relay, MSG_NPC_ANIM kind 2 / 3, `play_behaviour_anim`); then 4 more frames cut off 0.25 s apart. The other
run executed the freed block's vtable (0x24D2F858) from a UTFWin paint. BUG-0844's fix (mirror_ok: listed + serial)
covered guest_state, guest_tick and the snapshot's end only; nine other users of `g_gm[id].c` still asked
`object_alive`, which a freed citizen's block passes. The test's statehold keeps a removed mirror bound for seconds:
the mod's own defect (a hole in BUG-0844's fix), not the game's. Confidence: high.

## Fix
mods/coop/tribe.cpp: `member_mirror`, the animation relay, the held items, `mirror_of` (mirror_fx), `local_of`, the
ring's selected member, `for_each_tracked`, `for_each_diag`, the sync-check entries and `coop.tribe anims` ask
`mirror_ok`. Switch: `coop.fix tribe.mirror_member_lifetime off` (BUG-0844's: mirror_ok is object_alive then).

## Verification
Test `tribe_mirror_lifetime`.
* FAIL (rel/0.7.13 37ac75ef, without the fix): out/lab/20261112-041619-2 (#3: guest total faults 6), out/lab/20261112-031338
  (10 guest faults) in rel/0.7.13's out/lab.
* PASS: out/lab/20261112-043651 #1-#8 (no fault; #9 / #10 hit a bug of a later intermediate build, BUG-0951's epic
  check, fixed before the next build), out/lab/20261112-052420 (10 of 10 checks and no fault; 2 runs on a sync-check
  `t apart` of the held states, allowed since), out/lab/20261112-054446 (10 of 10).

## History
- 2026-11-12: opened.
- 2026-11-12: fixed on fix/w48-freed2.
