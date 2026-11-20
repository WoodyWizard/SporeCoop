---
id: BUG-1356
title: Test space_puppet (play_space.land): the guest zooms in on the planet it is still at when its clicks miss the wanted one
status: open
kind: test
stage: Space
reported: 2026-11-20
source: final-check-20261120-041304
role: 
versions: 0.7.15.5, unreleased
fixed_in:
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

## History
- 2026-11-20: opened from the 0.7.16 final check; fixed in play_space.py on fix/w60-misc.
