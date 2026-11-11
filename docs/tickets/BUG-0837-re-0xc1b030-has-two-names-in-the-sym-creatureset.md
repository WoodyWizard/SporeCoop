---
id: BUG-0837
title: RE: 0xC1B030 has two names in the .sym (CreatureSetSneaking and Creature_CombatVoice): one function
status: open
kind: tooling
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-sneak
related: BUG-0463 BUG-0600
---

## Report
Knowledge base contradiction found in the audit; no player-visible bug found.

## Root cause
Observed: data/symbols/overrides_dc04aee5.sym line 311 names 0x00C1B030 SporeCoop::CreatureSetSneaking (BUG-0463), line 635 SporeCoop::Creature_CombatVoice (BUG-0600); docs/re/tribe.md (BUG-0600 section) calls it the combat voice, docs/re/creature.md#creature-sneak the sneak setter. Both describe +0xBB1, +0xB80 and the sounds 0xC881120E / 0x529FEC69: one function. mirror_fx.cpp replays it on a guest's proxies with force = 1 (the sneak's force flag, which skips the sneak ability check); the host relays only a change of +0xBB1, so the replay matches the host's game (for an NPC without a sneak ability the host's call changes nothing and is not sent). A forced 'voice' on a proxy is a forced sneak: alpha eased to 0.2 by 0xC1CF30.

## Fix
Not fixed yet.

## Fix idea
Keep one name (CreatureSetSneaking, the behaviour) with the other as an alias comment; note in mirror_fx.cpp that force = 1 forces the sneak look; CORRECTION lines added in docs/re/creature.md and docs/re/tribe.md.

## How to test
no-fail-run: docs only. A check: `coop.sneak` on a guest proxy after a host NPC with a sneak ability enters a fight shows sneak=1 on both games.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
