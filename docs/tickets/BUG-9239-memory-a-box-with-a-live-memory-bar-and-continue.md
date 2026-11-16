---
id: BUG-9239
title: Memory: a box with a live memory bar and Continue (save, restart Spore, reconnect) when the address space runs low
status: fixed
kind: feature
stage: all
reported: 2026-11-16
source: owner decision 4S 2026-11-16
role: both
versions: 0.7.14.1-lineages60-dev
fixed_in: 0.7.14.1-lineages61-dev
branch: feat/lineages-47
commits: COMMITS
tests: manual Windows QA: TITAN host + guest (Dorger.spo), coop.memrefresh show / continue, screenshots
signature:
docs: docs/06-testing.md#testing-memory-box
related: BUG-0631, BUG-9222
---

## Report
Owner 2026-11-15 / 16 (decisions 4S, 4T A, 4U A): long sessions end in "out of memory"; "if we can detect when Spore is about to crash, then let's provide a popup menu warning saying 'Spore is natively unable to clear memory usage. Press continue below to restart Spore and reconnect.' We'll show a live bar that shows how full the memory is." Ignore as well as Continue; first at 15 % left, again at 10 %; the bar yellow from 20 % left, red from 10 %.

## Root cause
Spore is a 32-bit game (2 GB of address space; the large-address flag in SporeApp.exe is refused by the Steam copy's DRM, tested 2026-11-15) and its heap keeps every 16 MB segment it took (docs/re/graphics.md#graphics-address-space): only a restart gives the space back. BUG-0631's host crashed with 133 MB free but no free block over 12 MB.

## Fix
- mem_refresh.cpp: the address space walked every 10 s (2 s when under 30 % left or the box is up; on a thread, support::mem_now / mem_walk_soon). The box (coop_ui set_memory_popup: title, the owner's text, a bar of three zones (green; yellow from 20 % left; red from 10 %), each dim until the fill reaches it and bright where filled (owner 2026-11-16), "Memory used: N% (M MB left)", Ignore grey, Continue blue) comes at 15 % left or no free block of 48 MB, again at 10 % or none of 24 MB after an Ignore; an Ignore there is the last. Our BUG-0631 reserve counts as free. Not scaled with Spore's UI; the partner markers are not drawn over it.
- Continue, a guest: a clean leave, then the restart; the new game joins the same host under the same name and rejoin token (the same player, not "Name 2").
- Continue, a host or a game alone: the game's own save (refused: the reason in the box, the buttons stay), MSG_HOST_RESTART (94) to the guests (they try to rejoin every 20 s for 6 minutes), the end of hosting, the restart; the new game loads the same planet from the same save folder and, 15 s after it is in the game, hosts again on the same port.
- The restart: a hidden PowerShell (-EncodedCommand) waits for the game to end and starts SporeApp.exe with the same arguments, folder and environment (Steam's SteamAppId included); the game ends with TerminateProcess right after. <AppData>\SporeCoop\restart-<instance>.txt holds what the new game does (read once and removed at once; ignored after 15 minutes).
- notice(): the -NoShare- tag never shows in a notice (seen during the test: "Host-NoShare- evolves its own creature").
- Switch core.memory_box (off: the old notice every 10 minutes). Tests: `coop.memrefresh [status | show <percent left> | off | continue | ignore]`.

## Verification
PASS (TITAN, the QA copy, host Dorger.spo instance 9 + guest instance 8, console only):
- the bar's zones lit up to the fill at 50 %, 14 % and 5 % left (out/shots/r63/bar_zones.png, bar_solo.png); the box at 14 % left (first version, one colour), "Memory used: 86% (286 MB left)"; at 8 %: red, "Memory nearly full" (out/shots/r63/box14.png, box8.png); no partner marker over it (box_nochevron.png).
- guest Continue: the game ended (pid 35484), started again (pid 57040), rejoined as the same player after 11 s; 965 -> 1186 MB free.
- host Continue at 12 %: saved Dorger.spo, the guest told, restarted, loaded Dorger.spo again, hosting again 30 s after the click; the guest rejoined by itself at the first try; both back in Creature (after_restart_8.png, after_restart_9.png).
no-fail-run: a new feature (no earlier behaviour to fail). Not checked: the MSI over the internet.
PASS (MSI, 2026-11-16, a game Steam started itself: steam.exe -applaunch, host Vandh): Continue saved, the game ended and started again, loaded Vandh and hosted again 29 s after the click; Steam's own check passed on the restarted game.

## History
- 2026-11-16: opened.
- 2026-11-16: done (4S, 4T A).
- 2026-11-16: released in 0.7.14.1-lineages61-dev.
