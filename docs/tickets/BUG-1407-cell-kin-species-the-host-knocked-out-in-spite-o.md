---
id: BUG-1407
title: cell_kin_species: the host knocked out in spite of invulnfor as it is sent to the guest's place; the kin are judged where it lies, 185 m away
status: open
kind: test
stage: Cell
reported: 2026-11-21
source: lab rel/0.7.16 20261121-003202 cell_kin_species
role: 
versions: unreleased
fixed_in:
branch: fix/w62-flake-cc
commits:
tests: cell_kin_species
signature:
docs:
related: BUG-1334, BUG-1257
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 6): cell_kin_species FAIL "both players at the guest's place: the
guest's kin are still there ({'4DA56CA5': 1})"; every other check passed (apart, AI, the host's place, after the editor,
the save). Known flake (2 of 4 on rel/main); the earlier FAIL runs had other steps (fix/w57-cellkin, BUG-1334).

## Root cause
Observed: host log: hp 6 at s 110, hp 3 at s 120, `ko: own cell state 1` at s 121.93 ("Alice is knocked out"), `Alice
respawned small next to Bob` at s 131.94; the test had set `coop.pool invulnfor 900` (clamped to 120 s) on both at the
start of the apart step (~s 48), so the flag was meant to hold. The key frame at_guest_place (frames/cell_kin_species
manifest, 04:16:56, ~6 s after the test's `coop.pool tp` of the host to the guest) has the host at (-50.8 -6.4) and
the guest at (133.8 1.6): the host was never at the guest's place; the tp at ~s 122 came during the knockout and did
not hold (the knocked-out cell is held where it went down). The kin counted "at the guest's place" were the host's
around its own cell. Same class as BUG-1334 (a knockout in spite of invulnfor at level 10; 9 of 15 runs of this test had
one), which guarded only the apart sampling. Why the flag does not hold is still open (inferred: a damage path without
the +0x111 check: Bite's lethal branch BreakOrSwallow or the bigger-victim push 0xE76AF0, docs/re/cell.md
#cell-guest-hit-game; or a rebuilt avatar object without the flag). Not new in 0.7.16, not from a w61 change.

## Fix
Test (the kin check is unchanged): before the host goes to the guest's place it waits until it is up (`coop.pool`
downed=0 dying=0, up to 20 s), invulnfor again on both, the tp, then the host must be within r of the guest; if not, it
prints the knockout diagnostics (hp, invuln, downed, bites_taken, damage_taken, ko_respawns, last_hit) and tries once
more. A new check states that the host is at the guest's place before the kin there are judged.

## Verification
no-fail-run: test-only change (a knockout at that moment cannot be forced). Lab: `SPORE_DETECTOR=20 python3
tools/lab.py run cell_kin_species --lane N --retry 0 --no-build` PASS; twin `SPORE_FIX_OFF=cell.kin_species` FAIL as
before. To find why invulnfor does not hold: the same run's log prints the diagnostics line "the host is not at the
guest's place ... last_hit=..." whenever it happens. Open, waiting for the lab run.

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); fixed in the test.
