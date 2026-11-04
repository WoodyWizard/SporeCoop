---
id: BUG-0453
title: host crash 0xC0000005 at 0xD65D23 (SporeApp.exe+0x965D23, read 0) inside the NPC behaviour tree tick, 0.7.3.3 and 0.7.3.4
status: fixed
kind: crash
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 0.7.3.3, 0.7.3.4
fixed_in: 0.7.4
branch: fix/w40-crashes
commits: b063830c
tests: npc_lost_target
signature: dc04aee5:SporeApp.exe+0x965D23 (0xD65D23, read 0, BT_AttackTick 0xD67CD0)
docs: docs/re/creature.md#creature-lost-target, docs/06-testing.md#testing-lost-objects
related: BUG-0456, BUG-0426
---

## Report
Two host crashes with the same signature: 2026-11-01 14:27 (0.7.3.3) and 2026-11-02 18:03 (0.7.3.4), Creature, main thread, read of address 0. crash_triage chain: SporeCoop::BT_HerdLandScore+0x27C2 <- (frame names misleading, BUG-0346) <- Simulator::BehaviorTreeTickGeneric+0x540 <- ... i.e. a behaviour-tree node of an NPC reads a null object. Inferred: a node whose target is one of our objects (puppet, proxy, nest egg copy, pack member) or an object freed by the mod. Next: Ghidra on 0xD65D23 and the node's caller, the host log around the crash (report folder 20261102-180656). Class G9 (BUG-0426).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (dumps, disassembly, lab):
* 0xD65D23 is `mov edx,[ebx]` in 0xD65BE0, called only by the tick of the creature attack node 0x02D852E6 (`SporeCoop::BT_AttackTick` 0xD67CD0); ebx is the node's target argument, `[npc+0xE7C]` (the target's combatant): 0 in both dumps (eax = ebx = ecx = 0).
* The tick's own "no target" check (0xD65980: null -> the node ends) is skipped while the node's attack animation (node data +0x30) still plays; 0xD65BE0 then calls `target->vt 8` on null.
* The game itself clears the target of every NPC aimed at a creature that is destroyed (lab: NPC in 0x02D852E6 with the partner's puppet as target, the puppet destroyed: +0xE7C = 0 within 0.2 s). Co-op destroys the partner's puppet in a fight (death, hide, leave, rejoin); the vanilla game never destroys a creature in a fight.
* Lab, fix off: the target cleared while the attack animation plays (`coop.attackcut`) gives the players' fault exactly: 0xC0000005 at 0xD65D23, data 0, calls 0xD680B2 0xD8F463 0xD8B88A 0xD7D942 0xBC8B44 (135 times, every tick of that NPC).
Inferred: in both player crashes the cleared target came from the guest's puppet being taken away (the guest had died ~4 s before, the host's own respawn 50 ms before the crash, a cinematic playing); which object's destroy cleared it in that frame is not known.

## Fix
mods/coop/gamefixes.cpp: a hook on BT_AttackTick: an NPC whose target (+0xE7C) is null ends the node (returns 0, what the node's own check returns for a null target), animation or not. Switch `coop.fix npc.attack_lost_target off` (the old tick). The class (G9, BUG-0426: the game's AI holding our destroyed objects) is covered together with BUG-0456 (stimuli).

## Verification
coop_npc_lost_target.py (suite npc_lost_target): FAIL with SPORE_FIX_OFF=npc.attack_lost_target,npc.stale_sender: out/lab/20261103-143947 (host faults at 0xD65D23 x many, stale senders); PASS: out/lab/20261103-144227 (#2; #1 failed only on the monitor line of the test's own 3 s puppet hide, now allowed in suite.py) and out/lab/20261103-144842 (2 of 2).

## History
- 2026-11-02: opened.
- 2026-11-03: root cause (attack node with a target cleared by a destroy), fixed on fix/w40-crashes (npc.attack_lost_target).
- 2026-11-03 (fix/w40-gh-issues): GitHub issue #195, 0.7.3.6 GOG 2024, **single player** (no session), Creature: crash 50 ms after the player's own creature came back from a death (`ko: own avatar alive again`, then the fault). Same fault as above, observed in the crash text: 0xC0000005 at 0xD65D23 read 0, eax = ebx = ecx = 0, calls 0xD680B2 0xD8B88A 0xD7D942 0xBC8B44 (the attack node chain, NPCTickAI in the stack). Inferred: an NPC attacking the player kept its attack animation while the game replaced the dead avatar and cleared the NPC's target; so the bug is the game's own and also hits without co-op (the hook in gamefixes.cpp runs in single player too, no session check). Covered by npc.attack_lost_target (0.7.4).
- 2026-11-03: released in 0.7.4.
