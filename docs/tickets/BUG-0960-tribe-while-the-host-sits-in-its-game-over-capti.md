---
id: BUG-0960
title: Tribe: while the host sits in its game-over caption the guest plays on against a paused host (no caption, the host's knocked-out chief shown standing)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-12
source: fix/w48-tribe-mirrors, tribe_gameover_partner lab 20261112-093040 (BUG-0922)
role: guest
versions: 0.7.12 - 0.7.13
fixed_in: unreleased
branch: fix/w48-rest
commits: fd4b65ab, fcaec2b8, 6507e5fc
tests: tribe_gameover_wait, tribe_gameover_partner
signature:
docs: docs/re/tribe.md#tribe-gameover-wait
related: BUG-0922, BUG-0288, BUG-0685
---

## Report
Seen while closing BUG-0922 (tribe_gameover_partner, lab 20261112-093040, key frame `caption`): every member of the
player tribe knocked out on the host, the host's game shows "Alas! Your population has fallen below the minimum ..."
and pauses (busy=cinematic,input_lock,paused) until its player presses OK; the guest's game shows no caption and plays
on (HUD, orders) against a paused host. The host's knocked-out chief stood up (hp 168, dead 0) while the caption was
up: the guest's copy of it stands at the hut (as in the 0.7.12 final check's play_tribe end frame, lab
20261111-044257).

## Root cause
Observed: the caption comes on the host only (docs/re/tribe.md#tribe-bounce-mirrors: the guest's input lock stays 0).
Not decided: whether a guest should show the caption too (its OK would reload its own save, BUG-0685) or a co-op note
("the host's tribe is out, waiting for the host"). Low severity: the host's OK bounces both back.

Decided (2026-11-12, fix/w48-rest): the guest does not show the caption (its OK would reload its own save, BUG-0685);
its game waits with a co-op note while the host's caption is up, as the host's game does.

## Fix
mods/coop/tribe.cpp `gameover_host_tick` / `gameover_pause`: the host sends whether its game-over caption is up
(MSG_NPC_ANIM kind 20 {u8 up}, reliable, again every 3 s while up; up = Tribe, the game's input lock and stood time,
entered with every player-tribe member knocked out). A guest's game stands with the game's own
cGameTimeManager::Pause (reason 0x4BF38A7, the game's own UI-modal / hint reason, counted per reason: ours is balanced) and shows "The tribe is out: waiting for <host> (host) to answer the caption"; Resume on the host's "down", 10 s without news, the
stage's or the session's end. `coop.tribe` status line "tribe game over wait". Switch: `coop.fix tribe.gameover_wait
off`.

## Verification
Test `tribe_gameover_wait` (tools/tests/coop_tribe_gameover_wait.py, tribe fixture): every player-tribe member knocked
out on the host; the guest's game time stands within 6 s of the host's caption (`busy: paused`, the game's time
manager) and no caption of its own; key frame `caption`; the host's Enter: back in Tribe, the guest's time runs again.
FAIL (SPORE_FIX_OFF=tribe.gameover_wait): out/lab/20261112-141204 (the guest `busy: -` under the host's caption).
PASS: out/lab/20261112-140917, out/lab/20261112-141335.

## History
- 2026-11-12: opened.
- 2026-11-12: the guest waits (paused, a note) under the host's caption (tribe.gameover_wait), fix/w48-rest.
