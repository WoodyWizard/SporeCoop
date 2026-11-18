---
id: BUG-9226
title: Cell / Creature: when the whole party is down nobody is forced to respawn (they wait for a revive nobody can give)
status: fixed
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15
role: all
versions: 0.7.14.1-lineages58-dev
fixed_in: 0.7.14.1-lineages59-dev
branch: feat/lineages-47
commits: 95378ac3
tests: none yet (built; the owner's session was running)
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "When all party members die, it doesn't force the respawn." Host log: in Cell at 17:12:34 the host died the game's way while BigTexas was knocked out; BigTexas waited until the host came back and revived it (17:13:09).

## Root cause
Observed: a cell with no standing partner in the pool dies the game's way (go_down: "alone in the pool"); knocked-out partners kept their 60 s timer for a revive nobody could give. Creature: every downed player waited out its own timer.

## Fix
- Cell: the last standing cell that falls sends MSG_CELL_KO state KO_WIPE (4); a knocked-out cell that gets it respawns at once (as if its timer ran out); notice "The whole party is down: everyone respawns". Switch cell.party_wipe.
- Creature (and adventures): the host ends every active knockout timer once nobody has stood for 2 s (the usual loss, the game's death and respawn at the nest). Switch creature.party_wipe.

## Verification
no-fail-run: built only; to be checked on TITAN + the MSI after the owner's play session

## History
- 2026-11-15: opened.
