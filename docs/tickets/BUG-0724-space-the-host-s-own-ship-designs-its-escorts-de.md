---
id: BUG-0724
title: Space: the host's own ship designs (its escorts, defenders) showed the guest's own creation of the same id
status: fixed
kind: bug
stage: Space
reported: 2026-11-08
source: BUG-0424 sibling list, K2b audit 2026-11-08
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-b
commits: 9e2dadf1
tests: space_host_ship_design, space_host_ship_design_slow (coop_space_host_ship_design.py)
signature:
docs: docs/re/space.md#space-k2b-mirror-models
related: BUG-0424, BUG-0647, BUG-0670
---

## Report
K2b audit of the class BUG-0424: only civ.cpp resolved the host's design keys with creations::host_design (BUG-0647 vehicles, BUG-0670 buildings). The host's NPC ships in Space carry model keys too; the ships of the host's own empire (its escorts, its colonies' defenders) carry the keys of the host's own ship creations (My Creations, ids sequential per profile), and the guest's game resolved such a key to the guest's OWN creation of that id. The comment at creations.cpp:1949 ("a premade or a Sporepedia download: the same creation everywhere") was wrong for the same reason: a Sporepedia download is in one profile's cache only.

## Root cause
Observed (lab 20261108-032213, `SPORE_FIX_OFF=fleet.host_designs`): the host's My Creations held UFO model A under 2903A659, the guest's another model B under the same id; the host sent that key for its ships (`coop.fleet fakemodel 2 2903A659`): both mirrors on the guest were drawn from B (model hash 8F63EDA2, the host's A is 5DC76FCE).

## Fix
squadron.cpp: every non-zero model key of a host ship goes through creations::host_design(key, kMkUfo) before the existing key / content checks (`fleet.host_designs`): the host answers with the records of its own creation (registered here under a key of ours, the BUG-0647 path; mirrors never go into a save, BUG-0522) or "not mine"; while the answer is pending the mirror waits (`design_waits=`). The wrong comment is corrected (creations.cpp). `coop.fix fleet.host_designs off`: the key as the host sent it (0.7.10).

## Verification
FAIL: out/lab/20261108-032213 (`SPORE_FIX_OFF=fleet.host_designs`: mirrors 8F63EDA2, the guest's own). PASS: out/lab/20261108-031859 (space_host_ship_design: both mirrors 5DC76FCE, the host's design), out/lab/20261108-032608 (space_host_ship_design_slow, 20 fps over the player network).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065254, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): fixed, `fleet.host_designs`; FAIL and PASS runs above.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
