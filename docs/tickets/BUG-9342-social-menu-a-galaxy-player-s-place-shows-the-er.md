---
id: BUG-9342
title: Social Menu: a galaxy player's place shows the era key (Abber|Creature) instead of Abber - Creature
status: open
kind: bug
stage: menu
reported: 2026-11-22
source: TITAN QA run 2026-11-22 (Social Menu shots)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9334
---

## Report
TITAN QA 9, the Social Menu (F8) in the galaxy menu: a player of the galaxy on Abber's Creature era showed
"Abber|Creat..." under the name (the server's era key) where the menu writes "Samuth  -  Creature" elsewhere.

## Root cause
Inferred from the code: coop.cpp social_tick passes the PLAYERS entry's planet (the era key, "<planet>|<Era>") to
where_text as it is; net::galaxy::era_display would make it "Abber (Creature)" or the planet and its stage.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-22: opened.
