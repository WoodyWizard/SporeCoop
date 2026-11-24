---
id: BUG-1422
title: CORE-9: a guest's own editor ends host away and drops the DNA it earned while the host was away
status: fixed
kind: bug
stage: All
reported: 2026-11-22
source: audit
role: guest
versions: 0.7.16.1
fixed_in: 0.7.16.1
branch: fix/w63-editor
commits: 35163c73, 2a454d1e
tests: dna_own_away_editor, twin dna_own_away_editor_off
signature: guest log: "host away ended without its return (host mode ...): solo DNA N of mode ... dropped" when the guest opened its own editor
docs: 
related: BUG-0233, BUG-0151, BUG-0205, BUG-0829
---

## Report
External static audit of 0.7.15.5, item CORE-9 (out/research/ext-audit-0.7.15.5.md; verified REAL in
out/research/ext-audit-verify-net-core.md). Found by reading the code, no player report.

## Root cause
Inferred from the code (coop.cpp host_away_tick; 0.7.15.5 line 638, rel/0.7.16 647): the guest's "host away" ends
1 s after the guest itself leaves its stage (`!stage_mode(g_mode)`), also for its own editor. The host is still away
then, so dna_route::solo_end returns Drop and the DNA the guest earned meanwhile (in its bar already) is dropped; the
host's next total for it takes the DNA away again. The likely path: the host evolves (its editor at once,
core.editor_now), the guest plays on alone, earns DNA, then takes its Evolve now offer or its nest's editor.

## Fix
coop.cpp, switch `core.away_through_editor`: the guest's own editor (kEditor / kEdit) does not end the away state;
the guest's solo DNA is kept through it and the host's return decides as before (sent to the host). coop.status
prints `kept_in_editor` (frames). Off: as 0.7.16.

## Verification
Open, waiting for the lab run.
* Wanted PASS: `python3 tools/lab.py run dna_own_away_editor --lanes N` (the guest earns 20 while the host is in its
  editor, evolves in its own editor before the host is back: the host's game has the 20 afterwards, the guest's too).
* Wanted FAIL: `dna_own_away_editor_off` (SPORE_FIX_OFF=core.away_through_editor: the 20 are dropped).

- 2026-11-23 (0.7.16.1 final check): dna_own_away_editor PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened (audit CORE-9).
- 2026-11-22: fixed on fix/w63-editor (35163c73), tests 2a454d1e.

- 2026-11-22: renumbered from BUG-9307 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9307.
- 2026-11-23: released in 0.7.16.1.
