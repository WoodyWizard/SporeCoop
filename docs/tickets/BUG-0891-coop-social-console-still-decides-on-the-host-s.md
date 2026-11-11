---
id: BUG-0891
title: coop_social_console still decides on the host's acts_in counter, not on what the games play
status: open
kind: test
stage: Creature
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0149, BUG-0215, BUG-0713
---

## Report
BUG-0215's own Fix section left this open ("coop_social_console.py still decides on the mod counter acts_in") and it was never ticketed. The test of BUG-0149 (digits typed in the cheat console must not play social abilities) checks only that the host's mod counter `acts_in` did not change. A regression that plays the ability on the guest's own avatar without sending K_ACT, or a host that counts the act but plays nothing on the puppet, passes or fails for the wrong reason. Since BUG-0713 the keys come from the game window's message queue and only the console check (`C.blocked`) stops them, so this is the only guard and it has no game-state assertion.

Severity: low. Confidence: high.

## Root cause
* Observed (code): tools/tests/coop_social_console.py:75-76 (`acts()` reads `acts_in=` of the host's `coop.social`), :91 `ck(a1 == a0, ...)`, :104 `ck(a3 > a2, ...)`; nothing reads the guest avatar's or the host puppet's animation or social state.
* Observed (code): mods/coop/social.cpp:666-692, the key path is gated only by `on` (engaged, social stance, `C.blocked`, front window).

## Fix idea
Assert the game's state on both machines: the guest avatar's current animation is not a social ability animation while the console is open and is one after it closes; the host's puppet of the guest plays the same animation (or the NPC's social reaction changes) only after the console closes. Keep acts_in as printed diagnostics only.

## How to test
With `coop.social textfix off` the new animation assertion must FAIL (the avatar plays the ability while the console is open); with the fix it must PASS. Both run directories go in the ticket.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0215. Severity low, confidence high.
