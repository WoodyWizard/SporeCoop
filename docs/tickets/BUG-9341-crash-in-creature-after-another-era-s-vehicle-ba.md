---
id: BUG-9341
title: Crash in Creature after another era's vehicle bake was asked (era visitors, the creature dead in its death cinematic)
status: open
kind: crash
stage: creature
reported: 2026-11-22
source: TITAN QA run 2026-11-22 (crash-20261122-194216-i8)
role: host
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in:
branch:
commits:
tests:
signature: Editors::cEditor::HandleMessage+0x11E (data 0x00000000), 6 s after era visitors' bake ask
docs:
related: BUG-9296, BUG-9290, BUG-9291
---

## Report
TITAN QA run (feat/lineages-alpha, 2026-11-22): QA 8 hosted Abber (Creature), its creature dead (hp 0, the death
cinematic and input lock for minutes, nobody pressing anything). QA 9 entered Abber (Civ) as its host. 6 s after QA 8 got
QA 9's city vehicle design ("galaxy creations: ... (BUG-9290)", "era visitors: ... baked levels 0: asked the rest") it
crashed: 0xC0000005 at Editors::cEditor::HandleMessage+0x11E, a read of 0, the game's main thread (crash-20261122-194216-i8).
To reproduce: one game hosts a planet's Creature era (dead or not: not checked), another enters the same planet's Civ era.

## Root cause
Not looked into yet. The signature is BUG-9296's (the bake's end reporting to an editor), whose fix allows the bake in
the Creature stage only because "the Creature stage bakes the same models without trouble" (BUG-9291 runs): this run
contradicts that for at least a dead creature in its death cinematic. CORRECTION recorded in
docs/re/creations.md#creations-bake-in-tribe.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-22: opened.
