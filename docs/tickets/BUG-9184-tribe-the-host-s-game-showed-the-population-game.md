---
id: BUG-9184
title: Tribe: the host's game showed the population game over with no population event through the hold
status: open
kind: bug
stage: tribe
reported: 2026-11-13
source: test
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-9147 BUG-9155 BUG-9183
---

## Report
Found in the 3A A check (2026-11-13, TITAN host on the worn DuoQAJiffStand copy, 3 players): the host's game showed "Alas! Your population has fallen below the minimum ... Bounce back in time" (out/shots/host-native-gameover-1418.png) from 14:18:00 until OK was pressed at 15:27 (`busy=cinematic,input_lock,paused`). The host's village had been wiped twice before (party wipes 14:15:56 and 14:17:31, the second "refresh waits"). No population event reached h_TribeMessage before it: the first one logged after it is at 14:18:33 (held; a villager back 2 s later, under the caption: BUG-0172 territory). After OK (a reload) 25 minutes of play brought no second one.

## Root cause
Not found yet. Inferred: the game over came by another route than message 05C80207 through TribalGame_HandleMessage (CD81F0). The build now logs every TRG_* cinematic with its caller ("tribe: cinematic ... started from"), so the next one names the route.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
- 2026-11-14 (feat/lineages-47, 3Q A): not reproduced: two `coop.tribe wipe 0` 60 s apart on the DuoQAJiffStand copy
  (host + one guest) brought no game over (the test wipe runs the mod's party wipe, not the game's population count).
  Static search on the Steam QA copy (`coop.memfind <value> <max> code`, new: code and read-only data): the string
  TRG_PRE_PopDestroyed is the only "PopDestroyed" string and is pushed at CD8B01 only (the hooked listener's branch);
  05C80207 is posted at C26B80 only (message manager vt+0x14, so other listeners may get it too); FBCD6489 is no code
  constant (a hashed name). Open hypotheses: another listener of 05C80207, or a state that a party wipe's reload brings
  back. The support log now names every cinematic started in the Tribe stage (1ddb33ba), so the next one shows its route.
