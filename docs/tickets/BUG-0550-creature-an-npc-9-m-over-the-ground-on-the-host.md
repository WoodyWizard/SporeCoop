---
id: BUG-0550
title: Creature: an NPC 9 m over the ground on the host is drawn on the ground on the guest (sync-check n apart 9.3, want_agl 10.56 made_agl 0)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-04
source: final check out/lab/20261104-135838 play_creature
role: guest
versions: 
fixed_in: 0.7.8
branch: fix/w44-height
commits: 4b54c7a7, df579966
tests: npc_air
signature:
docs: docs/re/creature.md#creature-height-rule
related: BUG-0532, BUG-0425
---

## Report
play_creature in the 0.7.6 final check (out/lab/20261104-135838): `sync-check: MISMATCH ... n: 1 apart (n34 9.3)` on both games while the host's creature was dead (cinematic). Host: n34 species 065CB956, agl 9.31, moving 6.9 m/s. Guest: the NPC puppet `want_agl=10.56 made_agl=0.00 grounded=0`, drawn on the ground. Seen once (1 of 2 play_creature runs on rel/0.7.6). Reproduce: `tools/lab.py run play_creature --repeat 3`, look for `n: 1 apart` with an agl over 5 m on the host.

## Root cause
Not analysed yet. Inferred: an NPC in a jump or a flight (agl 9 m) on the host; the guest's NPC puppet keeps the ground height. Compare BUG-0532 (the player's puppet in the air).

Update (2026-11-05, rel/0.7.7 final check, out/lab/20261105-034103 long_creature): once more in the 40-min session at
s 1312: n216 (species 065CB956, spd 2.0, hp 31) 13.9 m apart, the host's NPC at agl 14.01 m. Also the same radial
kind in Tribe (BUG-0402's update of this date: a host NPC 8.4 m under the ground). Left open.

Fix analysis (2026-11-05, fix/w44-height):
* Observed: the host sent no state of an NPC's being off its ground; the guest walked the proxy (walk_place) or drove
  it (drive::step) and its game, with the proxy grounded (+0x137 = 1), put it back onto its terrain. A host NPC held
  4 m over its ground (`coop.npc lift`) was drawn at a median 1.05 m on the guest, only 50 % of its frames over 1 m
  (lab 20261105-112120, npc.air off).
* Inferred (not reproduced in a play test): n34 / n216 of the reports (species 065CB956, agl 9-14 m, 2-7 m/s) were
  fliers or jumpers; the same path drew them on the ground.

## Fix
npc.air (`coop.fix npc.air off`: 0.7.7) and drive.sender_agl (BUG-0425): the host flags NF_AIR (the game's grounded
byte +0x137 = 0 and more than 0.15 m off GetHeightAt) and sends each NPC's height over its terrain in a trailer of the
state batch; the guest places such a proxy at that height over its own terrain, not grounded, held through the game's
animation starts (drive::air_place, shared with the partner's puppet, BUG-0376 / BUG-0532). On its ground it is
driven as before.

## Verification
npc_air (coop_npc_air.py, Creature): FAIL with SPORE_FIX_OFF=npc.air out/lab/20261105-112120 (guest median 1.05 m,
host 4.00 m); PASS out/lab/20261105-112004 (guest 4.00 m in 100 % of the air frames, on the ground before and
after).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-tests): G1: not reached (no run, no change).
- 2026-11-05 (fix/w44-height): fixed (npc.air, drive.sender_agl); npc_air FAIL/PASS.
- 2026-11-05: released in 0.7.8.
