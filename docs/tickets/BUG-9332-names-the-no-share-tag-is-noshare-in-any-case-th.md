---
id: BUG-9332
title: Names: the no-share tag is -noshare in any case, the trailing dash optional
status: fixed
kind: feature
stage: all
reported: 2026-11-21
source: owner 2026-11-21
role: all
versions: 0.7.14.1-lineages68-dev
fixed_in: unreleased
branch: fix/l47-noshare-tag
commits: d3ea538b
tests: tests/text_tests.cpp
signature:
docs:
related: BUG-9222, BUG-9331
---

## Report
The owner (2026-11-21): "Can we change the -NoShare- to just be -noshare with no capitalization requirement?"

## Root cause
The tag was already matched in any case (BUG-9222); it needed the dash after it ("-noshare-").

## Fix
net/utf8.h `no_share_tag`: "-noshare" in any case; a dash right after it is part of the tag, so "-NoShare-" still counts and is hidden whole. A long name keeps "-noshare" at its end; the automatic tag of a test copy (BUG-9331) is "-noshare". Mixed builds cannot join (the host needs the same Lineages build), so no game reads the tag the old way in a session. No switch: a text rule, covered by the unit test.

## Verification
- tests/text_tests.cpp (built with llvm-mingw i686, run on TITAN): PASS ("ALL OK") with the change; FAIL on the old
  net/utf8.h (5 checks: "MSI-noshare" not a tag, not hidden, the long-name cut kept "-NoShare-").
- no-fail-run: a unit test, not a lab run (the FAIL / PASS pair is above).

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-noshare-tag, unit test FAIL old / PASS new.
