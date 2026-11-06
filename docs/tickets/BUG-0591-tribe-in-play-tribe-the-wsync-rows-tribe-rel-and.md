---
id: BUG-0591
title: Tribe: in play_tribe the wsync rows tribe_rel and tribe_food stop reaching some villages on the guest (detector rows from about 400 s on)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-05
source: lab (fix/w43-crashes) play_tribe with SPORE_DETECTOR=20, 20261105-003630
role: guest
versions: 
fixed_in: 0.7.8
branch: fix/w44-world-state2
commits: 06a664f2 94022d49 bf250d06 e016c580
tests: tribe_new_villages (coop_wsync_tribe.py villages)
signature:
docs: docs/re/tribe.md#tribe-new-village-ids
related: BUG-0580, BUG-0565
switch: wsync.tribe_pos
---

## Report
Found by the lab (no player report). play_tribe with `SPORE_DETECTOR=20` on fix/w43-crashes (W1 rows tribe_rel and
the new tribe_food), out/lab/20261105-003630: tribe_npc_food passes (lab 20261105-000745), but in play_tribe from
about 400 s on the detector keeps `Tribe:state:relation` (the village at (446 -150 185): -7 on the host, 0 on the
guest, 9 comparisons) and `Tribe:state:tribe_food` (the village at (502 -12 67): 150 -> 90 on the host, 0 on the guest;
later (446 -150 185) 110 on the host while the guest's fell 80 -> 49), 28 % of the Tribe comparisons. Both rows stuck
together on the same villages: ~ inferred, the guest's copy of those villages is not matched (other political id and
more than 40 m from the host's position in wsync match()), or the guest's rows were not applied then (`playing`).
Not seen in the baseline play_tribe of rel/0.7.7 (lab rel/0.7.7 20261104-230915: no relation rows, no raid).
Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_tribe --lane N`, `tools/detector_report.py <run>`; read the
guest's `coop.wsync` (differ / applied) and `coop.wsync dump tribe_rel` on both games when the rows appear.

## Root cause
Observed (lab 20261105-123323, the fix off; the 0.7.7 final play_tribe 20261105-015320): the rows start when the
host's NPC tribe controller forms new villages (418 s in that run: "the host's NPC tribe 2 / 3 / 4 ... made it"). The
guest makes each with its own next political id (host 000021CE / CF / D0 / D1 -> guest 000021D0 / D1 / D2 / D3 at the
same places), so a host id names another village on the guest. wsync's match() took the same political id before
the place: the host's relation and food of one village went to another, two villages got none (relation 0 / food 0
on the guest, the detector's rows). Not a cap, not a resend that stops.

## Fix
mods/coop/wsync.cpp match(): the closest village within 40 m first, the same political id only when none is that
close (villages do not move; the guest's copy is made at the host's centre). Used by tribe_rel, tribe_food and
tribe_goal. Switch `wsync.tribe_pos` (off: the political id first, 0.7.7).

## Verification
tribe_new_villages (the host forms 4 villages, gives each its own relation and food change; the guest's game read
with coop.wsync dump at the same place): FAIL with `SPORE_FIX_OFF=wsync.tribe_pos` out/lab/20261105-123323 (8 of 10
values differ, e.g. relation -4.5 / 0.0, food 205 / 0), PASS out/lab/20261105-123225 (0 differ). Detector before:
the rel/0.7.7 final play_tribe 20261105-015320 (Tribe:state:relation / tribe_food from 418 s). The same test with
SPORE_DETECTOR=8: with the fix off out/lab/20261105-132359 gives the final check's rows (Tribe:state:relation "the
tribe at (446 -150 185): -7.00 on the host, 0.00 on the guest", Tribe:state:tribe_food, 2 of 2 samples), with it
out/lab/20261105-132259 none. play_tribe with SPORE_DETECTOR=20 (out/lab/20261105-121916 before the fix,
20261105-124754 after): no state rows in either, but the host formed no village in these two runs (the condition was
not met: play_tribe is no check of this fix).

## History
- 2026-11-05: opened.
- 2026-11-05: fixed by wsync.tribe_pos (fix/w44-world-state2).
- 2026-11-05: released in 0.7.8.
