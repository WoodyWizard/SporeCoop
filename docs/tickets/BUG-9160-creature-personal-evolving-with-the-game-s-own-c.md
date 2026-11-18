---
id: BUG-9160
title: Creature: personal evolving with the game's own cocoon (decisions 2D, 2E)
status: open
kind: feature
stage: creature
reported: 2026-11-12
source: owner
role: both
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual: the owner on TITAN with the MSI watching; TITAN host + guest)
signature:
docs: docs/re/creature.md#creature-cocoon
related: BUG-9161 BUG-0508 BUG-0517
switch: creature.cocoon creature.personal_editor
---

## Report
Owner decisions 2D / 2E (2026-11-12): each player evolves on their own: the native mating call, the player clicks the mate, the creature goes into the game's cocoon with its native animation, the editor opens, and it comes out of the cocoon with the new creature, "very natural and immersive for an outside viewer of the nest". Replaces the evolution vote in Creature.

## Root cause
Design. Found in the game data (2026-11-12, FNV-1 ids of the names; Spore_Game / PatchData): the animations gen_evolve_cocoon 9C26F705, gen_evolve_cocoon_hold 4E0D3CBF, gen_evolve_emerge D4F48A35 (its event crg_cocoon_hatch) and the effects crg_cocoon_create 59A93586 (the woven shell; full 0.2 s after it is made, gone at 2.0 s), crg_cocoon_hold 6F02369D (a teal glow) and crg_cocoon_hatch D033D3AC (the burst); the world object cr_cocoon_collision (group 02E9C426, inst 69793182, model 0E3BD617) is not used. No game code references the ids (constants searched in the Steam exe .rdata): the cocoon is driven by data.

## Fix
mods/coop/evolve.cpp. creature.personal_editor (on): in Creature the host's editor request opens its own editor as a guest's does (no vote; coop.cpp h_Submit). creature.cocoon (on): a partner whose state says an editor (same world) is kept where it stood (coop.cpp drive_puppet): curl-in animation and the shell, then the hold animation, the glow and a new shell every 1.8 s (the shell lasts 2 s) while it edits; back in the world it stays in the cocoon until its puppet wears the new design (the partner's creature here changed and the puppet has it; the body came 1.2 s after the state and the puppet was made again, seen 2026-11-12), at most 15 s, 5 s when no new design came, then the emerge animation and the burst. The own creature: the egg scene after a mating is replaced by the cocoon (BUG-9161). Effects are world effects aligned with the planet's up at the creature (with the world's z the glow lay on its side). `coop.evolve [mates|partners|anim|fx|fxset|find|calls]`: tests and RE.

## Verification
no-fail-run: a feature. Live: the owner on TITAN (host) with the MSI as guest (2026-11-12): no vote, own editor, the MSI saw the cocoon and the emerge, 0 faults; the owner reported the shell gone after seconds and the old body coming out first (both fixed). TITAN host + guest (2026-11-13): the mating, the cocoon, the editor (no vote); on the guest the shell closed in 8 shots over 10 s, the emerge counted, 0 faults on both. Across the network (2026-11-13, TITAN host + TITAN guest + the MSI, three players after `coop.seats 3 now`): the MSI saw the host in the closed shell for 9 s and the shell crack open as it came out; then the MSI got the world (a guest on the host's species gets the host's new body, the existing BUG-0508 path); 0 faults on all three. Not seen yet: a guest with its own creature evolving (the host watching), the emerge from the guest's camera without the post-editor scene.

## History
- 2026-11-12: opened.
