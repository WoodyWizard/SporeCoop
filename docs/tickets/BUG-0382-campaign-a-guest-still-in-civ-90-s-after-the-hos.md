---
id: BUG-0382
title: campaign: a guest still in Civ 90 s after the host entered Space (monitor stage_apart)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
role: guest
versions: 0.7.3.1 - main 15539ad
fixed_in: 0.7.3.4
branch: fix/w31-civ-gate
commits: d463fc0 cea77bf
tests: space_share_wait (coop_space_share_wait.py), gates_selftest.py (monitor stage_apart)
signature: MONITOR-VIOLATION stage_apart <guest>: <guest> plays Civilization ..., the host Space ...
docs: docs/06-testing.md#testing-civ-gates, docs/07-modes.md
related: BUG-0019, BUG-0287, BUG-0301, BUG-0357
switch: world.share_wait_notice
---

## Report
Monitor calibration over recorded runs: campaign_space 11 stage_apart violations (a guest stays in Civ about 90 s after the host enters Space).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab 20261031-205600, host and guest logs, session clock): the host entered Space at
s 570.7 ("Alice entered the Space stage: everybody follows" on every guest). The share then waited
twice, both by design for limits of the game:
1. s 570.7 -> 610.5: the Space start cinematic (BUG-0019: a save during a cinematic breaks; the
   host's perf line says "Space, cinematic"; input_lock=1 on the host).
2. s 610.5 -> 648.2: "a player is waiting for the world: leave the planet to save": the Space stage
   saves only off a planet (docs/07-modes.md 7.2; a forced planet save loads with UI faults every
   frame). The campaign's host spent this time on the home planet's captions, the mission control
   talk and zooming out.
3. s 648.2 -> 659: 2 s settle, the save (1.8 s), the transfer (1.3 s) and the guests' load: 11 s.
Meanwhile every guest kept playing its old Civ world (host_away=0, nothing of the host's Civ
any more) and nobody but the host was told why. The vote, the transition path and the world share
work as designed; the 90 s are the game's planet rule plus the time the host stays on the planet
(which a real player decides: the guests wait as long). Not a bug of the share itself.

## Fix
`world.share_wait_notice` (d463fc0): when a share waits because the ship is on a planet, the host
sends every player a notice once per wait ("Alice's world goes to the others when Alice's ship
leaves the planet (the Space stage saves only in a solar system or the galaxy)"); the host's own
trace line ends with `share_wait=planet` (or `cinematic` while follow_phase holds a share for a
cinematic). The monitor's stage_apart (cea77bf) counts no guest as apart while the host's line
declares such a wait; an undeclared stay of 90 s is still a violation. The guests still join only
when the host's ship leaves the planet (not changed: a planet save does not load).

## Verification
* space_share_wait (host on a planet, guest rejoins; the same wait as after Civ -> Space): FAIL with
  `SPORE_FIX_OFF=world.share_wait_notice` lab 20261101-095159 (no notice on the guest, no
  share_wait on the host); PASS lab 20261101-095402 (2 runs: the guest's log has the notice, the
  host's trace share_wait=planet, the guest loads the host's system after the takeoff).
* gates_selftest.py: "no violation while the host says its share waits for the planet" fails on
  the old monitor, passes now; guests in Civ 2 minutes without a declared wait stay a violation.
* Not run: a whole campaign_space with the fix (the monitor replay of the recorded runs has no
  share_wait field: the old logs keep their 11 violations).

## History
- 2026-11-01: opened.
- 2026-11-01 (fix/w31-civ-gate): cause found (the game's planet rule + the Space cinematic wait), the wait declared and told to everybody.
- 2026-11-02: released in 0.7.3.4.
