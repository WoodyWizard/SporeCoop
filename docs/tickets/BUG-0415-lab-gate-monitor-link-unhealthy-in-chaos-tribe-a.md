---
id: BUG-0415
title: Lab gate: monitor link unhealthy in chaos_tribe around a guest rejoin (churn) on a clean network
status: open
kind: bug
stage: Tribe
reported: 2026-11-01
source: lab 20261101-150122 chaos_tribe#3, lane 7 (fix/w33-proxy-ground)
role: host
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
out/lab/20261101-150122-fix-w33-proxy-ground chaos_tribe#3 (lane 7, `--cold`, SPORE_FIX_OFF=npc.proxy_ground,
unrelated to it) FAILED on the monitor gate `link: host -> partner 1: link unhealthy on a clean network on 3 trace
lines in a row from s 50.013: resends/sent 0.66; pending 401; resends/sent 0.99`. The chaos bot rejoined the guest
4 times in 120 s (`churn=4`); the host's partner line at s 50.0 says resends=7306/11033 (the guest disconnected at
s 52.1 and joined again at s 54.7, two 1 s "saving" hitches of the host at s 40.7 and 56.7), at s 60.0 pending=401
and state_age 0.7 s.

## Root cause
Not analysed (outside fix/w33-proxy-ground's task). Inferred: the world share / resend burst of a rejoin (and the
host's save for it) on a link the chaos bot churns, i.e. the rule's thresholds do not allow for a rejoin; or a real
resend storm after a rejoin. The other chaos_tribe runs of that series (3 of 4) and 3 of 3 in 20261101-141252 passed.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
