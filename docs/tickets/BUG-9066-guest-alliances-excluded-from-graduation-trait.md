---
id: BUG-9066
title: Guest-earned Tribal allies are excluded from the native consequence score
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-06
source: Native Vantiegan five-alliance graduation
role: host guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_cityhall.py
signature: five native alliances incorrectly yield trait 0CAC124D
docs: docs/re/tribe.md
related: BUG-9038 BUG-9060
---

Two native games retain all five distinct allies, their five alliance totem
colors and all nine tools (trace9064-five-history.log). The host nevertheless
computes and stores aggressive trait 0CAC124D and opens military land editor
7D433FAD (role9066-stored.log and trace9064-religious-civ.log). The save just
before graduation reproduces the computed trait after a full process restart.

The CDBA30 consequence calculation calls C74680 at CDBAEE. The query is a
bool thiscall(profile, pointer to political ID), ret4 at +67 or +6D. The
caller tests AL and skips that tribe's relationship score if zero. Readback
on the same pre-graduation save: all five earned NPC allies have relationship
7 towards the home faction, but the native filter returns false for all five.
Home itself returns true but is excluded separately by pointer comparison.
See role9066-filter.log. Do not replace the consequence trait or editor ID.

Fix: only inside the native consequence calculation, also include a live NPC
whose native opinion of both registered founders reaches the relationship
manager's allied threshold. The original flag, every other caller, and the
game's timeline weight and role selection stay native. On the exact saved
five-alliance campaign, switching the fix off returns military 0CAC124D and
on returns friendly 60A78928 (role9066-control-fixed.log). Both peers retain
the same five NPC allies, totem colors and nine unlocks through this check.
The normal City Hall vote and religious 9AD7D4AA vehicle editor then complete.
Both games enter Civilization with the original two species, separate capitals,
matching custom City Hall and religious vehicle resources, and zero faults.
See role9066-editor.log, role9066-handoff.log and role9066-final-quit.log.
