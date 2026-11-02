---
id: BUG-0450
title: vote_net_4p [return]: two viewers draw a player as the species of another player standing next to it
status: open
kind: test
stage: Creature
reported: 2026-11-01
source: lab 20261101-223718 vote_net_4p#2
role: guest
versions: fix/w37-vote-leaver 324752dc (main f0cefbaa + tests)
fixed_in:
branch:
commits:
tests: vote_net_4p, party_rejoin_net (species_view)
signature:
docs:
related: BUG-0447
---

## Report
vote_net_4p#2 in lab 20261101-223718 (lane 5) failed only `[return] every machine draws every other player as
the species that player plays`: "Alice draws Carol as 7C329711 (Carol plays the world's 065CBE70)" and "Dave
draws Bob as 065CBE70 (Bob plays his own 'Kirgle', 7C329711 on the host)". Every other check of the step
passed, also `each guest's game shows the other players where they are (<= 3.0 m)`; #1 of the same run
passed the check. The host's trace has every partner 1-3 m from the host at that moment (d=1..3).
Similar earlier: feat/w33-tests-session 20261101-153001 party_rejoin_net `[both] Carol draws Dave as 7CF261BB`.

## Root cause
Not analysed (outside fix/w37-vote-leaver). Observed: in both wrong pairs the species drawn is the species of
another player of the party (a swap, not an unknown key). Inferred, not proved: species_view
(tools/tests/coop_party_rejoin_net.py) takes the puppet NEAREST to where the viewer's sync shows the subject;
with four players spawned within 1-3 m of each other after the rejoin, the nearest puppet can be another
player's (a matcher ambiguity), or a puppet was really built with the wrong body. Next step: log each
puppet's distance to the sample and the second nearest, or match by the puppet's player id (census row).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
