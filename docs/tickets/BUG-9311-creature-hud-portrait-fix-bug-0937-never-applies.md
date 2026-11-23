---
id: BUG-9311
title: Creature HUD portrait fix (BUG-0937) never applies when Windows relocates the exe: raw vtable address (UIFX-3)
status: open
kind: bug
stage: Creature
reported: 2026-11-22
source: audit
role: both
versions: 0.7.16.1
fixed_in:
branch: fix/w63-net
commits:
tests: hud_portrait (regression)
signature:
docs: docs/re/creature.md#creature-portrait-vtable-symbol
related: BUG-0937
---

## Report
UIFX-3 of the external static audit of 0.7.15.5 (verified row in out/research/ext-audit-verify-uifx-cell.md: REAL,
cosmetic, most Windows players). The same bug as BUG-9269 of PR #208 by IltonBJSilva (not in our tracker; credit to
them: they saw it on a Steam exe under Windows 11 and made the vtable a symbol). Unchanged on rel/0.7.16
(hud_portrait.cpp:65 at 5376d388). On Windows the Creature HUD portrait fix of BUG-0937 never applied: the portrait
kept the first species after a body swap.

## Root cause
hud_portrait.cpp compared the portrait drawable's vtable with the raw link-time number 0x01443248, while the two
functions it calls came from H->sym (rebased). SporeApp.exe is DYNAMIC_BASE (DllCharacteristics 0x8140) with a
.reloc directory (0xC2C04 bytes): Windows ASLR loads it elsewhere (PR #208: 0x00320000, every address -0xE0000; the
drawable's vtable there 0x01363248), so `shows()` answered -1 and the fix left the game's picture. Wine does not
relocate the exe: the lab always passed. Fail-safe (no crash).

## Fix
The vtable is the symbol `UTFWin::ImageDrawable::vftable` (overrides_dc04aee5.sym, verified: runtime BUG-0937 and
.rdata 2026-11-22: slot 4 = ImageDrawable::Paint 0x97D1E0, slot 6 = GetDimensions 0x97D0E0), asked with H->sym (as
PR #208 does). No switch: under Wine both sides are the same number, so no lab run could ever see the off side fail.
The same class looked for in mods/coop: no other raw vtable or game data address is compared; the only raw ranges
left are `0x401000..0x1300000` return-address filters of three diagnostic stack dumps (audio.cpp, game.cpp fx_from,
tribe.cpp BUG-0667 log): log text only, not fixed.

## Verification
no-fail-run: Wine never relocates SporeApp.exe, so the lab cannot run the relocated case; the PE header evidence
above and PR #208's Windows run are the failing side. Regression: `tools/lab.py run hud_portrait` must PASS (the
symbol equals the vtable the game uses under Wine).

## History
- 2026-11-22: opened.
