---
id: BUG-0620
title: Tribe: a host citizen that is no tribe's member stands near the guest on the host only (detector unmatched:citizen)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: detector, play_tribe (fix/w45-placement)
role: guest
versions: 
fixed_in: 0.7.9
branch: fix/w45-last
commits: ef1058d0, 1a891434, e485cc7a, 6c567738
tests: tribe_dead_player (coop_tribe_dead_member.py --player), tribe_member_place (coop_tribe_member_place.py; --attach over play_tribe)
signature:
docs: docs/re/tribe.md#tribe-member-body
related: BUG-0488, BUG-0481
---

## Report
Split from BUG-0488 (its members' row). The detector's `Tribe:unmatched:citizen` in play_tribe: "N on the host, N-1 on
the guest, 1 host / 0 guest without a partner within 15 m", 3-6 of 31 samples in the runs of 2026-11-05/06 (often 2-4
samples in a row), 3 of 32 in lab 20261106-070213, 1 of 31 in lab 20261106-073013 (this branch). Not in a quiet session
of the tribe fixture (tribe_member_place: 0 in 18 samples, lab 20261106-062912).
Observed with `coop_tribe_member_place.py --attach` over play_tribe (out/w45/watch_tribe2.log of fix/w45-placement,
play_tribe lab 20261106-071429): the host's lone citizen (noun 0x018EB4B7) stands still for 10-20 s
(ser 1823 at (403 -48 304) for 20 s, ser 3267 at (393 -91 313) for 10 s), and no member the mod tracks (`coop.sync` t<id>:
every member of every tribe's member list, tribe.cpp) is within 17-100 m of it on the host: it is a citizen object in
no tribe's member list, so the mod never mirrors it. Not a killed NPC member (tribe_dead_member, lab 20261106-072628:
the victim stays in its tribe, knocked out, mirrored) and not the bodies of a beaten NPC tribe (tribe_dead_tribe,
lab 20261106-072755: none left on the host).
Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_tribe --lane N` and, 75 s later,
`SPORE_LANE=N python3 tools/tests/coop_tribe_member_place.py --attach --minutes 11`: it prints the object's pointer,
vtable, age byte (+0xB34) and dead flag (+0xB5E) of every lone host citizen (not caught in lab 20261106-073013).

## Root cause
Observed (fix/w45-last, play_tribe lab 20261106-133817 with `coop_tribe_member_place.py --attach`, out/w45last/pt2.watch.txt
of the branch, and the new `coop.tribe who <pointer>`): the lone host citizen (0x1B9E2150, at (385 -44 326) for 16 s) had
**no tribe** (+0x1014 = 0, in none of the 2 tribes' member lists), the game's dead flag +0xB5E = 1, adult, its last order
record a gather (rec=2): it was the guest's member #7 (owner 1 in the same run's play_tribe log, 0x1B9E2150) that had died.
A player tribe's member that dies leaves its tribe's member list (an NPC tribe's stays in it, knocked out: tribe_dead_member)
while the game keeps its body 10-20 s; the host's snapshot is built from the member lists, so the body dropped out of it at
once and the guest removed its mirror: the body lay on the host only. Reproduced on purpose: tribe_dead_player (a member of
the host's own tribe killed with `coop.tribe pincer`, 1000 damage): on the host the body lay out of the member list for 11-12
samples (24 s) with no guest citizen within 3 m of it (lab 20261106-135809, 2 of 2).

## Fix
`tribe.member_body` (tribe.cpp, BUG-0620): the host keeps sending a member that left its tribe's list dead (its body:
in the object list, dead flag) with its last tribe and health 0, while the game keeps it (at most 60 s); the guest's mirror
plays the death (tribe.ko) and lies there until the host's body goes. A body's entry carries the bit 0x80 in its age
byte: the guest takes its mirror out of its tribe's member and selectable lists as the host's game did (a first version
left it a member: play_tribe failed "the same members on both" and a guest's order to the body was rejected, lab
20261106-143257), and destroys it (DestroyInstance) when the host's body goes; no new mirror is spawned for a body. `coop.fix tribe.member_body off`: dropped at once
(0.7.8). `coop.tribe` (host) counts bodies_sent.

## Verification
tribe_dead_player: FAIL with `SPORE_FIX_OFF=tribe.member_body` out/lab/20261106-135809 (2 of 2: 10 / 11 samples with the
host's body and no guest citizen within 3 m), PASS with the fix out/lab/20261106-140050 (2 of 2; bodies_sent 340 / 164), and with the guest's detach
out/lab/20261106-144737 (2 of 2).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-last, L5 of 0.7.9): the lone citizen named (a dead player member out of its tribe's list); fixed by tribe.member_body; test tribe_dead_player.
- 2026-11-07: released in 0.7.9.
