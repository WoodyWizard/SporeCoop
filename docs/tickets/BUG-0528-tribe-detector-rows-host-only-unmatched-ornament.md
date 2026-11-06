---
id: BUG-0528
title: Tribe: detector rows host_only / unmatched ornament (noun 0x018C88E4) in play_tribe: one ornament on the host the guest has not
status: not-reproduced
kind: desync
stage: Tribe
reported: 2026-11-04
source: fix/w41-tribe-orders, detector rows in play_tribe (lab 20261104-010027, 20261104-000550, w40-play 20261103-160342)
role: 
versions: 
fixed_in: 
branch: fix/w45-world-objects
commits: 
tests: play_tribe (SPORE_DETECTOR=20)
signature:
docs: docs/re/tribe.md#tribe-ornament-not-reproduced
related:
---

## Report
The divergence detector (SPORE_DETECTOR=20) in play_tribe prints, in 4 runs of 2026-11-03/04 (out/lab/20261103-160342
play_tribe#2 of fix/w40-play, out/lab/20261104-000550, 20261104-010027 #1 and #2 of fix/w41-tribe-actions):
`Tribe:unmatched:ornament` "ornament: 3 on the host, 2 on the guest, 1 host / 0 guest without a partner within 3 m"
(from ~160 s, during the hunt phase) and `Tribe:host_only:ornament` "1 ornament near the guest on the host, none on the
guest (e.g. at (326 -44 376))" (~340 s, gather phase). `ornament` is noun 0x018C88E4 in worlddiff.py (not the
interactive ornaments 0x03A2511E of BUG-0459 / BUG-0473, which the detector maps to BUG-0488 in Tribe).

## Root cause
Not found, not reproduced since 2026-11-04. Observed (2026-11-06): the Tribe fixture has 5 ornaments of the noun,
the same models and places in both games (0x356EB8A x3, 0x5B9A06E, 0x5D97F762 the tribe decoration of 0xC90080), and
they stayed the same for 80 s after the guest's members were ordered to hunt (`coop.tribe act_npc 10`). No Tribe
`ornament` row in play_tribe with the detector: lab 20261106-035845 (0.7.9 base) and 20261106-051335 (this branch), nor
in L1's 0.7.9 runs. The makers of the noun (CreateInstance(0x18C88E4)): 0xC90080 (tribe decoration), 0xCCEB90 path,
0xD130D0, 0xD2FFD0, 0xD99F50, 0xC60440, 0xB41FE0, 0xBE6A50 (city decorations, BUG-0541), 0xEC0F00, 0xF30B60, 0xF41A50,
0x1051860, 0xB25FC0: which made the host's extra one in the 2026-11-03/04 runs is not known.

## Fix
None. If the row comes back: `coop.wobj raw 18C88E4 3000 576` on both games prints each ornament's model (+0x228 /
+0x22C), which names its maker; a model-0x2AE5BA7 one is BUG-0541's (fixed there for Space).

## Verification
no-fail-run: not reproducible (play_tribe with SPORE_DETECTOR=20, lab 20261106-035845 and 20261106-051335: no
Tribe ornament row; the hunt test of 2026-11-06 on lane 7 changed no ornament).

## History
- 2026-11-04: opened.
- 2026-11-06 (fix/w45-world-objects): not reproduced in two detector runs of play_tribe and a hunt test; closed with the evidence (reopen with the model ids).
