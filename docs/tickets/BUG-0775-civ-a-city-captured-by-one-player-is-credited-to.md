---
id: BUG-0775
title: Civ: a city captured by one player is credited to the player whose vehicle is nearest, and a guest may skip the host's capture (suspected)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-10
source: Discord 2026-11-09 bug-reports
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-captures
commits: 70334b34, 7930f984, 3ec69627
tests: civ_capture_credit
signature:
docs: docs/re/civ.md#civ-capture-keeps, docs/re/civ.md#civ-wsync-owners
related: BUG-0648, BUG-0593, BUG-0492, BUG-0493, BUG-0777
---

## Report
Discord bug-reports 2026-11-09, "bug in the synchronization of attacks on enemies", no files, stage not given (this
ticket: the Civ reading; the Tribe reading is BUG-0777): "When a friend is attacking an enemy, their settlement is
captured, but the host's is not; the same applies in reverse." Readings: a capture shows on one game only, or the
city goes to the wrong player.

## Root cause
* Observed (lab 20261112-004556, civ.capture_credit off): a real siege by the guest's three vehicles (real clicks,
  the game's shots) took an NPC city; the host's own vehicle stood 18 m from the hall, the guest's 60-73 m: the city
  became the host's (owner 0 on both games, `credit_nearest=1`).
* Cause (code): h_Capture gave the city to the player whose vehicle was nearest. The capture runs inside the hit that
  took it (TakeDamage -> AddCaptureDamage -> CheckMilitaryCapture -> Capture), so b_damage's `g_hit_by` names the
  player whose vehicle hit. The player report (Discord 2026-11-09) matches this when the other player's vehicles
  stood nearer (not proven for that game: no logs). The cinematic-delay cause of BUG-0875 also lost the captor.

Earlier reading (2026-11-10):
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

## Fix
civ.cpp `capture_credit`: a purchase's or away capture's player (`g_capture_for`), else the player whose hit took the
city (`g_hit_by`), else the last player hit in the joint-siege window (`g_siege`), else the nearest vehicle.
`coop.civ tables` prints the credit counters. Switch `coop.fix civ.capture_credit off` (0.7.12). Guests skipping a
capture (BUG-0492 / BUG-0493 paths) were not changed: their log lines exist already.

## Verification
`coop_civ_capture_credit.py` (suite `civ_capture_credit`, civ_npc, ~2.5 min): three military vehicles given to the
guest attack the nearest NPC city through the guest's real clicks (city, crosshair); the other buildings cut to 5 hp,
the besiegers kept alive (`coop.civ vhp`), the host's vehicle put 20 m from the hall (`coop.civ vput`). The game's
siege takes the city; on both games it must be the guest's (owner 1).
* FAIL with `SPORE_FIX_OFF=civ.capture_credit`: lab 20261112-004556 (owner 0 on both games).
* PASS: lab 20261112-004320 (before vput, `credit_hit=1`), 20261112-004830 (lane 4).

## History
- 2026-11-10: opened.
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0648 (Discord 2026-11-06, 0.7.8: the guest's vehicles trigger captures but do not capture) is the same mechanism seen by a player before; its regression test civ_guest_attack never checked the captor.
- 2026-11-12 (fix/w48-captures): fixed, FAIL and PASS runs in Verification.
