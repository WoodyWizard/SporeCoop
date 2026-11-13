---
id: BUG-1021
title: SporeKit: SK_EV_POST_INIT is fired after a failed cSporeApp::Init
status: open
kind: tooling
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-15
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
Audit 2026-11-12 for 0.7.14, finding KIT-15. Not seen at runtime, found by code reading. SporeKit fires SK_EV_POST_INIT
("main menu ready") also when the game's own Init failed and the game is about to exit.

## Root cause
Observed in code (main efd2e0ba), confidence high: sporekit/gamehooks.cpp:63 r = o_SporeAppInit(...); :66 a failed
Init only writes the report (diag_init_failed); :68 fire(SK_EV_POST_INIT) runs regardless. Latent: no subscriber of
SK_EV_POST_INIT exists in mods/ or sdk/ samples today (grep), so nothing runs on a failed start yet; a future
subscriber that touches game managers would run on a half-initialised game. Severity: Low (latent).

## Fix idea
`if (r) fire(SK_EV_POST_INIT, nullptr);` and document in sdk/include/sporekit.h that it is not fired on a failed
Init. Switch: `kit.fix initfail` already exists for that path; reuse it or add `kit.fix post_init_ok`.

## How to test
tools/tests/kit_initfail.py (the BUG-0521 marker makes Init return 0): add a check that the log has no
"SK_EV_POST_INIT" fire line (log it at the fire). FAIL on main, PASS with the fix.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
