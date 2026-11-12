---
id: BUG-0947
title: Tribe: a guest that rejoins in Tribe has the ground round a village the host made live 4.5-4.9 m lower than the host (its copy stamps the ground differently)
status: open
kind: desync
stage: tribe
reported: 2026-11-11
source: final check of 0.7.12 (lab 20261111-151535, campaign, Carol after its rejoin)
role: guest
versions: 0.7.12
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-terrain-stamps
related: BUG-0332, BUG-0945
---

## Report
Found in the campaign of the third final check of 0.7.12 (lab 20261111-151535; no player report). Carol left and
joined again in the middle of the Tribe play (s 1473-1485). From then on the campaign's sync note says for Carol
only: "ground height differs (objects in place on their own ground): Carol n303: centre distance host 527.2 guest
522.3" and "n297: host 525.8 guest 521.3": the ground under two animals is 4.5-4.9 m lower in Carol's game. None of
the three earlier final checks has the note (their rejoin fell into Cell and Space).

## Root cause
Observed (the `terrain:` log lines of that run):
* The host loaded its own share save at s 1223 (tribe.host_reload, BUG-0332: one ground for everybody) and made an
  NPC village live at s 1290.9: `ApplyStamp from 0x0109DFBF pos (-43.37 -504.67 -98.57) |r| 516.03`.
* The guests that were there made their copy of the village with the same call site (Carol at s 12xx: 1 stamp
  from 0x0109DFBF).
* Carol after its rejoin: `RemoveStamp` x3, the load, then one `ApplyStamp from 0x01091981` at the same place (s
  1485.4): the other of the two stamp calls a village makes (docs/re/tribe.md#tribe-terrain-stamps: 0x0109DFBF and
  0x01091981 are both the village's).
Inferred: the rejoining guest's copy of a village the host made live gets a different stamp (or only one of the two)
than the first copy, so its ground round that village differs; BUG-0332's class (the game renders a live stamp and a
loaded one differently). Not checked: where n303 and n297 stood, whether the share save holds the village's stamp
while the village itself is stripped from it.

## Fix
None yet.

## Verification

## History
- 2026-11-11: opened (fix/w47-final12c). For 0.7.13. Reproduce: a four-player campaign with the rejoin in Tribe after
  `coop.tribe form` made a village, then `coop.terrain` on both games round the village.
