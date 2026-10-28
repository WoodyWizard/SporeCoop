---
id: BUG-0132
title: hooks that game worker threads run are removed without stopping those threads
status: fixed
kind: crash
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#111
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-sporekit
commits: 5d46dbb, 4de4d0a, 5e4dc7f
tests: tools/tests/kit_freeze.py (suite kit_freeze)
signature: 0xC0000005 in a freed trampoline (eip in no module) on a thread running a hook at removal
docs: docs/02-modkit.md (Diagnostics: hook removal), docs/06-testing.md#testing-kit-freeze
related: #111
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/111 (full text, discussion).

SporeKit's MinHook has thread freezing switched off by design: it deadlocked under Wine. The assumption is that hooks are installed and removed from the main thread while no other thread runs the hooked code. That does not hold for every hook. `coop.dll` hooks `Math::RandomNumberGenerator::RandomInt` and `RandomFloat`, and the game's worker threads draw from the global RNG too (docs/re/space.md 7.3). At quit (`App::cSporeApp::Shutdown` calls `mods_shutdown`) and on hot reload, `hooks_remove_all` restores the original bytes and frees the trampolines, and then `FreeLibrary(coop.dll)` runs. All of this happens while a worker thread may be:
- executing the 5 bytes being rewritten,
- running inside the trampoline being freed,
- inside `h_RandomInt` and about to return into the unloaded DLL.

### Where
- Removal of all hooks of a mod (no freezing, no wait): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/hooks.cpp#L78-L89
- MinHook patch: freezing off by default ("SporeKit installs hooks from the main thread between frames"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/third_party/minhook/src/hook.c#L327-L343
- Unload order (`on_unload`, `hooks_remove_all`, then `FreeLibrary`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/mods.cpp#L70-L86
- Quit path: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/gamehooks.cpp#L59-L65
- The other side, hooks a worker thread can run (`h_RandomInt` / `h_RandomFloat`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L478-L486 and their installation https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L939-L946

### How to reproduce
- The window is a few nanoseconds per call and exists only at quit or reload, so it is rare in play.
- To widen it for a test: a test-only command that starts a thread calling `RandomInt` on the global RNG in a tight loop (the function at `Math::RandomNumberGenerator::RandomInt` with `Math::RandomNumberGenerator::global_ptr`). Then run `mod.reload coop` 50 times through the console and count crash reports or faults in `exc`.

### Expected
Unloading or quitting never faults on another thread, whatever that thread was calling.

## Root cause
**What happens**

The racing sides are the main thread in `hooks_remove_all` / `FreeLibrary`, and a game worker thread calling RandomInt or RandomFloat.

1. `MH_DisableHook` copies the saved prologue back over the `jmp`. A thread that is decoding those bytes at that moment can execute a mix of old and new bytes.
2. `MH_RemoveHook` frees the trampoline. A thread that is executing the copied prologue in the trampoline jumps into freed memory.
3. `FreeLibrary(coop.dll)`. A thread that is inside `h_RandomInt` (it called `o_RandomInt` and has not returned) returns into an unmapped address.

Each case ends in an access violation on that thread, written as a crash report at quit or after `mod.reload`.

### Confidence
PLAUSIBLE. Freezing is off (checked by reading). That worker threads call the global RNG comes from docs/re/space.md 7.3. The window per call is tiny. A second reviewer verified the reading and the line numbers. Severity: low.

## Fix
Fixed on fix/w1-sporekit (the generic SporeKit part; coop's own reload order is BUG-0060).
* MinHook patch `MH_SetKeepTrampolines` (third_party/minhook): `MH_RemoveHook` no longer frees the
  trampoline. Observed: MinHook's `FreeBuffer` at once writes its free-list pointer into the first
  bytes of the trampoline, frees the whole block when it is empty, and hands the slot to the next
  hook; a thread inside a detour that then calls the original ran garbage. A few bytes per removed
  hook stay allocated (64 per hook per reload).
* `unload_from` (sporekit/mods.cpp): a mod that had hooks gets 50 ms after `hooks_remove_all`
  before `FreeLibrary`, so a thread inside a short detour (h_RandomInt) returns before the code is
  unmapped. Argued, not forced by the test: a detour that blocks longer is not covered.
* Not fixed here: writing the 5-byte `jmp` while a thread runs the first bytes of the function
  (making a hook, also at hot reload): BUG-0202. Removing a hook does not have that problem (while
  hooked no thread is inside those bytes).

Switch: `kit.fix hookkeep 0` (trampolines freed, no grace).

## Verification
Test: `tools/tests/kit_freeze.py` case `hookchurn`: `kit.test.hookchurn 200 3` hooks a function of
sporekit.dll whose detour spins before it calls the original; 3 threads call it in a loop. Each
round the threads wait at a gate while the hook is made (BUG-0202), then run while the hook is
removed with `hooks_remove_all` (the path of a mod unload).

FAIL (fixes off, `tools/tests/kit_freeze.py --old` on lane 3; manual run 2026-10-27 19:3x): the game crashed within 1.2 s; and with the final test (a second manual run):
`0xC0000005 access violation at 0x02FA03E0 ? write of address 0x00000000` (eip in the freed
trampoline block), crash-20261027-194744-i5.txt.

PASS (fixes on): lab runs out/lab/20261027-184811 and out/lab/20261027-184904 (lane 3, all six cases PASS both times; `diag` passed in the second run, its one flaky check "a minidump is written next to it" failed in the first attempt and on main before, 2026-10-24/25). With the fix: `200 rounds (200 hooked), 34520617 calls on 3 threads, trampolines kept`, no
crash. The `FreeLibrary` grace is a written argument only (the test does not unload a DLL).

### Related
#38 (hot reload of coop.dll can crash while a thread or a queued task still runs old code).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #111.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: verified on today's main (88341cf: same code), fixed on fix/w1-sporekit with switch `kit.fix`; test kit_freeze FAIL with the fix off, PASS twice with it.
- 2026-10-28: released in 0.7.3.0.
