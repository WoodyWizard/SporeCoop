---
id: BUG-0123
title: planet seeding flags are shared by all threads, so another thread can switch the seeding off
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#102
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-space
commits: 66d7a30, 4c74ae4
tests: space_seed_thread
signature:
docs: docs/re/space.md#space-w3-trips-clicks-defenders
related: #102
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/102 (full text, discussion).

The co-op planet seeding (the same planets for an unvisited star in both games) depends on two process-wide, non-atomic globals:
- `g_generating`: a generation is under way.
- `g_in_names`: a counter, the generation is inside `GenerateRandomName`.

The RNG hooks compare the thread id to make sure only the generating thread's draws are redirected. The name counter and the "generating" flag are not per thread, though. A `GenerateRandomName` call, or a `RequirePlanetsForStar`, on any other thread changes what the generating thread sees. Two threads changing `g_in_names` with plain `++` and `--` can lose an update and leave it non-zero, which switches the seeding off for the rest of the session.

### Where
- The globals and the per-draw check (reads `g_generating` and `g_in_names`, compares the thread id): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L465-L486
- The name hook (non-atomic `++`/`--` on any thread that calls it): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L488-L495
- The generation hook (sets and clears `g_generating`, and skips the seeding while it is set by anyone): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L498-L516
- Hook installation (RandomInt / RandomFloat / GenerateRandomName / RequirePlanetsForStar): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L939-L946
- docs/re/space.md 7.3 states that "other threads draw from the same object" (the global RNG).

### How to reproduce
- Not reproduced. First check whether `GenerateRandomName` or `RequirePlanetsForStar` ever run off the main thread. Log `GetCurrentThreadId()` against `H->main_thread` in both hooks, or use `probe` on both symbols and read the `tid` in `probe.list`, during a `coop_space_jump.py` run.
- To widen the window in a test: a test-only command that calls `GenerateRandomName` from a second thread in a loop while `coop.space` triggers generations. Compare the planet counts on both machines.

### Expected
Only the generating thread's own state decides whether a draw is redirected. Other threads never switch the seeding off.

## Root cause
**What happens**

The racing sides are thread A (the generating thread, in `h_RequirePlanets` and its nested hooks) and thread B (any other thread entering `h_RandomName` or `h_RequirePlanets`).

1. While A generates, B is inside `GenerateRandomName`, so `g_in_names` is 1. `generator_draw` returns false for A's own draws, which then go to the global RNG. A makes different planets than the other game. No error is logged and `planets_seeded` is still counted.
2. A and B both change `g_in_names` at the same moment (load, add, store on two threads). One change is lost and the counter stays at 1 (or -1). From then on `!g_in_names` is false for every generation, and no star is seeded again until restart.
3. B calls `RequirePlanetsForStar` for another star while A generates. It sees `g_generating` and runs unseeded. Its draws come from B's thread, so they are not redirected either.

### Confidence
PLAUSIBLE. The globals are non-atomic and shared (checked by reading). It is not verified that `GenerateRandomName` or `RequirePlanetsForStar` are ever called off the main thread. This is a candidate cause of the open known issue "Space: an unvisited star's planets can still differ between the two games" (docs/known-issues.md: once the host made 10 planets of 0046101B and the guest 6). Not proven. A second reviewer verified the reading. Severity: low to medium.

## Fix
Fixed on fix/w3-space (66d7a30): the generation state (seed, generating, name counter) is `thread_local` (native TLS of the llvm-mingw build), so only the generating thread's own state decides; a nested RequirePlanetsForStar on the same thread still runs unseeded as before. The hooks log once when they run off the main thread (`space: ... runs on thread N, not on the main thread`). `coop.space seedfix 0`: the old shared state (tests). `coop.space namesheld <ms>`: another thread inside the name hook's bookkeeping (tests).

Suggested in the review:

- Make `g_generating`, `g_in_names`, `g_planet_rng` and `g_gen_thread` `thread_local`, or keep them in a per-thread struct. Then `generator_draw` needs no thread-id comparison.
- Log a warning (once) when one of the hooks runs on a thread other than the main thread, so the docs can record whether it happens.

## Verification
Done (2026-10-28, lane 6, GOG 2024): `coop_space_seed_thread.py` (suite `space_seed_thread`): another thread holds the name counter while the guest jumps into two stars nobody generated, the host follows, the PlanetIDs of both games are compared. `--without-fix` FAILS: 6 and 5 planets of 00460024, 1 and 4 of 0046002F (/tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w3-space/seed_nofix.log); with the fix PASS: 6 and 6, 1 and 1, the same ids (/tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w3-space/seed_pass1.log). Whether the game itself ever calls GenerateRandomName or RequirePlanetsForStar off the main thread is still not known: no off-main warning was logged in these runs; the warning stays to find out.

Planned in the review: Planned test (must FAIL without the fix and PASS with it):

`coop_space_jump.py` already compares planets. Add a variant that runs the second-thread name loop described above during the jump, and assert the same planet count and PlanetIDs on both games. It should fail on main if the race is real and pass with thread-local state.

### Related
The known issue in docs/known-issues.md ("an unvisited star's planets can still differ"), docs/re/space.md 7.3.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #102.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-space (66d7a30): thread-local seeding state; space_seed_thread FAIL without / PASS with (forced second thread).
- 2026-10-28: fix/w3-space lab runs on lane 6: 20261028-010933 (14/14: the four new wave 3 Space tests and space_map, space_stale, space_fleet, space_puppet, space_rivalry, space_contracts, space_jump, space_guest_ui, space_allies, space_diplomacy), smoke 20261028-013951 (13/13; `ui` skipped: exclusive, needs a lane 1-5).
- 2026-10-28: released in 0.7.3.0.
