---
id: BUG-9122
title: Cell: a partner's respawn blacks out this player's screen
status: fixed
kind: bug
stage: Cell
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev
role: all
versions: up to 0.7.8-lineages53-dev (still seen on 53 after the 50 fix)
fixed_in: 0.7.8-lineages54-dev
branch: feat/lineages-47
commits:
tests: QA Cellfive.spo host 9 + guest 8, guest `coop.pool selfhit 20` + `coop.giveup`, host screenshots every 0.3-0.5 s (mean brightness)
signature: -
docs: -
related: BUG-0237
switch: cell.partner_hatch effects.own_screen
---

## Cause
0.4 s after a partner's KO_RESPAWNED the partners played "cell_egg_L0_autohatch" on its puppet. That is the own
cell's respawn hatch and includes the screen fade: on the partner's game the whole screen went black.

## Fix
No effect on the partner's puppet (it just appears next to its partner). "cell_hatch_ice" (the revive burst) was
tried: it still dimmed the screen.

## Verification
Host brightness right after the guest's respawn: egg hatch (off) 8 then 89-91; ice hatch 21 then 87; none (on)
79-83 throughout.

## Reopened (2026-11-11, owner report on 0.7.8-lineages53-dev)
"The screen still acts like I'm respawning in cell stage as well when someone respawns." Host log of the 3-player Cell
session (Vandh.spo, 20:25-20:54): partners respawned often (`ko: player 2 cell state 1 -> 3`, "BigTexas respawned
small next to Pandize" at 20:28:56, 20:33:33, 20:34:17, 20:44:45; Kittykath at 20:34:39, 20:45:11), some with
"(no puppet here)". The 50 fix removed only the host-side autohatch effect on the puppet. Leads not checked yet: the
effects relay forwarding the respawning player's own hatch / fade effect to the others, the respawn placing the
puppet next to this player (camera), three players (the QA check had one guest), a respawn with no puppet here.

## Cause (2026-11-11, reopened)
The own respawn's `cell_egg_L0_autohatch` (FNV-1 of the lower-case name: B4B06F12) on the respawning player's own cell
went through the effects relay (effects.cpp) as a world effect (pool 5) and every partner's game played it. QA host log:
"effect: from 1 played on other id B4B06F12 key 00000000 pool 5" 0.1 s after "Guest respawned small next to Host";
host brightness 88 -> 7 -> 6 -> 73 -> 90.

## Fix (2026-11-11)
effects.cpp `effects.own_screen`: the Cell egg autohatch ids are never sent and never played for a partner (both ends,
so a partner of an older version does not black out a fixed game either). Status: `coop.effects` own_screen_kept=.

## Test (2026-11-11, two PCs)
TITAN hosts Cellfive.spo (QA copy, instance 9) with a QA guest (8); the MSI (Steam Spore, profile MSIGuest, Lineages
build of this fix) joins over Tailscale (relayed, rtt ~150 ms): 3 players. scratchpad respawn2pc.py: `coop.pool selfhit
20` + `coop.giveup` on the respawning game, `shot` on the others every 0.4 s, mean brightness:
* switch off on all three, the MSI respawns: host 81 -> 6 -> 36 -> 81, QA guest 82 -> 10 -> 38 -> 81.
* switch on, the MSI respawns: host 72-74, QA guest 72-74 (the MSI: own_screen_kept=1).
* switch on, the QA guest respawns: host 82-87, MSI 78-82.
