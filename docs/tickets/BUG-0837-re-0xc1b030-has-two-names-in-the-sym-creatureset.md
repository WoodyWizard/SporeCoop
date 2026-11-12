---
id: BUG-0837
title: RE: 0xC1B030 has two names in the .sym (CreatureSetSneaking and Creature_CombatVoice): one function
status: fixed
kind: tooling
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-small
commits: 3a3c5d3b
tests: make test (tools/re_lint.py)
signature:
docs: docs/re/creature.md#creature-c1b030-decided, docs/re/creature.md#creature-c1b030-two-names, docs/re/tribe.md (BUG-0600 section)
related: BUG-0463 BUG-0600
---

## Report
Knowledge base contradiction found in the audit; no player-visible bug found.

## Root cause
Observed: data/symbols/overrides_dc04aee5.sym line 311 names 0x00C1B030 SporeCoop::CreatureSetSneaking (BUG-0463), line 635 SporeCoop::Creature_CombatVoice (BUG-0600); docs/re/tribe.md (BUG-0600 section) calls it the combat voice, docs/re/creature.md#creature-sneak the sneak setter. Both describe +0xBB1, +0xB80 and the sounds 0xC881120E / 0x529FEC69: one function. mirror_fx.cpp replays it on a guest's proxies with force = 1 (the sneak's force flag, which skips the sneak ability check); the host relays only a change of +0xBB1, so the replay matches the host's game (for an NPC without a sneak ability the host's call changes nothing and is not sent). A forced 'voice' on a proxy is a forced sneak: alpha eased to 0.2 by 0xC1CF30.

## Fix
Decided: 0xC1B030 is `SporeCoop::CreatureSetSneaking` (BUG-0463). Its ability check is the Creature stage's sneak (type 0xD: the host's fixture creature has it and the alpha reached 0.2 at runtime, docs/re/creature.md#creature-sneak) or the captain's stealth (0x52); +0xBB1 makes 0xC1CF30 ease the alpha to 0.2; a hit (TakeDamage) ends it. The "combat voice" sounds 0xC881120E / 0x529FEC69 are its start and end sounds.
* `data/symbols/overrides_dc04aee5.sym`: one entry (CreatureSetSneaking) with a CORRECTION; `SporeCoop::Creature_CombatVoice` removed (its callers and sounds moved into the entry), the frozen GOG 2016 list line for it removed.
* mods/coop/mirror_fx.cpp hooks the function by that name; a comment says that the replay with force = 1 is a forced sneak (the host relays only the changes its own game made, so a proxy shows what the host's NPC shows).
* CORRECTION lines in docs/re/creature.md#creature-c1b030-decided and docs/re/tribe.md.
No behaviour change (the same address, the same hook).

## Fix idea
Keep one name (CreatureSetSneaking, the behaviour) with the other as an alias comment; note in mirror_fx.cpp that force = 1 forces the sneak look; CORRECTION lines added in docs/re/creature.md and docs/re/tribe.md.

## How to test
no-fail-run: docs only. A check: `coop.sneak` on a guest proxy after a host NPC with a sneak ability enters a fight shows sneak=1 on both games.

## Verification
no-fail-run: knowledge base only (the .sym, the docs, a symbol name in the code); `make test` (re_lint: every name the code asks for is in the .sym) passes.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-small): fixed: CreatureSetSneaking is the name, the alias removed, CORRECTIONs added.
