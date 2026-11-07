---
id: BUG-0685
title: Tribe: in the 4-player campaign one guest's mirror of the host member #4 (the leader the guest's game took by tribe.leader_sync) is gone for 33 s+ (sync-check t: 1 missing on the guest)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-07
source: lab 20261107-141346 campaign (rel/0.7.10 e7d1734c, lane 2)
role: guest
versions: 0.7.10
fixed_in: unreleased
branch: fix/w46-tribe-leader
commits: f4e96be8 a64ded85
tests: tribe_leader_ko
signature: sync-check: MISMATCH with the host (Tribe 9652F8AD, 33 objects compared): t: 1 missing on the guest (t4); guest `t4 here: not listed`, host `t4 here: #4 ... dead=0 ... tool=11 leader=1`
docs: docs/re/tribe.md#tribe-guest-game-over, docs/06-testing.md#testing-guest-game-over
related: BUG-0652, BUG-0664, BUG-0645
---

## Report
The 4-player campaign on rel/0.7.10 (lab 20261107-141346, lane 2, 98 min) failed the sync-check gate in Tribe
(world 9652F8AD), besides its stuck findings. Two kinds:
* extra1 (Carol) at s 2973.9 and s 3007.0, the host the same at s 2973.9 ("2 checks 3 s apart"): `t: 1 missing on the
  guest (t4)`; Carol `t4 here: not listed`, the host `#4 266877C8 owner=0 ... hp=150.0 pos=(144.4 -470.7 -106.4) dead=0
  bt=04601D70 tool=11 leader=1` (campaign.extra1.game.log 2767-2785, campaign.host.game.log 2960).
* guest (Bob) and the host at s 2056: `t: 1 apart (t29 9.4)`, once (campaign.guest.game.log 2262).

## Root cause
Not a 0.7.10 regression: the same with `tribe.leader_sync` and `wsync.tribe_level` off (lab 20261107-164852); the
code paths (the citizen damage hook, the BUG-0172/0288 guard) are as in 0.7.9.

Observed (lab, tribe_leader_ko, docs/re/tribe.md#tribe-guest-game-over): a guest's game runs its own game-over rule on
its player tribe of mirrors. With the host's tribe down to its chief, a blow of the guest's own game (the citizen's
TakeDamage) that took the chief's mirror to 0 raised the guest's input lock at once and its caption "Alas! Your
population has fallen below the minimum ..." 12-19 s later, while the host's chief stood with full health (the mod put
the mirror's health back at once: the game over stayed). Its OK reloads the guest's own last save.

Observed in the campaign: the host's tribe was down to its chief (#4) and one more entry (bodies); only on Carol the
input lock and the pause came up at s 2950-2960 (`busy=input_lock,paused`, no cinematic: a caption that waits), and
#4's mirror went and was not made again for 47 s while the host kept sending it. `campaign_tc2` with the new log lines:
the campaign's repeated "(the leader was none)" lines are the KO replacements of the host's chief's mirror (`mirror #1
goes: dead here, back on its feet on the host`), not a leader the guest's game lost.

Inferred (the campaign logs had no per-mirror lines): Carol's own game over from such a blow; with her lock up
tribe.gameover's guard (a guest with the input lock up and no member with health = "the player tribe is out") refused
#4's new mirror. The new log lines (`mirror #N is not made: the player tribe is out ...`) name this next time.

## Fix
`tribe.mirror_blow` (mods/coop/tribe.cpp h_CitizenTD): on a guest (not while the host is away) a blow of its own game
never takes a member mirror below 0.3 health; the host's game decides when a member falls (tribe.ko plays it). Off:
the game's blow (0.7.10). Diagnostics: a guest logs when a member mirror goes and why one is not made again
(`tribe: mirror #N goes: ...`, `mirror #N is not made: <why>`, `mirror #N is made again`, at most 60 lines);
`coop.tribe` lifetime `mirror_blows_kept=`. Test hook `coop.tribe hurt <id> <dmg>` (guest).
Not changed: the guard itself (a guest's input lock from another caption with no mirror with health still blocks
mirrors; not seen).

## Verification
tribe_leader_ko (lane 5): FAIL with `SPORE_FIX_OFF=tribe.mirror_blow` out/lab/20261107-164611 (the guest's lock 1 at
once, the caption at 18.8 s); FAIL with `SPORE_FIX_OFF=tribe.mirror_blow,tribe.leader_sync,wsync.tribe_level`
out/lab/20261107-164852 (not a 0.7.10 regression); PASS out/lab/20261107-164728 and out/lab/20261107-165013 (with
tribe_coop, tribe_bounce_mirrors, tribe_ko, tribe_leader_sync, tribe_gameover: 6/6). make test passes.

## History
- 2026-11-07: opened.
- 2026-11-07: fixed on fix/w46-tribe-leader (tribe.mirror_blow); not a 0.7.10 regression.
