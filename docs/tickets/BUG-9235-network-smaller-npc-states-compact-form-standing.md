---
id: BUG-9235
title: Network: smaller NPC states (compact form, standing NPCs once a second, far ones at 5 Hz)
status: fixed
kind: feature
stage: all
reported: 2026-11-15
source: owner 2026-11-15 (4P A, step 2)
role: all
versions: 0.7.14.1-lineages60-dev
fixed_in: 0.7.14.1-lineages61-dev
branch: feat/lineages-47
commits: 809bb049
tests: manual Windows QA: the MSI two games, the net traffic line (A/B by the switch)
signature:
docs:
related:
---

## Report
Owner 2026-11-15 (4P A): improve connection speed. Measured (net traffic counters, 4P step 1): NPC states were about 60 % of a guest's traffic (780 KB of 1240 KB a minute in an A/B on the MSI, two games, Vandh.spo).

## Root cause
Observed in the code: MSG_NPC_STATE sent every relevant creature at 10 Hz as 47 raw bytes (f32 position, quaternion, velocity, health), standing ones included; Civ alone skipped standing ones (BUG-0973).

## Fix
- MSG_NPC_STATE_C (93): the batch in a compact form, 23 bytes an entry: positions as i16 offsets from the guest's place in 1/64 m (the old form beyond +-500 m), the rotation as a 32-bit smallest-three quaternion, velocity in cm/s, health f32, flags. Switch npc.compact.
- A standing creature's state goes once a second in every stage (the guest drops a proxy after 5 s). Switch npc.quiet_all.
- A creature over 60 m from the guest goes at 5 Hz. Switch npc.far_half.

## Verification
FAIL (the three switches off, the MSI, two games, one minute): MSG_NPC_STATE 780 KB, 1054 messages; total out 1238 KB.
PASS (on, the minutes before): MSG_NPC_STATE_C 77-82 KB, about 500 messages; total out about 500 KB. Guest proxies followed (err 0.0-0.7 m).
PASS (codec, a standalone build of tests/net_tests.cpp test_npc_compact): positions within 1 cm, speeds within 0.6 cm/s, rotations dot > 0.99999.
no-fail-run: Windows QA by hand; the lab test in net_tests.cpp is Linux only.

## History
- 2026-11-15: opened.
- 2026-11-15: done (4P A).
- 2026-11-16: released in 0.7.14.1-lineages61-dev.
