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
commits: 0d704785 (relay), 646dfc97 (test), 40c7834b (docs), d4e5a367 (world relay), c26a1d10 (tests), 41c0d56f (docs), 45ebc478 (review: no hook churn, main thread only, O(1) table), 091b056f (position +0xA0), 13cdfa7c (cost measurement), 02586842 (atomic stage gate)
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
  tribe member's effects): ~ superseded (2026-11-02): the transform setter is no longer hooked (see
  "Review fix" below). The create hooks record the effect and `effects::tick` reads the position
  from the effect object; when the position is on a local player-controlled object (`near_local`:
  avatar in Creature/Cell, `tribe::local_member_positions`, `civ::local_positions`, the local UFO in
  Space), the effect id and the transform are sent (pool 5) and the partner creates the same effect
  at the same world position with `cEffectsManager::CreateEffect` (0xA6CAD0) and the same transform.
  Animation-event effects (`AnimEffect_Create` 0x9FBF00, a thread-local flag) are skipped: the
  animation relay already makes them on the puppet.
* Cell, Tribe, Civ and Space use this same world path; only `near_local` differs. Cell and Tribe are
  tested (below). No effect could be made on a player's Civ vehicle or Space UFO with the fixture and
  the available test commands (no weapon-fire command; `coop.space hit` and `coop.fleet spawn` made
  no effect at all), so those two are wired but not verified here.
* ~ superseded (2026-11-02): the world hook was installed only in Creature/Cell. The faults came
  from removing/re-installing it at stage changes while the UI skinner's render thread was inside
  the function (`Skinner::cSkinnerTexturePainter::StartRender+0x28`). Now every hook is installed
  once in `game::init` (Tribe/Civ/Space included), a non-main thread only calls the original, and
  the stage logic only flips a work flag. See "Review fix" below.
* Diagnostics: `coop.effects` (counters, last sent/received effect with the game's effect object
  active flag and its position, the hook/tick cost, why a world effect was dropped),
  `coop.effects map`, `coop.effects probe <key>`, `coop.effects log on`, `coop.effects load <n>
  [period_ms] [id]` (the frame-time workload); counters also in `coop.status`.
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

## Review fix (2026-11-02)

A code review found three problems in the first shape; fixed on the same branch:

1. **Cost**: the transform setter hook did a linear scan of 4096 entries per transform set (every
   effect, every stage). It is gone; the create hooks use a fixed 1024-slot open-addressing table
   keyed by the effect pointer (no allocation) and `effects::tick` reads the position at
   `effect+0xA0` (VisualEffect_SetTransform 0xA827B0: the Vector3 object at +0x9C has a 4-byte
   header; reading +0x9C gave (0, x, y) and broke the world relay). Measured cost: 0.16-0.26 us
   per created effect, 0.0001-0.0006 % of the main thread; tick 0.001-0.02 ms/frame. Frame time on
   against off (20 effects/s, four 20 s windows, both orders): Creature -0.002 % (paired), Tribe
   +0.41 %, Civ +0.09 %, all under 1 % (out/perf-*.json).
2. **Threads**: the create functions also run in the UI skinner's render pipeline. Every hook body
   calls the original and returns when it is not on the game's main thread (id set by
   `effects::tick`); no naked stub writes a shared return-address slot any more; the animation
   wrapper 0x9FBF00 is a normal cdecl hook with a thread-local flag.
3. **Hook lifetime**: no `hook_create`/`hook_remove` during play. Every hook is installed once in
   `game::init`; `set_world_relay` (the stage logic) and `set_effect_relay_enabled` (the switch)
   only gate the work. The huge caught-fault counts of the earlier runs (Tribe 5357/9138, Civ
   2780) are gone: 0 faults in cell_coop, creature, tribe_coop, civ_coop, space_coop, chaos_tribe,
   guest_editor and editor_desync.

Verification of the review shape:

* PASS (final code, 02586842): `python3 tools/lab.py run
  creature_effects,cell_effects,tribe_effects --lane 2 --lane 3 --retry 0` ->
  out/lab/20261102-182618 (3/3 OK); the three are OK in the final regression too.
* FAIL (final code, 02586842): `SPORE_FIX_OFF=creature.effects_relay python3 tools/lab.py run
  creature_effects,cell_effects,tribe_effects --lane 2 --lane 3 --retry 0` ->
  out/lab/20261102-182754 (0/3, every case failed).
* Regression (final code): `python3 tools/lab.py run
  smoke,creature,team,puppet_walk,creature_social_ally,cell_coop,tribe_coop,civ_coop,space_coop,creature_effects,cell_effects,tribe_effects
  --lane 2 --lane 3` -> out/lab/20261102-180357 (the 18 game tests OK, every stage 0 faults; the
  `unit` test's lane-lock selftest is timing sensitive under the run's CPU load and failed its
  free-lane checks, `make test` alone passes on the same commit).
* The review-shape runs before the final one: PASS out/lab/20261102-164724, FAIL
  out/lab/20261102-164353 and out/lab/20261102-174202.
* Editors: guest_editor and editor_desync -> out/lab/20261102-170849 (2/2, 0 faults).
* Faults in the final regression: 0 for both games in cell_coop, creature, tribe_coop, civ_coop,
  space_coop and chaos_tribe (out/lab/20261102-180357).
* Perf runs: out/perf-creature.json, out/perf-creature-onfirst.json, out/perf-tribe.json,
  out/perf-civ.json (out/measure_effects_perf.py, lane 2).
* `make -j8` and `make test` passed.

## History

- 2026-11-02: opened.
- 2026-11-02: fixed on fix/w38-effects (relay through 0xC150D0, the effect pools and the generic
  world-effect/transform path; tests creature_effects, cell_effects, tribe_effects; docs/re/creature.md
  #creature-effects-relay). Not released yet (`fixed_in: unreleased`).
- 2026-11-02: review fix on the same branch: setter hook replaced by an O(1) table and a position
  read in the tick, main-thread-only hook bodies, hooks installed once (no churn). Measured under
  1 % frame time, 0 faults in every stage and in both editors. Tests and docs updated.
