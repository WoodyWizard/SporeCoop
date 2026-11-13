---
id: BUG-0862
title: Tooling idea: inspect, set with a verified backup and restore the Large Address Aware flag of SporeApp.exe
status: fixed
kind: tooling
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor tools/large_address_aware.py
role: 
versions: 
fixed_in: 0.7.13
branch: fix/w48-trust
commits: 62a0bdd2, a2002fc2
tests: tools/tests/tools_selftest.py laa, make test
signature:
docs: docs/INSTALL.md#install-laa, docs/06-testing.md#testing-trust-install
related: BUG-0631, BUG-0621
---



## Report
Tooling idea. Big worlds and adventures run out of the 2 GB 32-bit address space (the fork measured big adventures at
about 1955 / 2047 MiB, then a crash on a null allocation before the native write at 0x9AC8E0). Our plan-0.7.10 M1 asks
whether the Large Address Aware flag can be set safely (related BUG-0631, BUG-0621).

## Root cause
Not a bug in our code: SporeApp.exe is not LAA; our symbol loader already matches an LAA-patched exe by its code sha256.

## Fix idea
From the fork (jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor, tools/large_address_aware.py); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
A script that reads the PE header, reports the IMAGE_FILE_LARGE_ADDRESS_AWARE bit of FileHeader.Characteristics, sets it
only after writing a verified backup (hash checked), and restores the backup; their mod accepts exactly the LAA variant
of the GOG 2024 exe. For us: a setup / spore.sh option, never touching the player's exe without consent, and the PE
checksum handling to check.

## How to test
On a lane copy of the exe: set, check the flag and that only the Characteristics word changed, run smoke; restore and
compare the hash with the original.

## Fix
New `tools/laa.py status|set|restore <SporeApp.exe> [--force]`, re-implemented (not copied) after the fork's idea.
`set` writes a backup `<exe>.pre-laa` read back and compared plus `<exe>.pre-laa.sha256`, sets
IMAGE_FILE_LARGE_ADDRESS_AWARE in FileHeader.Characteristics, recomputes the PE checksum when it was not 0 (the
ImageHlp algorithm), checks that no other byte changes and replaces the exe through a verified temporary file.
Refuses: an exe that is already LAA (nothing to do), an existing backup, a non-PE / non-x86 file, and an exe with a
`.bind` section (Steam's DRM wrapper may check its header) without `--force`. `restore` checks the backup's hash
first. Never run by the setup or the mod: an optional tool documented in INSTALL.md (player consent = running it).
SporeKit's symbol match by code_sha256 is unaffected (the header is not part of it).

## Verification
- PASS: `tools_selftest.py laa` (13 checks on a fake PE32: only the flag and checksum bytes change, checksum valid,
  backup verified, set again is a no-op, restore gives the original, a damaged backup and a .bind exe are refused).
- FAIL on the base code: `SELFTEST_LAA=<missing tool> tools_selftest.py laa`: "tools/laa.py exists" FAILs (the tool
  did not exist).
- By hand on a copy of the GOG 2024 exe (dc04aee5..., in the scratch folder, never the game's): set changed 2 bytes
  (Characteristics 0x0102 -> 0x0122, checksum 0x017c5d61 -> 0x017c5d81, both valid), `tools/exe_info.py` still
  gives code_sha256 5a8267b2...; restore gave dc04aee5... again. The checksum code agrees with Wine's notepad.exe.
- no-fail-run: a game run with the LAA exe (smoke on a lane) was not part of this package (no lab); the INSTALL text
  says it is not tested in long sessions yet.

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-trust): tools/laa.py and its self-test; not run in the game (package A1 had no lab).
- 2026-11-12: released in 0.7.13.
