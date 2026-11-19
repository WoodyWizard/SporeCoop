---
id: BUG-1298
title: WP-S2b: a disconnect inside a guest's own editor dresses the shared tribe or civilization on the guest's game and loses the result
status: open
kind: bug
stage: Tribe, Civ
reported: 2026-11-19
source: plan
role: guest
versions: 
fixed_in:
branch: feat/w56-editors
commits: 8709f9fe
tests: own_editor_drop, own_editor_drop_off, own_editor_civdrop, own_editor_civdrop_off (coop_own_editors_robust.py drop|civdrop)
signature: TribeUI_EditorReturn 0x00CDCC90, Planner_ApplyDesign 0x00D095A0
docs: docs/re/creations.md#creations-own-editors-robust, docs/06-testing.md#testing-own-editors-robust
related: BUG-1272, BUG-1275, BUG-1276
---

## Report
Lineages comparison (item 3): a session's end inside a guest's own editor. The pending state was a session state
(designs.cpp, civ_editors.cpp, civ_arrival.cpp) and the return hooks asked for a connected guest: after a drop the
game's own return reloaded the shared tribe slot with the guest's outfit on the guest's game (every member there in
it), wrote the guest's design into its copy of the civilization (Civ_SetDesign) or made the Civ outfitter's result the
player species, and the result never reached the line. The fork needed a journal and an acknowledgement (its BUG-9044).

## Root cause
✔ (code) designs.cpp h_EditorReturn took the result only `is_guest()` and oo_reset (SESSION_STATE) dropped the
pending request; civ_editors.cpp active_return / take_editor need `guest_own()` (connected), reset() clears the takes;
civ_arrival.cpp drops its phase when not allowed() (connected) and at the session end. Not run.

## Fix
Switch `designs.own_editor_offline`: our own request stays ours until the editor returns (also without a session), the
returns are intercepted as in a session, the result waits until this game is a guest whose host sent its stored row
(`species::own_ready`), goes to the line and is kept until the host's broadcast row names it (`species::host_took`;
another drop first: again at the next session). civ_arrival follows an open editor to its end offline and does not
open the next one. In memory only (a restart loses the result; the creation stays in My Creations). Our design keeps
the host's save free of the guest's species: nothing is written into the tribe or the civilization.

## Verification
Not run yet: the lab waits for the orchestrator's word (2026-11-19, the CPU was busy with a foreign task). Ready:
`SPORE_DETECTOR=20 python3 tools/lab.py run own_editor_drop own_editor_drop_off --lane 6 --lane 7 --retry 0 --no-build`; the twin must
FAIL, the test PASS (run ids go here).
And `own_editor_civdrop own_editor_civdrop_off` (civ_npc).

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-editors: fix and its test written (switch, FAIL twin entry); the lab run waits for the orchestrator.
- 2026-11-19: fix/w57-own-editors: own_editor_civdrop PASS / own_editor_civdrop_off FAIL (fix/w57-own-editors worktree out/lab/20261119-130049, PASS again -142554); own_editor_drop: every BUG-1298 check PASS (offline=1 held=1, the slot kept, the outfit reaches the line after the join, acked=1), the only FAIL left is the host save's raw creation key (BUG-1326); own_editor_drop_off FAIL in -130049 (earlier than planned: without the offline switch the pending outfitter is dropped at the session's end and with it BUG-1308's part waiver, so Done stays). Open until BUG-1326.
