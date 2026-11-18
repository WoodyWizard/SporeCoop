---
id: BUG-9208
title: Creature: a guest arriving in a world new to it played its old creature and evolved at the host's nest (cocoon there) before its own nest existed
status: fixed
kind: bug
stage: Creature
reported: 2026-11-14
source: owner play test 2026-11-14 (night)
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 6bcfb9db
tests: tools/qa_win (TITAN, host 9 on Vandh.spo + guest 8 joining from the menu, 2026-11-14 night)
signature:
docs: docs/re/creature.md#creature-pregame-guest-editor
related: BUG-9203 BUG-9207 BUG-9194 BUG-9182
---

## Report
Owner play test 2026-11-14 (night): when the guests connected to the new world, they "were playing their original creatures"; "It should bring up the starting base creature (random spine body). They shouldn't be sharing their creatures or have family at their nest until they finish creating their first creature." And: "When Host spawned in, the connecting players had their cocoons on the Host's nest, not their own."

## Root cause
Observed (logs, TITAN guest 8 and the host): the guests arrived 21:44:31-33 and played their co-op window picks ('Lilku', 'Duckulon' from choices.ini, BUG-0345); the family followed the old species (`evolve: the own family follows ... 4 member(s)`); the BUG-9203 editor opened from that pick 12-56 s later (MSI 21:44:50, Titan 21:45:28) while their bodies still stood at the host's nest (the spawn), so the partners' cocoons (BUG-9194) showed there. The seat nests came only at 21:47:16-18 (home_nests, after the host's own intro), made with the OLD species while the guests were in the editor. With BUG-9207 a guest connected before the host's new world arrives with its fresh creature and none of this happens; a guest joining a running world new to it still takes this path.

## Fix
coop.cpp pregame_editor_tick, creations::needs_first_creature, world::hold_for_editor (switch `coop.fix creature.first_before_world off`): a guest in the galaxy menu whose host plays Creature in a world W (PlayerState.world, the crc of the save name, the same id as worlds.ini) where the guest has no creature of its own (no worlds.ini [creature] record, no choices.ini [landfall] entry, no first creature made or being made, and it did not choose the world's own) holds the arriving world at once and, when the world is here, gets the base creature editor (BUG-9207's). What it saves is its creature (choose); the held world loads 2 s after the editor is left. Found in the test: opened during the transfer (the received world written into the slot), the editor's 3D view stayed black (background, platform and body all missing): it waits for the whole world now; and game_busy() counts a held world as loading, so the gate is busy::kEditorGate.

## Verification
no-fail-run: manual on TITAN. Both runs: host 9 on Vandh.spo (FEC6C134) hosting, guest 8 with its world record for FEC6C134 removed, joining from the galaxy menu. FAIL (22:28:51, switch off): the world loaded at once, `Guest plays its own creature 'Pheecton'` (the old pick), `join: own editor for a creature of our own` 9 s later, the editor 31 s after that (in the world, at the host's nest: the owner's cocoon). PASS (22:38:27, 6bcfb9db): `it waits for our first creature` 1 s after the join, the world received (14 s) and held under the join loading screen, the editor with the base creature at 22:38:45 (screenshot: the scene renders), a mouth added and 'Melliza' saved, the world loaded 3.5 s later, `Guest plays its own creature 'Melliza'` on both games, its family took the new species, no in-world editor. Screenshots out/shots/bug9208/ (the black editor of the first try too).

## History
- 2026-11-14: opened.
- 2026-11-14 (night): opened from the owner's play test; the usual path (guests waiting while the host makes the world) is covered by BUG-9207; the late-join path (base creature editor at once, no old body, no cocoon at the host's nest) is next.
- 2026-11-14 (night): fixed, live FAIL/PASS on TITAN. Not covered: a guest already in another world when it joins (it loads the host's world from there as before, then BUG-9203's in-world editor).
