---
id: BUG-9183
title: Tribe: a guest whose own village is wiped out sees the game's own game-over screen
status: fixed
kind: bug
stage: tribe
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: ea9cd738
tests: tools/qa_win (manual, TITAN host + the MSI guest, 2026-11-13)
signature:
docs:
related: BUG-9147 BUG-9155
switch: tribe.guest_pop_hold
---

## Report
Found in the 2Z A check (2026-11-13): the MSI as a guest on a worn copy of the Jiff Tribe save (DuoQAJiffStand, every village down to one villager): the mod said "MSIGuest's village was wiped out: no win to lose yet: every village lost 10% of its food" (the party wipe, BUG-9155) and the MSI's game showed the native game over too ("Alas! Your population has fallen below the minimum needed to survive ... Bounce back in time", out/shots/guest-native-gameover.png). BUG-9147 keeps the host's game from it; a guest's own game was not checked.

## Root cause
Observed (the MSI log, 14:18:30): "the population event (05C80207): survival=1 active=1 host=0 players=3": the guest's game posts the event too, and h_TribeMessage held it only on the host (`is_host()`). Since BUG-9153 a guest's game shows its own village as the player's, so a wiped guest village is a wiped player tribe there.

## Fix
tribe.guest_pop_hold (on), tribe.cpp h_TribeMessage: a connected guest's game never runs 05C80207 (logged "the population event on a guest: held, the host decides"; `coop.tribe` status: "guest population events held="). The host already decides what a wiped village costs (the rescue window, the party wipe, a villager back: BUG-9147, BUG-9155). Off: the guest's game runs the event. Also a support log of every Tribe cinematic start (TRG_*) with its caller, for BUG-9184.

## Verification
no-fail-run: not reproducible on demand. The guest's own event came once in about an hour of play on the worn DuoQAJiffStand copy (14:18:30, before the fix: the native game over on the MSI, out/shots/guest-native-gameover.png). After the fix, 25 minutes with the switch off on the MSI and its village wiped seven times by raiders (the host's party wipe each time) brought no guest event; forcing it (the guest's copy of its last villager at 0 health on the MSI only) did not either. Built and on all three games; 0 faults.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on feat/lineages-47 (decision 3A A); BUG-9184 opened (the host's own game over seen on the way).
