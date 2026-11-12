---
id: BUG-0779
title: Screen tearing with the mod and Spore ModAPI: SporeKit undid a frame cap set by another program; the game asks for no vsync
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: Discord 2026-11-09 Screen tearing
role: host
versions: 0.7.10.1, 0.7.11.1
fixed_in: unreleased
branch: fix/w48-reports
commits: 87d4b985, 41f47c43
tests: kit_frame_limit, kit_wrapper_cap, env_notes
signature:
docs: docs/02-modkit.md#modkit-frame-limit, docs/INSTALL.md#install-tearing, docs/REPORTING.md#reporting-support-environment, docs/06-testing.md#testing-frame-cap
related: BUG-0653, BUG-0667, BUG-0672, BUG-0506, BUG-0782
---

## Report
Discord 2026-11-09 "Screen tearing" (host, Creature stage): "the game behaves like this since multiplayer is on, not in
single player; the same when the 30 fps lock is removed; 0.7.3.3 did not have it, the picture was stable". The
player's report archive (SporeCoop-report-20261109-163355.zip, read 2026-11-10; the video was not readable).

## What the report archive shows (observed)
* Build: Steam (the GOG 2024 code, exe 3010a413), Windows 10 19045, NVIDIA RTX 3060 Ti, 2560x1440. Runs in the
  archive: coop 0.7.3.3 / SporeKit 0.3.5 (three runs, each crashed in the editor, see History), 0.7.10.1 / 0.3.13
  (two runs, no session), 0.7.11.1 / 0.3.14 (two runs, host alone 4 min and 1 min: `players=1/4`, nobody joined).
* `other hooks: d3d9.dll from the game folder, Spore ModAPI, Steam overlay, OBS capture, NVIDIA overlay`; `extra DLLs
  next to SporeApp.exe: d3d9.dll (7672 KB)`: a d3d9 wrapper (which one is not known: DXVK, ReShade, a frame
  limiter ...) sits under SporeKit's hooks in every run, 0.7.3.3 included.
* `CreateDevice ... 640x480 windowed=1`, then `device Reset (2560x1440)` in every run (SporeKit logs neither the
  PresentationInterval nor the refresh rate). `frames from Present`; the game-loop fallback only during the
  player's alt-tabs / minimized windows (keepalive: "the main thread runs a message loop", watchdog "window
  minimized 1"), never during play.
* The frame rate is not capped in any run with the mod: Creature 109 fps solo before hosting, 118 and 82 fps in the
  session (host alone), 96 fps after leaving (0.7.11.1); 138 fps in Creature and 88-136 fps in the editor without
  a session (0.7.10.1); 64 fps in the last session minute. No 30 fps cap is in effect, with or without a session.
* The mod's work per frame is negligible and the same with the session on and off: `tick avg 0.001-0.010 ms, max
  0.05-3.4 ms/frame`, effects hooks 0.0004-0.0013 % of the main thread, no faults.
* No partner was ever in the session, so no puppet stepping can be in the picture of these runs.

## Root cause
Not found. Observed: the mod's frame work and the session do not change the frame timing (above); SporeKit never
changes the swap interval, the device or the window (`sporekit/d3d9hook.cpp` h_CreateDevice / h_Reset only log;
nothing in sporekit/ writes PresentationInterval, SwapEffect, timeBeginPeriod, or sleeps in the frame path), and
`git log v0.7.3.3..main -- sporekit/` has no change of the present path that could (BUG-0653 presentlost, BUG-0667
frameunwind, BUG-0672 outside tasks, report shots, BUG-0403 hook gate, BUG-0713 window input).
Hypotheses (confidence low): (1) the game runs with vsync off and uncapped (82-138 fps on a 2560x1440 display), and
tearing is the normal result; whatever capped it at 30 fps for the player (the d3d9.dll wrapper's limiter, a ModAPI
mod, a driver profile) is not active in these runs, perhaps because SporeKit's hooks on Direct3DCreate9 /
CreateDevice / Present sit over the wrapper's own; (2) the wrapper interacts with SporeKit's Present hook (both on
the same vtable). The earlier hypothesis "the mod's work before Present moves it against the vblank" is disproved
for this player by the tick numbers.

## Fix idea
1. SporeKit: log the device's present parameters at CreateDevice and Reset (Windowed, PresentationInterval,
   FullScreen_RefreshRateInHz, SwapEffect, BackBufferCount) and which module the hooked Present / CreateDevice live
   in (the game-folder d3d9.dll or the system one), and a Present interval statistic per minute in the perf line
   (mean, p95, max interval).
2. Ask the player: vsync in the game's options, which program the game-folder d3d9.dll is and where the 30 fps lock
   comes from, and a test of the same game without that d3d9.dll (and without ModAPI) with the mod.
3. If a wrapper's limiter is lost under SporeKit: hook Present on the device the game uses after the wrapper made it
   (the wrapper's object), not the system d3d9 entry points, or detect the wrapper and tell the player.

## How to test
Lab (Wine, DXVK as the game-folder d3d9.dll with `d3d9.maxFrameRate = 30` in dxvk.conf, the Creature fixture): the
game alone, then with SporeKit + coop, then hosting. Read the new Present interval statistic. PASS: 30 fps (interval
mean 33 ms +- 2) in all three; a FAIL in the second or third shows the cap is lost under the mod. Plus a unit check
that the new CreateDevice / Reset lines carry PresentationInterval.

## New information (Discord 2026-11-11, the reporter; data)
"SporeCoop is not compatible with ModAPI, which causes the tearing": without Spore ModAPI the picture is stable. Not
known whether the game-folder d3d9.dll was removed too.

## Root cause (2026-11-12, fix/w48-reports)
Observed in the lab:
* The game asks for **no vsync**: `CreateDevice asks for: 640x480 windowed=1 interval=0x80000000 (immediate: no
  vsync)`, the same at every Reset. Tearing is the game's own behaviour; only its frame cap keeps it mild.
* The cap is the property kFrameLimitMS (AppProperties id 44, ModAPI SDK `kFrameLimitMS`; 16 ms in the GOG 2024 game,
  not 33 as the docs said). **SporeKit read it once at its first frame and wrote that value back every 10 frames**
  (focus.cpp apply_frame_limit, since 0.5.0): any cap another program set later (a ModAPI mod's frame lock, a frame
  limiter, the game) was undone within 10 frames. Lab: 50 ms written by `mem.write` went back to 16 ms (FAIL run).
* Spore ModAPI itself (sources read, data only) has no d3d9 hook and no frame timing; ModAPI mods can set
  kFrameLimitMS through the SDK. So "with ModAPI" a mod's cap was lost under SporeKit: inferred to be the player's
  case (his archive: 82-138 fps uncapped in every run with the mod), confidence medium (his mods are not known).
* A d3d9 wrapper's limiter is not lost: the player's d3d9.dll is 7672 KB, the size of DXVK 3.1.1's 32-bit d3d9.dll
  (7 856 142 bytes); with it in the game folder and `d3d9.maxFrameRate = 30` the lab games ran at 33.3 ms with
  SporeKit alone, with coop and hosting (kit_wrapper_cap); SporeKit hooks the wrapper's own functions inline.

## Fix
* `kit.fix framelimit` (SporeKit, focus.cpp): a kFrameLimitMS value SporeKit did not write is taken as the game's own
  and kept; `frame.limit` / `[sporekit] frame_ms` still win. Off: the old write-back.
* `[sporekit] vsync = 1`: PresentationInterval ONE at CreateDevice and Reset (the remedy for the tearing itself).
* Logs: the present parameters at CreateDevice / Reset, the module of each hooked d3d9 function (and a jump another
  program placed first), a Present interval statistic per minute; console `frame.interval`.
* The mod tells the player once per start when the game runs at 50+ fps without its cap (`core.uncapped_note`), with
  the advice; docs/INSTALL.md#install-tearing.

## Verification
* `kit_frame_limit` (lane 4): FAIL with `SPORE_FIX_OFF=kit.framelimit`: lab 20261112-041025 (property 16, 14.3 ms);
  PASS: lab 20261112-040913 (property 50, 48.9 ms), 20261112-055106-2 (final code).
* `kit_wrapper_cap` (lane 4, the lane's folder put back after it): PASS lab 20261112-041138 (33.3 ms in all three,
  4.1 ms without dxvk.conf, vsync=1 logged and 16.9 ms); no fix depends on it (evidence that the wrapper cap holds).
* `env_notes` (lane 5, the uncapped notice): PASS lab 20261112-041609-2, 20261112-053347, FAIL with `SPORE_FIX_OFF=core.uncapped_note`:
  lab 20261112-041852.
* Not verified: the player's own mods (ask him which ModAPI mod gave the 30 fps lock, and to try `vsync=1`).

## History
- 2026-11-10: opened.
- 2026-11-10: the player's report archive read (plan/0.7.13): the session and the mod's tick do not change the frame
  timing, the game is uncapped in every run, a game-folder d3d9.dll wrapper and ModAPI are present; ticket rewritten.
  Side finding in the same archive: three crashes of coop 0.7.3.3 entering the editor
  (`3010a413:SporeApp.exe Editors::PlayModeBackgrounds::Load+0x3AA5`, 0xC0000005) with this player's missing save
  areas (Pictures / Movies / Animations, the ru-ru PlayerDir case); crash_triage links them to BUG-0506 (the missing
  save area guard, fixed in 0.7.4); the 0.7.10.1 and 0.7.11.1 runs enter the editor without a crash.
- 2026-11-12: fixed (fix/w48-reports): SporeKit keeps another program's frame cap, a vsync option, logs, a notice; a wrapper's cap holds.
