---
id: BUG-9269
title: Creature HUD portrait fix (BUG-0937) never works on the Steam exe: the ImageDrawable vtable was a raw GOG 2024 address
status: open
kind: bug
stage: Creature
reported: 2026-11-20
source: Lineages port test (lane A, Steam exe)
role: 
versions: 
fixed_in:
branch: feat/lineages-nests
commits:
tests: manual (lane A)
signature:
docs:
related: BUG-0937
---

## Report
Lane A 2026-11-20: the guest played its own creature (Guleskate) and its Creature HUD portrait showed the host's
species (Burpee). `coop.portrait` on both games: `image=00000000 ... same=-1 sets=0` (never set).

## Root cause
Observed: hud_portrait.cpp compared the portrait drawable's vtable with the number 0x01443248 (GOG 2024 at its
preferred base). This game ran at base 0x00320000 (preferred 0x00400000; sporekit.log `exe ... base=0x00320000`) and
SporeKit moves every symbol by -0xE0000 (`sym UTFWin::SporeStdDrawable::vftable` = 0x0133A978): the drawable's vtable
was 0x01363248, never equal, `shows()` answered -1 and BUG-0937's fix never set the portrait. Every game not loaded at
its preferred base (the Steam exe on Windows here) has it on main too; a raw address in the code (CLAUDE.md rule).

## Fix
The vtable is a symbol, `UTFWin::ImageDrawable::vftable` (overrides_dc04aee5.sym), asked by name.

## Verification
Lane A, the same session: before `image=00000000 ... same=-1 sets=0` on both games; after (new symbol and dll)
`same=1 sets=1` on the guest (`key=...7CA81684`, its own creature) and its portrait shows its own creature
(screenshot nests/g_portrait.png). The host's portrait was right already (same=1, sets=0).
no-fail-run: the failing state is the run before the fix in the same lane (same=-1); no lab test (the portrait check of
coop_creature tests runs on Linux at the preferred base, where the number matched).

## History
- 2026-11-20: opened and fixed on feat/lineages-nests.
