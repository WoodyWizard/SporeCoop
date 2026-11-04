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
fixed_in: 0.7.4
branch: fix/w38-effects
commits: 0d704785 (relay), 646dfc97 (test), 40c7834b (docs), d4e5a367 (world relay), c26a1d10 (tests), 41c0d56f (docs), 45ebc478 (review: no hook churn, main thread only, O(1) table), 091b056f (position +0xA0), 13cdfa7c (cost measurement), 02586842 (atomic stage gate), f81ef9bd (Release the relay's effect reference), 4d09ecd3 (3-player relay transform), aedd0954 (spoofing), 8706ea33 (validation), 15bd1ebc (stop relayed world effects), 7eb1478b (full transform + screenshots), 1e5ab55e (O(1) sent table)
tests: creature_effects, cell_effects, tribe_effects, effects_party, effects_validate, effects_live, effects_release
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

## Second review fix (2026-11-02)

The orchestrator's review found six more problems; each has its own commit and test where asked.

1. The host's relay to the other guests dropped the 0x44-byte transform of a pool 5 effect, so a
   third player never got a world effect (guest 2 dropped the short message). Fixed and tested by
   `effects_party` (3 players, `coop_effects_party.py`): the host's effect reaches guest 2 and guest
   1's effect reaches guest 2; without the forward the guest 1 -> guest 2 case fails
   (out/lab/20261102-184838).
2. Spoofing (BUG-0201): the host drops a guest message whose `player` byte is not the sender; a
   guest accepts only the host's relay. `coop.effects inject` feeds crafted messages through
   `on_message`; `coop.effects` counts `spoof=`.
3. Validation before a game call: stop pool/key, start id and pool (1, 2, 3, 5), pool 3 bone inside
   the model's bone array (cCreatureBase::CreateEffect 0xC150D0 indexes `bone * 700` without a
   check; the array end at model+0x2E8 gives the count, 24 in the fixture), pool 5 transform finite
   and position under 1e5. Counters `pool= id= bone= transform= short=`. `effects_validate` covers
   every rejection and a valid message that still plays.
4. Relayed world effects are stopped: 64 live at most (oldest stopped first), still active after 8 s
   stopped (IVisualEffect vt 0xC(0)), list dropped without a call on a stage / world change.
   `effects_live` relays 20/s for 30 s: the guest reaches 64, stays bounded, returns to 0.
5. The relayed transform now has scale 1.0 (+0x10) and the identity 3x3 (+0x14), the layout
   cCreatureBase's creators build. Visible proof: out/effect_relay_on_1.png (guest's screen with
   the effect at the host's puppet), out/effect_relay_off_1.png (same camera, no effect),
   out/effect_relay_host.png (the host's own screen); script out/effects_shot.py.
6. The 2048-entry linear scan of g_world_sent is a fixed 512-slot open-addressing set keyed by the
   effect pointer (cleared once a frame); `coop.effects` shows `sent_probes=`.

Runs (lane 2 and 3, the effects tests): PASS out/lab/20261102-191038, FAIL with
SPORE_FIX_OFF=creature.effects_relay out/lab/20261102-193333. The full rerun (smoke, creature,
team, puppet_walk and the effect tests) is in out/lab/20261102-191038 (the `unit` test's own lab
selftest lane-lock checks fail under the run's CPU load; `make test` alone and `unit` alone in the
lab, out/lab/20261102-194317, pass). Screenshots as above.

Item 7 (reference leak, after the orchestrator reviewed 15bd1ebc): EffectWorld::CreateEffect
0xA71300 AddRefs the effect for the caller (vt 0x00 at 0x923720), the game Releases such a pointer
with vt 0x04 (0xA81CE0, as the pool map erase 0xD2C9F0 does). The relay's live list now owns that
reference and releases it when it forgets the effect (ended, stopped, expired, evicted, session
end); a stage/world change still drops the list without a call. `coop.fix effects.release off` is
the old leaking behaviour; `coop.effects` prints released=/dropped=. Test effects_release (60 s per
window at the rate limit): on 2380 created / 2380 released / live 0 / commit flat, off 2320 / 0 /
+8.2 MB, 25 MB difference (out/lab/20261102-195942; a second run with coop.diag's mem line:
out/lab/20261102-201933); without the release the on window fails (out/lab/20261102-200305). The effects tests and effects_release pass in out/lab/20261102-202254 and fail
with SPORE_FIX_OFF=creature.effects_relay in out/lab/20261102-202617.

- 2026-11-02: not in 0.7.3.6: play_creature 1/5 with the relay vs 3/3 without (see BUG-0471); kept on fix/w38-effects for 0.7.4.
- 2026-11-03: rel/0.7.4 final check: a relayed world effect in Civilization faulted the receivers in the skin
  painter (effects_party_civ, out/lab/20261103-215559); 0.7.4 relays starts only in Cell and Creature
  (`coop.fix effects.other_stages`, off; Tribe faulted the same way, out/lab/20261103-223036), docs/known-issues.md#issue-effects-civ-space.
- 2026-11-03: released in 0.7.4.
