---
id: BUG-0575
title: Tribe: the guest crashed after double-clicking the home icon (3010a413:SporeApp.exe+0x6826F3)
status: open
kind: crash
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-model-effects-crash, docs/known-issues.md#issue-tribe-effect-list-crash
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

## Fix
Not done: not reproduced.

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

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (T1), crash site analysed from the dump, not reproduced.
- 2026-11-04 (fix/w43-transitions, T1): the call chain mapped to GOG 2024 (an object view's update); still not reproduced.
