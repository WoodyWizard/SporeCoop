---
id: BUG-0960
title: Tribe: while the host sits in its game-over caption the guest plays on against a paused host (no caption, the host's knocked-out chief shown standing)
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-tribe-mirrors, tribe_gameover_partner lab 20261112-093040 (BUG-0922)
role: guest
versions: 0.7.12 - 0.7.13
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0922, BUG-0288, BUG-0685
---

## Report
Seen while closing BUG-0922 (tribe_gameover_partner, lab 20261112-093040, key frame `caption`): every member of the
player tribe knocked out on the host, the host's game shows "Alas! Your population has fallen below the minimum ..."
and pauses (busy=cinematic,input_lock,paused) until its player presses OK; the guest's game shows no caption and plays
on (HUD, orders) against a paused host. The host's knocked-out chief stood up (hp 168, dead 0) while the caption was
up: the guest's copy of it stands at the hut (as in the 0.7.12 final check's play_tribe end frame, lab
20261111-044257).

## Root cause
Observed: the caption comes on the host only (docs/re/tribe.md#tribe-bounce-mirrors: the guest's input lock stays 0).
Not decided: whether a guest should show the caption too (its OK would reload its own save, BUG-0685) or a co-op note
("the host's tribe is out, waiting for the host"). Low severity: the host's OK bounces both back.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
