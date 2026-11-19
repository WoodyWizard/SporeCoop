---
id: BUG-9275
title: Party card of a guest without a creature of their own is empty (Space: the host's captain)
status: fixed
kind: bug
stage: Space
reported: 2026-11-19
source: Live audit 2026-11-19 (MSI pair)
role: host
versions: 0.7.14.1-lineages65-dev
fixed_in: 0.7.14.1-lineages66-dev
branch: feat/lineages-47
commits: e2ed6de4
tests: tools/qa_win/qa.py (manual: MSI pair, Space fixture, a guest without a creature of its own)
signature: Party card: name, place and bars, an empty picture frame
docs:
related: BUG-9191, BUG-9216
---

## Report
Live audit 2026-11-19 (MSI pair, the Space fixture): the host's Party card of the guest had an empty picture frame;
the guest's card of the host showed the host's creature.

## Root cause
Observed: the guest's log has no "sending our creature" / picture line. creations.cpp portrait_tick sends the
picture of the player's own creation (mine(kCreature)); only the host falls back to the game's player species. A guest
with no creature of its own (it plays the host's: the captain in Space) sends none, so its card is empty everywhere.

## Fix
Commit e2ed6de4: coop.cpp update_party_panel: a player with no picture, playing a stage (not in an editor, where they may be
making their own), gets the host's picture: the creature they play. Switch: `coop.fix ui.party_host_picture off`.

## Verification
no-fail-run: two games, the game's HUD. Manual 2026-11-19 on the MSI pair (Space fixture, host profile MSISpaceHost,
guest MSIGuest). FAIL on fc3c27a9 (no fix): the host's card of MSI has an empty frame (screenshot a_space_h). PASS with
the fix: the card shows the host's captain 100 s after the join (screenshot b_space_on). Turning the switch off in the
running game leaves the picture already set (the panel does not clear one); the off state is the build before.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on feat/lineages-47 (e2ed6de4).
