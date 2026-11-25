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
tests: tests/adv_editor_tests.cpp
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
Implemented in `mods/coop/adv_editor_logic.h` and `mods/coop/adv_editor.cpp` (wire-compatible with `creature 1.5`):
1. **Host session reset & guest re-sync**:
   - `host_ensure_session` (called on edit-mode entry, `tick()`, and immediately after `o_Save` in `h_Save`) guarantees a distinct `session_id` and broadcasts a `K_HELLO_SYNC` reset signal carrying `{new_draft_key, new_session_id}` to editor-capable guests; `reset_state` broadcasts `{draft_key, 0}` when the host leaves edit mode (Play-test / menu).
   - `GuestEditor::on_session_reset`, `accepts_draft_key`, `adopt_draft_key_if_needed`, and `session_mismatch` clear `synced`, discard stale leases/undo depths/pending ops, adopt host's new `draft_key` when saved from an unsaved or already-synced shared draft, and immediately re-send `K_HELLO_SYNC`. Defensive mismatch handling also triggers reset on stale `K_OP_APPLIED`, `K_LEASE`, `K_OP_REJECT`, `K_SNAPSHOT` session switches, and stale guest `K_OP_REQ`/`K_LEASE` on host.
2. **Immediate drag lease release**:
   - Added `DragLeaseTracker` in `adv_editor_logic.h` to track markers leased by interactive `h_SyncMarkerTransform` drags (leaving explicit console `coop.adveditor move`/`lease` behavior intact for `coop_adv_editor.py`).
   - `h_CommitHistory` flushes dirty transforms and immediately sends `LEASE_RELEASE` for all active drag leases; `tick()` also releases any idle-flushed drag lease after `kDragLeaseReleaseSettleSec` (0.25 s) without waiting for the 5 s TTL.
3. **Rejected transform snap-back & `LEASE_DENY` holder tracking**:
   - Host includes `LeaseTable::remaining_ttl_ms` in `LEASE_DENY`.
   - `GuestEditor::on_lease` records the conflicting holder on `LEASE_DENY` (falling back to `kLeaseTtlSec` if `ttl_ms == 0`), and `GuestEditor::on_op_reject` records `REJECT_LEASE_HELD` holders and returns the marker's authoritative `MarkerRec`.
   - On both `LEASE_DENY` and `K_OP_REJECT`, the guest drops pending dirty drags and queues a native snap-back (`g_applying = true`, `snap = true`) to the authoritative transform.
4. **Non-destructive `Ctrl+Z` / `Ctrl+Y` routing**:
   - Added `UndoRouter` in `adv_editor_logic.h` to record whether each `h_CommitHistory` corresponded to a synced edit hook (`Synced`) or a native-only edit (`Native`: terrain, atmosphere, goals, cast/marker properties).
   - `h_CanUndo`/`h_CanRedo`/`h_Undo`/`h_Redo` route `Synced` entries through the shared op log (with rollback on rejected guest `OP_UNDO`/`OP_REDO`) and fall through to `o_CanUndo`/`o_CanRedo`/`o_Undo`/`o_Redo` (wrapped in `g_applying = true`) for `Native` entries. Console `coop.adveditor undo`/`redo` remains routed to the shared op log.

## Verification
- Native build (`make -j$(nproc)`): 0 warnings, 0 errors.
- Native ASan+UBSan unit tests (`make unit-cpp`): `adv_editor_tests: 265 checks, 0 failed` (covering session reset & guest re-sync on Save/Play-test/mismatch, drag lease release on CommitHistory and 0.25 s settle, `LEASE_DENY` holder tracking + `K_OP_REJECT` snap-back, and mixed synced/native UndoRouter routing).
- Test lint (`python3 tools/tests/test_lint.py`): 0 problems.
- Pending live Wine/lab `coop_adv_editor.py` verification before closing.
