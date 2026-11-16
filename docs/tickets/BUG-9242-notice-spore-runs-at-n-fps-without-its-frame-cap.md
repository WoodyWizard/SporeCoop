---
id: BUG-9242
title: Notice: 'Spore runs at N fps without its frame cap' shows for a frame cap the player set (7 ms, about 143 FPS)
status: open
kind: bug
stage: all
reported: 2026-11-16
source: owner 2026-11-16 (62-dev)
role: host
versions: 0.7.14.1-lineages62-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs: docs/INSTALL.md#install-tearing
related: BUG-0779
---

## Report
Owner 2026-11-16 (62-dev, own Steam install): a notice "Spore runs at 79 fps without its frame cap (another mod or program lifted it). If the..." (cut off); "I have my FPS set to 120, why is it saying this?"

## Root cause
Observed in the owner's log (read only): "frame limit 0 ms (the game's own 7 ms)": the owner's framerate tool set Spore's own frameLimitMS to 7 ms (about 143 fps). diag_stats env_minute (BUG-0779, upstream) shows the note for any frameLimitMS under 10 ms at 50 fps or more, so a cap the player set on purpose counts as "lifted".

## Fix
- core.set_cap_quiet: a frameLimitMS of 1-9 ms (a cap someone set) only logs "a frame cap of N ms ... no note (BUG-9242)"; the note stays for no cap at all (0 ms). `coop.fix core.set_cap_quiet off`: the note for any limit under 10 ms.

## Verification
Not verified yet (built only).

## History
- 2026-11-16: opened from the owner's screenshot; fixed in code (core.set_cap_quiet), not verified yet.
