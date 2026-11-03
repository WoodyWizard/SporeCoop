---
id: BUG-0513
title: Creature: a guest whose game stops drawing for 17-38 s (minimized / alt-tab, device Reset) is shown sunk into the ground on the host's screen, with no notice why
status: open
kind: desync
stage: Creature
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature:
docs:
related: BUG-0347, BUG-0367, BUG-0507
---

## Report
Host's mark (0.7.3.6, report 20261103-191523): 'guest is bugged into the ground in my pov' (Creature). Host trace at that time: partner 1 `puppet=hidden: state older than 5 s`, state_age 24-34 s. The guest's log at the same session time (report 20261104-012758): `perf: hitch 19946 ms`, a WATCHDOG 'no frames', `hitch 38421 ms`, `hitch 17328 ms`, each followed by `device Reset`, and its report screenshots were not taken ('no frame was drawn through Present for 3 s'): the guest's game did not run for 17-38 s at a time (minimized / alt-tabbed full screen, or stalled).
Target: 0.7.4 (Creature, player-visible confusion).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the guest sent no state for up to 34 s; the host hides or freezes the puppet (state older than 5 s). Inferred: what the host saw 'in the ground' is the puppet left at its last pose / sinking when its driving stops, not a placement bug (BUG-0367's fix holds while states arrive). Fix ideas: when a partner's state is older than N s, show a caption ('X is not playing (game paused or minimized)') and park the puppet on the ground; let a minimized guest still send keepalive states (SporeKit background mode). Size: S.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage).
