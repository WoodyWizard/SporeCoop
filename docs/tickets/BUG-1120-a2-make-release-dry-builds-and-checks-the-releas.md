---
id: BUG-1120
title: A2: make release-dry builds and checks the release from a git archive copy with CI's Python 3.10 and toolchain
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-build
commits: acf0ef0e, 2e1d9aec
tests: make release-dry (tools/release_dry.sh), make test
signature:
docs: docs/10-dev-pipeline.md#pipeline-release-dry, CLAUDE.md section 4
related: BUG-0097, BUG-0165, BUG-1121, BUG-1123
---

## Report
Plan 0.7.15 A2 (audit KIT-1): 3 of the last 9 tags failed only in CI (v0.7.3.4 libstdc++ 12 headers, v0.7.11
Python 3.12 f-strings on the runner's 3.10, v0.7.14 a private ticket id in the notes, invisible without
out/tickets); main and the tag were pushed together, and v0.7.14 was moved after its failure.

## Root cause
Environment drift between the developer's tree (Python 3.14, out/tickets, Fedora headers, full history) and CI's
shallow checkout on ubuntu-22.04; nothing ran the tag workflow locally from a clean copy.

## Fix
`make release-dry` (tools/release_dry.sh [--rev <commit>] [--keep]): a `git archive` copy of the commit in an
empty folder, made a depth-1 shallow repository of the same commit (no history, no out/); CI's Python
(`CI_PYTHON` of tools/toolchain.env via `uv run --no-project --python 3.10`) and Pillow; the pinned llvm-mingw;
`env -i` with CI=true; then the workflow's steps: Python check, release_check.sh v<COOP_VERSION> (notes-check),
make, make test, package.sh, the release job's asset / SHA256SUMS / zip checks. Release order documented: release
commit, release-dry PASS, push main, wait for main's CI (`gh run watch`), tag only on a green main, never move a
published tag. Not covered: CI's host compiler (clang 14 / libstdc++ 12) for make test, the upload, the
attestation. No coop.fix switch (tooling).

## Verification
- PASS: release-dry of 6873373 (A2 head) in 95 s; of the branch's final tree in the agent report.
- FAIL as wanted: a commit with a 3.12 f-string in tools/tests/coop_ability_bar.py (Python 3.14 parses it):
  make test fails in test_lint ("does not parse"); a commit naming private BUG-1003 under Fixed: release_check
  fails ("no ticket docs/tickets/BUG-1003-*.md"), as the v0.7.14 tag build did.
- no-fail-run: tooling, not a lab test; the FAIL side is the two probe commits above (not on any branch).

## History
- 2026-11-15: opened.
