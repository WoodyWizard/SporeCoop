---
id: BUG-1148
title: Space: plants and animals a guest places on a planet it terraformed do not reach the host (the planet is not stabilized there)
status: open
kind: desync
stage: Space
reported: 2026-11-15
source: discord
role: 
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
Discord (bug question, 2026-11-15): in one session a guest terraformed a planet to T1 and placed the plants and
animals that stabilize it; when the host flew to that planet in the same session it was not stabilized and had no plant
or animal life.

## Root cause
Inferred from the code (not reproduced yet): only the atmosphere / temperature deltas and the planet record (+0xB0 /
+0xB4 / +0xB8) are synced (spacecoop.cpp K_TERRA, K_TERRA_REC; BUG-0749, BUG-0883). Nothing syncs a planet's
ecosystem (the plants and animals placed on it, which decide the stabilized T level): no code in mods/coop and no RE
note in docs/re/space.md for the per-planet ecosystem state. The cargo code (space_trade.cpp, AnimalCargo_Create,
PlantCargo_Template) only restores hold items.

## Fix
To do: RE of where the game keeps a planet's placed plants and animals (per planet record or per terrain data), a
message next to K_TERRA_REC, a twin test that places species with the ecosystem tools on the guest and checks the
host's planet (T level stabilized, species list).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
