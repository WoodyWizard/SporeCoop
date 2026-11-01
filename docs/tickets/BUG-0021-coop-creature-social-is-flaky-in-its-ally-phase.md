---
id: BUG-0021
title: coop_creature_social is flaky in its ally phase
status: open
kind: test
stage: Creature
reported: 2026-10-25
source: test runs
role: both
versions: 0.7.2.2+
fixed_in: 
branch: fix/w4-tests
commits: 5a72494
tests: tools/tests/coop_creature_social_ally.py (suite creature_social_ally, creature_social_ally_player)
signature: 
docs: docs/06-testing.md#testing-w4-tests
related:
---

## Report
3 OK / 3 FAIL on 0.7.2.2, 0.7.2.3 and main: the Kooks end at 2 of 3 impressions.

## Root cause
Not settled. Seen (2026-10-28): besides refusals, Kooks die during the ally phase and the species'
impressed count falls behind the host's `impressed` counter. Hypothesis (not checked): `engage()`
clicks the social stance button on every call, a second click may leave the social stance, and the
next key 1 is then a bite (a Kook killed, impressions lost). Next: print the stance before each key
press (or only click the button when the stance is not social) and log Kook deaths
(`coop.npc list` DEAD, attacker).

## Root cause (2026-11-01, feat/w33-tests-creature)
Not only the test: coop_creature_social_ally.py reads the stance from the game (GetAbilityMode, clicked only when
not social, so the "second click leaves the stance" hypothesis is excluded) and still saw the species record fall
in its first run (lane 4, 14:01): the guest impressed Kooks #21, #18, #20 (the host's counter impressed=3, each
of the three with the game's impressed flag on the host and on the guest), one game refused in between (#19, out
of range), no Kook killed (killed 0), and the Kook record (relation, impressed, killed) went (0, 1, 0) -> (4, 2, 0)
-> (3, 1, 0) instead of (6, 3, 0). The second run passed ((0, 1) -> (4, 2) -> (6, 3), each step event 3 by the
impressed NPC). Which event lowered the record is not known yet: the host now logs every change of the species
table (`social: relationship event <n> species ... relation a -> b, impressed x -> y`, social.cpp h_RelEvent) so the
next failing run names it. Candidate (inferred): the refused / timed-out social game of the guest leaves the
host's NPC in a social game with the guest's puppet that the game then ends as failed (a negative event).

Lab 20261101-141938 (creature_social_ally, lane 4) names the events (host log, the new diagnostics): the guest
impressed #52 (event 3: impressed 0 -> 1); then a Kook alpha #42 (2 rounds: the guest's feed "Impressing: 1/2")
got event 2 (impressed 1 -> 0) and event 0 (killed 0 -> 1) in the same frame, with `dna: 10 -> 19
cause=hunt:1+9` and "+9 DNA from Bob's hunt": **the guest's own avatar killed the Kook** (a hit request of the
guest applied by the host; the guest's health went 16 -> 14: the Kook bit back). Later #41 and #53 were impressed
((1, 0, 1) -> (3, 1, 0) -> (4, 2, 0)) and #53 was killed 7 s after its impression the same way (event 2 then 0:
impressed 2 -> 0, relation 4 -> 1), the species ended at (1, 0, 1): no ally. So BUG-0021's "Kooks die during the
ally phase" are kills by the guest itself during its social play. Which input attacks is not settled: the test
reads the game's stance before each key 1 (0 every time, which the mod and the tests take for social:
social.cpp `GetAbilityMode() == 0`; the button at 690, 998 sets 0, the one at 750, 998 sets 1), and it clicks the
Kook again between the rounds of an alpha (engage). Next: on a guest in the social stance, press key 1 and click
a mirrored Kook separately and watch `hits_sent` (`coop.npc` on the guest) and the host's `dna: ... cause=hunt:1`;
check what GetAbilityMode 0 / 1 mean (ModAPI's CreatureGameData).
Experiment (lane 4, 2026-11-01 15:0x, scratch script): next to a mirrored Kook on the guest, stance 0 + key 1: no
hit; stance 0 + a click on it: no hit (and no engage); stance 1 + a click: no hit; stance 1 + key 1: a hit
(`hits_sent` 0 -> 1). So 0 is the social stance (as the mod assumes) and key 1 bites only in the combat stance.
The lab run 20261101-144024 (creature_social_ally, SOCIAL_ALLY_PLANT run) lost the Kooks the same way ((1, 0, 1)).
The test now prints `key 1 sent a HIT (stance before / now)` and `the click sent a HIT` when the guest's
`hits_sent` grows, so the next failing run shows which input and stance attacked.

Lab 20261101-145803 (with the hit print): `#19: key 1 sent a HIT (stance before the key 0, now 0)` in the second
round of the alpha #19 (host log: event 2 then event 0, impressed 2 -> 0, killed 0 -> 1). So the guest's key 1 in
the social stance, next to the creature it is impressing, can make the guest's game hit that proxy. Why the game
plays an attack there is not known (inferred: the proxy has no AI and no social session, the game's key 1 then
falls back to an attack on the targeted creature).

## Fix
feat/w33-tests-creature: `social::drop_social_hit` (npc.cpp damage_filter, guest): a hit of the guest's own avatar
on an NPC proxy while the stance is social (GetAbilityMode 0) is not sent to the host. A first version dropped
only hits on the engaged creature; lab 20261101-151116 #3 showed the click that engages a Kook sending a hit
before any social act, so every proxy counts now. Test hits (`coop.npc hit`, npc::debug_hit) pass. `coop.status` social line: `social_hits_dropped=`. Switch: `coop.fix social.no_bite off`. Not verified
yet whether this covers every lost impression (see Verification). fix/w4-tests (5a72494) plays a refused game again with the same Kook (it walked out of the
range: host_act refuses acts from more than kActRange + 4 m) instead of moving on. Not enough: 1 OK of
4 runs.

## Verification
- feat/w33-tests-creature, creature_social_ally: FAIL with `SPORE_FIX_OFF=social.no_bite` 3 of 3 (lab
  20261101-152905: (1, 0, 1), (1, 0, 1), (3, 1, 0); run 1 had reached the ally and lost it to `key 1 sent a HIT`);
  PASS with the first (engaged-only) fix 3 of 3 (20261101-151116: (6, 3, 0) each, 1-3 dropped hits per run);
  PASS with the final fix (every proxy) 3 of 3: 20261101-153820 (with team, creature_kills, creature_party,
  social_console, corpse_positions: all OK), 20261101-154734 (2 runs).
  before the fix 4 of 5 runs failed (lab 20261101-141938, -144024, -145803; by hand 1 of 2).
- out/lab/20261028-030521: OK (ally (6, 3, 0)).
- out/lab/20261028-033546 (--repeat 3): 3 FAIL. The retry ran ("n19: no impression ... played
  again"), then: run 1 ended at (4, 2, 0) while the host's `impressed` counter said 3; runs 2 and 3
  ended at (1, 0, 1): a Kook had died during the ally phase (before the test's own kill, which is
  of another species) and the species' impressed count was 0 again.

## History
- 2026-10-25: reported (test runs).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: fix/w4-tests: refused games are played again; still 1 of 4 OK (Kooks die during the ally phase); not in the suite; left open.
- 2026-11-01: feat/w33-tests-creature: creature_social_ally (stance from the game); the record fell (4, 2) -> (3, 1) with no kill in 1 of 2 runs; diagnostics added.
