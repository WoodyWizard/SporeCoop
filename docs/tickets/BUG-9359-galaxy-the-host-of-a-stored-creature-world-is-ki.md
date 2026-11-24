---
id: BUG-9359
title: Galaxy: the host of a stored Creature world is killed on arrival
status: fixed
kind: bug
stage: galaxy
reported: 2026-11-23
source: qa
role: 
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits:
tests: tools/qa_win (manual, the MSI: host Samuth, coop.team die, coop.galaxy leave on the death screen, enter again)
signature:
docs: docs/known-issues.md#known-galaxy-host-killed-on-arrival
related: BUG-9215, BUG-9265
---

## Report
The MSI, 2026-11-23, TITAN's galaxy server: a game entering Samuth's stored Creature world (and Testplanet01, a copy of
it) as its host was killed at once: the death screen "You were killed in battle! When wounded, retreat to your nest
to heal." while the stage's intro ran, twice (two profiles). After its check the game went on (respawn).

## Root cause
The stored world was saved while its host's creature was dead (the death screen up): Spore keeps the death in the
save (the avatar's health 0 and mbDead +0xB5E 1, read on arrival: "hp 0.0/20.0, mbDead 1"), and the next host loads
into the same death screen. galaxy_client's saves already waited for a quiet moment with the creature alive
(quiet(), owner test 2026-11-17), but the timed save gave up waiting after 150 s and every way out (Leave, the menu,
Alt+F4) saved at once. out/galaxy_fixtures/worlds_base's Samuth is such a save (the repro).
Not enough (tried first, 2026-11-24): full health and mbDead cleared at the first look at the arrived avatar (the
game still paused by the load): the death screen came all the same (the screen's state is restored by the load
itself, not from the health).

## Fix
galaxy_client.cpp start_upload / host_dead_tick: no galaxy save while the host's creature is dead (is_dead or health
0, the last look in the Creature stage, kept for Spore's own save on the way to the menu): the timed save waits for
the respawn (no 150 s limit then); leaving keeps the galaxy's last save and says so ("Could not save <planet> to the
galaxy: your creature is dead; the galaxy keeps the last save from before"). Switch `coop.fix galaxy.no_dead_save
off`: as before. Worlds already stored dead (the fixture) still open on the death screen once; its OK respawns.

## Verification
The MSI, TITAN's galaxy server (a copy of out/galaxy/worlds_8qa1, Samuth saved alive), the host MSIHost-NoShare-
(profile MSISpaceStart): enter Samuth, `coop.team die` (the death screen), `coop.galaxy leave` on it, enter again.
- FAIL (switch off, worlds_9359d): "Saved Samuth (Creature) to the galaxy." on the way out; the second arrival "hp=0/42
  flags=0x11", the death screen (images/bug9359_arrival_off_on.png, left).
- PASS (switch on, worlds_9359e): "leaving with the creature dead: no save (BUG-9359)", "Could not save Samuth ...
  your creature is dead; the galaxy keeps the last save from before"; the second arrival "hp=3/42 flags=0x1", in the
  world at its nest, no death screen (the same image, right).
- The first try (full health on arrival, switch galaxy.arrive_alive, not kept): "arrived dead (hp 0.0/20.0, mbDead 1):
  full health", hp=20/20 in the trace, and the death screen all the same.
- Not run: the timed save waiting past 150 s on the death screen (the same check in start_upload).
no-fail-run: the FAIL is the switch off on the same build.

## History
- 2026-11-23: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the galaxy save waits for a living creature), FAIL and PASS on the MSI.
