---
id: BUG-0385
title: Creature: the host sends a stray puppet of a saved world as an NPC for a moment; the guest cannot spawn its species (sync-check n missing)
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01 (host log 13:35:01)
role: guest
versions: 0.7.3.3
fixed_in:
branch: fix/w30-npc-walk
commits:
tests:
signature:
docs:
related: BUG-0010, BUG-0020, BUG-0339
---

## Report
Host log of a 0.7.3.3 session: `sync-check: MISMATCH ... n: 1 missing on the guest (n142)` once, right after
the host came back from the editor (OK again 30 s later).

## Root cause
Observed in the logs (not reproduced): after the host's editor the host's world held a creature of species
7CA388E5, the host's own key of the guest's creature (the partner's puppet saved with the world). The host
gave it NPC id 142 and sent it; the guest did not know that species ("npc 142: proxy spawn failed (unknown
species) key=7CA388E5") and asked the host, but the host had removed it meanwhile as a stray ("removed a
creature of species 7CA388E5 without tuning in a herd like ours (from a save made while a partner's puppet
existed)") and answered "player 1 asks for species 7CA388E5: not found here". Inferred: the host's NPC
tracking registers such strays for the frames before `remove_saved_strays` runs (once a second).

## Fix
None (harmless: the stray is gone within a second; the mismatch is one sync-check). A fix would skip creatures
of our own kind of herd (`own_kind_herd`) in host_tick.

## Verification
no-fail-run: not fixed.

## History
- 2026-11-01: opened.
