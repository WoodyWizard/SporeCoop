---
id: BUG-9323
title: Civ: this player's own name tags show the -NoShare- test tag
status: fixed
kind: bug
stage: Civ
reported: 2026-11-21
source: QA 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-final-rings
commits: 30549e5b
tests: tools/qa_win (manual, two TITAN QA games, 2026-11-21)
signature:
docs:
related: BUG-9222
---

## Report
Live QA 2026-11-21 (TITAN QA copy, Civ, a guest joined as `CivGuest-NoShare-`): the guest's own vehicle's name tag read
`CivGuest-NoShare-` and its capital's `[CivGuest-NoShare-]` (out/qa-shots/civ_guest_own_rings_on_own_vehicle.png).
BUG-9222 hides the tag in the player list and join lines. Partners' tags read the name without it.

## Root cause
Observed in the code: draw_civ_tags and draw_owner_tags (Tribe) took this game's own name as typed (g_name) for its own
units, while partners' names come from net::Session::name_of, which BUG-9222 made the shown name. A real display bug,
not test-only: BUG-9222's rule is that the tag is never shown where a name is shown, and anyone may type it (it is the
owner's documented way to keep a game's creations out of others' Lineages Cache). Only names with the tag are affected.

## Fix
coop.cpp tag_name(owner): the own name through utf8::shown_name, as the partners' names, for the Civ city and vehicle
tags and the Tribe member tags. Switch: BUG-9222's `net.shown_names` (off: names as typed everywhere, 67-dev).
Seen in the code, not changed: vote texts built from g_name (`g_name + " wants to ..."`, start_vote) still carry the tag
in the co-op window's vote line; notices strip it (BUG-9239).

## Verification
- Live 2026-11-21 (TITAN QA copy, Civ, host DuoQACivHost / Abber, guest DuoQACivGuest joined as CivGuest-NoShare-, the
  guest's camera on its own vehicle and capital): PASS on (the vehicle tag "CivGuest":
  bug9323_civ_own_tags_PASS_switch_on.png), FAIL with `coop.fix net.shown_names off` ("CivGuest-NoShare-" and
  "[CivGuest-NoShare-]": bug9323_civ_own_tags_FAIL_switch_off.png). Tribe own tags: not seen live (this player had no
  villagers in the Tribe session).
- no-fail-run: not a lab run (TITAN QA games by hand).

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-final-rings (30549e5b), FAIL / PASS live in Civ.
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
