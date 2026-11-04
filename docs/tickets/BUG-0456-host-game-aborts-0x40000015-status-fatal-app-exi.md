---
id: BUG-0456
title: host game aborts (0x40000015 STATUS_FATAL_APP_EXIT) at 0xD8F2AD (SporeApp.exe+0x98F2AD), 0.7.3.4
status: fixed
kind: crash
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 0.7.3.4
fixed_in: 0.7.4
branch: fix/w40-crashes
commits: b063830c
tests: npc_lost_target
signature: dc04aee5:SporeApp.exe+0x98F2AD (0x40000015 at 0xD8F2AD, _purecall via a destroyed stimulus sender)
docs: docs/re/creature.md#creature-lost-target
related: BUG-0453, BUG-0426
---

## Report
Dump crash-20261102-184600.dmp: exception 0x40000015 (a fatal app exit / abort) at SporeApp.exe+0x98F2AD (base 0x150000) = dc04aee5 0xD8F2AD, host, 2026-11-02 18:46. Cause unknown; the .txt was only a Discord preview. Next: Ghidra on 0xD8F2AD (who calls abort: a CRT check, pure virtual call, assertion).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (dump crash-20261102-184600.dmp, disassembly):
* 0xD8F2AD is the return address after `call edx` at 0xD8F2AB in 0xD8F1C0 (the herd-help node, `SporeCoop::BT_HerdHelpEval`): it calls `[c+0x5A8]` vt 0xC on the creature taken from the NPC's "attacked" stimulus (0x200, sender via 0xD998E0). Who calls abort: msvcr90's _purecall (frames msvcr90+0x719F9 below the context's esp) -> abort -> STATUS_FATAL_APP_EXIT; the context's eip is the return address 0xD8F2AD.
* The sender object (ebp = 0x3042B3C8 in the dump) has the root base vtable 0x13EB92C (slots 0 and 1 `_purecall` 0x11E0710) instead of cCreatureAnimal's 0x146A080: a destroyed creature.
* Stimuli keep a reference to their sender and decay over seconds; in the lab the "noticed" stimulus still named the destroyed puppet 4 s after its destroy, 28-37 stale senders over 6 removals of the puppet in one run (fix off).
Inferred: the destroyed sender was the partner's puppet (the guest attacks NPCs, TakeDamage posts 0x400 / 0x200 to the victim and its herd with the puppet as the sender; the puppet goes on the partner's death). No host log of this crash was posted.

## Fix
mods/coop/gamefixes.cpp: a hook on cGameNounManager::DestroyInstance: before a creature or a citizen is destroyed (ours or the game's), every creature's and citizen's stimulus naming it is cleared as the game's own clear 0xBC9A60 does (masks, value, sender released, union rewritten). Switch `coop.fix npc.stale_sender off`.

## Verification
coop_npc_lost_target.py: "no NPC stimulus named the removed puppet": FAIL with the switch off out/lab/20261103-143947 (28 stale), PASS out/lab/20261103-144227 and out/lab/20261103-144842 (0). The abort itself was not reproduced in the lab (no-fail-run for the abort: the herd-help node must read the stale sender before it decays; the test asserts the state that leads to it).

## History
- 2026-11-02: opened.
- 2026-11-03: root cause (a destroyed creature named by NPC stimuli, pure virtual call), fixed on fix/w40-crashes (npc.stale_sender).
- 2026-11-03: released in 0.7.4.
