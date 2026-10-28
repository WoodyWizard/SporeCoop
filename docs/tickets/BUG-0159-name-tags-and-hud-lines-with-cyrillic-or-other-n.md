---
id: BUG-0159
title: name tags and HUD lines with Cyrillic or other non-Latin text are drawn off-centre
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#138
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 116731e
tests: make test (tests/text_tests.cpp)
signature:
docs:
related: #138
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/138 (full text, discussion).

The overlay centres its text with a width estimate of `bytes * 9` px. UTF-8 takes 2 bytes per Cyrillic letter and 3 per CJK character or symbol, so these labels are moved left by up to half their width:
- names over partners' creatures, cells, tribe members and cities;
- chief tags, KO labels and the off-screen arrows;
- the Space "SHIP DISABLED" banner;
- the Russian contract banner.

The arrows (►, ◄, ▲, ▼: 3 bytes each) shift every arrow label by about 9 px, even in English. `space_map.cpp` already counts code points correctly, so the code is inconsistent.

### Where
- [coop.cpp#L1203](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1203) `text_width` (used for name tags, owner tags, marks, arrows at L1252-L1304, L1412)
- [cellsync.cpp#L1365](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1365) (KO label and off-screen arrow)
- [spacecoop.cpp#L875-L877](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L875-L877) (`strlen(b) * 9`)
- [space_contracts.cpp#L1141-L1151](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_contracts.cpp#L1141-L1151) (`strlen(b) * 9` on the Russian text)
- Correct version for comparison: [space_map.cpp#L598-L605](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L598-L605)

### How to reproduce
- A guest named with Cyrillic letters joins a creature game. Compare the tag's position with an English name of the same length (`coop.team point` gives the screen position of the head).
- Set `[ui] lang=ru`, accept a contract in Space: the banner is visibly off-centre.

### Expected
Labels are centred on their anchor whatever the script.

## Root cause
**What happens**

- A partner named with 8 Cyrillic letters (16 bytes): the tag is centred as if it were 144 px wide instead of about 72, so it sits 36 px left of the creature's head.
- With the Russian UI, the contract banner ("КОНТРАКТ  армада, волна 1/2 ...", about 46 characters, about 80 bytes) is estimated at about 720 px instead of about 414. It is always drawn about 150 px left of the screen centre.

### Confidence
CONFIRMED by reading, low severity. A second reviewer verified it.

## Fix
Fixed on fix/w3-sys (116731e). One helper `utf8::overlay_width` (letter cells * 9 px; a CJK
character two cells) is used by coop.cpp `text_width` (name, owner and civ tags, marks, arrows),
cellsync.cpp (KO label, arrow), spacecoop.cpp (SHIP DISABLED), space_contracts.cpp (the contract
banner) and space_map.cpp. `coop.corefix text_utf8 0` does not bring the byte width back (only the
unit test compares).

## Verification
Test: `tests/text_tests.cpp` in `make test`: width("Ivan") == width("Иван"), width("► Bob") = 5 cells,
"太郎" = 4 cells.
* FAIL with `text_tests --old` (the byte width): 3 checks.
* PASS with the fix. Not checked in game with a screenshot.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #138.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (116731e); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
