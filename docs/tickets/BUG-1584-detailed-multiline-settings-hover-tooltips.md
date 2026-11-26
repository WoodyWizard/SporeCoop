---
id: BUG-1584
title: Settings UI: detailed multi-line hover tooltips for every individual setting row across all 5 languages
status: fixed
kind: feature
stage: All
reported: 2026-11-26
source: Owner feature request for v0.8.2
role: both
versions: 0.8.1.3
fixed_in: 0.8.2
branch: main
commits: 22860a46
tests: text_tests, ui_tooltips, ui_privacy_i18n
signature: settings hover tooltip single-line overflow and only firing on the right-hand value button
docs: docs/08-ui.md
related: BUG-0874, BUG-1575
---

## Report
Short setting names in the F9 Settings page did not always make it clear what each setting does, and the previous tooltip only appeared after 0.8 s when hovering the small value button on the right, rendering as a single un-wrapped line that overflowed or clipped longer descriptions.

## Root cause
- `mods/coop/coop_ui.cpp`: `tooltip_tick()` only hit-tested widgets registered in `g_named` (so only `D.set_btn[i]` on the right, not `D.set_label[i]` or `D.set_tag[i]`), used a flat 0.8 s delay on settings rows, and `refresh_tooltip()` rendered a single `D.tip_win` caption at fixed height `32 px`.
- `mods/coop/i18n.h`: `kSettingsTr[]` and the 5 local Display tooltips had brief one-line summaries rather than full ON/OFF and Together/Rivals explanations.

## Fix
- `mods/coop/tooltip_wrap.h`: added a pure, zero-dependency UTF-8 and CJK pixel-width line wrapper (`tooltip_wrap::wrap()`, `tooltip_wrap::measure()`, `tooltip_wrap::single_line()`) using Spore's measured dialog body font advances.
- `mods/coop/coop_ui.cpp` (`ui.settings_tooltips`):
  - Hit-tested the entire visible band of each setting row (`D.set_label[i]`, `D.set_btn[i]`, and `D.set_tag[i]`) on the Settings page with a responsive `0.35 s` hover delay (preserving the `0.8 s` delay on lobby/connect buttons).
  - Rendered multi-line tooltips inside the native 9-slice `D.tip_win` container using flush-left child `sui::label` lines (`kTipMaxLines = 8`), with a gold `<Setting Name> (<Scope>)` header line, generous inner padding inside the rounded 9-slice glow border, and automatic below-row flipping when near the top of the dialog.
  - Sanitized multi-line tooltip text to single-line `' | '` format without 256-byte truncation in `coop.ui state`.
- `mods/coop/i18n.h` & `mods/coop/settings_core.h`: upgraded all 38 session setting descriptions and all 5 local Display setting descriptions (`Frame rate cap`, `Borderless window`, `Third-person FOV`, `Hide IP`, `Language`) with detailed ON/OFF rules, timers/percentages, and Together vs Rivals notes in all 5 languages (`EN`, `RU`, `HU`, `ES`, `ZH`).
- `tests/text_tests.cpp` & `tools/tests/coop_ui_tooltips.py`: added unit tests for UTF-8/Cyrillic/CJK wrapping and 5-language completeness across all 38 settings, plus live Wine lab verification hovering left-hand setting labels on multiple Settings pages.

## Verification
- no-fail-run: UX enhancement requested directly by owner; verified with `tests/text_tests.cpp` (`make test`), visual screenshot inspection (`out/shots/ui_tooltip_setting_local_ru.png`, `out/shots/ui_tooltip_setting_session_ru.png`), and Wine lab tests.
- PASS runs: `20261126-161154:ui_tooltips,ui_privacy_i18n`, `20261126-161502:ui_tooltips`, plus `make test` (all 10 suites).
