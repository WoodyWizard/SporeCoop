---
id: BUG-0399
title: Tribe: the guest shows some of the host's animals 0.75-1.25 m under its ground for 20-30 s after joining (driven at their own height)
status: open
kind: bug
stage: Tribe
reported: 2026-11-01
source: lab monitor (feat/w32-monitor-diag, proxies rule), lane 6
role: guest
versions: unreleased
fixed_in:
branch:
commits:
tests: tribe_coop, chaos_tribe (monitor rule proxies)
signature:
docs: docs/06-testing.md#testing-monitor-fields
related: BUG-0329
---

## Report
The invariant monitor's `proxies` rule (guest trace line `proxy_under=`, sampled 4 times a second at
the start of the frame) found NPC proxies under the guest's own ground (cPlanetModel::GetHeightAt) right
after the guest entered the host's Tribe world:
* out/lab/20261101-123454-feat-w32-monitor-diag tribe_coop: `proxy_under=4` on the lines at s 20 and
  s 30, 0 from s 40 on; `npc: proxy 14 (species 16DE4FEA) 0.75 m under this game's ground ... for 1.0 s,
  the host's NPC 0.6 m from it (driven)`, `proxy 2 (species 0680AA69) 1.25 m under ... for 4.7 s, the
  host's NPC 0.4 m from it (driven)`.
* out/lab/20261101-120947-feat-w32-monitor-diag chaos_tribe: 1 proxy under on the lines at s 20 and 30.
The player tribe's members on the same guest read 0.00 m (`agl=` on the trace lines): the query matches
the ground the members stand on.

## Root cause
Inferred, not checked: Tribe proxies are driven by `drive::step` with `own_height` (BUG-0329,
drive_plan.h `at_own_height`): the target keeps the proxy's own distance from the planet's centre. A
proxy spawned or teleported at the host's absolute height where the guest's ground is higher starts
under it and keeps that height; the game does not lift a creature placed under its terrain
(docs/re/creature.md#creature-puppet-ground). They come up after 20-30 s (a walk, a teleport).

## Fix
Open. Allowed in the lab meanwhile (suite.py ALLOW_ALL, monitor kind `proxies`, Tribe under the ground).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
