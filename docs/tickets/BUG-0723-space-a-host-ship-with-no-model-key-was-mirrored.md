---
id: BUG-0723
title: Space: a host ship with no model key was mirrored with SpawnUFO's default model (a model this game may lack)
status: fixed
kind: bug
stage: Space
reported: 2026-11-08
source: BUG-0424 sibling list, K2b audit 2026-11-08
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-b
commits: 9e2dadf1, 8c11d8d4
tests: space_zero_key, space_zero_key_slow (coop_space_zero_key.py), space_mirror_model
signature:
docs: docs/re/space.md#space-k2b-mirror-models
related: BUG-0424, BUG-0323, BUG-0546, BUG-0686
---

## Report
K2b audit of the class BUG-0424 (no player report): squadron.cpp treated a host ship whose model key (cGameDataUFO +0x780) is all zeros as a known model (`have_key = !(key[0]|key[1]|key[2]) || key_known`) and spawned its mirror with `SpawnUFO(type, &pol, {0})`; its own comment said that such a spawn had faulted (0xE98DD0).

## Root cause
Observed (lab 20261108-031320, `SPORE_FIX_OFF=fleet.zero_key`, `coop.fleet fakemodel zero` on the host, three NPC ships spawned): the guest mirrored both pirates with a key of zeros; SpawnUFO then takes its default key and the ship gfx the default UFO 9D5DB21A/476A98C7/40656200, which the GOG 2024 install does not have: the BUG-0546 guard (`game.ufo_model`) logged `ship model 9D5DB21A ... is not in this game: the last known ship model 0675F7B2` twice and drew both pirates as the guest's own ship (without the guard: the fault at 0xE98DD0 of BUG-0546).

## Fix
A key of zeros is a model this game does not have (`fleet.zero_key`): the mirror takes the model of a live ship of that empire this game made itself (BUG-0323's local_model), else the model the game itself gives a raid without an empire (cPlayer +0x1280, cGameNounManager +0x74, the key 0xC5C8F3 copies: 067256EA in the lab), checked with key_known, else no mirror. The same last fallback for a key this game does not have (BUG-0323's case: before, a pirate with an unknown model got no mirror at all, and the lab's sync-check found the host's ship missing on the guest, lab 20261108-043549 / 044742). Counters `zero_keys=` in `coop.fleet list`. `coop.fix fleet.zero_key off`: SpawnUFO with the zeros (0.7.10).

## Verification
FAIL: out/lab/20261108-031320 (`SPORE_FIX_OFF=fleet.zero_key`: the guest's guard counter `ship models defaulted` 0 -> 2, both mirrors with key 00000000). PASS: out/lab/20261108-031552 (space_zero_key and space_zero_key_slow at 20 fps over the player network: both mirrors with the game's raid model 067256EA, no guard counter moved, no fault), out/lab/20261108-044523 (space_zero_key and space_mirror_model after the unknown-key fallback). The unknown key: FAIL out/lab/20261108-044742 (space_mirror_model, `SPORE_FIX_OFF=fleet.zero_key`: sync-check "s: 1 missing on the guest"), PASS out/lab/20261108-044523. Detector (SPORE_DETECTOR=20): no row of the class in these runs.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): fixed, `fleet.zero_key`; FAIL and PASS runs above.
