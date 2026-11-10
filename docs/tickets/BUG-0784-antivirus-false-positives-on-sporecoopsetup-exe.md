---
id: BUG-0784
title: Antivirus false positives on SporeCoopSetup.exe, steam_api.sporekit.dll and the zip (VirusTotal 9-11 of 71)
status: open
kind: tooling
stage: none
reported: 2026-11-10
source: GitHub #200, GitHub #201
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/INSTALL.md
related: 
---

## Report
GitHub #200 and #201 (0.7.10.1): VirusTotal 10/71 on SporeCoopSetup.exe (Microsoft Defender Wacatac.B!ml, Bitdefender
and others), 11/71 on steam_api.sporekit.dll, 9/67 on the release zip; one antivirus quarantines the installer.
Players ask whether these are false positives and how to verify the release.

## Root cause
Not a code bug. What matches the heuristics (observed in the code):
* No VERSIONINFO resource in any binary: `installer/setup.rc` has the icon and the manifest only, the DLLs no
  resources; the manifest's assemblyIdentity is version 1.0.0.0.
* Unsigned binaries.
* The setup elevates itself (`installer/setup.cpp:366`, ShellExecuteW "runas"), downloads a custom `.pack` from GitHub
  over WinHTTP and writes DLLs into the game folder; a DLL proxy named `steam_api.dll` with the original renamed
  (classic DLL side-loading); files replaced with MoveFileEx (installer/install.cpp, updater/winupdate.cpp).
* coop.dll has sockets, WinHTTP and code hooks together.
* Good already: no packer (no UPX), release binaries stripped (tools/package.sh), SHA256SUMS.txt published by the
  workflow, built by GitHub Actions from the tag. No SECURITY.md; INSTALL.md says two lines about antivirus.

## Fix idea
1. VERSIONINFO (CompanyName, ProductName, FileDescription, OriginalFilename, version from COOP_VERSION) for the setup
   and the three DLLs (windres in the Makefile); the manifest's version from COOP_VERSION.
2. GitHub artifact attestation in .github/workflows/build.yml (`actions/attest-build-provenance`), so anyone can run
   `gh attestation verify` on a file.
3. Submit each release's files as false positives to Microsoft (WDSI) and the vendors that flag them.
4. INSTALL.md section "Antivirus warnings and verifying a release" and a SECURITY.md: what each file does, where its
   source is, SHA256SUMS, attestation, the manual install from the zip with install.bat instead of the setup.
5. Later: code signing (for example SignPath for open source), elevation only when the folder needs it.
Answer to the players: false positives of machine-learning heuristics on an unsigned loader that downloads files
and replaces steam_api.dll; source is public, binaries built by GitHub Actions from the tag, hashes in
SHA256SUMS.txt; reported to the vendors; the zip install avoids the setup.

## How to test
`make` then a check in tools/release_check.sh: every release binary has a VERSIONINFO with the release version
(llvm-readobj --coff-resources), and the workflow run lists an attestation. Main: no version resource: FAIL.

## History
- 2026-11-10: opened.
