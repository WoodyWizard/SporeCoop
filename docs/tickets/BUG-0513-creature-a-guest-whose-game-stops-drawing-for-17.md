---
id: BUG-0513
title: Creature: a guest whose game stops drawing for 17-38 s (minimized / alt-tab, device Reset) is shown sunk into the ground on the host's screen, with no notice why
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-guest-combat
commits: 111958f0, 306b9ecf, 8c41e570
tests: coop_puppet_walk.py (puppet_deep), coop_partner_silent.py
signature:
docs: docs/re/creature.md#creature-partner-stalled
related: BUG-0347, BUG-0367, BUG-0507
---

## Report
Host's mark (0.7.3.6, report 20261103-191523): 'guest is bugged into the ground in my pov' (Creature). Host trace at that time: partner 1 `puppet=hidden: state older than 5 s`, state_age 24-34 s. The guest's log at the same session time (report 20261104-012758): `perf: hitch 19946 ms`, a WATCHDOG 'no frames', `hitch 38421 ms`, `hitch 17328 ms`, each followed by `device Reset`, and its report screenshots were not taken ('no frame was drawn through Present for 3 s'): the guest's game did not run for 17-38 s at a time (minimized / alt-tabbed full screen, or stalled).
Target: 0.7.4 (Creature, player-visible confusion).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the guest sent no state for up to 34 s; the host hides or freezes the puppet (state older than 5 s). Inferred: what the host saw 'in the ground' is the puppet left at its last pose / sinking when its driving stops, not a placement bug (BUG-0367's fix holds while states arrive). Fix ideas: when a partner's state is older than N s, show a caption ('X is not playing (game paused or minimized)') and park the puppet on the ground; let a minimized guest still send keepalive states (SporeKit background mode). Size: S.
Observed (the logs): the guest's own trace has its creature 6.5 m under its own ground while paused in a stall
(agl=-6.53, busy=paused), the host's trace the puppet at agl -6.32 and -3.2 while states arrived; then hidden after 5 s
without a word. creature.puppet_ground leaves a target more than 3 m under the ground where it is. Observed (lane 2):
a walking puppet with states 6 m under the ground is drawn under it. Not known: why the stalled guest's game holds
its creature under its ground.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`creature.puppet_deep` (modes.cpp creature_drive): a target 3-15 m under this game's ground goes onto it
(drive::out_of_ground). `core.partner_silent` (coop.cpp silent_tick): a notice when a partner's states are older than
5 s ("is not responding (game minimized or frozen?)") and when they come back. Off: 0.7.3.6. A keepalive state from a
minimized game (SporeKit background mode) is not done.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
* puppet_deep: FAIL with `SPORE_FIX_OFF=creature.puppet_deep`: out/lab/20261103-174314 (144 frames under the ground); PASS: out/lab/20261103-174441.
* partner_silent: FAIL with `SPORE_FIX_OFF=creature.puppet_deep,core.partner_silent`: out/lab/20261103-174020 (no notice); PASS: out/lab/20261103-174151.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage).
- 2026-11-03: fixed on fix/w40-guest-combat (creature.puppet_deep, core.partner_silent).
- 2026-11-03: released in 0.7.4.
