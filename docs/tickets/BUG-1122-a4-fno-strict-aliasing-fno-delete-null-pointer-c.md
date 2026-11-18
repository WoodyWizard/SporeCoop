---
id: BUG-1122
title: A4: -fno-strict-aliasing -fno-delete-null-pointer-checks, printf format attributes, warnings fixed, -Werror
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-build
commits: acf0ef0e, 1f1911fa
tests: smoke (lab run 20261115-014032, lane 9), make test, make release-dry
signature:
docs: docs/10-dev-pipeline.md#pipeline-compiler-flags
related: BUG-0164, BUG-1120
---

## Report
Plan 0.7.15 A4 (audit KIT-4, KIT-5): the DLLs read and write game memory through casts at fixed offsets and
SporeKit lets code go on after a repaired fault, yet were built with strict aliasing and null-check deletion; 21
warnings at -Wall; the printf-style SkHost::log / out_printf / sk::logf had no format attributes (the 32-bit
vararg crash class: `%s` with an int, `%d` with a 64-bit value).

## Root cause
Preventive: no crash traced to it; such miscompiles show as rare, unexplainable state.

## Fix
COMMON (Makefile): `-fno-strict-aliasing -fno-delete-null-pointer-checks -Wimplicit-fallthrough -Werror`.
`SK_PRINTF(f, a)` in sdk/include/sporekit.h on SkHost::log / logv / out_printf, sk::logf / logv / out_vappend,
the static wrappers h_log / h_logv / h_out_printf (sporekit/main.cpp), outf (commands.cpp), trace.cpp write,
net session.cpp logf (trace::line / verbose / ev had it). 0 format mismatches in the tree; a deliberate
`H->log(.., "%s", 42)` is a build error. The 21 warnings fixed: 18 dead counters deleted (incremented, never read:
cellsync, creations, npc, social x2, terrain_diag, tribe x2, teamplay `M`, coop.cpp x6), a dead symbol lookup
(tribe.cpp a_TribeUI; find.cpp keeps its own), two documented UFO offsets `[[maybe_unused]]` (squadron.cpp),
braces for the dangling else in game.cpp (meaning unchanged), the chained volatile assignment split
(sporekit/exceptions.cpp). coop.cpp's six are the separate last commit "A4: coop.cpp warnings" (kept apart for
the B2 coop.cpp split; its hash changes when it is replayed; it also drops the temporary `-Wno-error` for
coop.o). Not taken: -Wformat=2 (15 -Wformat-nonliteral hits: translated format strings in coop_ui /
space_allies / space_contracts, by design). No coop.fix switch: a build flag, no behaviour of the mod changes.

## Verification
- Code size: .text coop.dll 0x36B3B6 -> 0x377066 (+1.35 %), sporekit.dll 0xAE784 -> 0xB29D4 (+2.4 %), loader
  unchanged (rel/0.7.15 built from git archive vs this branch).
- PASS: smoke on lane 9 with this build (lab run 20261115-014032): the 12 game tests ok; `unit` failed only in
  `tickets.py check` on private tickets of another package (out/tickets, not this branch), see the agent report;
  `ui` skipped (needs a free lane). make test PASS on the branch; make release-dry PASS (agent report).
- FAIL as wanted: the format probe (`%s` with an int, `%d` with a uint64_t) fails with the new header under
  -Werror and compiles with the old one; the same `%s` line put into teamplay.cpp stops `make`.
- no-fail-run: preventive build flags; no known miscompile to reproduce.

## History
- 2026-11-15: opened.
- 2026-11-18: released in 0.7.15.
