---
id: BUG-0546
title: Space: the host faults at 0xE98DD0 (UfoGfx_SetVisible, null model) after a jump to an NPC empire's system in space_diplomacy, lane 5 only
status: fixed
kind: bug
stage: Space
reported: 2026-11-04
source: space_diplomacy on lane 5, fix/w42-space-life regression runs
role: host
versions: 
fixed_in: 0.7.6
branch: fix/w42-infra
commits: bfeeed18, 51558c36
tests: tools/tests/coop_space_npc_model.py (suite space_npc_model, space_npc_model_coop), space_diplomacy
signature: 0xC0000005 at 0x00E98DD0 SporeCoop::UfoGfx_SetVisible+0x40 data 0x4 (host, main thread)
docs: docs/re/space.md#space-lane5-pollination, docs/re/space.md#space-ufo-null-model
related: BUG-0323, BUG-0469, BUG-0545
---

## Report
space_diplomacy on lane 5 (GOG 2024): the host jumps to the NPC empire's star 004A0014 ("Plushio The 3rd
Empire"), about 1.8 s later its game faults once at 0x00E98DD0 (`UfoGfx_SetVisible+0x40`, data 0x4, the null
model of BUG-0323) and the lab fails the test on the fault: out/lab/20261104-110322, out/lab/20261104-111725 and
out/lab/20261104-111922 (the last with space.save_strip, space.keep_colonies and kit.longout all off). The same test
passes on lane 4 (out/lab/20261104-113833). Not the mod's ships (Bob's puppet was hidden, the host shows no
mirrors): inferred, a ship of that empire whose model lane 5's host profile lacks (BUG-0323 was lane 8 only, on the
guest). Not analysed further (not in S2's tickets). Reproduce: `lab.py run space_diplomacy --lane 5`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (2026-11-04, fix/w42-infra):
* The lanes' game copies are identical; the host's test profile differs. A/B/A/B on lane 5 with the same build: lane 5's own Pollination.package FAILs (out/lab/20261104-121214, -121818), lane 4's copy PASSes (-121357, -122038). Lane 5's cache holds no UFO with its model record (type 0x476A98C7), only summaries (0x02D5C9AF).
* The game's own bug: one game, no co-op session (offline), the same profile and fixture, `coop.space jumpto` to star 004A0014: the same fault (test space_npc_model with the guard off, out/lab/20261104-130233).
* Path (Ghidra GOG 2024): UfoGfx_InitFromUfo 0xE9B180 gives the default UFO only to a ship whose key is all zeros; UfoGfx_SetModel 0xE9A920 keeps no model when the model world makes none for the key (an NPC empire's ship 29133E22 / 2913505D from the cache without its model) and calls UfoGfx_SetVisible 0xE98D90, which writes [GetModel()]+4 without a null check. The game's default UFO 0x9D5DB21A is not in this install either (with it the game crashed later at 0xE99524 / 0xE9A908).
* Inferred: the trigger is the profile's caches together: lane 4 with lane 5's Pollination.package did not hit the case (its GraphicsCache.package probably has those ships' baked models). Any player whose caches lack a ship's model can hit it.
* BUG-0545 (fix/w42-space-play, lane 2) is the same fault: closed as a duplicate.

## Fix
mods/coop/gamefixes.cpp, switch `game.ufo_model` (on): a hook on UfoGfx_SetModel replaces a key the resource manager does not have (creations::key_known with the model type) with the default UFO when this game has it, else with the last ship model key it had (the player's own ship, set up first: 0675F7B2 in the test); a hook on UfoGfx_SetVisible returns when GetModel is null (backstop). `coop.status` counts both. Not covered: a ship set up before any known ship model (its key is kept).

## Verification
Test tools/tests/coop_space_npc_model.py (one game, `--solo`; suite space_npc_model; the co-op variant space_npc_model_coop) with lane 5's cache of 2026-11-04 (a copy is needed: out/fixtures/bug0546-Pollination.package of the main checkout, else this worktree's out/bug0546/; without it SKIP; a run where no ship needed the guard is a SKIP too). FAIL with SPORE_FIX_OFF=game.ufo_model: out/lab/20261104-130233 (lane 5, host fault at 0xE98DD0). PASS: out/lab/20261104-130403 (lane 5: space_npc_model, space_npc_model_coop, each 'ship models defaulted 2'; space_diplomacy with lane 5's own bad cache). Lane 4: space_diplomacy PASS, out/lab/20261104-130333 (its space_npc_model passed without the case: no ship needed the guard). After lane 4's cache went back onto lane 5: out/lab/20261104-130939 (space_npc_model 'defaulted 2' PASS, space_diplomacy PASS).

## History
- 2026-11-04: opened.
- 2026-11-04: fixed on fix/w42-infra: a game fix (game.ufo_model) and its twin test; BUG-0545 closed as a duplicate.
- 2026-11-04: released in 0.7.6.
