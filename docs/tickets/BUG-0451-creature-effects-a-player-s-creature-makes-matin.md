---
id: BUG-0451
title: creature: effects a player's creature makes (mating-call hearts, social / ability particles) are not shown on the other players' screens
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: owner's manual test, lane 8, 2026-11-02 (screenshots: hearts over the host's creature on the host, none on the guest)
role:
versions:
fixed_in: unreleased
branch: fix/w38-effects
commits: 0d704785 (relay), 646dfc97 (test), 40c7834b (docs), d4e5a367 (world relay), c26a1d10 (tests)
tests: creature_effects, cell_effects, tribe_effects
signature:
docs: docs/re/creature.md#creature-effects-relay
related:
---

## Report

The owner watched the host's creature make pink mating-call hearts on the host's screen
(`out/hearts_host.png`); the guest's screen showed the host's creature (the puppet, name tag
"Alice") with no hearts (`out/hearts_guest.png`). The same was expected for every effect a player's
own creature makes (social ability particles, attack / ability effects, eating, level-up, ...), not
only the hearts.

## Root cause

Observed (lane 2, GOG 2024, 2026-11-02, logs from `coop.effects log on`, screenshots and Ghidra):

* The mating-call **animation** is relayed and played on the guest's puppet (MSG_ANIM: the guest
  reported `remote animations: received 1, played 1`), and the guest's own animation-event effects
  appeared on the puppet (footstep puffs `2D4ADB36`, the green mating-call ring).
* The effect the host's avatar makes is not an animation event: the host's mating call created
  `E1B24DDB` (bone 9) through `Simulator::cCreatureBase::CreateEffect` 0xC150D0 (manager caller
  0xC1511A), and the pack members made `4EC6CD85` through the same function. The guest created
  neither: that function runs only in the local player's ability/action code.
* The creature's persistent effects go through its two effect pools (`CreateEffectForPool1` 0xC1D3F0 /
  `CreateEffectForPool1Renamed` 0xC1D280 / `CreateEffectForPool2Renamed` 0xC1D5C0, starts and stops
  by `StartOrStopEffectFromPool1/2` and `StopEffectFromPools`). Nothing relayed those either.

Inferred: the class is "effects the game makes for the local avatar's creature through its one-shot
creator or its effect pools". Animation-event effects are already covered by the animation relay
(and must not be relayed twice).

## Fix

`mods/coop/effects.cpp` with hooks in `mods/coop/game.cpp` (`coop.fix creature.effects_relay`):

* Hooks the game's one-shot creature effect creator 0xC150D0 (`CreateEffect`, pool 3 with the bone as
  key) and the pool creators / `StopEffectFromPools` (pool 1 / 2, key = pool key).
* Sends `MSG_EFFECT 61` only for the local avatar (`game::avatar()`, never a puppet), so a relayed
  effect is never echoed; the host relays a guest's message to the other guests. Rate limit 40/s;
  a stop is sent only for a key this game sent a start for.
* The receiver calls the same game function on the partner's puppet: `CreateEffect(puppet, bone, id)`
  for a one-shot, `StartOrStopEffectFromPool1/2(puppet, 1, id, key)` for a pool effect.
* A second, generic path for effects that bypass both (a death, a part found, a cell's effects, a
  tribe member's effects): the base effect transform setter 0xA827B0 is hooked; when the effect's
  position is on a local player-controlled object (`near_local`: avatar in Creature/Cell,
  `tribe::local_member_positions`, `civ::local_positions`, the local UFO in Space), the effect id and
  the 0x44-byte transform are sent (pool 5) and the partner creates the same effect at the same world
  position with `cEffectsManager::CreateEffect` (0xA6CAD0) and the same transform. Animation-event
  effects (caller 0x9FBF00) are skipped: the animation relay already makes them on the puppet.
* Cell, Tribe, Civ and Space use this same world path; only `near_local` differs. Cell and Tribe are
  tested (below). No effect could be made on a player's Civ vehicle or Space UFO with the fixture and
  the available test commands (no weapon-fire command; `coop.space hit` and `coop.fleet spawn` made
  no effect at all), so those two are wired but not verified here.
* The direct `EffectWorld::CreateEffect` hook faults when it sees the UI skinner's render pipeline
  (Tribe/Civ: `Skinner::cSkinnerTexturePainter::StartRender+0x28`, thousands of caught faults). It is
  installed only in Creature/Cell; the always-on `cEffectsManager::CreateEffect` forwarder covers
  Tribe. The hooks only queue into a fixed ring, `effects::tick` sends on the main thread.
* Diagnostics: `coop.effects` (counters, last sent/received effect with the game's effect object
  active flag and its position), `coop.effects map`, `coop.effects probe <key>`,
  `coop.effects log on`; counters also in `coop.status`.
* Switch off: `coop.fix creature.effects_relay off` (the old behaviour, nothing sent or played).

## Verification

`tools/tests/coop_creature_effects.py` (suite `creature_effects`, registered in
`tools/tests/suite.py`): the host's mating call (Backspace) and the guest's, and the guest's join
greeting; the partner's own game must have the game's effect object active at the actor's puppet
(`coop.effects` reads the effect's vt 0x10 and its position, compared with `coop.status`'s puppet
position).

* PASS: `python3 tools/lab.py run creature_effects,cell_effects,tribe_effects --lane 2 --lane 3
  --retry 0` -> out/lab/20261102-093244 (3/3 OK).
* FAIL: `SPORE_FIX_OFF=creature.effects_relay python3 tools/lab.py run
  creature_effects,cell_effects,tribe_effects --lane 2 --lane 3 --retry 0` ->
  out/lab/20261102-094033 (0/3: every case failed).
* The earlier creature-only pass/fail: out/lab/20261102-070230 / 20261102-070312.
* Final regression: `python3 tools/lab.py run
  smoke,creature,team,puppet_walk,creature_social_ally,cell_coop,tribe_coop,civ_coop,space_coop,creature_effects
  --lane 2 --lane 3` -> out/lab/20261102-094358 (17/17 passed).
* The Tribe/Civ UI Skinner fault of the first world-hook shape was fixed (the world hook is only in
  Creature/Cell now, the hooks queue and tick on the main thread): tribe_coop, civ_coop and
  chaos_tribe -> out/lab/20261102-093433 (3/3 OK).
* `make test` passed.

## History

- 2026-11-02: opened.
- 2026-11-02: fixed on fix/w38-effects (relay through 0xC150D0, the effect pools and the generic
  world-effect/transform path; tests creature_effects, cell_effects, tribe_effects; docs/re/creature.md
  #creature-effects-relay). Not released yet (`fixed_in: unreleased`).
