---
id: BUG-0185
title: Space: an unvisited star's planets can still differ between the two games
status: fixed
kind: desync
stage: Space
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: both
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.2
branch: fix/w13-space
commits: 0d69be7, 2d62fea, fdd1e69, 6cdb0d5
tests: space_seed_profile
signature:
docs: docs/re/space.md#space-seed-profile
related: BUG-0123
switch: space.seed_profile
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* Each game generates the planets of a star the first time it is needed (a flight there, the jump);
  the generation draws from the game's global random generator, so host and guest made different
  planets for the same star (count and PlanetIDs, e.g. 4 and 6). Since fix/space-jump the generation of
  the generating thread draws from a private generator seeded by the star (docs/re/space.md 7.3): the
  same planets in 8 of 8 probe sessions and in most `coop_space_jump.py` runs, but once the host made 10
  planets of 0046101B and the guest 6 (lane 7, GOG 2016, 2026-10-23). Not found: which draw still
  differs (another generator object, `rand()`, or state such as star +0x0C = play minutes, set right
  before the generation, or the empire's state read by 0xC309F0). `coop_space_jump.py` prints "same" or
  "DIFFERENT planets" without failing. A sure fix: the host sends the planet records of a newly
  generated star (or the guest asks for them before entering).

## Root cause
Observed (lanes 8 + 9, GOG 2024, the seeded generation now logs its inputs and draws):
`RequirePlanetsForStar` calls `cStarRecord_GetSpeciesProfile` right before `GeneratePlanetsForStar`.
A first load of the star's species profile (not cached at star +0x80 yet) draws RandomInt from the
global generator in 0x4D4B30 (return addresses 0x4D4E2F, 0x4D4F34). Inside the co-op hook those
draws went to the private, star-seeded generator and shifted every draw of the generation. Whether
a game had loaded that profile before depends on what it did earlier (galaxy view, an empire's
colour, GetEmpireForStar), so the two games differed. Same seed D34F3953 for 0046101B: one profile
draw gave 6 planets, two gave 10, none (profile cached) gives 8. 3 of 6 `coop_space_jump.py` runs
ended DIFFERENT (lab 20261030-135159, lanes 8/9); the per-draw trace showed the extra 0x4D4F34 draw
as the only difference before the planet count draw.
Ruled out by the same logs: the empire object (made in all runs), tech level, star flags, the
pending tech list, the template argument (always null, caller TravelToStar's 0xBB5920), threads.

## Fix
`spacecoop.cpp` hooks `cStarRecord_GetSpeciesProfile` (0xBBA670, verified thiscall, plain ret);
draws inside it stay on the global generator, like the planet names'. Every game then makes the
"cached" layout (8 planets of 0046101B). Switch: `coop.fix space.seed_profile off` (old routing).
The seeded generation logs one INFO line per star (seed, count, draws, hash); SK_DEBUG adds the
records and the first 48 draws. Note: planets of stars nobody visited change once versus 0.7.3.1
(both players run the same version, so they match). Names (GenerateRandomName) still draw from the
global generator: names may differ (not compared).

## Verification
Test `tools/tests/coop_space_seed_profile.py` (suite `space_seed_profile`): the host loads the
species profile of the unvisited NPC star 0046101B (`coop.space profile`), the guest does not; the
guest jumps there, the host follows; both games' loaded PlanetIDs must be equal.

* FAIL without the fix (lane 9, `SPORE_FIX_OFF=space.seed_profile`): lab run 20261030-142550
  (8 and 6 planets); direct run on lane 9 with `--without-fix`: 8 and 6.
* PASS with the fix: direct run lane 9 (8 and 8, both logs hash 5FA0B667); lab run
  20261030-142409 (fix on: the test switched it back on, 8 and 8); lab run 20261030-142729 (lane 9,
  8 and 8, in the Space regression run).

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w2-space: still open. Not reached in wave 2 (budget spent on the 7 other Space tickets). What is left: find the draw that still differs (or the sure fix from the report: the host sends the planet records of a newly generated star, a protocol feature). No new runtime data.
- 2026-10-28: fix/w3-space: still open. BUG-0123 (fixed there) is one cause that makes exactly this difference: with another thread inside GenerateRandomName during the generation the guest made 5 planets where the host made 6 and 4 where it made 1 (coop_space_seed_thread.py --without-fix); with the per-thread state both rounds matched. Whether the game itself ever names things off the main thread is not known yet: the mod now logs once (`space: GenerateRandomName runs on thread N, not on the main thread`). Not reached: the sure fix (the host sends a new star's planet records).
- 2026-10-30 (fix/w13-space): root cause found with logs of the seeded generation (species profile load draws), fixed behind space.seed_profile; FAIL 20261030-142550, PASS 20261030-142409 + direct runs.
- 2026-10-30: Space regression on lanes 8/9, lab 20261030-142729: 12/13 (space_seed_profile, space_coop, space_play, space_page, space_share_takeoff, space_captain, space_puppet, space_rivalry, campaign_space, space_jump, space_seed_thread, space_land_early ok; space_finale failed on a counter race unrelated to this change, BUG-0320).
- 2026-10-30: more PASS runs: lab 20261030-145308 (space_seed_profile x2, 8 and 8; #1 on lane 8 failed only on the lane-8 guest fault BUG-0323, also seen on main) and 20261030-150155 (space_seed_profile x3 on lane 9 ok; space_jump same planets 3/3, lane 8 runs failed on BUG-0323).
- 2026-11-01: released in 0.7.3.2.
