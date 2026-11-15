---
id: BUG-0717
title: Creature: a guest's hit on an animal skips the game's TakeDamage on the guest: does it lose the hit's feedback?
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0428 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in:
branch: fix/w46-k2b-content-a
commits:
tests: npc_hit_feedback
signature:
docs: docs/re/creature.md#creature-guest-hit-feedback
related: BUG-0428, BUG-0350, BUG-0464, BUG-0409, BUG-0714
---

## Report
From the BUG-0428 audit (K2b, 2026-11-08): on a guest npc::damage_filter returns false for a hit on an NPC proxy (the host applies it, MSG_DAMAGE_REQ), so the whole cCreatureAnimal::TakeDamage is skipped on the guest; the audit inferred that the guest loses the hit's local feedback (BUG-0350 fixed only the Cell ring).

## Root cause
Not a loss. Observed (runtime, lane 6, lab 20261108-040716, three animals hit by the guest as players hit, `coop.npc hit` through the game's TakeDamage call): the proxy's health in the guest's game (the game's combatant health) dropped 0.07 / 0.15 / 0.28 s after the hit (the host's state: 0.1 s interval plus the round trip), the proxy played the host NPC's animation in 10/14, 12/14 and 13/15 samples of the 4 s after it (the behaviour animations relay, BUG-0464: the hit reaction, flight or attack, is the host NPC's behaviour tree's), and the guest's TakeDamage was filtered (no second health change). What TakeDamage does on its own is the host's to do: the health and death (sent), the throw of a push hit (the host throws, the proxy follows, BUG-0409), the stimuli and ApplyEvent (the proxy has no AI; the guest takes the host's relationship table). Running it on the proxy as well would apply each of them twice. The Cell case differs: the health ring is drawn by the attacker's own DamageCell there (BUG-0350); in Creature the target's health bar reads the proxy's health, which the state brings.

## Fix
None: the guest's game shows the host's result of its hit; the test stays as a guard (npc_hit_feedback).

## Verification
no-fail-run: nothing is lost to reproduce; the evidence run: out/lab/20261108-040716 (npc_hit_feedback: health drop on the guest after 0.07-0.28 s, the host's animation on the proxy, the hit filtered once).
Re-run on the merged branch (the sub-worktree run dirs were removed): npc_hit_feedback PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: measured what the guest's game shows; closed as not-reproduced.
