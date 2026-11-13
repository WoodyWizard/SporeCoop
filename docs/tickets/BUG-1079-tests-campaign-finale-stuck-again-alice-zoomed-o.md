---
id: BUG-1079
title: tests: campaign finale stuck again: "Alice: zoomed out to the galaxy view" after the campaign's jump to the space fixture
status: fixed
kind: test
stage: Space
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-camp
commits: 2623e169
tests: campaign (finale)
signature:
docs: docs/06-testing.md#testing-w49-camp, docs/re/civ.md#civ-cinematics-enabled
related: BUG-0991, BUG-1073
---

## Report
fix/w49-tests full campaigns on lane 4, lab 20261113-082316, 20261113-101808 and 20261113-060756: after the Civ end got
stuck (BUG-1073) the host jumped to the space fixture (`coop.load` in game, slot 11) and the finale reported
`[stuck] finale: Alice: zoomed out to the galaxy view`; the finale then went on through its shortcut and ended ok
(the guests flew to Alice). BUG-0991's fix (S1: the death caption, the repair) is in the branch; the host's ship was
not checked for a death caption in these runs (not analysed). In the runs without a space jump this was not seen.

## Root cause
Observed (host logs of the four runs): the host's ship was destroyed during the Space play and the game showed no
death cinematic, so spacecoop's respawn_tick forced the respawn ("space: the ship is destroyed but no death cinematic
runs: respawn (msg 0x67B2A95)": 11:53, 11:56 twice, 11:58 in 20261113-101808; three times each in -060756 and -082316).
The respawned ship is a new ship with the save's tools: the interstellar drive the bots added in the play phase
(`tool add interstellardrive`) was gone, and without it the wheel stops at the solar system. to_galaxy put the drive
back only after it had seen the death caption (BUG-0991's path), which never came. The missing death cinematic is
BUG-1073 (the cinematics left off since the Creature -> Tribe change, through every load). In 0.7.13's final check
(20261112-175447, no jump) the same finding came after the host's ship was disabled and lost (`lost=1` at 19:12:24):
again a new ship without the bots' drive. So: a test bot problem (the drive is the bot's own shortcut), made frequent
by BUG-1073; no player impact beyond BUG-1073's missing death cinematic.

## Fix
tools/tests/coop_space_finale.py to_galaxy: before the first wheel turn the ship gets the drive again when it lacks it
(`coop.space tool add interstellardrive`, "already there" otherwise), printed as "the ship had no interstellar drive (a
new ship since the play): added again"; `OLD_FINALE_DRIVE=1`: the old bot. BUG-1073's fix brings the death cinematic
(and BUG-0991's caption path) back.

## Verification
- no-fail-run: a test bot fix (the drive the bot itself adds); the FAIL side is the recorded finales (20261113-060756,
  -082316, -101808, 20261112-175447: the host's zoom-out stuck after a new ship). With both fixes: the full campaign
  20261113-142203 (after the jump to civ_npc and Civ -> Space by the game): every player zoomed out (2-3 notches),
  entered the core and got the Staff of Life; the same in the full campaign 20261113-160426 (no jump). campaign_civ_jump 20261113-133150 stuck once on a guest (Bob) right after
  "Fly to Alice" near the core, no destruction in its log: not explained (the zoom-out failure now prints the finale state,
  the ship and its drive).

## History
- 2026-11-13: opened.
- 2026-11-13: root cause found on fix/w49-camp (a new ship without the bots' drive after a forced respawn, BUG-1073); the finale bot fixed.
