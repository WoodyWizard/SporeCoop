---
id: BUG-9322
title: UI: the co-op notices are drawn over the Spore Guide window
status: open
kind: bug
stage: all
reported: 2026-11-21
source: QA 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/08-ui.md#ui-spore-guide-window
related: BUG-9319
---

## Report
Live QA 2026-11-21 (TITAN QA copy, Civ, guest screen): with the Spore Guide open (H) a co-op notice plate ("CivHost
earned 1040 money (shared)") was drawn over the guide's text (out/qa-shots/civ_bug9319_guide_PASS_switch_on.png, top
right). Reproduce: Civ with a partner earning shared money, press H.

## Root cause
Not analysed. Inferred: the notices are the mod's own window, not hidden by the guide (BUG-9319 hides only the world
labels).

## Fix
Not done (cosmetic).

## Verification
no-fail-run: not fixed yet.

## History
- 2026-11-21: opened.
