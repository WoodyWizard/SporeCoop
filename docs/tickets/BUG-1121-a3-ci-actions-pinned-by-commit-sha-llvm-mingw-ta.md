---
id: BUG-1121
title: A3: CI actions pinned by commit SHA, llvm-mingw tarball checked by SHA-256
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-build
commits: acf0ef0e, 2eb9710f
tests: tools/ci_lint.py (make test)
signature:
docs: docs/10-dev-pipeline.md#pipeline-ci-pins
related: BUG-0784, BUG-1120
---

## Report
Plan 0.7.15 A3 (audit KIT-12): CI actions were pinned by moving tags (`@v4`, `@v2`), the llvm-mingw tarball
was downloaded with no checksum, Pillow unpinned, the toolchain version written twice (Makefile, workflow).

## Root cause
Supply chain: a moved or hijacked tag or a replaced release asset would change what CI builds the release with.

## Fix
tools/toolchain.env: LLVM_MINGW_VERSION, LLVM_MINGW_SHA256 (5f9c6ed9..., GitHub's asset digest, equal to a
fresh download; the local toolchain has the same 7421 files byte for byte), CI_PYTHON, PILLOW_VERSION. The
Makefile includes it (`make toolchain-path`), package.sh and update_check.py ask the Makefile, the workflow copies
it into GITHUB_ENV, fails unless python3 is CI_PYTHON, checks the tarball with `sha256sum -c`, installs
`pillow==12.3.0`. Actions pinned by commit SHA with the tag in a comment: checkout 11d5960a (v4.4.0),
upload-artifact ea165f8d (v4.6.2), download-artifact d3f86a10 (v4.3.0), attest-build-provenance e8998f94
(v2.4.0), each the current head of the major that ran green. tools/ci_lint.py in make test keeps it so.
No coop.fix switch (tooling).

## Verification
- PASS: ci_lint on the new workflow, 0 problems; the workflow's version step run locally under 3.10 (passes)
  and 3.14 (fails: "python3 is 3.14"); `sha256sum -c` of the downloaded tarball OK.
- FAIL as wanted: ci_lint on the old workflow (rel/0.7.15): 9 problems (5 unpinned actions, no tarball check,
  version in the workflow, toolchain.env not read, Pillow unpinned); `sha256sum -c` with a changed hash fails.
- no-fail-run: CI itself not run (no push from agents); the next push of main runs it.

## History
- 2026-11-15: opened.
