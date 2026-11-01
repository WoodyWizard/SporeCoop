---
id: BUG-0270
title: Guest fault in the game's UI code while loading a shared Space world after a fixture jump (dc04aee5:0x0095904B, jump to 0x65)
status: fixed
kind: crash
stage: space
reported: 2026-10-29
source: lab 20261029-093233 campaign
role: guest
versions: 0.7.3.0
fixed_in: 0.7.3.2
branch: fix/w9-space
commits: 1cc86fe
tests: dead_modal (coop_dead_modal.py)
signature: dc04aee5:0x0095904B
docs: docs/re/space.md#space-owed-refund-identity, docs/06-testing.md#testing-space-world-identity
related: BUG-0284, BUG-0285
---

## Report
Lab 20261029-093233 campaign#2 (lane 4, out/lane4/campaign/20261029-093248, finding 4): after the
campaign's jump to the space fixture the host shared its world; Bob (a guest) loaded Madas.spo and
60 ms later caught `0xC0000005 at 0x00000065 (data 0x65)`, eax=0x65 (a call through a bad pointer),
calls 0095904B 00959832 0092245B 00922514 0095AB23 00812E3E ..., "called from" UTFWin::Image::GetProxyID
+0x1B0B < ... < UTFWin::UILayout::SetParentWindow+0x9BE. The game handled it (first chance) and went
on ("space: world epoch 2"); one fault, no crash report. The other guests had none. Not seen before
(`tickets.py find 0095904B`: nothing).

## Root cause
Observed (static analysis of GOG 2024 + the fault's registers): 0x959049 is `CALL EAX` in cWindowManager::UpdateHover (0x958FB0), reached from ProcessMessage 0x9596E0 (mouse messages 6..9) <- cWindowManager::Update 0x95A9E0 <- the app frame 0x812E20. ECX = [WM+0x82C] (the modal window, set by PushModal 0x95AE80) = 0x237C6FA0, whose vtable was 0x13EB92C: the refcounted base vtable the destructor 0x410D00 leaves, so the window had been destroyed; slot 0xF8 lies past that table's end and reads 0x65. The co-op windows are not modal (the mod never calls Push/PopModal). Inferred, not proven: a game window that was modal when the mod took the guest from Space to the galaxy menu (rejoin, `quit to the galaxy menu posted`) and loaded the shared world from there was destroyed without PopModal; which window is open.

## Fix
Contained: gamefixes.cpp hooks UpdateHover; when the modal window's vtable slots 0xC4/0xF8 do not lead into executable code, it drops the modal window (+0x82C/+0x830 = 0, as PopModal of the last one does, without calling the dead handler) and logs "the window manager's modal window ... was destroyed". Switch `game.dead_modal`. The game's own reason is not fixed (unknown).

## Verification
Simulated state (`coop.deadmodal` puts an object with 0x65 vtable slots at WM+0x82C on the guest, then mouse moves): FAIL with `SPORE_FIX_OFF=game.dead_modal` out/lab/20261030-002848 (6 faults at 0x65 from 0x0095904B, the player's signature); PASS out/lab/20261030-005133 (2 runs, cold and warm). The real trigger was not reproduced (seen once in three campaigns).

## History
- 2026-10-29: opened.
- 2026-10-30: analysed and contained on fix/w9-space (dead modal window guard, test dead_modal).
- 2026-11-01: released in 0.7.3.2.
