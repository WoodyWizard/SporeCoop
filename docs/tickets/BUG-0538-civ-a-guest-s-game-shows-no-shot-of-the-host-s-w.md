---
id: BUG-0538
title: Civ: a guest's game shows no shot of the host's when more than 24 shooters fire (the shot list was over the packet size)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-04
source: code reading, fix/w42-civ-left
role: guest
versions: 
fixed_in: 0.7.6
branch: fix/w42-civ-left
commits: d8ea4474
tests: coop_civ_shots.py (civ_shots), coop_civ_religious.py (civ_religious)
signature:
docs: docs/re/civ.md#civ-hp-shots-turrets
related: BUG-0477, BUG-0530
---

## Report
Found reading civ.cpp host_send_shots (0.7.5): the host's shot list held up to 40 shooters of 46 bytes in one
MSG_CIV_STATE message (1845 bytes with 40), over net::kMaxPacket (1200); the connection drops such a message, so in a
fight of more than 24 shooters a guest got no shot list at all and saw no shot.

## Root cause
Observed (code): the list was capped by count (40), not by bytes; protocol.h kMaxPacket = 1200.

## Fix
The list goes in messages of at most 22 shooters (47 bytes each with BUG-0530's count): host_send_shots (commit
d8ea4474, with BUG-0530). No switch (a size limit, no behaviour change below 22 shooters).

## Verification
no-fail-run: needs more than 24 shooters at once (not in a lab scenario); civ_shots and civ_religious pass with
the split lists (out/lab/20261104-094742).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): found and fixed.
- 2026-11-04: released in 0.7.6.
