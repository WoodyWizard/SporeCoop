---
id: BUG-0944
title: Lab: creature_kills fails 5 of 5 on lane 4: the combat stance click is sent into the guest's stage-entry pause and lost, the hunt is played in the social stance
status: fixed
kind: test
stage: creature
reported: 2026-11-11
source: final check of 0.7.12 (lab 20261111-151535, 20261111-170604)
role: guest
versions: 0.7.12
fixed_in: unreleased
branch: fix/w47-final12c
commits: 6d0b109a
tests: creature_kills (coop_creature_kills.py; twin KILLS_BLIND_STANCE=1), family_attack
signature:
docs: docs/06-testing.md#testing-stage-entry-input
related: BUG-0690, BUG-0313, BUG-0021
---

## Report
The third final check of 0.7.12 (rel/0.7.12 lab 20261111-151535, lane 4) and its rerun (20261111-170604, lane 4, 4 of
4) failed creature_kills: "FAIL the guest killed host NPC #20 with real input (35 combat keys)", the prey at full
health for the whole 90 s while it ran 130-200 m off, then the three follow-up checks. The test had passed on the
same lane in the second final check (20261111-090547, rel/0.7.12 at 94ca59ab) and on fix/w47-final12, so it looked like
a regression of the merges since (BUG-0933, BUG-0934, BUG-0939, BUG-0942).

## Root cause
A test race, not the mod: the hunt's first input, the click on the combat stance button, is sent about 1.5 s after
`start()` returns, and on lane 4 the guest's game still stands on the stage entry's pause then (pause reason
04BF38A7, a black screen; docs/re/tribe.md#tribe-game-time-pauses). The click is lost, the guest stays in the social
stance and plays the whole hunt there: the click on the animal is the social approach, key 1 sings, the herbivore
walks away from the follower and nothing bites.

Observed:
* It is the lane, not the code: at HEAD df6b707d the test passes on lane 3 (20261111-171711) and lane 5
  (20261111-171905) and fails on lane 4 (20261111-171903); the code of the morning's PASS (94ca59ab, its own build in
  a scratch worktree) fails on lane 4 the same way (20261111-172725-at-94ca59ab: 1 key, 7 clicks, 6 hp to the end).
* Every failing run has the guest's 10 s trace line with `busy=paused` and the avatar still on its spawn point
  (-171.6 -467.5 -84.0); every passing run has it running there (9 runs compared).
* `coop.pauses` on the guest right after `start()` (a probe, 2 x per lane): lane 4 `flags=3 paused=1 reasons:
  04BF38A7=1` for 4.7 s (both times), lanes 3 and 5 `paused=0` at once. A screenshot inside the window is black with
  only the mod's overlay.
* The failing run's last screenshot (out/lane4/shots/kills_guest_corpse.png) shows the green social stance and the
  mission card "Impress 3 Kook to ally"; the passing lanes show the red combat row and "Hunt 2 Kook". The twin run
  prints `stance 0` and the new check fails first: "the guest hunted in the combat stance (the game's ability mode 0)".
* With the wait the same lane prints "game time ran after 4.5-4.9 s, stance 1" and kills in 16-52 s.
Inferred / not found: why lane 4's entry pause got 4.5 s longer between 10:42 and 15:15 (the guest's profile: its
GraphicsCache.package is at the 1 GiB cap with a 156 MB overflow file, as on lane 3; the same own creature 'Kirgle'
as in the morning). The pause is the game's wait for its bakes and has always varied by lane and stage (0 s here,
7-9 s and 10-40 s in Tribe, BUG-0313); the test relied on it being over.

## Fix
Test only (no mod change):
* twoinst.py: `game_runs(con)` waits until `coop.pauses` says `paused=0`; `ability_mode(con)` reads the game's stance
  (cCreatureGameData::GetAbilityMode); `set_stance(con, 'combat' | 'social')` waits for the game time, clicks the
  stance button and reads the stance back (up to 4 clicks, none when it is already on).
* coop_creature_kills.py: the hunt waits and sets the stance that way, takes it again when the game left it (a
  creature swap builds the bar again), prints the wait and the stance, and has a check of its own "the guest hunted
  in the combat stance", so the next such failure names its cause. `KILLS_BLIND_STANCE=1` is the old single
  unchecked click (the twin).
* coop_family_attack.py (the same blind click on a guest right after the join): `set_stance`.
Not changed: join_during and avatar_stale_target click the stance on the host (its game runs long before the guest
is in); play_creature plays for minutes and clicks the stances many times.

## Verification
* FAIL: 20261111-173758 (lane 4, KILLS_BLIND_STANCE=1: stance 0, 1 key, 6 clicks, the prey at 6 hp); the same failure
  without the twin: 20261111-171903 (lane 4, HEAD), 20261111-172725-at-94ca59ab (lane 4, the code that passed in the
  morning), rel/0.7.12 20261111-151535 and 20261111-170604 (4 of 4).
* PASS: 20261111-173009 (lane 4, 5 of 5), 20261111-173012 (lane 3, with family_attack).
* The area's tests with the same code: 20261111-180402 (play_creature, npc_lost_target, herd_help_stale,
  avatar_stale_target, family_attack on lanes 3-5: 5 of 5), 20261111-182544 (play_creature on lane 4, creature_kills on
  lane 5, with the final build).

## History
- 2026-11-11: opened; found to be the lane's longer stage-entry pause and the test's unchecked click; fixed in the
  test (fix/w47-final12c).
