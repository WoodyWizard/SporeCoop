---
id: BUG-0633
title: Cell: the host cannot collect the part (card) a kill dropped
status: open
kind: bug
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-02
role: host
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0467, BUG-0316, BUG-0632
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/Sterben-02 (the host, mark at s 211.6): "can't take a card after
a kill, Cell stage". The host's shot shows the goal "Collect / Unlock Cell Parts 3/6"; the guest's shot at s 212 shows
several silver part tokens and meat chunks.

## Root cause
Observed (the host's log): `[s 193.958] eat: from 1 gone c2935292748 (part) ... applied` (the guest swallowed a part
token of the host's pool), `[s 195.293] parts: 1 Cell part(s) of the party collected here (found by player 1)`, the
party's parts 0E2C -> 0F2C. Guest kills at s 206.3 and 206.9 (each applied twice, BUG-0632), refused at 207.5. No
part-token, collect or loot line from the host's own game between s 196 and the mark.
Inferred, weak: the token of that kill exists in one game only, or the guest took it first and the host was left with
one it cannot collect; or a side effect of the doubled break (BUG-0632).
Related: BUG-0467 (Cell parts shared, fixed 0.7.4), BUG-0316 (a guest collecting a part token of the host's mirror, not
reproduced).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: the guest kills a part-carrying cell next to the host, then the host swims into the dropped token: check
     CollectPart 0xE5D7B0 on the host and that the token exists in both pools. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
