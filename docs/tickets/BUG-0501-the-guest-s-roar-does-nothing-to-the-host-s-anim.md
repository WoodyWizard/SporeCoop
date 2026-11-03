---
id: BUG-0501
title: the guest's roar does nothing to the host's animals (split from BUG-0462)
status: open
kind: bug
stage: Creature
reported: 2026-11-03
source: split from BUG-0462 (player report 2026-11-02)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-roar
related: BUG-0462, BUG-0418
---

## Report
The second half of BUG-0462's host mark: "ROAR ability is broken for guests and work only host". Not analysed on
fix/w40-creature-acts (2026-11-03): which ability the players call the roar is not known (neither fixture creature has
an ability named so; the types are 0xD 0xE 0x10 0x11 0x1D 0x1E 0x25 0x28 0x31 0x32 0x33). A guest's ability plays on its
own avatar and its puppet (animation relay), but its effect on NPCs (a scare / social stimulus posted by the ability
code with the avatar as sender) runs only in the guest's game, where the NPCs are AI-less proxies; the host's NPCs never
get it (class B, docs/sync-architecture-plan.md L1: a request to the owner). Inferred, not observed.
To do: find the ability (Ghidra: the callers of BT_PostStimulus 0xBC9940 in the ability code), relay its stimulus to the
host as a request, the host posts it with the guest's puppet as sender.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not fixed (fix/w40-npc-anims, 2026-11-03, about 20 minutes of static analysis). Observed (Ghidra): the roar is the BT
blackboard variable `HeardRagingRoar` (global 0x169F134, end callback 0xD85400 shared with HeardSirenSong); its
setter, the ability that roars, was not found (the global is referenced only by its static init). Inferred: the
roar affects the NPCs of the roaring player's own game only. docs/re/creature.md#creature-roar. A test needs a
creature with the Raging Roar (a carnivore Cell's consequence ability); the fixtures have none.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: likely reported again (0.7.3.6, guest's report 20261104-012758, text 'ability doesn't work', Creature; the mark itself has no text). Not confirmed which ability.
- 2026-11-03: analysed on fix/w40-npc-anims (static only), not fixed.
