---
id: BUG-0096
title: coop.vote editor submits the editor request inside Present, a path the real game never takes
status: fixed
kind: test
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#74
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tests
commits: 1e2136a
tests: vote
signature:
docs:
related: #74
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/74 (full text, discussion).

`coop.vote editor`, the command coop_vote.py uses to open an editor "like the game's own editor entry", is registered with `SK_CMD_MAIN_THREAD`. Those commands run in `pump_main_tasks()` inside the hooked Present. It calls `EditorRequest::Submit` there. When the vote does not hold the request (a single player, or a guest's own request), `h_Submit` hands it to the original Submit inside Present. The mod's own code takes care to submit editors outside the frame ("editors create D3D resources"). Severity: minor for players. The test exercises a different path than the game, and can deadlock or fault under DXVK where the real flow would not, or pass where the real flow fails.

### Where
- The command: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2736-L2766
- `h_Submit` falls through to `o_Submit(req)` at once when it does not hold the request: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L697-L728
- The rule the rest of the code follows (`submit_tick`, `own_editor_tick`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L625-L648
- MAIN_THREAD commands run inside Present: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/d3d9hook.cpp#L185-L207

### How to reproduce
1. One instance alone in the Creature stage (or a guest): `coop.vote editor`.
2. Under DXVK, watch for a stall in Present or a fault in the editor's resource creation. Compare with opening the editor at the nest (the game's own path: Submit outside Present).

### Expected
The test command takes the same route as the game: Submit outside the frame.

## Root cause
**What happens**

1. `coop.vote editor` builds an EditorRequest and calls `cdecl_call<bool>(submit, req)` from the command callback.
2. MAIN_THREAD commands are pumped by `frame_common()` (in `on_frame_begin_present`), i.e. inside `IDirect3DDevice9::Present`.
3. The hooked Submit:
   - as a host with partners, it starts the vote, and the request later goes out through `submit_tick` outside the frame (fine);
   - as a guest, alone, or as the host with `share_save_due()` false, it reaches `o_Submit(req)` inside Present.
4. The request is never released by the command (no `vcall(req, 2)` after Submit), unlike `hold_submit`'s own reference handling. Whether this leaks depends on the constructor's initial count (not checked).

### Confidence
CONFIRMED by reading that Submit runs inside Present. Whether Submit itself creates D3D resources synchronously is not verified (the editor may only be created on a later frame). The mod's comments assume it must be outside.

## Fix
Fixed on fix/w4-tests (1e2136a): `coop.vote` is registered with `SK_CMD_OUTSIDE_FRAME`, so the
`editor` branch submits its request outside Present, where the game submits its own (and where
`submit_tick` / `own_editor_tick` already submit). The other branches (test, action, go) start or end
a vote, which the game's own Submit also does outside the frame. No switch: a test cannot tell the
two paths apart (as the ticket says). The request's reference count was not changed: whether the
constructor's initial reference leaks stays unchecked.

## Verification
Regression check only (the ticket's plan): `coop_vote.py` PASS with the change,
out/lab/20261028-025535 (the vote, the host's and the guest's editor through `coop.vote editor`).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #74.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fix/w4-tests: coop.vote runs outside the frame; coop_vote.py passes.
- 2026-10-28: released in 0.7.3.0.
