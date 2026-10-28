---
id: BUG-0061
title: a partner can stay shown as knocked out for good (translucent, frozen "KO" label)
status: fixed
kind: desync
stage: Cell
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#39
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-cell
commits: e7ab004, 595e12f, b6039e6, 55b3c2a
tests: tools/tests/coop_cell_sync.py (suite cell_sync), tests/cell_tests.cpp ko()
signature:
docs: docs/re/cell.md#cell-sync-robustness
related: #39
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/39 (full text, discussion).

The knockout state each machine keeps per remote player (`g_ko`) is never cleared when that player leaves, and the "up again" message is lost if it arrives while the receiver is outside the Cell stage. The partner's cell is then drawn translucent with a frozen "KO n" label and an off-screen arrow, although that player is fine. This lasts until that player is knocked out and revived again. Severity: wrong behaviour.

### Where
- The state map: [mods/coop/cellsync.cpp#L203-L212](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L203-L212)
- `on_ko` (changes only on a new state): [mods/coop/cellsync.cpp#L249-L268](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L249-L268)
- Opacity forced every tick while `KO_OUT`: [mods/coop/cellsync.cpp#L1131-L1139](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1131-L1139)
- The KO label and arrow: [mods/coop/cellsync.cpp#L1411-L1418](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1411-L1418)
- All cell messages dropped outside the Cell stage: [mods/coop/cellsync.cpp#L1209-L1234](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1209-L1234)
- Revived / respawned sent once: [mods/coop/cellsync.cpp#L310-L320](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L310-L320), [L324-L338](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L324-L338)
- Player leave does not tell cellsync: [mods/coop/coop.cpp#L1707-L1717](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1707-L1717)
- Player ids are reused (lowest free id): [mods/coop/net/session.cpp#L116-L127](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L116-L127)

### How to reproduce
Case A:
1. Host and one guest in the Cell stage, same pool, the guest far from the host.
2. Knock out the guest (`coop.pool hp 1`, then a bite, as `tools/tests/coop_cell.py` does). Within the 10 s, kill the guest's game (`tools/spore.sh kill` on its lane) or disconnect it.
3. Start the guest again and join the same host. It gets id 1 again.
4. On the host, `coop.pool` shows `ko[1]=1 ... op=0.35` and the overlay shows "KO" above the partner, although the partner is up.

Case B: as above, but instead of disconnecting the guest, open the host's cell editor right after the knockout, wait 12 s, leave the editor.

### Expected
A remote player's knockout state is cleared when that player leaves, and a missed "up again" message does not leave a stale knockout. A player who has just joined is never shown as knocked out.

## Root cause
**What happens**

Case A, player leaves mid-knockout:
1. Guest id 1 is knocked out. Every 0.25 s it sends `MSG_CELL_KO {1, KO_OUT, left, hold}`; the host stores `g_ko[1].state = KO_OUT` and relays it to the other guests.
2. The guest disconnects (quits, crashes, times out) before the revive or respawn. `cb.player_leave` despawns the puppet and erases the remote, but `g_ko[1]` stays `KO_OUT`. Only `on_session_end` clears `g_ko`, and the host's session does not end when one guest leaves.
3. Someone joins (or the same player rejoins) and gets id 1 again from `alloc_id`.
4. `symbiosis_tick` finds `g_ko[1].state == KO_OUT` and sets the new puppet's opacity `+0xA4` to 0.35 every tick. `draw_overlay` draws "KO n" with the last `left` value above it, or an arrow on the screen edge.
5. `on_ko` changes state only when a different state arrives. The new player's first knockout sends `KO_OUT` again (same state), and only its later revive or respawn clears the label.

Case B, receiver outside the Cell stage:
1. Guest id 1 is knocked out while the host is in the Cell stage. The host stores `KO_OUT`.
2. Within the 10 s, the host enters its cell editor (mating call) or a loading screen.
3. The guest is revived or respawns and sends `KO_REVIVED` / `KO_RESPAWNED` once. `on_message` returns at once because `!in_cell()`, so the message is dropped (and not relayed to other guests either).
4. The host returns to the Cell stage: `g_ko[1]` is still `KO_OUT`, and the guest's puppet is translucent with a frozen label, as in case A.

### Confidence
CONFIRMED by reading the full path (leave callback, id allocation, `symbiosis_tick`, `draw_overlay`, the early return in `on_message`). Not run in the game.

## Fix
Fixed on fix/w2-cell (e7ab004). `cb.player_leave` calls `cellsync::on_player_leave(id)`: its
knockout state (the puppet made opaque again), puppet record and food bookkeeping are erased.
`MSG_CELL_KO` is stored and relayed in any stage (effects only in the Cell stage). A remote `KO_OUT`
not refreshed for 2 s (the owner sends it every 0.25 s) ends by itself (`ko_expire`), which also
covers an "up again" lost for another reason. Old behaviour: `coop.pool fix kostale 0`.

The review suggested:

- Give cellsync an `on_player_leave(id)` that erases `g_ko[id]` (and `g_puppet_seen[id]`), and call it from `cb.player_leave`.
- Clear `g_ko` when the local player leaves the Cell stage (the block in `tick` that calls `forget_mirrors`).
- Make the state self-correcting: while up, a player sends its `KO_UP` state at a low rate (for example once a second for 5 s after a revive or respawn), or include the KO state in the regular player state. The receiver treats `KO_OUT` older than about 1 s without a refresh as stale (the downed player refreshes it every 0.25 s).
- Handle `MSG_CELL_KO` (store and relay) even outside the Cell stage.

Keep the old behaviour behind `coop.fix kostale off`.

## Verification
`coop_cell_sync.py`, case A of the report: the guest is knocked out, leaves 1 s later and joins
again (id 1 again); the host's `coop.pool` prints the guest's puppet opacity `+0xA4`. FAIL with
`SPORE_CELL_FIX=0` (out/lab/20261027-231147): `puppet 1 op=0.35` (and `ko[1]=1`) after the rejoin. PASS (out/lab/20261027-231004,
out/lab/20261027-231523): op=1.00, no knockout entry. Case B (the host in its editor) is covered by the 2 s expiry
and the handling outside the stage, not run in the game.

The planned test was:

`tools/tests/coop_cell_ko_leave.py` on `twoinst.start` (and a restart of the guest instance): knock the guest out, kill or disconnect the guest within 10 s, start it again, rejoin, swim next to the host. Assert on the host's game state: the opacity `+0xA4` of the guest's puppet cell is 1.0, read through `coop.pool` `op=`. Also assert that no "KO" overlay is drawn: check the overlay text through a debug hook, or that `ko[1]` is absent. A second case enters the host's cell editor during the knockout. Both fail today and pass with the fix.

### Related
- Knockout design: [mods/coop/cellsync.h#L1-L7](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.h#L1-L7)
- The local side of the same state not being reset is a separate issue (KO state not reset on session end or stage leave).
- PR #5 (BUG-0028, do_leave skipping cellsync cleanup) is a different leave path.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #39.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-cell (bug wave 2); test FAIL without the fix, PASS with it.
- 2026-10-28: released in 0.7.3.0.
