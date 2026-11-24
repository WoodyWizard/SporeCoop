---
id: BUG-1421
title: CORE-2: a guest in Creature without its avatar yet when the landfall editor's vote ends loses its first own Creature editor
status: fixed
kind: bug
stage: Creature
reported: 2026-11-22
source: audit
role: guest
versions: 0.7.16.1
fixed_in: 0.7.16.1
branch: fix/w63-editor
commits: 35163c73, 2a454d1e
tests: editor_stand_landing, twin editor_stand_landing_off
signature: guest log: "vote: ended by the host: ... editor=FD4902BD" with no "own editor" line after it
docs: docs/09-coop-design.md
related: BUG-1240, BUG-1245, BUG-1336
---

## Report
External static audit of 0.7.15.5, item CORE-2 (out/research/ext-audit-0.7.15.5.md; verified REAL in
out/research/ext-audit-verify-net-core.md). Found by reading the code, no player report.

## Root cause
Inferred from the code (session_glue.cpp, the MSG_VOTE handler; 0.7.15.5 lines 963-980, rel/0.7.16 990-1010): at a
vote's end the guest's own editor is due only if it is in Creature with its avatar (`g_mode == kCreature && g_local`)
or, for the landfall editor FD4902BD, in a mode before Creature (BUG-1240's "on its way": `g_mode != kCreature`). A
guest in Creature whose avatar is not there yet (its landing after the host's world loaded) fits neither, and the host
does not wait for it (BUG-1245's "on its way", vote.cpp), so the vote can end right then: the guest lost its first own
Creature editor and played the host's species.

## Fix
session_glue.cpp, switch `core.own_editor_no_avatar` (vote.cpp): such a guest (in Creature, no local avatar) takes
BUG-1240's arriving branch: its own editor stays due and opens 15 s after the avatar is there (own_editor_tick's
arrival rule, 300 s at most). Test seam `coop.vote noavatar <s>`: a vote end in that time sees no avatar here.
CORE-3 (the guest's own editor after the Creature stage-end vote) is untouched: the owner wants it. Off: as 0.7.16.

## Verification
Open, waiting for the lab run.
* Wanted PASS: `python3 tools/lab.py run editor_stand_landing --lanes N` (the guest's game opens its own landfall
  editor after the vote whose end saw no avatar).
* Wanted FAIL: `editor_stand_landing_off` (SPORE_FIX_OFF=core.own_editor_no_avatar: no own editor).

- 2026-11-23 (0.7.16.1 final check): editor_stand_landing PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened (audit CORE-2).
- 2026-11-22: fixed on fix/w63-editor (35163c73), tests 2a454d1e.

- 2026-11-22: renumbered from BUG-9306 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9306.
- 2026-11-23: released in 0.7.16.1.
