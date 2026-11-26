---
id: BUG-1575
title: Add 5-language localization (EN/RU/HU/ES/ZH), live Language selector, and F9 UI polish
status: fixed
kind: feature
stage: UI
reported: 2026-11-26
source: YouTube 0.8.0 stream audit + user request
role: both
versions: 0.8.0
fixed_in: 0.8.1.2
branch: main
commits: fa4b4d07, 1e808fcb
tests: ui_privacy_i18n, ui_golden
signature:
docs: docs/08-ui.md
related: BUG-1572
---

## Report
The 0.8.0 stream audit highlighted several Russian UI / layout polish issues (all 38 session settings and tooltips were untranslated in English, `"live"` was mistranslated as `"живая"`, `"+3 deg"` instead of `"+3°"`, cramped non-English button captions on `Report a problem` and Rivals action buttons, raw `a= b=` race progress diagnostics in the Rivals tab), and the user requested full localization for Hungarian (`hu`), Spanish (`es`), and Simplified Chinese (`zh`) in addition to English (`en`) and complete Russian (`ru`).

## Root cause
- Localization was previously scattered as ad-hoc binary `ru() ? ... : ...` checks across `coop_ui.cpp`, `rivals.cpp`, and `space_ext.h`, with no dictionary for settings or other languages, no manual Language selector in Settings, no CJK glyph advance handling in `caption_width()`, and no CJK fallback font in SporeKit's D3D9 overlay.

## Fix
- **Centralized 5-language i18n (`mods/coop/i18n.h`)**:
  - Added portable dictionary supporting English (`en`), Russian (`ru`), Hungarian (`hu`), Spanish (`es`), and Simplified Chinese (`zh`), covering all 38 session settings (labels + descriptions), setting groups, value labels, F9 lobby/connect/Rivals/Space UI, tooltips, and common gameplay notices.
  - Added Windows primary LANGID auto-detection (`lang_from_win_primary()`), manual override persisted in `coop.ini [ui] lang` (`auto|en|ru|hu|es|zh`), live `Language` selector on Settings page 4 (`Display (this PC)`), and `coop.uistate lang <code|auto>` command.
- **UI polish & layout safety (`mods/coop/coop_ui.cpp`, `mods/coop/rivals.cpp`, `mods/coop/space_ext.h`, `mods/coop/vote.cpp`, `mods/coop/civ.cpp`, `mods/coop/autosave.cpp`)**:
  - Replaced `"живая"` with `"на лету"` for live settings, `"+3 deg"` with `"+3°"`, and raw `a= b=` in Rivals Race Progress with human-readable per-stage progress strings.
  - Added hover tooltips for settings rows (`set0..set11`), dynamic widening for `D.report_btn` and `D.rivals_button[i]` when non-English captions need extra width (preserving exact English golden pixel rects), full static widget refresh in `refresh_static_labels()` on live language change, and CJK glyph advance in `caption_width()`.
- **SporeKit overlay CJK support & lint (`sporekit/d3d9hook.cpp`, `tools/glyph_lint.py`)**:
  - Added lazy CJK fallback `ID3DXFont` (`Microsoft YaHei` -> `SimSun` -> `Noto Sans CJK SC` -> `Arial`) in `sporekit/d3d9hook.cpp` whenever overlay text contains CJK codepoints (`>= 0x2E80`), properly integrated with `OnLostDevice` / `OnResetDevice` / `font_release()`.
  - Updated `tools/glyph_lint.py` to allow Latin Extended-A (`U+0100..U+017F`, WGL4 / Hungarian `ŐőŰű`) everywhere and CJK codepoints inside `mods/coop/i18n.h` only.

## Verification
- Unit tests (`make unit-cpp unit-net unit-lint`, `tests/validate_tests.cpp::test_i18n_and_privacy`, `tools/glyph_lint.py`): PASS (verifies all 38 settings and every string entry across EN/RU/HU/ES/ZH are non-empty and free of U+2014 em dashes; `glyph_lint: 0 problem(s)`).
- Wine lab verification:
  - Before `refresh_static_labels()` fix (`out/lab/20261126-131908`): `ui_privacy_i18n` **FAIL** (`D.settings_btn` stayed on previous language after live language switch).
  - After fix (`out/lab/20261126-132121`): `ui_privacy_i18n` **PASS** (34 s, 0 faults; live-switched through `ru`, `hu`, `es`, `zh`, `en`, verified translated `Settings` and `Report` captions on all 5 languages, verified Russian `Report` button dynamically widened `> 210 px` and English restored exact `210 px`, verified Russian Settings uses `"на лету"` and never `"живая"`).
  - Regressions: `ui_golden` **PASS** (`out/lab/20261126-132121`, 87 s; English pixel-perfect golden match preserved), `ui` **PASS** (`out/lab/20261126-133040`, 108 s).

## History
- 2026-11-26: opened from 0.8.0 YouTube stream audit + user localization request.
- 2026-11-26: implemented on `main` (`fa4b4d07`, `1e808fcb`), verified with unit tests, `ui_privacy_i18n`, and `ui_golden` (`out/lab/20261126-132121`).
