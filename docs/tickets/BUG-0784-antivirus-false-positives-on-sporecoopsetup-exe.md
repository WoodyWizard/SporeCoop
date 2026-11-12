---
id: BUG-0784
title: Antivirus false positives on SporeCoopSetup.exe, steam_api.sporekit.dll and the zip (VirusTotal 9-11 of 71)
status: fixed
kind: tooling
stage: none
reported: 2026-11-10
source: GitHub #200, GitHub #201
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-trust
commits: 2d2deb8d, 204a70f6, a2002fc2
tests: tools/pe_version.py --expect (tools/package.sh), make test
signature:
docs: docs/INSTALL.md#install-antivirus, SECURITY.md, docs/release-trust.md
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

## Fix
What the build does now:
1. VERSIONINFO in the setup and the three DLLs (installer/version.rc included by installer/setup.rc, loader/loader.rc,
   sporekit/sporekit.rc, mods/coop/coop.rc; build/version.h from COOP_VERSION, rewritten only when it changes);
   the setup manifest's version is COOP_VERSION (build/setup.manifest) instead of 1.0.0.0. tools/package.sh runs
   `tools/pe_version.py --expect <version>` on the stripped release binaries and fails without it.
2. The release job attests the zip, the pack, the setup and the three DLLs inside the zip
   (actions/attest-build-provenance@v2, `id-token: write`, `attestations: write`), after the publish and with
   continue-on-error, so the existing release path (release_check.sh, SHA256SUMS.txt, gh release create) is
   unchanged and never blocked.
3. `tools/pe_version.py --repro-sha256`: builds of one commit in two folders differ only in two time stamps and the
   CodeView GUID; with those zeroed all four binaries hash the same.
4. INSTALL.md "Antivirus warnings and verifying a release", SECURITY.md (what each file does), a README paragraph.
5. For the owner (in his name, not done here): docs/release-trust.md: enable private vulnerability reporting,
   check the attestation per release, false-positive submissions (Microsoft WDSI, Bitdefender, 360, Kaspersky,
   Avast, ESET, the VirusTotal contact list) with a ready text, the answers to #200 / #201, code signing later
   (SignPath Foundation, Azure Artifact Signing).
Switch: none (build metadata and docs; the FAIL run is the base build).

## Verification
- FAIL on the base code: `tools/pe_version.py --expect 0.7.12` on the binaries built from rel/0.7.13 572c8e68:
  "no VERSIONINFO resource" for all four, exit 1 (out/w48-trust/pe_version_base.txt).
- PASS with the fix: the same on this branch's build and inside `tools/package.sh` (all four 0.7.12.0 with
  description, original name, product; out/w48-trust/pe_version_fix.txt); cross-checked with llvm-readobj
  --coff-resources (a VERSIONINFO entry, ID 16).
- Release job: not run (no push); its steps were run locally on out/dist (release_check.sh v0.7.12, sha256sum,
  the unzip into inzip/, every subject-path matched one file) and the YAML parsed. The attestation itself is
  checked by the owner after the first release (docs/release-trust.md).
- no-fail-run: the vendor submissions and the answers are the owner's steps; the detections themselves cannot be
  tested here.

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-trust): the build parts done, the owner's parts prepared in docs/release-trust.md.
