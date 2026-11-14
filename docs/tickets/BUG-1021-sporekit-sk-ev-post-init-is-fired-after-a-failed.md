---
id: BUG-1021
title: SporeKit: SK_EV_POST_INIT is fired after a failed cSporeApp::Init
status: fixed
kind: tooling
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-15
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-kit2
commits: 702f51dd
tests: kit_initfail
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

Observed vs inferred (2026-11-13, fix/w49-kit2): observed at runtime with the new log line: after the test's failed Init the log had "SK_EV_POST_INIT fired" (out/lab/20261113-083626, switch off). No subscriber exists, so the effect stays latent (inferred).

## Fix idea
`if (r) fire(SK_EV_POST_INIT, nullptr);` and document in sdk/include/sporekit.h that it is not fired on a failed
Init. Switch: `kit.fix initfail` already exists for that path; reuse it or add `kit.fix post_init_ok`.

## How to test
tools/tests/kit_initfail.py (the BUG-0521 marker makes Init return 0): add a check that the log has no
"SK_EV_POST_INIT" fire line (log it at the fire). FAIL on main, PASS with the fix.

## Fix
sporekit/gamehooks.cpp: SK_EV_POST_INIT is fired only when cSporeApp::Init returned non-zero, logged either way
("SK_EV_POST_INIT fired" / "not fired: the game's own Init failed"). sdk/include/sporekit.h documents it. Switch
`kit.fix postinit` (new); the test marker mods	est-initfail-<instance> with "postinit" in it turns it off from the
start.

## Verification
Test kit_initfail, new check "SK_EV_POST_INIT is not fired after the failed Init":
* FAIL `SPORE_FIX_OFF=kit.postinit`: out/lab/20261113-083626.
* PASS: out/lab/20261113-083655, out/lab/20261113-083723.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit2 (FAIL/PASS twin).
- 2026-11-14: released in 0.7.14.
