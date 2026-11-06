---
id: BUG-0648
title: Civ: the guest's military vehicles cannot shoot an enemy city hall and do not capture the city
status: open
kind: bug
stage: Civ
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-10, M0ng0r-16
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0602, BUG-0031, BUG-0477, BUG-0420
---

## Report
Discord report 2026-11-06 (0.7.8): the host (discord-2026-11-06/Sterben-10, s 6263) "the guest's vehicles can't capture
cities but trigger them"; the guest (M0ng0r-16, s 6269) "as guest with military vehicles I can't shoot at the enemy
city centre".

## Root cause
Observed (the host's log): the guest's orders were accepted (the only rejections from player 1 are command kinds 2 and
3, buy and build, at s 6373 / 6440); `military_hits=0` all session, but that counter counts only players in the
military role and the guest's role was "none" (s 5962 shot), so it proves nothing. Observed (the guest's log):
`sync-check MISMATCH v: 5 apart (v11 146.8 v13 145.1 v5 89.0 ...)` at s 6275: the guest saw vehicles up to 147 m from
the host's; "M0ng0r captured a city" at s 6585, ~5 min after the marks (a guest capture works at least sometimes).
Inferred (code): an order's target is a host city index (`g_host_cities`) or noun + position matched within 10 m on the
host (`resolve_target`); a city hall standing elsewhere on the host (the BUG-0602 relayout after a capture) or a city
missing from `g_host_cities` resolves to another target or none. Class BUG-0420 / BUG-0431.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: civ_npc, the guest's military vehicles ordered on an NPC city's hall; on the host check the order's
     target, the hall's damage and the capture meter; again after the host captured another city (BUG-0602). -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
