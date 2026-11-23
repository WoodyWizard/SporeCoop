---
id: BUG-1356
title: Test space_puppet (play_space.land): the guest zooms in on the planet it is still at when its clicks miss the wanted one
status: fixed
kind: test
stage: Space
reported: 2026-11-20
source: final-check-20261120-041304
role: 
versions: 0.7.15.5, unreleased
fixed_in: 0.7.16
branch: fix/w60-misc
commits: 8273ee9a
tests: space_puppet
signature:
docs:
related: 
---

## Report
Final check of 0.7.16 (lab 20261120-041304, lane 3): space_puppet FAIL "round 4: both on planet 40460035 and see each
other" and, following from it, "round 4: the game freed the puppet, both still draw (freed by the game: host 4, guest
3)" (no puppet to free: they were not together). Rounds 1-3 passed; the same tree passed in 20261119-214823.

## Root cause
Observed (space_puppet.guest.game.log): round 3 ended with the guest taking off from P03 (`Space 43460035 -> Space
80460035` at 05:04:45); in round 4 the host landed on the home planet 40460035 (05:04:54) and the guest's
`land_retry('00460035')` landed on 43460035 again (05:05:32): its clicks on the home planet did not make it the
active planet (`coop.space star` active), and `Player.land()` zoomed in after its 3 click attempts anyway, which lands
on the planet that is still active (the one the ship just left). land_retry stops once the ship is on a planet, so the
wrong landing ended the round. A test fault (tools/tests/play_space.py), not the mod.

## Fix
play_space.py `land(want)`: when a planet is wanted and the clicks did not make it the active one, it does not zoom in
(not landed, counted as `planet_misses`); land_retry clicks again. land() without a wanted planet is unchanged.

## Verification
no-fail-run: test-only change (the harness's landing). Lab tonight: space_puppet PASS (rounds 1-4). Open, waiting for
the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): space_puppet PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643. Closed.

## History
- 2026-11-20: opened from the 0.7.16 final check; fixed in play_space.py on fix/w60-misc.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
