---
id: BUG-0450
title: vote_net_4p [return]: two viewers draw a player as the species of another player standing next to it
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: lab 20261101-223718 vote_net_4p#2
role: guest
versions: fix/w37-vote-leaver 324752dc (main f0cefbaa + tests)
fixed_in: 0.7.11
branch: fix/w46-g4-tests
commits: 26750599
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
Observed (code, G4): species_view took the puppet NEAREST to where the viewer's sync shows the subject; after the
rejoin the four players stood 1-3 m apart (the host's trace d=1..3) with NEAR 6 m, so the nearest puppet could be
another player's: both wrong pairs of the report were a swap between players of the party, never an unknown key.
Inferred: no puppet was built with a wrong body (the matcher's ambiguity); the new matcher would show a wrong body.

## Fix
The census prints a puppet's player id (census.cpp, `owner=` of a puppet row, via the remote that holds it); species_view
takes the subject's own puppet by that id and still checks it is drawn within NEAR of where the viewer shows the
subject (the nearest puppet only for an old build without owner ids). Test change, plus a read-only census field.

## Verification
FAIL (the nearest matcher): lab 20261101-223718 vote_net_4p#2 (the report), feat/w33-tests-session 20261101-153001
party_rejoin_net. PASS 3 of 3 on lane 3: out/lab/20261108-091929 (every viewer draws every other player as its species,
the puppets matched by player id).

## History
- 2026-11-01: opened.
- 2026-11-08 (fix/w46-g4-tests): the matcher takes the subject's puppet by player id (census owner=); vote_net_4p 3 of 3; fixed.
- 2026-11-08: released in 0.7.11.
