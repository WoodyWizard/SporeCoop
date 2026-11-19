---
id: BUG-9264
title: Guest's HUD portrait keeps the old picture after its first creature is made (no png found for the new species)
status: open
kind: bug
stage: Creature
reported: 2026-11-19
source: MSI QA 2026-11-19
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0937, BUG-9263
---

## Report
MSI QA 2026-11-19: a fresh guest made its first creature (start editor) and played it in the host's world; the
Creature HUD portrait (bottom right) kept another creature's picture.

## Root cause
Observed: `coop.portrait` on the guest: `image=00000000 avatar=1 key=40626200!7CD928C8.2F7D0004 same=-1 sets=0`.
hud_portrait.cpp (BUG-0937) found no png for the new species' key (UTFWin::Image::GetImage failed), so it left the
game's picture. Not known yet: whether the png of a creature saved this session is under another key, or is not in
the resource system until a reload. Cosmetic.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
- 2026-11-19: seen while verifying BUG-9263; not fixed.
