---
id: BUG-0829
title: Creature: DNA a guest earned while the host was away may move the guest's evolution bar twice (suspected)
status: open
kind: desync
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0630 BUG-0205
---

## Report
No player report. Suspected: when the host returns from an editor / load / menu while a host DNA total is in flight, the guest's evolution bar runs ahead of the host's by the DNA it earned meanwhile.

## Root cause
Code reading (line numbers of main e49e1e23 = 0.7.11.1): the away gain is applied through AddEvolutionPoints (coop.cpp:299-304) and sent to the host at the away end (1280-1285); a host total sent before the host added it is applied as soon as g_host_away is false (4241), lowering the DNA by SetEvolutionPoints (bar untouched, game.cpp:2004), the next total raises it again by AddEvolutionPoints (bar moved). Not checked: that AddEvolutionPoints moves the bar total here. Confidence: low-medium.

## Fix
Not fixed yet.

## Fix idea
After the away end ignore host totals until one arrives that includes the forwarded DNA (a sequence number or the ledger).

## How to test
Host opens an editor while the guest earns DNA, host returns with ~300 ms added latency (SPORE_NET delay); compare evolution_bar on both games 5 s later: equal (fix) vs guest ahead by the solo DNA (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
