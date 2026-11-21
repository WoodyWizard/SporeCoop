---
id: BUG-9320
title: Tribe: repeated party wipes of the host's village (village down about every 100 s, the hut read 1/400 right after a wipe reload)
status: open
kind: bug
stage: Tribe
reported: 2026-11-21
source: QA 2026-11-21
role: host
versions: 0.7.14.1-lineages67-dev (fix/l47-tribe)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9155, BUG-9318
---

## Report
Live QA 2026-11-21 (MSI two games, Vandh, Tribe; host MSITribeHost instance 2, guest MSIGuest instance 1), while testing
BUG-9318 with `coop.tribe npc_tribe` villages, `coop.tribe win <id>` test wins (allies) and one `mod.reload coop` on the
host (which ended the session; the host re-hosted and loaded Game0 again). From then on the host's village went down
over and over: host log "Village down. Bring a partner villager ..." at 10:32:45, 10:34:23, 10:36:01, 10:40:12,
10:41:53, 10:43:32 (about every 98 s), "Last villager down" at 10:46:49, each followed 60 s later by "party wipe, seat 0:
Host's village was wiped out: its newest ally (00002296) is a rival again, a totem piece and a step of progress are
lost" and the wipe's reload. `coop.tribe huthp` on the host's hut right after one wipe reload (about 10:41:30): `health
1/400 exact=1.00000 state=1`; after the 10:44:32 wipe, before its reload: 160/400 (the 40 % the rescue sets) and then
376/400 five seconds later (being hit). A crowd of white villagers of the Lavender Village (a native NPC tribe,
archetype 7) stood at the host's hut (out/qa-shots/bug9318_host_native_totem_cutscene.png background).

## Root cause
Not analysed. Candidates: (1) a wipe's checkpoint reload restoring the hut at the 1 hp the rescue holds it at (the save
taken while the village was down), so the next blow downs it again at once; (2) the wipe turning the newest ally into
a rival again, which then raids; (3) the test setup itself (test wins, the session restart). Needs a clean repro
without `mod.reload` and test wins: a native raid that downs the host's village once, then watch the hut's health
after the wipe's reload.

## Fix
Not done.

## Verification
no-fail-run: not fixed yet.

## History
- 2026-11-21: opened (fix/l47-tribe live QA).
