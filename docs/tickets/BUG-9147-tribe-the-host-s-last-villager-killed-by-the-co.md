---
id: BUG-9147
title: tribe: the host's last villager killed by the co-op epic without the knockout, native game over
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: found in QA 2026-11-12 (four-player Jiff test copy)
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + guest and the MSI host alone, 2026-11-13)
signature:
docs: docs/re/tribe.md#tribe-population-event-alone
related: BUG-9135 BUG-0172
switch: tribe.founder_survival_alone
---

## Report
QA 2026-11-12, four players on the Jiff test copy (every founder village down to one member after 4 idle hours): `coop.tribe epic` near the host's village; the host's chief died (hp 0, dead=1) without "Last villager down" and the host got the native game-over caption ("Alas! Your population has fallen below the minimum...").

## Root cause
Not analysed. protect_founder (tribe.cpp) refuses a knockout when the village is not intact (hut gone or damaged to 0), when no partner village has a living member, or when the hit is not lethal on that call; the epic's attack path is not known to go through the citizen damage hook. Earlier the same day two epics did knock villagers out.

## Root cause (2026-11-13)
✔ (runtime, a support log in h_TribeMessage) The population event 05C80207 was held only with a partner online
(players >= 2): a host alone (the partners offline, their villages the AI's) got the native rewind. In the MSI's copy of
the save no founder village had a living member (the host's village empty, partner 1's only member not alive): the hold
needs a living partner member, so the rewind came. The event's data: [+0] 0147B844, [+8] a pointer that changed with each
death (the citizen that died, ~), no village pointer. After a party wipe (BUG-9155) the newest ally becomes a rival and
its raiders killed the one-member villages again: the event came every 10-20 s.

## Fix
tribe.founder_survival_alone (on), tribe.cpp: (1) the rewind is held while a partner founder village has a living
member also with no partner online (multi-founder rosters); a host village with no living member gets one villager back
at its home 2 s later. (2) With no founder village alive the rewind is held too (owner decision 1U: the party wipe
instead): one villager back at every empty founder village and party_wipe(0) with its world refresh. (3) The event
again within 300 s is only held (no second wipe), and a village gets a villager back at most once per 300 s (a villager
made there that did not count as living came back every event before this guard). The first 20 events are logged
("the population event (05C80207): ...", the seats' living members).

## Verification
no-fail-run: manual (no Windows lab lane). FAIL (the code before, the MSI host alone on its copy of the save, host village
0 members, partner 1's member not alive): after about 3 minutes cinematic FBCD6489 (the native game over), twice.
PASS: "the population event (05C80207) ... players=1", "village 800021CF had no living member: one villager is back",
"village 000021D5 ... one villager is back", "party wipe, seat 0: ... a totem piece and a step of progress are lost"; the
later events "held, no second wipe"; no game-over cinematic, 0 faults. With a guest online on TITAN (the epic next to the
host's last villager): "Last villager down" (knockout), "Your partner's village survives", the party wipe after the
window and the villager back at its village (unchanged path). The 4-player epic kill without the knockout (the first
report) is covered by (1) and (2) but was not replayed.

## Seen again 2026-11-12 (MSI, copies of the Jiff save)
The copies made after this report keep the host's village with no living member (`coop.tribe villages`: the player
village, 0 members). Loaded and played for a few minutes, the host got the native game over ("Alas! Your population has
fallen below the minimum needed to survive...", cinematic FBCD6489, the game paused, every founding stopped). So the
native population check still ends the game for everyone when the host's village is empty by any way other than a
knockout (here: already empty in the save). To do: catch the game over in Tribe while co-op founders exist (the party
wipe and a member at the host's village instead) or keep a member alive; the native check is not found yet.

## History
- 2026-11-12: opened.
