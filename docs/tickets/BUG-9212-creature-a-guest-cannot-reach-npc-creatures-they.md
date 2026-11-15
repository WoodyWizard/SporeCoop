---
id: BUG-9212
title: Creature: a guest cannot reach NPC creatures: they move away at the same speed or are pushed away, no attack or impress
status: open
kind: bug
stage: Creature
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-0361
---

## Report
Owner: "Titan couldn't get close to a creature. Upon walking up to them, the other creatures would walk away at the exact same speed, or pushed away. So they couldn't attack or impress."

## Root cause
Not analysed yet (guest Titan on this PC, a new Creature world). The guest sees the host's NPCs as proxies placed by the host; on the host the guest is a puppet. Candidates: the host's NPCs keep away from the guest's puppet (their AI or the puppet's collision pushing them), and the guest's proxies follow.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
