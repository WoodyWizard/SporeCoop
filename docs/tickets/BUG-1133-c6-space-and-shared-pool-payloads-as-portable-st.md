---
id: BUG-1133
title: C6: Space and shared pool payloads as portable structs (decode, check, apply), fuzzed, golden bytes; libFuzzer in CI
status: open
kind: tooling
stage: Space
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Plan 0.7.15 C6 (audit NET-4, NET-10): about 170 hand reads of Space and shared-pool payloads sat next to game calls,
unreachable by the decoder fuzzer; checks applied unevenly (a cut K_KEPT_DONE list applied in part, `std::abs` of
-2^31 in K_MERGE_FIX); the libFuzzer build existed but nothing ran it.

## Root cause
No portable payload layer for the module messages (protocol.h had one only for the session's own types).

## Fix
mods/coop/wire/*.h: one struct per message kind for every MSG_SPACE kind (49 kinds, 1..87) and MSG_RES / MSG_RES_ADD;
handlers decode, check, apply (docs/04-netcode.md#netcode-wire-structs). No wire change: tests/wire_tests.cpp pins
59 shapes by hex and by the old writer calls copied verbatim. fuzz_decoders `fuzz_wire` fuzzes every struct (and
MSG_FIELDS); `make fuzz` is the libFuzzer build; `.github/workflows/fuzz.yml` runs it (2 min per push, 20 min weekly),
actions pinned by SHA (ci_lint clean). No switch (no behaviour change for valid input).
Behaviour changes, malformed input only: a K_KEPT_DONE cut inside its list is not used (the host keeps waiting and
resends K_KEPT after 10 s, before: partly applied and counted as answered); a K_MERGE_FIX of -2^31 is refused (before:
`std::abs` overflow let it through). Everything else refuses exactly what the old parse refused.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
