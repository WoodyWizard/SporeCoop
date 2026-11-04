---
id: BUG-0512
title: Creature: when the host targets or attacks the guest's creature (no damage), the host's pack attacks the guest for real and kills it
status: fixed
kind: bug
stage: Creature
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-guest-combat
commits: f0902456, 22870f85
tests: coop_creature_pack_target.py
signature:
docs: docs/re/creature.md#creature-pack-target
related: BUG-0374, BUG-0168, BUG-0420
---

## Report
Host's mark (0.7.3.6, report 20261103-193309): 'I'm host, my pack mate attacked the guest when I accidentally targeted him'. A direct message (0.7.3.6): the host can attack the guest without dealing damage, but the pack targets the guest and actually kills it.
Target: 0.7.4 (Creature).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed. BUG-0374 (fixed in 0.7.3.4) stopped the pack's retaliation when the guest hits a pack member; this is the other direction: the host's own attack order on the guest's puppet is copied by the pack AI (the game's 'help the leader' rule), and the puppet's hits on the guest are forwarded as real damage. Inferred fix: the pack AI must not take a partner's puppet as a target (the host's target is a partner: drop it for the pack, as BUG-0374 did for retaliation), and the host's target selection on a partner should not start an attack. Size: S-M.
Observed (the host's log of report 20261103-193309): in that fight the pack hit the guest's puppet every 0.3 s and
every hit was dropped by npc.party_peace (`hit by pack posse=1 amount=0.0 dropped`); the damage the guest took was
forwarded from other NPCs (`hit by npc posse=0`), the guest's log has only those. So the pack fought the guest on
both screens (no damage), next to wild NPCs that did hurt it. Observed (lane 2): a click on the puppet sets the
avatar's +0xE7C to the puppet's combatant; the pack did not follow it in 15 s (with PlayAbility / the key 1 either);
a pack member given the puppet as target (SetTarget, combatant) keeps it for good. Not found: the game's trigger
that makes the pack take the host's target (inferred: the avatar's real attack on its target, or the stimuli).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`npc.pack_spares_partners` (npc.cpp `pack_spares_partners`, from host_tick): every frame a host pack member (the
pack list, which the creature update list does not always hold) or family member aimed at a partner's puppet
(+0xE7C inside the puppet) gets SetTarget(null, 1, 0) and its stimuli naming the puppet cleared
(gamefixes::clear_stimuli_from). The damage stays dropped by npc.party_peace. Off: 0.7.3.6.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
coop_creature_pack_target.py (suite pack_target). The pack's target is set by the test (the natural trigger was not
reproduced); the host's own click on the guest is checked as well (no pack target, no hit on the guest).
* FAIL with `SPORE_FIX_OFF=npc.pack_spares_partners`: out/lab/20261103-172853 (the pack kept the guest as target, 16 of 16 samples, 3 rounds).
* PASS: out/lab/20261103-173004.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage); the 0.7.3.4 fix BUG-0374 covers another path.
- 2026-11-03: fixed on fix/w40-guest-combat (npc.pack_spares_partners); trigger not reproduced, the state made by the test.
- 2026-11-03: released in 0.7.4.
