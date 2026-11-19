---
id: BUG-9264
title: Guest's HUD portrait keeps the old picture after its first creature is made (no png found for the new species)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: MSI QA 2026-11-19
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in: 0.7.14.1-lineages66-dev
branch: feat/lineages-47
commits: ec783b75
tests: tools/qa_win/qa.py (manual: MSI pair, a guest with its own creature joins; coop.portrait with the switch off and on)
signature: coop.portrait: vt=01C33248 same=-2 (was -1) sets=0
docs:
related: BUG-0937, BUG-9263
---

## Report
MSI QA 2026-11-19: a fresh guest made its first creature (start editor) and played it in the host's world; the
Creature HUD portrait (bottom right) kept another creature's picture.

## Root cause
Observed (MSI 2026-11-19, `coop.portrait` with the drawable's vtable added): `drawable=... vt=01C33248 image=00000000
same=-2 sets=0`. hud_portrait.cpp (BUG-0937) recognised the portrait's picture only for the drawable vtable
0x01443248, the GOG 2016 build's (docs/re/creature.md#creature-posed-top-portrait, CORRECTION 2026-11-19); on Steam
(the supported GOG 2024 code) it never matched, "not known" left the game's picture, and the portrait was never set:
every guest playing its own creature, or the host's evolved one, kept the first species' picture. The png of the new
species exists (GetImage succeeds).

## Fix
Commit ec783b75: when the window's picture cannot be read but the avatar's species png exists, the portrait is set to it
once per species (and again after a stage change or a load). Switch: `coop.fix creature.hud_portrait_unread off`
(BUG-0937 only).

## Verification
no-fail-run: two PCs, the game's HUD. Manual 2026-11-19 on the MSI pair (host Samuth, the guest profile with its own
creature 'Naurkawne' in that world): FAIL with `coop.fix creature.hud_portrait_unread off`: after the join
`sets=0 (unread 0)`, the portrait kept the other creature. PASS with it on: `sets=1 (unread 1)`, log "portrait: set
to 40626200!7CD928C8", the portrait shows the guest's yellow creature (screenshot).

## History
- 2026-11-19: opened.
- 2026-11-19: seen while verifying BUG-9263; not fixed.
- 2026-11-19: cause found (the GOG 2016 vtable) and fixed on feat/lineages-47 (ec783b75).
