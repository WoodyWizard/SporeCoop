---
id: BUG-0095
title: after a failed co-op jump the next real zoom-out to the galaxy skips the game's galaxy handler
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#73
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-space
commits: 30a25b0
tests: space_jump (regression only)
signature:
docs:
related: #73
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/73 (full text, discussion).

The co-op jump hides the game's "entered the galaxy" handler once, for 5 s, because the jump passes through the galaxy view without the player seeing it. If the jump stops before the galaxy view was reached, that skip stays armed. A real zoom-out by the player within the next 5 s then loses the game's handler: tutorial caption, hints, first look at the galaxy. Severity: minor (wrong behaviour in a narrow window).

### Where
- The skip hook: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L336-L353
- Where it is armed and where the jump can then fail: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L398-L405

### How to reproduce
1. Make `SystemToGalaxy` refuse or not reach context 2 (e.g. during a state where the zoom-out is blocked; the exact conditions are not known, hence "can't reach the galaxy view").
2. `coop.space jumpto <StarID>` answers "can't reach the galaxy view".
3. Zoom out with the mouse wheel within 5 s.
4. `coop.space` shows `galaxy_msgs_skipped` increased by one for a zoom-out the player made.

### Expected
The skip applies only to the jump's own galaxy pass.

## Root cause
**What happens**

```cpp
if (ctx == 1) {
    if (o_GalaxyEntered)
        g_galaxy_pass_until = H->time_seconds() + 5;
    thiscall<void>(to_galaxy, mode);      // SystemToGalaxy
    ctx = cdecl_call<int>(a_Context);
}
if (ctx != 2)
    return "can't reach the galaxy view";  // g_galaxy_pass_until stays set
```

1. `g_galaxy_pass_until` is set before `SystemToGalaxy`.
2. If `SystemToGalaxy` did not switch to context 2, it did not post 0x5E37A3B, so nothing consumes the skip.
3. `h_GalaxyEntered` skips the next 0xFDABE0 call within 5 s, whoever caused it.
4. If the player zooms out by hand right after the failed jump (an obvious reaction to "can't jump"), the game's own handler for that zoom-out is skipped.

### Confidence
CONFIRMED by reading the code. Which situations make `SystemToGalaxy` fail is not verified. If it never fails in practice, this is only a robustness fix.

## Fix
Fixed in `travel_to` (spacecoop.cpp): when SystemToGalaxy did not reach context 2 the skip is
disarmed (`g_galaxy_pass_until = 0`) before "can't reach the galaxy view" is returned.

## Verification
Written argument (2026-10-27): the failing condition (SystemToGalaxy refusing to leave the
system) is not known, so it cannot be forced from a test. The fix is one assignment on the only
return between arming and the galaxy pass; the success path is unchanged (`space_jump` passed in
20261027-190159).

The original plan:

In `tools/tests/coop_space_jump.py`:
1. Add a debug switch that makes the jump's `SystemToGalaxy` step be skipped (`coop.space jumpto <id> nogalaxy`), simulating the failure.
2. Then zoom out with a real key or wheel input.
3. Assert on the game's side that the handler ran: the tutorial save's caption SPG_EnteredGalaxy_201 is shown (cinematic / caption state), as `coop_space_play.py` checks captions.
4. Fails on main (the caption is skipped), passes with the fix.

### Related
- docs/re/space.md 7.3 ("The galaxy pass is hidden").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #73.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-space (wave 1, Space).
- 2026-10-28: released in 0.7.3.0.
