---
id: BUG-0202
title: making a hook while another thread runs the first bytes of the hooked function can crash that thread (hot reload re-hooks RandomInt)
status: fixed
kind: crash
stage: All
reported: 2026-10-27
source: found by kit_freeze.py hookchurn, fix/w1-sporekit
role: both
versions: 0.7.2.4 and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 51e8cce
tests: tools/tests/kit_freeze.py hookrace (suite kit_freeze)
signature: 0xC0000005 two bytes into the hooked function
docs: docs/02-modkit.md#modkit-crash-hard-cases
related: BUG-0132
---

## Report
Seen in the first version of `kit.test.hookchurn` (tools/tests/kit_freeze.py, BUG-0132), which made
and removed a hook 200 times while 3 threads called the hooked function: with the BUG-0132 fix on
the game still crashed, `0xC0000005 at sporekit.dll+0x22102, write of address 0x794B2100` (lane 3,
lab run out/lab/20261027-184432, crash-20261027-194506-i5.txt): eip two bytes into the hooked
function, executing the rel32 bytes of the new `jmp` as instructions.

In play: hot reload of coop.dll (`mod.reload coop`, `tools/lab.py reload`) hooks
`Math::RandomNumberGenerator::RandomInt` / `RandomFloat` again while game worker threads may call
them (docs/re/space.md 7.3). At the first load the hooks are made before worker threads draw from
the RNG (likely), so it matters mostly for the reload.

How to reproduce: make `hooks_churn_test` create the hook while the worker threads run (remove the
gate `g_churn_go` in sporekit/hooks.cpp) and run `kit.test.hookchurn 200 3`.

## Root cause
Observed: MinHook writes the 5-byte `jmp` over the function's first instructions without stopping
the other threads (freezing is off under Wine, it deadlocked). A thread that has executed the first
original instruction (here 2 bytes) continues at offset 2 and decodes the middle of the `jmp`.
MinHook's own answer (Freeze + ProcessThreadIPs moves suspended threads' eip) is what is switched
off. Removal does not have this problem: while hooked, no thread can be inside the first 5 bytes.

## Fix
Fixed on fix/w3-sys (51e8cce): parked hooks. Every hook jumps to a SporeKit stub `jmp [cur]`; making
(after the first time), enabling, disabling and removing a hook only switch `cur` (an aligned dword).
A removed hook is parked (`cur` = MinHook's trampoline, the original code); the next hook on that
target (the same mod after a hot reload) takes the stub over, so a target's code is written once per
process. The first hook is still a plain write (at boot, before the game's threads call it; the
RNG hooks of coop are made at its first load). `kit.fix hookpark 0` = MinHook's create / remove.
`kit.test.hookchurn <rounds> <threads> nogate` makes the hook while the threads run the function.

## Verification
Test: `tools/tests/kit_freeze.py --case hookrace` (`kit.test.hookchurn 200 3 nogate`).
* FAIL with `kit.fix hookpark 0` (`kit_freeze.py --old --case hookrace`, manual run on lane 3,
  2026-10-28 02:11): no answer, crash report `crash-20261028-021159-i5.txt`.
* PASS with the fix: 2 manual runs 2026-10-28 (200 rounds, ~40 M calls on 3 threads) and lab run
  out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842). The `hookchurn` case (BUG-0132) now switches `hookpark` off itself, so it still tests
  MinHook's removal.

## History
- 2026-10-27: opened (found by the BUG-0132 test on fix/w1-sporekit).
- 2026-10-28: fixed on fix/w3-sys (51e8cce); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
