---
id: BUG-0669
title: Tribe: the guest's mirrors of walking members make almost no footstep sounds
status: open
kind: bug
stage: Tribe
reported: 2026-11-07
source: found in fix/w46-tribe-civ (BUG-0646 analysis)
role: guest
versions: 0.7.9
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0646, BUG-0600
---

## Report
Found while recording both games' audio for BUG-0646 (tribe fixture, lane 4, 2026-11-07, `coop.audio mark` / `list`, day length 30 s, the host's 9 members going about their village work for 3 min): sound 0x4A776923 (type 3, played from 0x9A50A7 <- 0x9A6BAE <- 0x9A6CBC <- 0xA01282) 436 times on the host, 36 times on the guest; the guest instead played 0x76BDAADD (same path) 565 times against the host's 84.

## Root cause
Observed (Ghidra): 0x9A6CBC is in FUN_009A6BD0, which plays a sound per animation channel of an animated creature only while its velocity (+0x1660..+0x1668, or the angular +0x166C) is above 0.1 / 0.2: footsteps of a moving creature. Inferred: the guest's mirrors are moved by the mod (positions), their animated creature has no velocity, so their footsteps (and the surface's sound) are not the host's. Not analysed further; related to BUG-0600 (fight sounds host-only).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
