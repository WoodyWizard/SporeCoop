---
id: BUG-9118
title: downed players get 30 s for a rescue and can give up
status: fixed
kind: feature
stage: All
reported: 2026-11-11
source: owner request on 0.7.8-lineages47-dev
role: all
versions: 0.7.8-lineages47-dev
fixed_in: 0.7.8-lineages48-dev
branch: feat/lineages-47
commits:
tests: QA host 9 + guest 8, Samuth.spo (Creature): coop.giveup down, the 30 s countdown, coop.giveup, DNA before/after, see Verification
signature: -
docs: docs/06-testing.md#test-downed-give-up
related: BUG-9089 BUG-9010
switch: creature.down_30 cell.down_30 tribe.down_30 space.down_30
---

## Request
"Increase the death timer to 30 s and allow them to give up. Check on the progression loss: when a
player dies and times out, progress should be lost."

## What each stage did (before)
| stage | downed timer | nobody came |
|---|---|---|
| Creature, Adventure | 10 s (creature_rescue) | the pack loses 10% DNA (Adventure: none), native death and nest respawn |
| Cell | 10 s (cellsync) | respawn small next to the partner: **no progress lost** |
| Tribe | 10 s (last villager / village) | food setback of 10%, respawn (or capture for a village) |
| Space | 20 s | the ship is destroyed the native way (native penalties) |

## Change
* 30 s in every stage (switches above; off = the old time). Tribe's rescue countdown on the wire went
  up to 300 tenths (it was clamped to 100 and a guest refused more).
* Give up: hold G for one second with the game window in front (give_up.h), or `coop.giveup`. The
  result is the timer's at once, with its loss. Creature / Adventure: a guest sends event 3 (GiveUp) on
  MSG_CREATURE_RESCUE, the host settles it ("<name> gave up. The pack lost N DNA."). Cell: the own
  knockout ends. Tribe: the host's founder directly, a guest's with CMD_GIVE_UP (9). Space: the own
  disabled ship ends. The notices say "hold G to give up".
* Open: Cell still loses no progress on a timeout (a decision for the owner: editor DNA or food).

## Verification
Creature, QA host 9 + guest 8 (Samuth.spo), the guest 30 m from the host:
* `coop.giveup down` on the guest: downed; 15 s later "Downed: 15 s for your partner to revive you
  (hold G to give up)" (the 30 s timer).
* `coop.giveup`: "Guest gave up. The pack lost 3 DNA." on both games, DNA 29 -> 26 on host and guest,
  the guest's creature died natively (hp 0). 0 faults.
* Before moving apart the guest was revived at once: the host stood within 6 m (a partner nearby revives).
* Not run: Cell, Tribe and Space give-up and timers in a live game; the G key itself (the console path
  was used; the key reads GetAsyncKeyState with the game window in front).

## History
- 2026-11-11: implemented on feat/lineages-47.
