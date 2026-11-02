---
id: BUG-0373
title: Creature: a partner's creature always shows full health on the other screen (puppet health pinned to its max)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: player reports 2026-11-01 (host mark 13:13:19)
role: host
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-creature-hp
commits: d62e750, 116b213
tests: tools/tests/coop_creature_partner_hp.py (suite creature_partner_hp), coop_creature_brain_hp.py
signature:
docs: docs/re/creature.md#creature-max-hit-points
related: BUG-0375, BUG-0350
---

## Report
Host mark (0.7.3.3, Creature, host GOG 2024, guest Steam): "in creature stage we dont see each others
HP correctly (no dmg being dealt to other player on my screen)".

## Root cause
Observed (code and lane 9): `modes.cpp creature_drive` set every partner's puppet to its own full
health every frame ("puppets mirror the remote health, never die locally"); the partner's health and
max health arrive in every PlayerState but were only printed in the trace. The puppet's max is also
computed on the observer's machine from the puppet's species (the partner's creation is "another
species": level 0 + species health, docs/re/creature.md#creature-max-hit-points), so it differed from
the partner's own max after a brain upgrade. Logs: the guest's own game had 4/10, 1/14, 0/14 at
times; the host's puppet of the guest was full all along.

## Fix
`creature.partner_hp` (mods/coop/creature_hp.cpp): the puppet keeps the partner's reported health
(never below 1 % of its max: puppets never die locally; a dead partner's puppet is hidden), and the
hooked GetMaxHitPoints 0xC05D50 answers the partner's max for a puppet. Off: pinned to full (0.7.3.3).

## Verification
`coop_creature_partner_hp.py`: each player in turn takes a 45 % hit in its own game (`coop.team hit`,
the game's TakeDamage); within 3 s the other game's puppet has the same health and max health
(`coop.hp`: the puppet's health field and GetMaxHitPoints).
* FAIL with `SPORE_FIX_OFF=creature.partner_hp`: out/lab/20261101-080722-fix-w30-creature-hp (the other game's puppet at 16.0 / own 13.58 and 10.0 / own 8.46: full while hurt).
* PASS: out/lab/20261101-080343-fix-w30-creature-hp (with the other two tests), out/lab/20261101-081255-fix-w30-creature-hp (warm, with the regression tests creature, team, creature_desync, npc_run_hp, puppet_walk: 8/8).

## History
- 2026-11-01: opened from the host's report (0.7.3.3); reproduced and fixed on lane 9 (fix/w30-creature-hp).
- 2026-11-02: released in 0.7.3.4.
