---
id: BUG-1216
title: Civ: NPC proxies on a guest 8-200 m off the host's NPCs in the 4-player campaign's Civ (sync-check n apart; campaign_tc2), also on v0.7.14.1
status: fixed
kind: desync
stage: Civ
reported: 2026-11-16
source: final check 0.7.15 (lab 20261116-025154 campaign_tc2)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in: 0.7.15
branch: fix/w51-civsync
commits: e8284a09, 081b955f, 9828f4b6, c5718d0c, e995b718
tests: civ_proxy_air, civ_wild_moved, net_tests test_diag_npc_agl, campaign_segment (from the run's Civ save), campaign_tc2
signature: sync-check: MISMATCH with the host (Civilization A3930F03, ...): n: N apart
docs: docs/re/civ.md#civ-npc-ground, docs/06-testing.md#testing-civ-proxy-air, docs/06-testing.md#testing-final-triage-tribe-civ-0715
related: BUG-0402, BUG-1106, BUG-0329, BUG-0339, BUG-1100, BUG-0499, BUG-0394, BUG-1228
---

## Report
Final check 0.7.15 (lab 20261116-025154, rel/0.7.15 11114769, lane 4): campaign_tc2 printed `RESULT: OK`, the lab's
sync-check gate failed it: `12 on extra1, extra2, guest, host (n apart): ... (Civilization A3930F03, 45 objects compared):
n: 3 apart (n406 9.7 n404 8.8 n403 8.7)`, about 2.5 min into Civ (s 1363 and s 1397, two check pairs). Rerun of the
failed segment (`lab.py run campaign_segment` with a segment.json for the run's Civ save, seed 482315165; the gate
failure leaves no segment.json of its own): 20261116-084409 PASS (no MISMATCH), 20261116-085558 FAIL (`n: 2 apart
(n809 8.3 n810 8.2)` on Carol). The same segment on v0.7.14.1 (a detached worktree of the tag, its build, the 0.7.15
tools): 20261116-091255-2 #1 `n: 7 apart (n467 205.5 n462 186.6 ...)` on Carol. So not a 0.7.15 regression.

## Root cause
Three kinds, each from the `sync-check:   nNNN here:` lines of the host and the guest:
* A (the final check, all three guests, standing herd, species 067A147A): the host's NPC stands at agl 0.00 on the
  host's ground; at the same point every guest's ground is 10.47 m higher (`want_agl=-10.47`), the proxies stand on
  their own ground (`agl=0.00`, own height, docs/re/tribe.md#tribe-animal-height). The proxy is where the height rule
  puts it; the check compares absolute positions, so the terrain difference between the host's live Civ and the
  guests' copy of its save shows as `n apart` (BUG-0402's fourth kind, here in Civ). No terrain stamp of either game is
  within 50 m of the point (the stamp lines of 03:11:40-03:16 differ only in the host's own Civ start stamps); why the
  ground differs is not found. The segment runs (every game loads the same save) did not show this kind.
* B (segment run 2, Carol only, two walking animals of species 0654E5BC, 4 m/s): Carol's terrain at the target is the
  host's (`want_agl=0.02`), the proxies are 8.2 m over it (`agl=8.23`, `grounded=0`, their own speed 0) next to the
  target (`err` = the height): walked proxies in the air in Civ; the out-of-ground rule only lifts, nothing lowers a
  walked proxy there (inferred: in Tribe the game's own update grounds animals, docs/re/tribe.md#tribe-animal-height;
  not checked for Civ).
* C (v0.7.14.1 segment, Carol, the same species 0654E5BC, after Dave's rejoin): Carol's targets (`want`) lie 120-205 m
  from where the host's NPCs of those ids are, fresh (`age=0.10`, 9 states a second), the proxies 50-63 m from them:
  inferred an id or target mix-up of that herd on one guest; not analysed further.
Not a 0.7.15 regression (kind C on v0.7.14.1, BUG-0402 / BUG-1106 of 0.7.7-0.7.14 the same class).

Update 2026-11-17 (fix/w51-civsync), per kind, with what a player sees:
* A (observed): the host's live Civ world and the guests' copy of its save differ by 9.2-10.5 m at that herd; every
  guest's copy stood on its own ground (`agl=0.00` on all three). A player sees the herd standing on the ground, so
  the check was wrong, not the game: it compared absolute positions. Why the terrain differs is not found (no stamp
  within 250 m; only a live Tribe -> Civ host shows it). Seen there too: the guest's ground is a steep slope where the
  copies slide and are trail-teleported every ~2.5 s (a visible jitter; BUG-1228, open).
* C (observed, reproduced on rel/0.7.15 in the segment, lab 20261117-122701: `n: 7 apart (n1030 205.2 ...)`, the
  guest's targets 82-103 m under its ground): the Civ game's pool moves the host's wild creatures hundreds of metres at
  once (to and from unplaced points with z = 0, docs/re/civ.md#civ-wild-reused-frozen). The guest's proxy buffer
  blended across that step (no jump_speed for NPCs): the target slid along the chord through the planet for seconds.
* B (observed in civ_npc, inferred for the campaign's two copies): the same jump went into the proxy's path velocity
  (1000+ m/s for 0.2 s); a standing creature's proxy is walked at its path's speed (BUG-0394), so walk_place took it
  the whole way in a few frames on the straight line from where it stood: 10-84 m over the guest's ground over a
  valley (the new `drawn X m over this game's ground` line: 27 a minute in civ_wild_sync, 92-132 per guest in a
  segment run). A player sees animals fly through the air. The campaign's two copies 8.2 m up (`tp=1`, made while
  the host's game refilled its herds) were inferred such a flight's end held by own_height. Disproved on the way: the
  Civ game does ground a copy the mod leaves in the air (herd shift, shove onto lower ground, a copy put 8 m up: agl
  0.00 within 50 ms; labs 20261117-121304, -121752, -122049); a switch for that (walked Civ copies at the sender's
  height) had no failing twin and was dropped.

## Fix
* `npc.civ_jump` (npc.cpp): a Civ proxy's buffer does not blend across a step faster than 40 m/s (BUG-0499's puppet
  jump speed), and a target that jumps more than 10 m in one frame puts the proxy there (onto this game's ground,
  walk ended, a fresh path: no path velocity across the jump). Off: blended and walked (0.7.14).
* `sync.npc_agl` (diag_sync.cpp, net/diagmsg.h): every NPC entry of a sync-check digest carries its height over the
  sender's terrain (a trailer appended after the BUG-0606 scale: an older reader stops before it, an older sender's
  entries are compared absolutely); the host compares NPCs by the distance along the ground and the difference of the
  two heights. Wire: the digest message grows by 2 bytes + 4 per NPC; 0.7.14 and 0.7.15 still read each other's.
* `npc.frozen_moved` (npc.cpp, a fourth kind found in the segment runs, lab 20261117-130736: a herd of 7 moved
  392-398 m by the pool during a 24 s pause of the host, both guests kept it where it stood, `n: 7 apart`, states 20 s
  old): in Civ the host's frozen pass retires a tracked object more than 20 m from where the game last updated it (as
  BUG-1100's reused ones); host_tick gives it a new id at its new place when the game goes on. Off: kept (0.7.14).
* Diagnostics: a guest logs `npc: proxy N ... drawn X m over this game's ground ... (BUG-1216)` (once per copy per 10
  s) for a Civ copy over 2 m up while its creature is on the host's ground.

## Verification
* FAIL runs: 20261116-025154:campaign_tc2 (rel/0.7.15), 20261116-085558:campaign_segment (fix/w51-t4 = rel/0.7.15's mod
  code), 20261116-091255-2:campaign_segment#1 (v0.7.14.1, fix/w51-t4-v07141 worktree).
* PASS: 20261116-084409:campaign_segment (fix/w51-t4); v0.7.14.1 20261116-091255-2 #2 and #3.
* Rate of the segment: rel/0.7.15 1 of 2 failed, v0.7.14.1 1 of 3 (the lab's sync-check gate on `n apart`).
* A third run on fix/w51-t4, 20261116-101838, failed the same gate on another kind: `v: 1 missing on the guest (v13)` on
  Dave 10 s after the host's campaign shortcut founded a city (Dave: `the host has 3 cities, this game 2 and 3 founded
  (on their way)`, then the vehicle design asked): BUG-0624's kind (an instant founding late on a guest at low fps;
  load average 25 on the machine). Not analysed further.
* Fix runs (fix/w51-civsync):
  * civ_proxy_air (kind B/C): FAIL with `SPORE_FIX_OFF=npc.civ_jump` 20261117-125242:civ_proxy_air (a copy 58-62 m up
    in two samples in a row, 59 "drawn high" lines), PASS 20261117-125257:civ_proxy_air (none, 105 jumps).
  * sync.npc_agl (kind A): net_tests test_diag_npc_agl FAILs with `--nofix` (pos:n1, the terrain difference "apart"),
    passes with it; the final check's n406 (8.9 m absolute) is 1.7 m apart by height over the ground.
  * civ_wild_moved (the pause kind): FAIL with `SPORE_FIX_OFF=npc.frozen_moved` 20261117-131957:civ_wild_moved (7 copies
    60 m off for the whole pause), PASS 20261117-132012:civ_wild_moved.
  * Civ segment of the campaign (4 games, lanes 5 + 9, the final check's Civ save): 20261117-122701 FAIL before
    npc.civ_jump (kind C, `n: 7 apart` 83-205 m on Dave), PASS 20261117-125728 with it (1-5 "drawn high" lines per
    guest, all 2-3 m single frames on a mountain slope, against 92-132); 20261117-130736 FAIL with npc.civ_jump, before
    npc.frozen_moved (the pause kind above); with every fix: PASS 20261117-132944 (after one restart of an extra game
    that did not start, an environment retry) and PASS 20261117-134154, no MISMATCH, 0-7 "drawn high" lines per guest.
  * Related tests with every fix: civ_proxy_air, civ_wild_moved, civ_wild_pooled, civ_wild_reused, civ_wild_sync,
    play_civ --quick all PASS (20261117-132159).

## History
- 2026-11-16: opened (fix/w51-t4, final-check triage 0.7.15): old, three kinds; not fixed in the triage.
- 2026-11-17 (fix/w51-civsync): kinds A (the check), B / C (npc.civ_jump) and the pause kind (npc.frozen_moved) fixed; the slope jitter seen with A split to BUG-1228.
- 2026-11-18: released in 0.7.15.
