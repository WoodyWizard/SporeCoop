---
id: BUG-0291
title: tribe: animals far from the player tribe are missing on guests (NPCs are sent only within 250 m of the tribe)
status: open
kind: desync
stage: Tribe
reported: 2026-10-30
source: fix/w9-tribe while analysing BUG-0288 (static, from docs/re/host-centric.md M5/M6/M12)
role: guest
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
Static reading (not verified at runtime), found while analysing BUG-0288 and pointed out by the
host-centric audit (docs/re/host-centric.md on docs/w9-host-centric, items M5, M6, M12):
* npc.cpp host_tick: in the Tribe stage the host streams NPC animals only within kRtsRelevance
  (250 m) of the stage centre (the player tribe), one centre for every guest; the guest's update
  filter removes the guest's own animals. A guest that looks at a spot farther than ~250 m from
  the village sees no animals there, while the host's game may have them.
* The campaign's coverage check (coop_multi.measure) counts the host's NPCs within 90 m of the
  guest's pointer: with the pointer far from the village such NPCs are counted and never sent, a
  coverage desync that is this gap (or a test artefact, if the host's game has no live animals
  there either: the planet activation pass 0xACE4E0 follows the host's camera).
Not seen at runtime yet. The campaign's coverage finding now names the missing keys per kind
(coop_campaign.py, fix/w9-tribe), so the next 4-player campaign shows whether `n` keys go missing.

How to reproduce (proposed): tribe fixture, 2 players, the guest moves its camera 300+ m from the
village near animals of the host's world; compare `coop.sync` n keys within 90 m of the guest's
pointer on both machines.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
