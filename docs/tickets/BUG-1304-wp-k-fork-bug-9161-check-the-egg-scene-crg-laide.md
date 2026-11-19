---
id: BUG-1304
title: WP-K: fork BUG-9161 check: the egg scene (CRG_LaidEgg) after a mating never ends by itself in co-op
status: open
kind: bug
stage: Creature
reported: 2026-11-19
source: plan
role: any
versions: 0.7.15.2 (checked)
fixed_in:
branch: feat/w56-cocoon
commits:
tests: creature_mating, editor_now, guest_mating
signature:
docs: docs/re/creature.md#creature-lineages-cocoon-mate
related: BUG-0177, BUG-1263, BUG-1303, BUG-1305
---

## Report
The Lineages fork (PR #196, its BUG-9161): after a mating the egg scene (cinematic CRG_LaidEgg B6A1DBE1) never ended
by itself in its co-op runs; the game ends it only from its key handler (0xE45760 -> 0xE37920, flag +0x371, a 500 ms
timer, 0xE45E0F posts the evolution request). The fork presses Escape for the player after 12 s. Checked whether ours
can hang there.

## Root cause
Host: not in our code. rel/0.7.15.2 lab run 20261118-144132: creature_mating (the host's click on the mate, then only
polling, no key) shows busy=cinematic on the host from ~50 s and "editor request in Creature: editor 312E9D6A tag
012191CA" at 74.7 s, 27 s after the click, the editor open; editor_now the same (request at 76.5 s). The fork's own
cause line (#creature-lineages-cocoon-mate) is its own avatar wearing a player species while the nest family kept the
world's older one, which our host does not do (the host plays the world's species). The guest's egg scene is checked
by `guest_mating` (with BUG-1305).

## Fix
None needed on the host; no switch (nothing changed).

## Verification
no-fail-run: not reproduced on the host (runs above); the guest's case waits for guest_mating (lab pending).

## History
- 2026-11-19: opened; host checked from the 0.7.15.2 runs.
