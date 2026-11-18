---
id: BUG-1251
title: Creature: a fossil (interactive ornament) one player digs stays (model, glow) in the other's game, or is dug where the other sees none
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in: unreleased
branch: fix/w53-advorn
commits: 3d2d26cb
tests: wobj_ornament_model
signature:
docs: docs/re/creature.md#creature-ornament-models, docs/06-testing.md#testing-adventure-after-creature
related: BUG-0459, BUG-1261
---

## Report
A players' video: the guest sees bones the host does not and cannot click them; bones the partner dug stay and glow in the other player's game; the guest digs a fossil in empty grass on the host's screen. Suggested cause: worldobj.cpp's orn_use() marks the reward given (+0x24 = 1) and turns off clicking (+0xA3 = 0) but does not call cInteractiveOrnament_NextModel (a_OrnUsed, which the debug `use` command calls), so the skeleton stays as new; and ornaments have no drop_own.

## Root cause
* Observed (disassembly): the game's own dig (interact node 0xD869B0) calls cInteractiveOrnament_NextModel 0xC40360
  (+0x248 to the next 12-byte model of +0x24C..+0x250, SetModelKey on the spatial, +0xA4 = 0; nothing without a next
  model), clears +0xA3 and posts the reward. worldobj.cpp's copy of a use (orn_use, the host's snapshot on the guest and
  the guest's request on the host) only set the reward's +0x10 / +0x24 and +0xA3: a fossil with a model list kept its
  old model (and the glow of that model) in the other game. The suggested cause was right.
* Observed (runtime, lane 9): none of the creature fixture's 12 ornaments has a model list; the game's own dig of a
  fixture bone changes only +0xA3 and the reward, exactly what the copy changed, and the bone's sparkle stays on both
  games (part of its model). So the fixture's bones show no difference; the players' fossils have a "dug" model.
* Not explained here: bones only the guest has (BUG-1261).

## Fix
worldobj.cpp orn_use calls NextModel before it marks the reward used (as the interact node does): the copy shows the
same next model. `coop.fix wobj.ornament_model off`: the old copy. Test tools: `coop.wobj orns`, `coop.wobj ornmem`,
`coop.wobj ornlist` (a model list for a fixture bone).

## Verification
wobj_ornament_model (two fixture bones given a list of two bone models, one dig each way): FAIL with
`SPORE_FIX_OFF=wobj.ornament_model` out/lab/20261118-131226 (the other game's copy stays at model 0, both ways); PASS
out/lab/20261118-131729 and RUN5. Key frames of the PASS run: the same view of each bone on both games, the dug bone shows
the other bone model on both.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-advorn): fixed (wobj.ornament_model); the guest-only bones split to BUG-1261.
