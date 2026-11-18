---
id: BUG-9106
title: Lineages Cache: a creation whose import failed once never comes back
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: two-PC QA (MSI guest)
role: all
versions: 0.7.3.6-lineages41-dev
fixed_in: 0.7.3.6-lineages42-dev
branch: feat/lineages-42
commits: 63abe7bf
tests: QA (MSI received cache), see Verification
signature: "Received PNG has no readable creation."
docs: docs/re/creations.md#creations-cache-pass-on
related: BUG-9093 BUG-9094
---

## Report

On the MSI (QA guest) `coop.library` showed failed=1, error "Received PNG has no readable
creation.", for 9d9d7a18... (TITAN's creation 291D1C4E), with an all-zero key. The same PNG
inspects fine on TITAN (`coop.library inspect`: PNG 40626200 Lilku) and another instance
imported it without trouble, so the PNG is valid; the decode failed once on the MSI (it arrived
while the MSI was in a stage, likely during a load).

## Root cause

A failed import is kept with `failed=1` in index.bin and never queued again (load_index queues
only entries that did not fail), and a partner's later offer finds the file already present
(`have`), so it is not sent again either. One transient failure loses the creation for good.

## Fix

A decode that fails is tried again 60 s later, three times per game start, and only then marked
failed (logged "could not be decoded yet" / "import rejected"); load_index clears the failed mark
once per start so a creation that failed in an earlier start gets its tries again. `coop.fix
library.retry` off restores the old behaviour; `coop.library` counts `retried`.

First version (retry once at load) failed again on the MSI: the retry ran right after the game
started, when the decoder fails; the same file inspected fine from the console a minute later.

## Verification

FAIL without the fix: on the MSI build before it, creation 9d9d7a18 stayed failed=1 and was never tried again (Report). PASS with it: below. Manual QA, no lab run id.

MSI QA (stripped build 757CFA57): the creation stuck since the first two-PC test, 9d9d7a18,
"could not be decoded yet" 2 s before GalaxyGameEntry, imported 59 s later on the delayed try
(key 291D2947); failed 1 -> 0, imported 35 -> 36, retried=2, 0 faults. The first version (retry
only at load) had failed again on the same file.

## History
- 2026-11-10: found in the two-PC cache test; fix built on feat/lineages-42.
- 2026-11-10: released in 0.7.3.6-lineages42-dev.
