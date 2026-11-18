---
id: BUG-9040
title: Distinct guest species requires manual founding after entering Tribe
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo campaign transition qualification
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_founding.py --control and --restored; tools/tests/duo_creature_tribe.py
switch: tribe.auto_founders
signature: No automatic caller for found_guest and register_guest
docs: docs/re/tribe.md
related: BUG-9002 BUG-9038
---

The separate-village implementation requires found_guest, register_guest and
village_orders setup commands. The approved baseline requires the guest's
current species to found its village automatically after entering Tribal.
Reuse the verified native factory, archived model and confirmed checkpoint
paths. Never claim an unrelated NPC village or overwrite a saved founder.

Transition driver A reached the native pre-Tribal editor but mishandled the
expected lack of a controlled avatar there. Its cleanup also used an invalid
Console.inst attribute. Both test errors are corrected in driver B; neither
is evidence of a native game failure. Native tutorials must be dismissed with
F9 closed before the guest can click Ready.

Automatic founding now waits for native gameplay and a saved planet, archives
the transmitted guest model, selects clear level ground, uses SpawnNpcTribe,
records the immutable founder roster, then confirms a native checkpoint and
reloads both peers. Retries retain scalar identities, refuse conflicting
rosters/species and do not claim existing NPC settlements. Explicit session
village_orders off remains respected. Four-species founding stays disabled.

FAIL m2-auto-found-a.log and b.log exposed the archive blockers BUG-9041/9042.
Run c created and checkpointed both founders but its short movement target
failed the progress assertion. Run d uses a40-metre tangent destination and
passes automatic creation, one village per species, ownership and actual
walking on both peers. m2-auto-found-cold-a.log independently passes full
process restart and fresh guest transfer with the same native species
291489FF/7CF26CCB. Both pairs exit with zero faults.

PASS m2-live-complete-b.log: final patch enabled through the complete live
Creature-to-Tribe transition, both own native editors, fire cinematic, Tribal
introduction, automatic founding and shared checkpoint/reload. Ownership,
one village per species and guest walking pass on both games, zero faults.
The preceding m2-live-transition-final.log driver stopped too early at the mode
change; the retained integration test waits for playable entry instead.
