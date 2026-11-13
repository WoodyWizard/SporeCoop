---
id: BUG-0829
title: Creature: DNA a guest earned while the host was away may move the guest's evolution bar twice (suspected)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-creature
commits: 5af05b04
tests: dna_away_bar
signature:
docs: docs/re/creature.md#creature-c2-audit-0713, docs/06-testing.md#testing-c2-creature-0713
related: BUG-0630 BUG-0205
---

## Report
No player report. Suspected: when the host returns from an editor / load / menu while a host DNA total is in flight, the guest's evolution bar runs ahead of the host's by the DNA it earned meanwhile.

## Root cause
Observed at runtime (confirms the code reading). The guest's solo DNA moves its bar when earned (AddEvolutionPoints) and goes to the host at the away end; a host total sent before the host added it lowered the guest's DNA through SetEvolutionPoints (bar untouched) and the next total raised it through AddEvolutionPoints: the bar moved a second time. Lab 20261112-073101 (SPORE_NET delay=250, the host's DNA changing every 0.1 s around the away end): host bar +80, guest bar +111 (the 40 solo DNA counted ~31 more).

## Fix
At the away end the guest remembers the solo DNA it sent; host totals without the host's Earned cause of this player (the ledger tail of MSG_DNA) are not applied until one has it, at most 5 s. Switch `coop.fix core.solo_await off`.

## Fix idea
After the away end ignore host totals until one arrives that includes the forwarded DNA (a sequence number or the ledger).

## How to test
Host opens an editor while the guest earns DNA, host returns with ~300 ms added latency (SPORE_NET delay); compare evolution_bar on both games 5 s later: equal (fix) vs guest ahead by the solo DNA (main).

## Verification
Test `dna_away_bar` (coop_dna_away_bar.py over SPORE_NET="delay=250"): the host goes to the menu, the guest earns 40 DNA, the host loads its world again and changes its own DNA every 0.1 s around the guest's away end; both games' DNA and evolution bar (`coop.dna bar`) 10 s later.
* FAIL: out/lab/20261112-073101 (`SPORE_FIX_OFF=core.solo_await`, lane 6): host bar +80.0, guest +111.0.
* PASS: out/lab/20261112-073116 (lane 7): +79.0 / +79.0, the same DNA.
* PASS: out/lab/20261112-080216 (lane 6, the switch on; only nest.mirror_epoch off in that run): the second PASS.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): fixed (core.solo_await); FAIL 20261112-073101, PASS 20261112-073116, 20261112-080216.
- 2026-11-12: released in 0.7.13.
