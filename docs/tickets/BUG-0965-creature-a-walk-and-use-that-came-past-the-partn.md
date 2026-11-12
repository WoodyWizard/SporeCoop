---
id: BUG-0965
title: Creature: a walk-and-use that came past the partner's creature stops beside it 1.4-2 m from the animal without a bite
status: open
kind: bug
stage: Creature
reported: 2026-11-12
source: fix/w48-pass lab 2026-11-12 (partner_pass)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-partner-pass-rel
related: BUG-0913
---

## Report
With creature.partner_pass on, the guest's walk-and-use from behind the host's creature (1-1.6 m from the animal) came
to gap 1.1-1.3 m (the bite reaches 1.0), then stood at 1.4-2.0 m beside the host's creature for 12 s without a bite
(the animal's health in the host's game unchanged), further presses doing nothing (lab 20261112-114736 r10,
-122259 r10; about 1 round in 15).

## Root cause
Not a Havok pair: `coop.partnerpass phantom_pairs=` and pairs_skipped show the avatar-puppet pairs refused and no
phantom pair between them. Inferred: the walk-and-use's arrival or the game's attack position next to another
creature (not read). The test does not count it as the BUG-0913 stop (that one is behind the partner).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
