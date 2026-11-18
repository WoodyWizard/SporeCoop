---
id: BUG-9143
title: loading a save with fewer player slots than players connected ends the whole session
status: fixed
kind: bug
stage: all
reported: 2026-11-12
source: found in QA 2026-11-12
role: host
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 99b846b6
tests: tools/qa_win (manual QA 2026-11-12)
signature: "This saved campaign has fewer reserved founders than the current lobby. Rehost from this planet."
docs:
related: BUG-9135
switch: core.slots_on_load
---

## Report
QA 2026-11-12: four players connected; the host loaded another save (Vandh, 3 player slots): the whole session closed for everybody ("fewer reserved founders than the current lobby").

## Root cause
world.cpp apply_saved_rules refuses a saved campaign whose slots are fewer than the connected players and the host leaves the session. With slots added mid-stage (BUG-9135, `coop.seats N now`) the host could raise the loaded campaign's slots instead (difficulty up), or ask.

## Fix
Owner decision "2A": world.cpp apply_saved_rules raises the loaded save's slots (founders::raise_seats) to fit the
players connected, in Cell to Civ, and says so in the chat: "This save had 3 player slot(s): raised to 4 so everyone
stays (difficulty rises with them)." Space keeps its next-load rule (the session still ends there). Off
(`coop.fix core.slots_on_load off`): the session ends as before. Before the fix the message read "This save has fewer
player slots than players connected. Rehost from this planet." (BUG-9144 wording).

## Verification
no-fail-run: two-PC manual run. QA 2026-11-12: four players in the Jiff test copy (4 slots), the host loaded Vandh
(3 slots): the chat line above, the session stayed 4/4, the world went to every guest.

## History
- 2026-11-12: opened.
