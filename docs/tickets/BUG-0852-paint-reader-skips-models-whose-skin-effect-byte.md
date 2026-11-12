---
id: BUG-0852
title: Paint reader skips models whose skin-effect bytes are run-compressed, so their paints are never checked or substituted
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9041
role: all
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: unreleased
branch: fix/w48-small
commits: fe67469a
tests: make test (tests/validate_tests.cpp test_model_paint_runs)
signature: 
docs: docs/re/creations.md#creations-paint-runs
related: BUG-0559, BUG-0469, BUG-0727
switch: creations.paint_runs
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
Root cause confirmed in the code (the reader returned false for the whole model). `mods/coop/model_paint.h` (portable): `effects_read` decodes the byte runs over the 12 effect bytes and says per layer whether its four bytes are written byte by byte (`patch[k]`); `effects_strict` is the old reader. creations.cpp `model_effects` uses the decoding one (`coop.fix creations.paint_runs off`: the strict one, 0.7.12), so the content check (`unknown_paint_ids`), the substitution, the species data alignment and `coop.creations paints` see the paints of a run-packed model. `substitute_paints` rewrites only a layer that can be patched in place; an unknown paint inside a run is logged ("run-packed: not substituted in place", counter `in_run=` of `coop.creations paints`) and the content check then refuses the creation (the game's fallback shows), as for any paint without a substitute.

## Verification
Unit test `test_model_paint_runs` in tests/validate_tests.cpp (BUG-0727's untried case: the fork's shape, a model with a literal Creepy & Cute base paint and `00 FE 07` over coat and detail; a base id whose last byte runs into the coat; a model written byte by byte; a short header):
* FAIL with the fix off (`validate_tests --nofix`: the strict reader, as before the fix): 7 checks of test_model_paint_runs fail (validate_tests.cpp:903-916, "ok", the ids, the patch flags), the run's output in the branch's scratch (35 FAILED in total, the other 28 are the older --nofix cases).
* PASS twice with the fix (`validate_tests`: "validate tests OK", 2026-11-12), and in `make test".
Not run in the lab: the lab profiles have no creation with such a model (the fork's 1934-byte landfall creature is not ours); the reader is the same code the game path calls.

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0727 (not reproduced: a model whose records cannot be read built unchecked) is the parent case; this ticket is the same outcome for readable records. Linked.
- 2026-11-12 (fix/w48-small): fixed (creations.paint_runs), unit test.
