---
id: BUG-0941
title: Space: a connected guest in Space does not follow the host's load of another Space save (it keeps playing its copy of the old one)
status: open
kind: bug
stage: Space
reported: 2026-11-11
source: fix/w47-final12b, first draft of coop_space_save_change.py (lab 20261111-105829, 20261111-105831)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0933, BUG-0709
---

## Report
Seen while writing BUG-0933's test (first draft: both games in the space fixture, then the host loads a copy of that
save from another slot, `coop.load Game9 Otherspace`; lab 20261111-105829 and 20261111-105831). The connected guest
did not follow the host into the loaded save: its log has no "receiving world" until it left and joined again
(then: "the host plays Madas: loading Madas.spo"). Until then it kept playing its copy of the old save in the same
star system, the host's ship shown there ("Alice appeared in the world"), and with equal save names the shared money
went on between two different saves.

## Root cause
Not analysed. Observed only: a host's load from Space into another Space save sends no world to a connected guest
(in the gated stages a guest elsewhere is pulled into the host's world). Also observed there: the save name the mod
uses in Space is the file's name until the first save (the copied file `Otherspace.spo` gave world 32DFFED8; after
the host's save for a share the name was `Madas.spo` again, 33DE75F6: "the game's save name is now 'Madas.spo'"),
so BUG-0709's "another Space save" can change in the middle of a session for a renamed copy.
With BUG-0933's fix the guest hears the host's new save (MSG_HOST_WORLD) and holds the shared money while it
plays the old one, which is the right answer for two different saves; whether the guest should be taken along is
the open question.

## Fix
None yet.

## Verification

## History
- 2026-11-11: opened.
