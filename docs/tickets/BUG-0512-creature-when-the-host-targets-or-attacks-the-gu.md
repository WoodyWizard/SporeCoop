---
id: BUG-0512
title: Creature: when the host targets or attacks the guest's creature (no damage), the host's pack attacks the guest for real and kills it
status: open
kind: bug
stage: Creature
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature:
docs:
related: BUG-0374, BUG-0168, BUG-0420
---

## Report
Host's mark (0.7.3.6, report 20261103-193309): 'I'm host, my pack mate attacked the guest when I accidentally targeted him'. A direct message (0.7.3.6): the host can attack the guest without dealing damage, but the pack targets the guest and actually kills it.
Target: 0.7.4 (Creature).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed. BUG-0374 (fixed in 0.7.3.4) stopped the pack's retaliation when the guest hits a pack member; this is the other direction: the host's own attack order on the guest's puppet is copied by the pack AI (the game's 'help the leader' rule), and the puppet's hits on the guest are forwarded as real damage. Inferred fix: the pack AI must not take a partner's puppet as a target (the host's target is a partner: drop it for the pack, as BUG-0374 did for retaliation), and the host's target selection on a partner should not start an attack. Size: S-M.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage); the 0.7.3.4 fix BUG-0374 covers another path.
