---
id: BUG-9034
title: NPC village food stores do not reflect native gifts on the guest
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: Native gift delivery qualification
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_gift.py
signature: delivered gift recipient food240 on host versus230 on guest
docs: docs/re/tribe.md
related: BUG-9033
---

m2-gift-final-c.log: native delivery pays ten food and credits the NPC village,
but only the two founders' food stores are included in the citizen snapshot.
The guest shows the original NPC food total. Reconcile other live villages
through bounded host snapshots without enlarging every citizen packet.

The host sends at most32 stores once per second. The guest validates world,
sequence, faction, species and position, then uses the existing native food
mirror helper. Founder stores retain their more frequent citizen-cycle updates;
empty retired villages cannot invalidate other stores. Switch: tribe.npc_food.

PASS m2-gift-native-f.log and m2-gift-ui-g.log: turning this fix off reproduces
the stale NPC total after delivery. Turning it on restores matching recipient
and payer stores on both peers. Both games finish with zero native faults.
