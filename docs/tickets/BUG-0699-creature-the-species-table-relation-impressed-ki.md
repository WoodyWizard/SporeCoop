---
id: BUG-0699
title: Creature: the species table (relation, impressed, killed) differs between the host and a guest (detector species_relation / species_only / species_killed)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-07
source: K2a audit of BUG-0419 (0.7.11), detector
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state-b
commits: 1391b0ac 149ac2b6
tests: species_table
signature:
docs: docs/re/creature.md#creature-table-cadence-stun
related: BUG-0419
---

## Report
Detector rows of the BUG-0419 class in Creature (lab runs of 2026-11-06/07, 1948 Creature comparisons): `Creature:state:species_relation` 12 (e.g. 2 on the host, 3 on the guest), `Creature:state:species_killed` 10, `Creature:state:species_only` 2 (campaign_tribe, long_creature, mem_reserve).

## Root cause
Observed (lab species_table): a change of the host's table reached the guest after 0.13-0.95 s (the host looked once a second); with the 1.5 s bracket of the detector this explains part of the rows. species_only rows came from samples during a guest's rejoin (planet nest 0 / 113 in the same sample). Not explained: rows that stay over 1.5 s in 4-player campaigns at low frame rates; the guest now logs every record it takes from the host's table with the table's age (`social: species ... from the host's table`) for the next campaign.

## Fix
`social.table_fast`: the host looks at its table every 0.25 s (sent only on a change); the guest had a change within 0.07-0.31 s.

## Verification
* FAIL with `SPORE_FIX_OFF=social.table_fast`: out/lab/20261107-230154 (step 2a, 0.5 s).
* PASS: out/lab/20261107-224108, out/lab/20261107-225634.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
- 2026-11-08: released in 0.7.11.
