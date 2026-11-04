---
id: BUG-0471
title: effects relay: play_creature passes 1 of 5 with the relay merged (3 of 3 without): puppet apart, chat typing lost, host freeze
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: orchestrator A/B, 2026-11-02 (rel/0.7.3.6 candidate)
role: 
versions: 
fixed_in: 0.7.4
branch: fix/w40-effects
commits: 9aad65c7 (effects.here), f7dff4b5 (effects_party in every stage), 607f0092 (effects_here test), 74b1fa5a (test fix)
tests: play_creature, effects_here, effects_party, effects_party_cell, effects_party_tribe, effects_party_civ, effects_party_space
signature:
docs: docs/re/creature.md#creature-effects-relay-ab
related: BUG-0451, BUG-0482, BUG-0499, BUG-0500
---

## Report
The 0.7.3.6 candidate with fix/w38-effects merged ran play_creature 5 times: OK once (out/lab/20261102-213928 in
SporeCoop-wt/rel/0.7.3.6), FAIL 4 times with different symptoms: the host's puppet 176.8 m from its player at a
respawn (20261102-204139), an NPC 9.8 m apart + "the chat line takes the typing while the creature lives (1 line
lost)" (20261102-212559), the host FROZE 10 s on a loaded machine (20261102-223713, 7 lanes), the puppet 11 m apart
(20261102-230314). Without the relay: 3 of 3 OK (main 20261102-211307, feat/w7-diag 20261029-164355, -172009).
Hypotheses to measure: the relay's messages (send_event, rate limit 40/s, host relays with broadcast_reliable)
delay player states (head-of-line on the reliable channel) or the chat; the main-thread work of the hooks during
fights (many effects); a looping effect stopped by the 8 s rule while the game still drives it.
Not merged into 0.7.3.6 for this reason. Next: play_creature x6 with and without the relay (coop.fix
creature.effects_relay off) on the same build, net stats (resends, backlog, state_age) side by side.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (A/B on one build, fix/w40-effects fe5afc69, lane 4, play_creature 10 min x4 each,
sequentially; numbers from the test log and both game logs, `out/ab_parse.py`):
- relay on (out/lab/20261103-120602): test 3 of 4 OK (lab with log gates 2 of 4);
  relay off, SPORE_FIX_OFF=creature.effects_relay (out/lab/20261103-124913): 0 of 4.
- the failures are the same kinds with and without the relay: chat line lost (BUG-0482), a puppet once
  11-177 m off at speed 0 (BUG-0499; also on main), no joint hunt / creature under the ground
  (BUG-0500), NPC missing or apart on the guest (sync-check gate, BUG-0487).
- puppet p90 0.49-0.52 m on / 0.50-0.72 off; state_age p90 0.1 s both, max 0.5-0.7 s both; backlog max
  600 both (the guest's world transfer after its menu exit); rtt max 650-830 ms both; fps 64-68 both.
- the relay sends 7-41 effects per 10 min (under 0.1/s): no head-of-line blocking is possible at that
  rate; hooks at most 0.0006 % of the main thread, tick 0.003 ms a frame.
So the relay does not make play_creature fail: the 1-of-5 of 2026-11-02 was the test's own flakiness
(main 0.7.3.6 without the relay: 2 of 6) and a machine with 7 lanes busy.
Found on the way (observed in code, then in the test): the relay sent effects to partners in other
places, and a receiver created world effects whatever its game showed (main menu, editor, loading
screen, another star).

## Fix
No change for the A/B (nothing to fix). `coop.fix effects.here` (on): a start is sent only while some
partner plays in the sender's place and played only while its owner plays in the receiver's place
(state same_place + PF_IN_WORLD); stops always pass; counted as `effects elsewhere` in coop.effects.
effects_party now runs in every stage with 3 players and checks the relay's frame-time cost.

## Verification
All run dirs in SporeCoop-wt/fix/w40-effects, lane 4.
- A/B play_creature: relay on out/lab/20261103-120602 (3/4 test OK), off out/lab/20261103-124913 (0/4).
- effects_here: PASS out/lab/20261103-133351 (guest in the main menu: host not sent 24, guest's game
  called 0 times); FAIL with SPORE_FIX_OFF=effects.here out/lab/20261103-141143 (the guest's game in
  the main menu created 12 of the host's effects).
- 3 players in every stage (effects_party, _cell, _tribe, _civ, _space): PASS out/lab/20261103-134650
  (relay cost 0.11-0.51 % of the frame time with all three games sending 2.5 effects/s); FAIL with
  SPORE_FIX_OFF=creature.effects_relay out/lab/20261103-135351 (with creature_effects, cell_effects,
  tribe_effects, effects_release: 0/9).
- creature_effects, cell_effects, tribe_effects, effects_release: PASS out/lab/20261103-133351.

## History
- 2026-11-02: opened.
- 2026-11-02: the chat symptom (1 line lost) also happened WITHOUT the relay (rel/0.7.3.6b, out/lab/20261102-232023): that one is not the relay's; the puppet-apart and freeze runs stay to be measured.
- 2026-11-03: on main 0.7.3.6 (no relay) play_creature failed 2 of 3 in the nightly run (chat line lost, n missing, joint hunts): its flakiness is not only the relay's; the A/B must be repeated with more runs per side before blaming the relay.
- 2026-11-03: A/B on one build (4+4 runs): the relay does not worsen play_creature or its net numbers; fixed the relay into other places (effects.here) on fix/w40-effects.
- 2026-11-03: released in 0.7.4.
