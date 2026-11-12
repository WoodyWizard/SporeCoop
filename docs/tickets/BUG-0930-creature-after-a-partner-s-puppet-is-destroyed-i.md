---
id: BUG-0930
title: Creature: after a partner's puppet is destroyed in a fight, NPCs still hold raw pointers into it (AI block +0x68C / +0x694 / +0x6D8, creature +0x354..+0x370) and one NPC keeps it alive as its last attacker (combatant +0x28)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-11
source: found with coop.staleref while fixing BUG-0926 (lab lane 2, 2026-11-11)
role: host
versions: 0.7.x
fixed_in: unreleased
branch: fix/w48-rest
commits: 70c01f1f, 184396b6, 42f198bc
tests: stale_last_attacker, herd_help_stale, npc_lost_target
signature:
docs: docs/re/creature.md#creature-stimulus-attacker, docs/re/creature.md#creature-stale-holders
related: BUG-0926, BUG-0456, BUG-0918
---

## Report
No player report. `coop.staleref` (a scan of the host's memory for pointers into a destroyed creature, lab lane 2,
tools/tests/coop_stale_refs.py) lists what still names a partner's puppet after it is destroyed in a fight, besides
the stimulus fields fixed by BUG-0456 / BUG-0926 and the targets (BUG-0453, BUG-0918).

## Root cause
Observed (per removal, about a second of game time after the destroy; offsets of the holder):
* NPC AI block (creature +0xB4C) **+0x694** -> the puppet (+0), 2-8 NPCs, in every removal, also after the puppet's
  block was freed; **+0x6D8** -> the puppet, 1-4 NPCs, 5 of 6; **+0x68C** -> its combatant (+0x5A8), 1 NPC (the one
  that attacked it), gone a second later in the runs where the NPC lived.
* Creature **+0x354 .. +0x370** -> the puppet's +0xC0 sub-object, 1-4 creatures, changing from scan to scan (a list
  of neighbours that is rebuilt).
* The NPC the puppet hit last: **+0x5D0** (combatant +0x28) -> the puppet's combatant, with a reference: it keeps the
  destroyed puppet's block alive (vtables intact, destroyed flag set, count 1) until that NPC is hit by somebody
  else or goes. One creature's memory per such death stays that long.
Not known: what these fields are, which code reads them and whether it checks the destroyed flag. No fault or stop
was seen from them (6 + 6 + 8 removals with the block freed while they named it, lab lane 2).

Named (2026-11-12, fix/w48-rest; GOG 2024, Ghidra and lab lanes 4 / 5, docs/re/creature.md#creature-stale-holders):
* CORRECTION: "AI block +0x68C / +0x694 / +0x6D8" are not in the AI block. The AI block (cBehaviorTreeData, creature
  +0xB4C) is 0x670 bytes (GeneralAllocator::new_(0x670) at 0xC1FF71; its heap header reads 0x679); `coop.staleref`
  took 0x700. The next heap block is the creature's **cModifiers** (+0xB48, new_(0x230) at 0xC0B797, ctor 0xBCA010),
  so the holders are cModifiers +0x14 (a combatant), +0x1C and +0x60 (a creature): the data of its 8 modifier slots
  (0x44 bytes each: data +0x00..+0x1F, id +0x20, keys +0x24 / +0x28 (-1 = free), time +0x2C, end / tick callbacks
  +0x34 / +0x38, two references +0x3C / +0x40 released by the game). In the lab dumps the slots holding the puppet
  were free (key -1, the references already released by the game) at the scan: stale data of a free slot. The tick
  (0xBCA4C0) walks only the live slots of the mask +0x22C, the find (0xBCA230) compares keys and the two reference
  values without a call, the clear (0xBCA150) releases only +0x3C / +0x40: nothing reads a free slot's data.
  Not a use: left as it is.
* Creature +0x354 .. +0x370 (the puppet's +0xC0): rewritten from scan to scan; no fault in 20 removals whose block was
  freed while they named it (here and in the runs below, the block now freed 0.2-0.8 s after the destroy). Left.
* The last attacker (combatant +0x28 = creature +0x5D0) holds a reference (cCombatant::TakeDamage 0xBFCF10 swaps it
  with the combatant's AddRef vt 0x60 / Release vt 0x64): the only holder that kept the destroyed puppet alive.

## Fix
mods/coop/gamefixes.cpp `forget_last_attacker` (the DestroyInstance hook): right after the game's destroy of a
creature or citizen, every listed creature whose last attacker is it lets go of it (the field null, as for a creature
never hit; the reference released with the combatant's own Release, vt 0x64). The destroyed puppet's block is then
freed with its other references (0.2-0.8 s in the lab) instead of living on for 25 s and more. `coop.staleref` now
sizes the AI block 0x670 and names the cModifiers block as its own kind. Switch: `coop.fix npc.stale_last_attacker
off`.
Two wrong first versions, found by the test and not released: the release before the game's destroy and through the
combatant's vt 4 (no Release: the thunk 0xC06640 to the creature's Write 0xC04380) faulted at 0xC0439F, after which
the host's outside-frame console commands got no answer (BUG-0966).

## Verification
Test `stale_last_attacker` (tools/tests/coop_stale_last_attacker.py): the guest hits a host NPC until it attacks the
puppet (its +0x5D0 = the puppet's combatant), the host removes the puppet; in the host's memory: the NPC's +0x5D0
no longer names it 1 s later and the puppet's block is freed within 6 s, 3 rounds.
FAIL (SPORE_FIX_OFF=npc.stale_last_attacker): out/lab/20261112-133753 (3 of 3 NPCs kept it, 3 of 3 blocks alive).
PASS: out/lab/20261112-135316, out/lab/20261112-135318 (0 of 3, blocks freed after 0.21-0.84 s).
How to look further: `SPORE_LANE=2 python3 tools/tests/coop_stale_refs.py --rounds 6` (report only; `--strict` fails
on any holder in a listed object, an AI block or cModifiers).

## History
- 2026-11-11: opened.
- 2026-11-12: holders named (cModifiers, not the AI block; the last attacker), the last attacker released after a destroy (npc.stale_last_attacker), fix/w48-rest.
