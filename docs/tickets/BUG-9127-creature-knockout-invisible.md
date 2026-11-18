---
id: BUG-9127
title: Creature: a knockout ("downed") shows nothing, so a death or a knockout cannot be told apart
status: fixed
kind: bug
stage: Creature Adventure
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev ("downed but not out doesn't display much; sometimes you die, sometimes not")
role: all
versions: all
fixed_in: 0.7.8-lineages50-dev
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9 + guest 8, 28 m apart, `coop.giveup down` on the guest, screenshots of both
signature: -
docs: -
related: BUG-9118 BUG-9089
switch: creature.rescue_ui
---

## Cause
creature_rescue::status() (the countdown and the partner's "revive" line) went only into a console report. Nothing
on screen said a player was down, so a knockout looked like random survival.

## Fix
* The status line is drawn centred under the top bar: the downed player's countdown in red, the partner's "Revive
  <name>: stay close for 1.5 s" in yellow.
* The host tells everybody when a knockout starts: "<name> is down! Stand next to them within 30 s."
* A downed partner's off-screen / over-head arrow is red.

## When there is no knockout (a plain death)
A knockout needs exactly two players in the session (`duo`), the partner's state fresh (< 2 s) in the same world,
and the lethal hit through the creature damage hook. Otherwise (3+ players, the partner loading or in an editor)
the death is native. Within reach of a partner the older teamplay rescue may also pull the player up at once.
