---
id: BUG-0557
title: Creature: the guest cannot eat some bodies of creatures the players killed
status: fixed
kind: desync
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: both
versions: 0.7.5
fixed_in: 0.7.9
branch: fix/w44-guest-player2, fix/w45-creature-reports
commits: 2ce39e6a, 7f2c5dcd
tests: creature_eat (coop_creature_eat.py)
signature:
docs: docs/re/creature.md#creature-eat-body, docs/re/creature.md#creature-body-meat
related: BUG-0383
---

## Report
Both players: the guest cannot eat some of the bodies of creatures they killed together.
Reports: SporeCoop-report-2026-11-04 181848, 181852 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s3016 / s3024: the host sees two bodies of killed creatures by a nest; the guest's screen at the same time shows that ground without them (a darker view).
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs, s 3016 / 3024): no "eat:" line from the guest for those bodies; at s 3016 the guest's
  screen shows the ground by the nest without the two bodies the host sees.
* Lab: corpse_positions (BUG-0383, bodies of NPCs killed standing, running, thrown, on a slope, by an NPC, while
  the guest was away, eaten) on 0.7.6: see Verification. Inferred: the bodies the guest misses are kills whose
  proxies were gone on the guest (out of its 110 m, or removed with their herd) or kills by the host's pack (P1).

* Observed (Ghidra, fix/w44-guest-player2): the eat cursor 0xD2FFD0 needs a dead creature (+0xB5E) of another
  species whose meat +0xB84 is above 0. Runtime (out/lab/20261105-112339): the guest's dead proxy has the host body's
  meat (100 / 100) and dead flag: the guest's game may eat it. Eating by clicks in the lab was unreliable for both
  players (the host's control too), so not judged. Inferred: the reported bodies were missing from the guest's game.
* Observed (lab, fix/w45-creature-reports): the eat was never judged before because the creature fixture's species
  eat no meat: the eat node (start 0xD70E40, tick 0xD70FD0) bites (0xD70A80: meat +0xB84 -= rate x dt) only when the
  eater's species +0x608 is not 0 (0xC0B8E0, named IsHervibore in the March 2017 symbols), and both fixture species
  have 0 (out/lab/20261105-225751: the click walked the creature to the body and held the eat node, meat stayed 100).
  With +0x608 = 1 (`coop.rules diet 1`) and the player's click (`coop.rules click <id>` = the game's click handler
  0xD32FD0 on the body, cursor kind 8) both players eat (out/lab/20261105-225954).
* Observed (same run): **eating is each game's own act**: the guest ate its body to -31 meat, the host's body of the
  same NPC kept 100; the host ate its body to 12, the guest's kept 100. A body eaten out stays in the other game
  whole, and a body the guest ate out stays in the guest's game with meat below 0: the game offers no "eat" over it
  any more while the host's screen shows it untouched ("my guest can't eat creatures we killed").
* Inferred: the report's bodies were bodies the guest (or the host's pack) had eaten in one game; the guest's
  screenshot (no bodies by the nest) is the other half: bodies the host's game had eaten out and removed (despawned
  on the guest) or the guest's own eaten ones. The logs have no line per meal in Creature, so not observed there.

## Fix
Not fixed: not reproduced. `coop.rules meat <id>` prints a body's meat and dead flag on either game.
fix/w45-creature-reports (`npc.body_meat`, npc.cpp, new MSG_NPC_MEAT): the meat of a body is one value of the
party. The host sends the meat of every body a guest mirrors on a change of 0.5 and every 2 s; a guest's bites of its
proxy (the drop of +0xB84 in its game, 5 times a second) go to the host and come off the host's body; the guest's
proxy takes the host's value when it is lower (a body only loses meat). A body the host's game removes when eaten out
is despawned on the guest as before. `coop.fix npc.body_meat off`: each game eats its own bodies (0.7.8).

## Verification
no-fail-run: not reproduced (out/lab/20261105-112339; corpse_positions PASS out/lab/20261104-202315).
creature_eat (coop_creature_eat.py): two bodies killed by the host; the guest eats one and the host the other with
the game's own click; each game's cursor over the body is "eat", the eater's meat goes down and the other game's
body shows the same meat within 5 % (or both gone).
- FAIL with `SPORE_FIX_OFF=npc.body_meat`: out/lab/20261105-230449 (guest ate to -31.01, host kept 100.0; host ate to
  12.0, guest kept 100.0).
- PASS: out/lab/20261105-230313 (the other game within 2 meat during the meal; the body the guest ate out went from both
  games; the host's at 11.97 on both).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), not reproduced: corpse_positions PASS out/lab/20261104-202315.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): not fixed, not reproduced: no time left in P1 for the pack-kill case; no eat predicate of the player was found on the way.
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): the eat rule found (meat +0xB84), the guest's bodies have it; not reproduced, not fixed.
- 2026-11-05 (fix/w45-creature-reports, P3 of 0.7.9): reproduced (eating is per game); fixed behind npc.body_meat.
- 2026-11-07: released in 0.7.9.
