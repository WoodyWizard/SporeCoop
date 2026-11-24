---
id: BUG-1261
title: Creature: the guest sees bones the host does not have and cannot click them (players' video, split from BUG-1251)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis (split from BUG-1251)
role: guest
versions: <0.7.17
fixed_in: 0.7.17
branch: fix/w82-creature
commits: f447d4f0, 9a4c9bc3
tests: tools/tests/coop_ornament_fade.py (ornament_fade_dig, ornament_fade_gone, ornament_fade_settle; twins ornament_fade_dig_off, ornament_fade_gone_off), tests/core_tests.cpp (ornament_fade_rules)
signature:
docs: docs/re/creature.md#creature-ornament-fadeout
related: BUG-1251
switch: wobj.ornament_fade
---

## Report
From the players' video (BUG-1251's report): the guest sees bones the host does not have and cannot click them; the
guest digs a fossil where the host sees empty grass.

## Root cause
Not reproduced (fix/w53-advorn, 2026-11-18): the creature fixture has the same 12 interactive ornaments in both games
(from the save: the same serials, places and model keys, `coop.wobj orns`), and a copied dig changes the same fields as
the game's own (+0xA3, the reward's +0x10 / +0x24).

Inferred from the game's code (fix/agy-open-bugs 576df657, an outside agent; its static findings re-checked by the
0.7.17 review, docs/re/creature.md#creature-ornament-fadeout): after a real dig the interact node's Exit 0xD868C0 calls
cInteractiveOrnament_FadeOut 0xC40990 (0.5 s, destroy): the game destroys the bone 3.5 s later; a part tier's
TierBoneCleanup 0xD408E0 fades the bones on screen and destroys the others at once. The copy of a dig on the other game
only marked the reward used (an unclickable bone stayed for good), and a bone the host's game no longer had (kGone) was
left on the guest, clickable: the players' "bones the host does not have".

## Fix
Ported from fix/agy-open-bugs 576df657 with the review's fixes (worldobj_orn.h, worldobj.cpp): the ornament state carries
the game's fade bit (kFading, +0x238 & 7 == 2); a copied fade runs FadeOut once (never restarted; DestroyInstance when
the game would not fade it); a bone the host's snapshot lacks inside its inner radius fades on the guest (bound at
once, unbound after kOwnGrace snapshots) only from a host of creature 1.3 and kGoneSettleS (10 s) after the host was
away, the world changed or the guest was out of the stage (review finding 6: a host load / editor return among bones);
a guest reports fades only to such a host (finding 7: a 0.7.16.1 host would mark the bone used); the host sends no
ornament list without its reward handler. Switch `wobj.ornament_fade` (off: 0.7.16.1). Test hooks `coop.wobj use <orn>
fade`, `coop.wobj ornkill <orn>`.

## Verification
Open, waiting for the lab run (pending night run: ornament_fade_dig, ornament_fade_dig_off, ornament_fade_gone,
ornament_fade_gone_off, ornament_fade_settle; regression wobj_ornament, wobj_ornament_model).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run ornament_fade_dig ornament_fade_gone ornament_fade_settle --lane N --retry 0 --no-build`.
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run ornament_fade_dig_off ornament_fade_gone_off --lane N --retry 0 --no-build` (the guest's bone stays).
* Unit: tests/core_tests.cpp ornament_fade_rules (the outside agent's cases plus gone_trusted and the old-host gate).
0.7.17 release lab: PASS: ornament_fade_dig (lab 20261124-020506), ornament_fade_gone (lab 20261124-020506), ornament_fade_settle (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-18: opened (split from BUG-1251 on fix/w53-advorn; that ticket fixed the dug-up model).
- 2026-11-23: fix (port of fix/agy-open-bugs 576df657 with the review's fixes) and tests on fix/w82-creature (f447d4f0); waiting for the night lab run.
- 2026-11-24: released in 0.7.17.
