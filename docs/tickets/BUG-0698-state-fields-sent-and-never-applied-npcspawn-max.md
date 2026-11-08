---
id: BUG-0698
title: State fields sent and never applied: NpcSpawn.max_health, PlayerState PF_RUNNING / PF_SWIMMING / anim
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0419 (0.7.11)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b
commits: 1391b0ac
tests: state_fields_check (make test)
signature:
docs: tools/tests/state_fields_check.py
related: BUG-0419
---

## Report
From the K2a audit of BUG-0419. NpcSpawn.max_health is sent and stored (px.max_health) and never read; PlayerState PF_RUNNING is set and never read, PF_SWIMMING never set or read, PlayerState.anim always 0 (animations go by MSG_ANIM). State the receiver never applies, or wire bytes that mean nothing.

## Root cause
Code: NpcSpawn.max_health stored, never read; PF_RUNNING never read, PF_SWIMMING never set, PlayerState.anim always 0 (animations by MSG_ANIM). Nothing visible depends on them: a proxy of the same species and age gets the host's max by the game's own rules (health is applied absolutely); the gait follows the drive's speed.

## Fix
The outcome check of class BUG-0419: tools/tests/state_fields_check.py (in make test) lists every snapshot field and PF_/NF_ flag with its apply site (checked by a pattern) or the reason it is not applied; a new field or a lost apply site fails it. The census now prints each creature's max health and the detector compares it (`max_hp` row), so a max that differs shows in the lab.

## Verification
no-fail-run: a unit check of the source (it failed with 20 problems before the table listed the fields, 0 after); no lab FAIL exists for fields that change nothing on screen.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
