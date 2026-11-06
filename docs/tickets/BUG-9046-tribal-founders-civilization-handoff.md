---
id: BUG-9046
title: Automatic Tribal founder handoff to separate Civilization nations is missing
status: fixed
kind: bug
stage: Tribe Civilization
reported: 2026-11-05
source: Baseline campaign continuation after qualified native Tribal actions
role: host guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_cityhall.py tools/tests/duo_civ_auto_founders.py
switch:
signature: civ bind_guest exists only as an explicit development command
docs: docs/re/civ.md
related: BUG-9040 BUG-9043 BUG-9045
---

The duo branch currently has explicit Civilization species binding, but no
automatic handoff from the saved Tribal founders to separate nations/capitals.
First exercise the remaining native team objectives and native stage transition.
Record what Spore preserves and creates before deciding how to assign nations.
Preserve distinct species and a single team's victories, unlocks and history.
Four-founder gameplay and independent stage progression remain out of scope.

Integration fixture DuoQATribeFinishA4/5 starts from the saved two-victory
alliance campaign. Food and short travel/1HP huts are explicit fixtures;
native ordered guest AI must deliver each lethal strike and native goals must
award history and unlocks. m2-tribal-finish-a.log earned victory3 on faction21E1.
Its next camera action failed because native registration reopened after the
checkpoint transfer, despite coop.status reporting busy:-. No game fault was
reported. The resumed probe dismisses registration after each reload and keeps
the same campaign; it does not grant a victory or recreate the save.
Automatic founding implementation and native FAIL/PASS evidence are recorded in
docs/re/civ.md#civ-automatic-founders-control. The second capital now preserves
the guest species on both games. Full Civilization controls/progression and
interrupted/offline founding qualification are still in progress.

Handoff qualification (2026-11-05): tribal-graduation-lifetime-final.log and
tribal-graduation-capitals-final.log pass native economic graduation and both
original species' capitals after a long vehicle edit. The automatically
founded Vantiegan campaign reaches five earned victories, then native military
graduation in tribal-roles-offline-final.log. The guest leaves during vehicle
editing, the host enters Civilization alone, and rejoining preserves immutable
291489FF/7CF26CCB identities, outfit models2916C9C5/7CFFC56B, two distinct
native capitals and matching custom City Hall/vehicle designs. Both runs exit
cleanly with zero faults. Full Civilization play is separate from this handoff.

Further boundary checks: tribal-absent-final-resume.log finishes when the
guest leaves before History advancement. Its first driver needed the normal
native Continue caption dismissed, not a gameplay patch. The actual packaged
DLL cold-loads both saved capitals before guest join, then restores the same
current outfits and custom designs on rejoin (tribal-package-cold-final.log).
Both processes exit with zero recorded faults.
