---
id: BUG-0795
title: Creature: an effect message of the partner is played on a puppet the game freed between two frames (BUG-0091 sibling)
status: open
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0091, BUG-0426, BUG-0451, BUG-0520
---

## Report
No player report. A partner's creature effect (pool start / stop / one-shot) that arrives while the partner's puppet was just freed by a world teardown or by the game itself can crash the receiving game.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): effects.cpp:893 (`on_message`) takes `C.puppet_of(player)`, which is the raw `Remote::puppet` (coop.cpp:6055), and calls game::start_effect / create_creature_effect / stop_effect (game.cpp:926-949) on it. No creature_listed / puppet_now check, no check that the puppet adapter is a creature one. Stops are gated by neither stage_off() nor C.here(); C.here() compares with the previous frame's g_mode / g_world, so between frames it still says 'same place'. Inferred: in the BUG-0091 window (a teardown between two frames, the queued reliable messages handled first in g_sess.update()) or after the game frees the puppet itself (UFO abduction BUG-0520, a nest claim), the game's pool effect code runs on freed memory; a late stop can also reach a Cell puppet (a pool cell) or a Tribe Marker (a 40-byte heap object). Confidence: high on the code, the window is the one BUG-0091 showed is real.

## Fix
Not fixed yet.

## Fix idea
Take the puppet through coop.cpp puppet_now() and require creature_puppet(adapter) && game::creature_listed(puppet), as on_remote_anim does (count drops like g_anims_dropped); keep the old path behind a switch (effects.puppet_alive).

## How to test
Two instances in Creature. Guest keeps a looping effect running (sprint) and spams abilities; host `coop.puppet gamekill` (the game's own destroy of the guest's puppet) or a world share in the same frame. Assert on the host: no fault in the log / crash report, and a new counter of dropped effects > 0. Must fault or call into freed memory on main (with a probe that logs start_effect on an unlisted creature), pass with the fix.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0520 (the game's UFO abduction freeing the partner's puppet on the host, duplicate of BUG-0507) is the known native free that reaches this path on 0.7.10+; BUG-0507's check sits in drive_puppet only. Linked both ways.
