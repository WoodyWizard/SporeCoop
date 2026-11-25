---
id: BUG-9265
title: Galaxy planet: a new host's first creature is made in the 2,000 DNA Creator with every part
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: owner
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 5cf76575
tests: tools/qa_win/qa.py (manual: TITAN QA 8 hosting the galaxy planet Samuth without a creature record; no lab test yet)
signature: "galaxy: the new host's first-creature editor" opening editor 465C50BA with no parts list
docs: docs/12-server-and-players.md#server-host-start-editor
related: BUG-9263
---

## Report
Owner 2026-11-19 (passed on by the Troubleshooting session with BUG-9263): "We can't let them make a level 5 endgame
creature upon joining any player". BUG-9263 closed the guest's paths on live; on the alpha a galaxy planet's new host
without a creature of its own there (gx.hosted_stored, coop.cpp) still got the Creature Creator.

## Root cause
Observed (TITAN QA 8, 2026-11-19, switch off): the host's first-creature editor is editor 0x465C50BA with no creation
key and no parts list: every part of every tab is unlocked (two pages of mouths), the budget is the planet's DNA.

## Fix
gx.hosted_stored submits the Creature stage's start editor (0xE46C381E, as BUG-9263's guests). Opened inside a running
world its request must not name the galaxy menu as the mode to go back to (+0x1C; the wizard's value is right only
from the menu): submit_new_creature_request sets it only in the menu. Switch `coop.fix creature.host_start_editor`.

Seen with it: the start palette (three legs; the three mouths, the cell-era parts and three eyes), the budget is the
planet's DNA (570 on Samuth, not the menu's 145: the editor takes it from the running game), the red cross goes back
to the world, saving goes back to the world, creations::host_first_tick records the new creature for the world and
the host plays it ("own avatar is now 2928DC1E").

Left as they are (decision 6U, answered A by the owner 2026-11-19: leave it): the Tribe and Civ late joiner's editor (open_new_creature_editor) is the Creator;
a creature's parts do not change Tribe or Civ play. The test command coop.newcreature and editor_depth's test open the
Creator on purpose; the game's own evolution request (0x465C50BA with the evolution tag and the collected parts) is
the limited in-world editor.

## Verification
no-fail-run: no lab test (galaxy planets have none yet). Manual, TITAN QA 8 (profile without a creature record for
Samuth's world), hosting Samuth from the galaxy server's stored world, 2026-11-19:
- switch off (FAIL): "the new host's first-creature editor (the Creator)", every part unlocked.
- switch on (PASS): "(the start editor)", the start palette; a mouth placed, a dice name, saved: back in the world,
  "the new host's first creature 2928DC1E is its creature in world D296F25C", the avatar is that creature.

## History
- 2026-11-19: opened and fixed on feat/lineages-alpha.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
