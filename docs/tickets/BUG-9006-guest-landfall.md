---
id: BUG-9006
title: A guest with its own cell starts the first land editor on the host species
status: fixed
kind: bug
stage: Creature
reported: 2026-11-03
source: Duo fork Windows lab
role: guest
versions: 0.7.3.6-duo6
fixed_in: 0.7.3.6-duo7
branch: duo-species
commits:
tests: tools/tests/duo_landfall.py
signature:
docs: docs/re/creations.md#creations-duo-cell-conversion
related:
---

On Vantiegan the host played cell 2DDF0D92 and the guest played BAF1AD40 with
an empty Creature library. Both landed. Both actual early editor requests used
29143A75 (Gaal, the host's yellow cell). A control that took BAF1AD40 through
Spore's own landfall yielded the visibly different spiked Stabbella.

Duo 7 generates the guest's early land model, ancestry metadata and thumbnail
from the guest cell. Both body shapes appeared in the early editors. A ready
vote received while the guest loads the new planet is honored after landfall.
