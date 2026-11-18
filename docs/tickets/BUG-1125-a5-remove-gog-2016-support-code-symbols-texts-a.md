---
id: BUG-1125
title: A5: remove GOG 2016 support (code, symbols, texts); a GOG 2016 exe gets a not-supported message
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-hygiene
commits: 6d9ec899, 65bbb8ee, 7e6e614d, 21bb1846
tests: make test (re_lint, tools_selftest relint / triage / symlist)
signature:
docs: CLAUDE.md, docs/INSTALL.md, README.md, docs/05-re-notes.md 5.1.1, docs/01-architecture.md 1.5
related: BUG-0108, BUG-0165, BUG-0216
switch: none (removal of an unsupported build; GOG 2024 code and data of coop.dll are byte-identical)
---

## Report
A5 of docs/plan-0.7.15.md, owner decision 3 (KIT-8, CORE-12 of the 0.7.15 audit): remove GOG 2016 support
(exe 922af7c7, 24 888 320 bytes, not supported since 0.7.8): its frozen code paths, symbol files, texts and tool
paths. The exe-hash detection stays: a GOG 2016 game gets a clear "not supported" message instead of the mod.

## Root cause
Not a bug: frozen files kept after the decision of 2026-11-05 ("frozen until they are removed"). They cost a
re_lint mode, 260 KB of symbol tables, a frozen_off list, wrong texts (setup "GOG 2016 build (supported)", the zip
README, INSTALL.md "0.7.x is the last version for GOG 2016", CLAUDE.md naming overrides_922af7c7.sym as THE symbol
file) and a Ghidra q.sh default of 2016.
A search of every frozen_off name and every build check in sporekit/ and mods/ (2026-11-15) found no code path that
runs only on GOG 2016 and does real work: build_name's 2016 arm, the setup's label, install.bat's WANT1 and three
comments. Kept on purpose: optional-symbol null guards (they also protect an unknown build), civ.cpp kill_local's
Die-only else (reachable on GOG 2024 with `coop.fix civ.mirror_remove off`), cellsync's `else a_ViewBoxes = 0`
(guards a partial symbol set), `build_id()` (the peers' build check).

## Fix
Code (6d9ec899): sporekit/symbols.cpp knows the GOG 2016 exe hash; such a game loads no symbols (also stale files of
an older install) and `unsupported_build_text()` is set; `mod_load` refuses every mod with that text; the log says it
(SK_ERROR), the overlay draws it at the top left every frame, `build_name` is "GOG 2016 (not supported: co-op off)".
Setup: the game line says "GOG 2016 build: this game version is not supported. Please update to the current GOG or
Steam version of Spore." and Install is disabled for it (Uninstall stays). install.bat stops with the message. The
uninstall lists (upd::kSymbolFiles, uninstall.bat) keep the two old file names: an older install still gets cleaned
up. Comments in game.h, game.cpp, audio.cpp reworded.
Removed (65bbb8ee): data/symbols/gog2016_922af7c7.sym, overrides_922af7c7.sym, frozen_off_922af7c7.txt,
tools/addrport/port_modapi.py; re_lint's FROZEN mode and frozen_off checks; tools_selftest's frozen_off case
(replaced by a table-lookup case on an unchecked generated name); Ghidra common.sh / q.sh / mkproject.sh default to
2024 and refuse 2016; crash_triage names a 2016 report and answers "not supported" without analysis (selftest case in
21bb1846); package.sh's zip README; net_tests' list of real pack paths.
Texts (7e6e614d): README, INSTALL, docs/README, CLAUDE.md ("Builds" and section 1 name overrides_dc04aee5.sym),
01-architecture 1.5, 02-modkit, 05-re-notes 5.1.1 / 5.2, 09, 10, the triage-crash skill, the issue template,
THIRD_PARTY_NOTICES, CHANGELOG. mods/coop/coop.cpp needed no edit (nothing 2016-specific in it), so there is no
"A5: coop.cpp" commit. Lane 8 (GOG 2016, the owner's) was not run or changed.

## Verification
no-fail-run: a removal, not a fix with a failing test. Proofs that GOG 2024 behaviour did not change:
* coop.dll built before (rel/0.7.15 12f470d4) and after: .text, .rdata and .data byte-identical (llvm-objcopy dumps).
* sporekit.dll: the new branches run only when the exe hash is the GOG 2016 one (symbols_load returns before reading
  any file; mod_load and the overlay test `unsupported_build_text()`, "" on every other build).
* re_lint: 615 symbols used by the code before and after; 0 problems (builds dc04aee5 only).
* make -j8 and make test pass; tools_selftest relint / triage / symlist pass.
* Not run: a GOG 2016 game (lane 8 is the owner's): the in-game overlay line, the log line and the refused mod load
  need a start of that exe; the setup window on a GOG 2016 folder.

## History
- 2026-11-15: opened.
- 2026-11-15: done on feat/w51-hygiene (6d9ec899, 65bbb8ee, 7e6e614d, 21bb1846).
- 2026-11-18: released in 0.7.15.
