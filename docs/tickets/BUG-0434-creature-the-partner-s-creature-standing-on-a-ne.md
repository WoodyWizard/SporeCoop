---
id: BUG-0434
title: Creature: the partner's creature standing on a nest mound is drawn up to 1 m low for one frame when it comes into the observer's view
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: fix/w34-ability-pop (BUG-0414 analysis)
role: guest
versions: 0.7.3.3+
fixed_in:
branch: fix/w34-ability-pop
commits:
tests: tools/tests/coop_puppet_fight.py (listed as `entry`, not judged)
signature:
docs: docs/re/creature.md#creature-out-of-view-ground
related: BUG-0414, BUG-0435
---

## Report
Found while analysing BUG-0414 (lane 4, guest at 30 fps, netem `player`): the host's creature stands on the
fixture's nest mound (1.03 m over `GetHeightAt`); the guest turns its camera. Every time the puppet comes back
into the guest's view it is drawn on the plain terrain (1 m low) for one frame, then on the mound. 26-28 such
frames in 70 s of camera turns (`lod_exp` experiment, out/lane4/track/lod_exp_20261101-163454.json).

## Root cause
Observed: the game puts a creature outside the view on the plain terrain (`GetHeightAt`) and lifts it onto the
mound one update after it is in view again, so the first frame in view is drawn low
(docs/re/creature.md#creature-out-of-view-ground). Not found: which update grounds a culled creature; writes at
the mod's frame (teleport with or without the mark, +0x137 = 0, bit 0x800 of +0xB58) are undone by the next
update. Not checked: whether the game's own NPCs on a mound do the same (inferred: yes, nothing of ours runs
between the game's update and its drawing).

## Fix
Not fixed. Needs the function that grounds a culled creature (a hook after it, or keeping the puppet "in view").

## Verification
Reproduce: the host on the mound (`coop.npc gopos` to (-125.79, -489.25, -64.02) lifted 1.3 m: it lands 1.03 m over the
terrain), the guest 7-40 m away turning its camera with right-drags; `coop.track` rows of the puppet with vis 1
and hd 0.0 followed by hd 1.03. coop_puppet_fight.py lists these steps as `entry` (not judged).

## History
- 2026-11-01: opened (fix/w34-ability-pop).
