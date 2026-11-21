---
id: BUG-9315
title: Tribe: the green Village food line at the top right shows in releases (a test line)
status: open
kind: bug
stage: Tribe
reported: 2026-11-21
source: owner 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Owner 2026-11-21: "I also saw green text on the top right still, we need to remove that from our releases. It can stay during testing if you need it."
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
The green "Village food   Host x   Guest y" line at the top right (coop.cpp on_overlay, tribe::village_food_line) was drawn in every build; it is a test line.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
The line is drawn only with `[coop] dev_lines=1` in the mod's config (QA copies); the food stays in the co-op window. Switch `coop.fix hud.no_test_lines off`: always drawn. Branch feat/lineages-47.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
