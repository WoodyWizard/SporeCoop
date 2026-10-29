---
id: BUG-0232
title: the wait for the evolution cinematic before a world share adds up per guest and can go stale, so a later share goes out during the cinematic (BUG-0019 back once)
status: fixed
kind: bug
stage: Creature
reported: 2026-10-28
source: GitHub #166 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-core
commits: c4f4981
tests: npc_tests test_share_wait (make test; fails with npc_tests --nofix)
signature:
docs: docs/06-testing.md#testing-w5-core
related:
---

## Report
From GitHub issue #166: https://github.com/WoodyWizard/SporeCoop/issues/166 (the full analysis, links and the scenario are there).

**Summary**
The host waits for the evolution cinematic before it sends its world (fix for BUG-0019, commit 4096536, release 0.7.3.0). That wait uses one global timer for all guests, and the timer is reset only when a world goes out. With two or more waiting guests the 120 s limit counts again for each guest in turn. A timer left over from a guest who left, or from a host who went to the galaxy menu, lets the next share go out at once during a cinematic: the guest then gets the hatchling world again (the BUG-0019 symptom, once). The timers are also not reset at the session end. Severity: wrong behaviour, low.

**Where**
- The timers: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/mods/coop/coop.cpp#L969-L977
- Set at a mode change: https://github.com/

## Root cause
Confirmed by reading b423f56 (unchanged since the review): `follow_phase` used one global `g_share_cine_since` for every guest, reset only when a world went out. Several waiting guests were released one per 120 s; a timer left by a guest who left, a trip of the host to the galaxy menu (`follow_phase` returns outside a stage) or an earlier session made the next wait end at once, during the cinematic (the hatchling world of BUG-0019). `g_share_due_at` was not reset at the session end either.

## Fix
- The decision is `share_wait::hold(since, cinematic, now, due_at)` (mods/coop/share_wait.h); `follow_phase` passes a per-guest timer `Remote::cine_wait_since`, reset whenever the guest is marked as needing the world (`on_mode_change`, a join while the host is in a menu, `share_all`) and after its share. A guest who leaves takes its `Remote` (and timer) with it.
- `end_session_modules` resets `g_share_cine_since` and `g_share_due_at`.
- Switch `coop.fix npc.sharecine_each` (off: the single global timer, no session-end reset, as 0.7.3.0). `npc.sharecine` still switches the wait itself.
- Not changed: a guest who joins while the host plays still gets the world at once (no wait), as the issue says.

## Verification
- `npc_tests` test_share_wait (make test): three guests waiting through a 400 s cinematic are all released at 120 s; a guest that waited and left does not shorten a new guest's wait 200 s later; a guest marked again starts from 0; without a cinematic all go out together once the 3 s are over. `npc_tests --nofix` (the global timer): 3 checks FAIL (guests at 1123 / 1243 / 1363 s, the new guest sent at once).
- No game test: the issue's test needs host + 2 guests on the migration fixture with the caption held for 240 s (3 games, over this wave's memory budget). The game path is the same function with the timer it is given; `evolve_world` (BUG-0019) is the regression test of the wait itself.

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w5-core): fixed: per-guest cinematic wait (share_wait.h, npc.sharecine_each), timers reset at the session end; unit test FAIL with --nofix, PASS with the fix; no 3-game test.
