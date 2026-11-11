---
id: BUG-0881
title: Creature: during the host's migration the guest walks to its own next nest, never checked against the host's, so a second migration can leave the two games on different home nests
status: open
kind: desync
stage: Creature
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0640, BUG-0496, BUG-0642
---

## Report
The host's species migrates to a new nest (a second migration in one session, or a guest standing far from the host). The guest's game then migrates to a nest that may not be the host's new one. After the claim the guest's home nest, respawn place and heal are at a different nest from the host's, and the row never corrects it. BUG-0640 lists this as "Not verified: two migrations in a row".

Severity: medium. Confidence: low.

## Root cause
* Observed (code): mods/coop/wsync.cpp:800, r_nest sends only the host's migration state and its current home nest; the host's target nest is never sent.
* Observed (code): mods/coop/wsync.cpp:841-845, s_nest: while the host's state is 1 or 2, the guest starts a migration when it shares the host's old home, with its own `target_nest()` (wsync.cpp:775, CreatureGame_NestHerdFor(1)) as the target. Only for host state 0 (line 844) is that target compared with the host's nest. The comment at wsync.cpp:756 says "when its own game has the same target nest", but the code checks it only for state 0.
* Observed (docs/re/creature.md:638): NestHerdFor(1) is the nearest player-species nest whose brain range holds the level, so the result can depend on where the guest's avatar stands (in the BUG-0640 report the guest was 410-440 m from the host).
* Inferred (low): with more than one player-species nest in range, the guest claims another nest; after the host's claim (state 0) the guest's NestHerdFor(1) is its own claimed nest, line 842 (`nest_dist(target, mine) < kSameNest`) returns 0 and it stays there for good.

## Fix idea
Send the host's target nest (its NestHerdFor(1) position) in row 11 and start the guest's migration only when its own target is within kSameNest of it, for states 1 and 2 as well as 0; otherwise log the mismatch, or give the guest the host's nest herd for the claim.

## How to test
Extend coop_nest_follow.py with a second migration (more DNA, the host dies and respawns again) and a case where the guest stands near a different player-species nest when the host's migration starts. Assert on the guest's game that its avatar herd's nest position (Herd_NestPosition) equals the host's within 30 m after each claim. Must FAIL on the current code in the far-guest case and PASS with the host's target compared.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0640. Severity medium, confidence low.
