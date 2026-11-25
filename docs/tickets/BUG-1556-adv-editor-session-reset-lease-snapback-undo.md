---
id: BUG-1556
title: Shared Adventure Editor: broadcast Save/Play-test session reset, release leases on drag end, snap-back on reject, and non-destructive Ctrl+Z
status: open
kind: bug
stage: Adventure
reported: 2026-11-25
source: v0.8.0 independent audit
role: both
versions: 0.8.0
fixed_in:
branch: w0801/adveditor
commits:
tests:
signature:
docs: docs/10-adventures.md
related: BUG-0863
---

## Report
Independent static audit of `BUG-0863` found real-UI blockers in `mods/coop/adv_editor.cpp`:
1. Saving an unsaved draft or starting Play-test changes/resets host's `session_id` without notifying guests (`adv_editor.cpp:783-813,1411-1414`), permanently desyncing guests (`g_guest.synced` stays true and drops all future `K_OP_APPLIED`).
2. `flush_dirty_transforms` (`889-911`) and `h_CommitHistory` (`1170-1175`) never send `LEASE_RELEASE`, leaving a 5 s post-drag lockup.
3. `K_OP_REJECT` (`1363-1384`) and `LEASE_DENY` (`1331-1335`) do not snap rejected guest marker transforms back to authoritative state.
4. `h_CanUndo`/`h_Undo`/`h_CanRedo`/`h_Redo` (`1177-1219`) unconditionally hijack native Undo/Redo, breaking Ctrl+Z for non-synced terrain, goal, and property edits.

## Root cause
See `docs/plan-0.8.0.1.md` WP-6.

## Fix
TBD.

## Verification
TBD.
