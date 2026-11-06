---
id: BUG-0575
title: Tribe: the guest crashed after double-clicking the home icon (3010a413:SporeApp.exe+0x6826F3)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.8
branch: fix/w44-crashes
commits: b37ea975, f3b238dc, 0570e27d
tests: crash_model_fx (tools/tests/coop_crash_guards.py --case model_fx)
signature: 0xC0000005 at 0xA826F3 (GOG 2024; Steam +0x6826F3), read of 0x94, VisualEffect_Start <- SpatialEffects_UpdateByDistance 0xC8ACC4 <- CreatureView_Update 0xE8C96E
docs: docs/re/tribe.md#tribe-model-effects-crash, docs/re/tribe.md#tribe-model-fx-guard, docs/known-issues.md#issue-tribe-effect-list-crash
related:
---

## Report
The guest crashed after double-clicking the home icon in Tribe. Crash 0xC0000005 at 0x00B626F3 (SporeApp.exe+0x6826F3; chain cSpatialObject::SetModelKey+0x594 <- QuaternionToMatrix <- cModelObject <- cPlanetaryArtifact::SetLocomotion <- Property::GetValue <- SerializerReadStream::GetRecord), Steam build 3010a413, coop 0.7.5. The chat just before: the guest gave tribe members to the host several times.
Reports: SporeCoop-report-2026-11-04 200118 (crash-20261104-194521) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (dump, tools/crash_triage.py): 0xC0000005 at 0x00B626F3 = GOG 2024 0xA826F3 (the Steam .text is the
  GOG 2024 code), read of 0x94, in the function 0xA82660 (thiscall, a Swarm visual effect's vt+8 with arg 0: it
  walks its component vector +0x144..+0x148, 0x5C bytes each, and calls each component's +0x38 object vt+8).
  The dump's context has edi = 0x5C, esi = 1: the first component's call returned with the callee-saved registers
  broken (a freed component whose vtable is garbage), then the loop read [0x5C + 0x38].
* Observed (disassembly): the caller is 0xC8AC40.. (return 0xC8ACC4): a model's attached-effect list (vector at
  +0xC0/+0xC4, 0x3C bytes per entry, effect at +0x38, a distance threshold at -0x30) started or stopped by the
  camera distance (vt+8 / vt+0xC with 0). The home double-click jumps the camera: effects of the models there are
  started; one of them was freed (inferred: an effect of a member given to the host, i.e. a mirror whose effect
  the relay of BUG-0451 attached, freed with the member's old creature).
* The mod's log has no line before the crash (19:45:21).

* Observed (the guest's log, fix/w43-transitions): the session's log (previous/sporekit-20261104-194522.log of the
  report) has gifts of single members at 19:35, 19:38, 19:41, 19:42, nothing of the mod in the 9 s before the
  fault. Its `calls:` frames mapped to GOG 2024 (the Steam exe loaded at 0x4E0000, minus 0xE0000): 0xC88F20+0x14,
  QuaternionToMatrix, **0xC8ABF0+0xD4** (the model's attached-effect loop), **0xE8C100** (+0x82D, +0x860),
  0xC69BA0+0x26, **0xE8E5B0** (+0x23, +0x495: an object view's update), 0xE8CD10+0x2B: an object's view comes into
  the camera's range and starts its model's effects; one effect's component is freed.

* Observed (Ghidra / asm, fix/w44-crashes, docs/re/tribe.md#tribe-model-fx-guard): the report's names
  (SetModelKey, QuaternionToMatrix, cModelObject, cPlanetaryArtifact::SetLocomotion) are nearest-symbol names of
  stale stack values; nothing set a model or a locomotion. The real chain: VisualEffect_Start 0xA82660 <-
  SpatialEffects_UpdateByDistance 0xC8ABF0 (the camera range check of a model's attached effects) <-
  CreatureView_Update 0xE8C100 (a creature's view, list owner = the creature's spatial +0xC0) <- the view list walk
  0xB3CB6F. Start calls vt 8 of each component of the effect without a check; the registers (esi = 1, edi = 0) mean
  the first component's vtable was not one: the component was freed while the effect stayed listed.
* Not found: what freed the component. The mod's effect relay keeps and releases only its own world effects (pool
  5), not the attached lists; its references balance. Inferred, not shown: a creature given to the host (a mirror
  removed and made again) or an effect world change.

## Fix
Guard `game.model_fx` (mods/coop/gamefixes.cpp): hooks on SpatialEffects_UpdateByDistance 0xC8ABF0 and
SpatialEffects_SetTransform 0xC89EF0 check every listed effect before the game's loop (its vtable in the game's
image with code slots; for a VisualEffect the component vector and each component's vtable); a broken one leaves
its entry without a call or Release (the game makes it again when it wants it). Arithmetic checks only (every view,
every frame). The class: any model or view whose attached effect has freed parts, not only a creature's.
**Not verified against the player's crash** (not reproduced; the guard is where the dump shows the bad pointer).
`coop.fix game.model_fx off`: 0.7.7. Test commands: `coop.fxcut`, `coop.fxlist [creature|all]`.

## Verification
Not reproduced: lab tribe fixture, the guest gave 8 members to the host one by one (`coop.give`), panned and
double-clicked the HUD's home button after each (out/shots575/ of this worktree, not in git); no fault, no crash.
The click on the home icon did not move the lab camera (the collapsed minimap holder takes the click there), so
the camera jump of the report was not made. Package T1.
Not reproduced again (fix/w43-transitions, lane 5, a scratch run 2026-11-04 23:2x): the guest gave its only member
of the fixture's split, then 8 rounds of a minimap click away (150, 1000) and a double-click on the minimap's house
icon (55, 918): no fault; the house icon did not move the guest's view either (the view stayed where the minimap
click put it). A repro needs the players' camera jump: a guest with many members given one by one and the jump done
by the game's own "home" (? which control: not found).

* fix/w44-crashes: the crash state made on purpose (`coop.fxcut`: the next model with a running attached
  effect is stopped by its own loop, gets one freed component, a vtable of nulls, and is back in range: the range
  check starts it, as the camera jump did):
  - FAIL with the fix off: lab 20261105-101857 (`SPORE_FIX_OFF=game.model_fx`, crash_model_fx: 422 faults on the
    guest, a call through null at VisualEffect_Start+0x9E <- 0xC8ACC4, the player's frame, <- view 0xE92FF3 <- 0xB3CB71).
  - PASS with it: lab 20261105-101523 (crash_model_fx: dropped 1, planted 1, no fault, the guest draws).
  - The players' scenario itself (gifts of members + the home camera jump) still not reproduced.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (T1), crash site analysed from the dump, not reproduced.
- 2026-11-04 (fix/w43-transitions, T1): the call chain mapped to GOG 2024 (an object view's update); still not reproduced.
- 2026-11-05 (fix/w44-crashes, X2): real call chain found (the report's names were stale), guard game.model_fx with a planted-state test (FAIL 20261105-101857, PASS 20261105-101523); the cause of the freed part not found; status fixed (guard, not verified against the player's crash).
- 2026-11-05: released in 0.7.8.
