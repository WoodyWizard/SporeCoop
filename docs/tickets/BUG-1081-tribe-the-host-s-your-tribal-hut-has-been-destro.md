---
id: BUG-1081
title: Tribe: the host's 'Your tribal hut has been destroyed' caption does not hold the guests' games (BUG-0960 covers the tribe knocked out only)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-hutwait
commits: 710a1593
tests: tribe_hut_defeat_wait
signature:
docs: docs/06-testing.md#testing-hut-defeat-wait, docs/re/tribe.md#tribe-hut-defeat-caption
related: BUG-1076, BUG-0960
---

## Report
Found on fix/w49-fault (BUG-1076), lab 20261113-132309-2 (lane 7, `tribe_own_hut_fall` with the fix): the host's game
showed its own defeat caption "Oh, the humanity! Your tribal hut has been destroyed! Perhaps a fresh start is just the
thing you need." (the key frame 01 of the host; its OK reloads the host's save, inferred from the caption)
while the guest's game played on (`busy -` for 45 s). BUG-0960 (`tribe.gameover_wait`) pauses a guest only while the
host's player tribe is out of members (`player_tribe_out`), not for the hut's defeat. Inferred: the guest plays on
against a host that stands, then the host's reload re-aligns the guest's planet (orders and moves made meanwhile are
lost). Reproduce: `coop.tribe hutfall <players' hut position>` on the host (docs/06-testing.md#testing-own-hut-fall).

## Root cause
Observed (lab 20261113-140055 / -140058, `coop.status` and `busy`): the blow on the players' own hut sets its
combatant state to 2 at once; 7.4 s later (after the burning hut's cinematic) the host's game shows "Oh, the
humanity! Your tribal hut has been destroyed! ..." with `busy: cinematic,input_lock,paused`. BUG-0960's host check
(`gameover_host_tick`) sent "up" only when the input lock and the stood time came with `player_tribe_out` (no member
with health); with the members alive it never sent, `host_up=0 sent=0`, and the guest stayed `busy -` for the whole
caption. Observed: the player tribe's `mpHut` (+0x368) still points at the dead hut while the caption is up.

## Fix
One shared condition on the host: `gameover_why(tribe)` returns the reason the player's game is over: the tribe out
(BUG-0960) or, new, its hut (cTribe +0x368) in combatant state 2 (as `cTribe::HutIsDead` reads it). The rest is the
BUG-0960 path unchanged: the host's "up" news (MSG_NPC_ANIM kind 20, again every 3 s, now with the reason byte
instead of 1; any non-zero is "up"), the guest's pause through the game's cGameTimeManager::Pause with the UI-modal
reason, no caption of its own, the resume when the host's caption is gone (its OK reloads the host's save and the
realign follows it), after 10 s without news or at the stage's or session's end. The guest's co-op note names the
reason ("The tribal hut is destroyed: waiting for <host> ..."). `coop.status`: `why=`, `hut_ups=`.
Switch: `coop.fix tribe.hut_defeat_wait off` (only the tribe knocked out holds the guests, 0.7.13).

## Verification
- Test `tribe_hut_defeat_wait` (tools/tests/coop_tribe_hut_defeat_wait.py): asserts the guest game's own state
  (`busy: paused` within 6 s and 8 s later, no defeat caption of its own), the host's reload, the guest running again
  and the players' hut standing in both games after it.
- FAIL with `SPORE_LAB_FIXOFF="tribe_hut_defeat_wait=tribe.hut_defeat_wait"`: lab 20261113-140058 (lane 7: the
  guest `busy -` while the host's caption was up, `host_up=0`).
- PASS: lab 20261113-140055 (lane 6), 20261113-140322 (lane 6); key frames show the host's caption and the guest's
  game standing; the guest's log has the wait and the resume 11 s later.
- Regression: tribe_gameover, tribe_gameover_wait, tribe_gameover_slow (20261113-140322), tribe_own_hut_fall,
  tribe_gameover_reload, tribe_gameover_reload2, play_tribe --quick (20261113-140327) OK; tribe_gameover_partner
  failed once in 20261113-140322 (a flake of the knock-out phase, BUG-1083), OK in 20261113-141231.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-hutwait (`tribe.hut_defeat_wait`), test tribe_hut_defeat_wait.
