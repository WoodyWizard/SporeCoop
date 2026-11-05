---
id: BUG-0603
title: Tribe: a guest's game plays none of the hit impact effects of the members' attack animations (the animation event's per-target effects)
status: open
kind: bug
stage: Tribe
reported: 2026-11-05
source: lab (fix/w44-guest-player3, BUG-0600 analysis)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-mirror-fx
related: BUG-0600
---

## Report
Found in the lab while fixing BUG-0600 (no player report yet): in a 40 s fight of the host's members (tribe fixture,
`coop.tribe act 7`) the host's game played sound 0xD05B58ED 14 times (out/lab/20261105-135554, probe with
`coop.effects log on`) and in other runs 0xA7E3E1FB 18 times (out/lab/20261105-135339, out/lab/20261105-115203); the
guest's game none, also with BUG-0600's fix on. Reproduce: coop_tribe_fight_sounds.py, read the `host` / `guest` lines.

## Root cause
Observed (lab + Ghidra): 0xD05B58ED is the sound of effect 0x682E8D4B, an animation event's effect (made through the
animation system's effect wrapper 0x9FBF00 from the event handler 0x9A6D70, kind 1); its siblings 0xF0D5A994 and
0xCFECC623 likewise (host 13-14, guest 0). Other effects of the same animation (0x6397C4AC, 0xE6D770B8) play on both
games: the guest's mirrors play the animation (the BUG-0569 relay). The handler makes the missing ones in its loop over
param_18 entries (FUN_009a47a0 filters them by the event's flags & 0x70 against a list): inferred, the attack's target(s);
a mirror's relayed animation has no target. 0xA7E3E1FB: inferred the same class (same sound path, not matched to an effect).
Not fixed (BUG-0600 relays the pool effects, sprints and voices only).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened (fix/w44-guest-player3, P2b of 0.7.8), analysed, not fixed.
