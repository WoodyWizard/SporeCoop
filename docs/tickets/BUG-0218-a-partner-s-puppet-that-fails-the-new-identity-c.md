---
id: BUG-0218
title: a partner's puppet that fails the new identity check is left in the game as an armed NPC ship
status: open
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #152 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #152: https://github.com/WoodyWizard/SporeCoop/issues/152 (the full analysis, links and the scenario are there).

The BUG-0111 fix (eb7541e, 3984987, release 0.7.3.0) decides "is this still our puppet?" by the address, the model key, the invincible byte and the owner mission. When a real puppet fails only the flag part of that check, the mod forgets it but does not destroy it: the ship stays in the world with our political id, its own AI and its weapons, and the host sends it to the guests as one of its NPC ships. Severity LOW: nothing in the mod changes these flags on a real puppet (only the test command `coop.fleet impostor` does), so it needs the game to change one of them.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
