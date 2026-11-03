---
id: BUG-0471
title: effects relay: play_creature passes 1 of 5 with the relay merged (3 of 3 without): puppet apart, chat typing lost, host freeze
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: orchestrator A/B, 2026-11-02 (rel/0.7.3.6 candidate)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
- 2026-11-02: the chat symptom (1 line lost) also happened WITHOUT the relay (rel/0.7.3.6b, out/lab/20261102-232023): that one is not the relay's; the puppet-apart and freeze runs stay to be measured.
