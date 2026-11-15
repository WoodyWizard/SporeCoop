---
id: BUG-0727
title: Content: a model whose records the mod cannot read was built unchecked
status: not-reproduced
kind: bug
stage: All
reported: 2026-11-08
source: BUG-0424 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in:
branch: fix/w46-k2b-content-b
commits:
tests: content_audit_tribe, content_audit_civ, content_audit_space (coop_content_audit.py)
signature:
docs: docs/re/creations.md#creations-k2b-design-content
related: BUG-0424, BUG-0469, BUG-0852
---

## Report
K2b audit of the class BUG-0424: creations::ensure_model builds a model whose records the mod cannot read (records_from_game false: a database without a file location, a format the mod does not parse) unchecked, as 0.7.3.5 did (log line "its records cannot be read here: built as it is").

## Root cause
Not seen: records_from_game reads the package file the resource manager's FindRecord names (DBPF, compressed records included). Every database of the GOG 2024 and Steam installs and of a player's profile is such a file; a folder database (no file location) is the only case, and none was registered in any lab game. The warning appears in none of the 32157 test logs of 2924 lab runs on this machine (all worktrees, 2026-10-30 .. 2026-11-08); the audit test content_audit_<stage> lists the model of every placed object in Tribe, Civ and Space: no model was unreadable (unknown_parts never `unreadable` for a model kind).

## Fix
None (not reproduced); the old behaviour (built as it is, logged once) stays. Known issue entry with how it would show.

## Verification
no-fail-run: not reproduced (0 of 32157 lab logs; content_audit runs below show no unreadable model).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): not reproduced; evidence above.
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: one level below this ticket the class is real: BUG-0852 (open) shows that a model whose records ARE read can still go unchecked when its skin-effect bytes are run-compressed (creations.cpp model_effects returns false, the callers skip the paints). The content audit (content_audit_<stage>) counts no such model because it does not see them either. Untried test: a creature model with an FE n run across its coat / detail layers (the fork's 1934-byte example) through the content check. Verdict of this ticket (records readable) unchanged.
