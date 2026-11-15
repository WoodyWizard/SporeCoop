---
id: BUG-9207
title: Creature: guests make their first creature (the base body) while the host makes a new world; the editor's check waits for the host's world
status: fixed
kind: feature
stage: Creature
reported: 2026-11-14
source: owner play test 2026-11-14 (night)
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 2ddb7877
tests: tools/qa_win (TITAN, host 9 + guest 8 over 127.0.0.1, 2026-11-14 night)
signature:
docs: docs/re/creature.md#creature-pregame-guest-editor
related: BUG-9203 BUG-9164 BUG-9208 BUG-0345
---

## Report
Owner play test 2026-11-14 (night, 3 players, the 3W build): the host started a new Creature world from the galaxy menu and sat in its creature editor; the guests waited in the menu. The owner: "skip 3UA and send guests into the editor while the host is making the world while in their editor" (decision 3U, mockup images sent 2026-11-14: the guest's check greyed until the host's world is ready). The guests' first creature should be the base creature, not their old one.

## Root cause
A feature. The host's player state already says it is in the creature editor (PlayerState.editor 0x465C50BA) and its mode before was the galaxy menu (host log: `Galaxy menu BDBE3B52 -> Editor BDBE3B52 'SaveGame'`, the world id of the menu). The guest can open the creature editor with no creation key (the base creature, as tribe.late_create_editor does), and a received world already waits for an open editor (world.cpp, the deferred load). The editor's finish button is control 0x058A3350 (`coop.ui at` on the MSI's creature editor, 40 x 40 at the bottom right, 2026-11-14).

## Fix
coop.cpp pregame_editor_tick (switch `coop.fix creature.pregame_editor off`): a guest in the galaxy menu, the host in the creature editor (PlayerState.editor 0x465C50BA) after the galaxy menu: the creature editor with no creation key (submit_new_creature_request, shared with tribe.late_create_editor) opens with the base creature. Its check (control 0x058A3350) is disabled (sui::set_enabled, the game draws it grey) while the host is in that editor and no world waits (world::load_queued); it works again when the host's world is here or the host is back in the menu. creations.cpp begin_first_creature: back in the menu, the newest new creation is chosen (choices.ini, the co-op window's pick) and kept out of the BUG-9203 "new here" editor in the first world it plays. Test command: `coop.newcreature`.

## Verification
no-fail-run: manual on TITAN (no Windows lab lane). FAIL (22:09:30, switch off on the guest): the host opened the creature editor from the galaxy menu (`coop.newcreature`); 25 s later the guest was still in the galaxy menu. PASS (22:10:01, switch on): the guest's editor opened 1 s later with the base creature, its check `enabled=0` and grey (screenshot; the host's green); the host quit to the menu at 22:12:02: `the check works`, `enabled=1`; the host loaded Vandh.spo, the world waited for the guest's editor; the guest added a mouth and saved 'Pheecton': `the first creature 2922F340 made while the host made the world`, the world loaded, `Guest plays its own creature 'Pheecton'` (both logs), no BUG-9203 editor in the next 50 s. Screenshots out/shots/bug9207/.

## History
- 2026-11-14: opened.
- 2026-11-14 (night): built (creature.pregame_editor; creations::begin_first_creature makes the saved creation the pick and keeps it from the BUG-9203 editor); live test pending.
- 2026-11-14 (night): live on TITAN, FAIL/PASS above. Open: the guest's editor is the full Creature Creator (all parts, a 2,000 budget), not a stage editor; ships (a Space start) not covered.
- 2026-11-15: decision 3Z B (the guests' parts as the host's editor's): the host's real new-world editor is the stage start editor 0xE46C381E (logged), not the Creator, so the guests' editor did not start for it: fixed (e4278a12), checked live (the host's 0xE46C381E from the menu, the guest's editor 1 s later). Guests still get the Creator: the start editor offers no mouth on every profile tried, so it cannot be finished there (docs/re/creature.md#creature-stage-start-editor). Open: how the owner's creature got its mouth.
