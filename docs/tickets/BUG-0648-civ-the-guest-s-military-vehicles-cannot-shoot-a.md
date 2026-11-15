---
id: BUG-0648
title: Civ: the guest's military vehicles cannot shoot an enemy city hall and do not capture the city
status: not-reproduced
kind: bug
stage: Civ
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-10, M0ng0r-16
role: guest
versions: 0.7.8
fixed_in:
branch: fix/w46-tribe-civ
commits: c9265c9a
tests: civ_guest_attack
signature:
docs: docs/re/civ.md#civ-guest-attack
related: BUG-0602, BUG-0031, BUG-0477, BUG-0420, BUG-0775, BUG-0875
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

Observed 2026-11-07 (lab lane 4, 0.7.10 code): the guest's own attack (its city menu's crosshair, and `coop.civ attack` through the guest game's SetOrder) reaches the host as order 2 on the host's city index; the host's vehicles drive there, the city's hit points fall (5200 -> 3971) and its capture meter rises for the player civilization on both games. Inferred for the report (0.7.8): the guest's vehicles were up to 147 m from the host's (sync-check) and captured NPC cities were laid out anew (BUG-0602, fixed in 0.7.9), so the guest saw its vehicles at a city hall the host's had not reached; the guest's capture at s 6585 shows the path worked then too.

## Fix
None: not reproduced on 0.7.10. A regression test of the guest's attack was added.

## Verification
no-fail-run: not reproducible on 0.7.10 (the guest's attack works); regression test civ_guest_attack (coop_civ_guest_attack.py), PASS run: out/lab/20261107-082715.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07: not reproduced in fix/w46-tribe-civ; regression test civ_guest_attack.
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: the host's half of the report ("the guest's vehicles can't capture cities but trigger them") now has a code explanation that the 0.7.10 retest did not cover: h_Capture credits the city to the player whose vehicle is nearest (civ.cpp:1704-1718, BUG-0775), and with 2 players the next resplit deals every city out round-robin (civ.cpp:2310-2321, BUG-0875); both are in the code since 2026-10-21 (74144ecf), so they apply to 0.7.8. civ_guest_attack checks the hit points and the capture meter, not who owns the city after the capture. Untried test: the guest's army takes a city while a host vehicle stands nearer, then read g_city_owner and the city menu's owner on both games (expected: the guest). The fix belongs to BUG-0775 / BUG-0875 (R2 of 0.7.13); the guest's own half (cannot shoot the city hall) stays unexplained.
