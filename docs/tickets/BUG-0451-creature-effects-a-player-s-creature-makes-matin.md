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
commits: 0d704785 (relay), 646dfc97 (test), 40c7834b (docs)
tests: creature_effects
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

* PASS: `python3 tools/lab.py run creature_effects --lane 2 --lane 3 --retry 0` ->
  out/lab/20261102-070230 (lane 2: OK; the host's effect was 0.47 m from the puppet, the guest's
  0.11 m; both games `total faults: 0`).
* FAIL: `SPORE_FIX_OFF=creature.effects_relay python3 tools/lab.py run creature_effects --lane 3
  --retry 0` -> out/lab/20261102-070312 (both directions: `sent 0 -> 0`, no effect reached the
  partner).
* Regression with the gates on: `python3 tools/lab.py run
  smoke,creature,team,puppet_walk,creature_social_ally --lane 2 --lane 3` ->
  out/lab/20261102-064552 (16/16 passed).
* `make test` passed.

## History

- 2026-11-02: opened.
- 2026-11-02: fixed on fix/w38-effects (relay through 0xC150D0 and the effect pools; test
  creature_effects; docs/re/creature.md#creature-effects-relay). Not released yet (`fixed_in:
  unreleased`).
