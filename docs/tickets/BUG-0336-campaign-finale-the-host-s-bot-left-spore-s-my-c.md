---
id: BUG-0336
title: Campaign finale: the host's bot left Spore's My Collections / Mission Log window open; it pauses the game and takes no wheel, the whole finale stalls
status: open
kind: bug
stage: Space
reported: 2026-10-31
source: lab 20261031-024148 campaign#1 (lane 2), run dir out/lane2/campaign/20261031-024151
role: host
versions: main 8874eb1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Fourth final verification, lab 20261031-024148 campaign#1 (lane 2, 4 players, run dir
out/lane2/campaign/20261031-024151): space/finale failed. "[stuck] 04:04:32 finale: Alice: zoomed
out to the galaxy view", "the host waits at the core (... ctx=0 busy=1 cin=00000000 zone=1 ...
flights=0 ...)", then every guest stuck on both "Fly to Alice" steps (no Alice near the core) and
"every player entered" failed; the campaign took 6534 s instead of ~4900 (the finale ran its 30-60 s
settles and three `goto core` retries against a stuck host). The host's screenshot shows Spore's
My Collections window (Mission Log tab) open over its planet.

## Root cause
Observed: the host's game time was paused (finale busy=1 is cGameTimeManager +0x48 bit 0 or a
cinematic; cin=0, `coop.space hp` cine=0 movie=0: no caption) from the first finale step on: the
My Collections window was open. Reproduced on lane 9 (GOG 2024): a click on the 4th small HUD
button at the bottom left (114, 1054 in the 1440x1080 window) opens My Collections, the 3rd (84,
1054) the Sporepedia; both pause the game time, the mouse wheel does nothing (ctx stays 1) and
Enter / the green check (what the bots' `settle` presses) do not close them; Esc does (without
opening the pause menu). The finale tools (`coop_space_finale.settle`, `to_galaxy`) and the Space
play bot only knew captions, the captain screen (BUG-0303) and the pause menu.
Not a mod bug and not the BUG-0335 step logic (that only reads the guests' state; the guests'
failures here follow from the host never reaching the core). Inferred, not found: which of the host
bot's clicks hit the HUD button during the Space play phase.

## Fix
Test tools: `coop_space_finale.stray_window/close_stray`: the game time paused with no caption and
no cinematic means a window like these; Esc closes it (an Esc that opened the pause menu is undone
with Resume Game). Used by `settle` (after 6 s of such a pause), `to_galaxy` (before each wheel)
and `play_space` captions (the Space play bot). `OLD_FINALE_TOOL=1` turns it off (the switch these
tools already had for BUG-0303). No mod change.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-31: opened.
