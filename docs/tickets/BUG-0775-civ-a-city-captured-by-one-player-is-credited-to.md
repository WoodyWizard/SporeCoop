---
id: BUG-0775
title: Civ: a city captured by one player is credited to the player whose vehicle is nearest, and a guest may skip the host's capture (suspected)
status: open
kind: desync
stage: Civ
reported: 2026-11-10
source: Discord 2026-11-09 bug-reports
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/civ.md#civ-wsync-owners
related: BUG-0648, BUG-0593, BUG-0492, BUG-0493, BUG-0777
---

## Report
Discord bug-reports 2026-11-09, "bug in the synchronization of attacks on enemies", no files, stage not given (this
ticket: the Civ reading; the Tribe reading is BUG-0777): "When a friend is attacking an enemy, their settlement is
captured, but the host's is not; the same applies in reverse." Readings: a capture shows on one game only, or the
city goes to the wrong player.

## Root cause
Suspected, confidence low (no logs):
* Captures happen on the host only: `h_Capture` returns 0 on a guest (`mods/coop/civ.cpp:1697-1700`); a guest
  applies the host's capture from MSG_CIV_STATE with ChangeOwner (`civ.cpp:3489`), but skips it silently while its
  city planner holds the city (BUG-0493) and when it has no civilization of the host's owner (BUG-0492, log line
  "is no civilization here").
* Who gets the city: the player whose vehicle is nearest the city when the capture runs (`civ.cpp:1704-1718`),
  not the player who did the capture damage. With the host's vehicles closer, a guest's capture is the host's city
  (and the other way), which matches "their settlement is captured, but the host's is not".
* The siege already knows the attacker: `g_siege` (city -> player -> last hit) and `g_hit_by` in b_damage.

## Fix idea
Credit the capture to the player with the most capture damage (or the last hit) from `g_siege` within the joint
window, nearest vehicle only as the fallback. Log every skipped guest capture once with its reason. Ask the player
for the report zip and the stage.

## How to test
`coop_civ_guest_attack.py` extended: the guest's vehicles capture an NPC city while a host vehicle stands nearer to
the city hall. Assert on both games the city's pol (`pol_of(city)`, the player civ) and the mod owner the city menu
shows (the guest's name). Main credits the host: FAIL.

## History
- 2026-11-10: opened.
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0648 (Discord 2026-11-06, 0.7.8: the guest's vehicles trigger captures but do not capture) is the same mechanism seen by a player before; its regression test civ_guest_attack never checked the captor.
