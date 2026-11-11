---
id: BUG-0862
title: Tooling idea: inspect, set with a verified backup and restore the Large Address Aware flag of SporeApp.exe
status: open
kind: tooling
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor tools/large_address_aware.py
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
## Verification

## History
- 2026-11-10: opened.
