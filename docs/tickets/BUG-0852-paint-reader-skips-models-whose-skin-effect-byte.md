---
id: BUG-0852
title: Paint reader skips models whose skin-effect bytes are run-compressed, so their paints are never checked or substituted
status: open
kind: bug
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9041
role: all
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: 
docs:
related: BUG-0559, BUG-0469, BUG-0727
---

## Report
A creature model whose paint ids are run-length compressed in its header (for example a landfall creature that kept
its Cell paint with zero coat and detail layers: FE 07 over the zero bytes) is skipped by the mod's paint check and
substitution. Inferred: a Creepy & Cute paint in such a model is not replaced on a guest without that pack (other
colour), and the content check does not see it.

## Root cause
* Observed (code): creations.cpp:985 model_effects returns false when an FE n run touches the 12 effect bytes
  ("an id is not written byte by byte"); its callers (creations.cpp:1100, 1145, 3407) then `continue`, so the model's
  three paints are not looked at.
* Observed (fork): a valid native 1934-byte model with FE 07 across zero coat/detail layers (base 0x2F363540).
* Inferred: also any id whose bytes repeat across a layer boundary. Confidence high for the code fact, low impact.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9041); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* For reading (validation of an archived model) the fork uses a bounded read-only byte-run decoder (the one the
  code already has for run expansion): it decodes the first 38 header bytes, checks the BEM magic, then reads the
  three effect ids from the decoded bytes. In-place substitution keeps the strict model_effects (it needs byte
  positions to patch).
* For us: split "read the paints" (decode runs, always works) from "where to patch" (strict), so the content check
  and the unknown-paint decision see compressed models; a model whose unknown paint is inside a run needs a rewrite
  of the record rather than an in-place patch (or a substitution refusal with a log line).

## How to test
Host unit test (validate_tests or fuzz_decoders): a BEM header with FE 07 over zero coat/detail and a C&C base paint:
the reader returns the three ids and the unknown-paint path flags it. FAIL on main (model skipped), PASS with the fix.
Lab: a creature with a C&C base paint and no coat/detail, guest without C&C, compare the colour (model hash) on the
guest.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0727 (not reproduced: a model whose records cannot be read built unchecked) is the parent case; this ticket is the same outcome for readable records. Linked.
