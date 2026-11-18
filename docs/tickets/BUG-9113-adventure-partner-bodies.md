---
id: BUG-9113
title: Adventure partners are captain bodies of the right size, not name tags
status: fixed
kind: feature
stage: Adventure
reported: 2026-11-10
source: owner request (full shared Adventures, BUG-9089)
role: all
versions: 0.7.3.6-lineages43-dev
fixed_in: 0.7.3.6-lineages44-dev
branch: feat/adventure-bodies
commits:
tests: tests/net_tests.cpp (captain scale on the player state); QA host 9 + guest 8, see Verification
signature: partner body scale 1.585 against the own captain's 2.093
docs: docs/re/space.md#space-adventure-partner-bodies
related: BUG-9089 BUG-9114 BUG-9003
---

## Report

In an Adventure the partner was drawn as a name tag at its network position: enemies, allies and the
camera saw nothing there. The opt-in body prototype (`adventure.captain_bodies`, BUG-9089) was off
because its bodies were too small, and every frame of a mission's intro fed "failed to show <name>".

## Root cause

A body is spawned from the partner's species with that species' own scale. The scenario scales every
captain itself (2.093 in Bahaha 500, 2.000 in City Assault); a spawned body kept 1.585 / 1.576 (about
75 %). The spawn refused while the play state was not 3 (the intro), and the caller reported each
refusal as a failure.

## Fix

* The player state carries the captain's scale in an Adventure (optional trailing field after the
  legacy gate: older readers ignore 4 or 20 bytes left). The body takes the partner's scale at spawn
  and is corrected if it changes (`adventure_drive`); without it, this game's captain scale.
* No spawn attempt during the intro (`modes::adventure_body_ready`): the body comes with the mission.
* `adventure.captain_bodies` is on by default; off draws the name tag as in 43.

## Verification

GOG 2024 code (Steam QA copy), two games on one PC (instances 9 host, 8 guest), 10 fps.
* Bahaha 500: partner scale 2.093 = own 2.093 on both games (before: 1.585 / 1.576); a body 4 m
  beside the host's captain, same size (screenshot in the session). The host reached the finish, both
  games completed the mission and returned to the galaxy menu: 0 faults.
* City Assault: scale 2.000 on both, bodies present through two full missions (the allied army wins by
  itself in about 3 minutes), both results screens, Done, galaxy menu: 0 faults.
* Not tested: two PCs, 3-4 players, an Adventure started from a Space campaign.
