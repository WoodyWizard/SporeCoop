---
id: BUG-0112
title: with 2+ guests waiting for the world, the host posts one save per guest in the same frame
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#91
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: df2f0a9, 75b7383
tests: tools/tests/coop_world_queue.py (suite world_queue)
signature:
docs: 
related: #91
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/91 (full text, discussion).

When two or more guests need the host's world at the same time, the host posts one game save per guest, all in the same frame. A guest that joins a few seconds after another can also trigger a second save while the first one is still being written. Nothing checks whether a save is already running. Two saves at the same moment are known to break a slot (docs/re/space.md, see below). The world the guests then load comes from that slot, and a second "game saved" box can stay open on the host and pause its game. This only happens with 3 or 4 players; with one guest there is only one waiting entry.

### Where
- `share_with` only skips a second save request for the *same* guest: [world.cpp#L364-L381](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L364-L381)
- `world::tick` calls `save_game` once for every waiting entry that needs a save, in one loop: [world.cpp#L680-L708](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L680-L708)
- `save_busy()` exists but neither `save_game` nor `save_blocker` asks it: [world.cpp#L383-L391](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L383-L391), [world.cpp#L312-L348](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L312-L348), [coop.cpp#L651-L667](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L651-L667)
- `on_mode_change` marks *every* guest as needing the world on each new stage or evolution: [coop.cpp#L800-L815](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L800-L815)
- `follow_phase` then calls `share_with(id, err, g_save_on_share)` for all of them in the same frame: [coop.cpp#L817-L872](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L817-L872)
- A guest's join while the host plays shares (with a save) at once: [coop.cpp#L1508-L1535](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1508-L1535)
- The "game saved" box watcher stops watching once it has confirmed and closed one box: [world.cpp#L265-L297](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L265-L297)

### How to reproduce
- 3 or 4 players: the host and guests B and C in the same stage (e.g. the `tribe` or `creature` fixture). The host evolves (editor, back to the stage) or enters the next stage. The host log shows several "world: sharing ..." lines after one save each, and `g_saved_at` moves for each guest.
- Or: the host plays, guest B joins, and guest C joins 2-5 s later. The host log shows a second save while the first one's "saved box" has not been seen yet.
- Watch the host: count the "game saved" boxes and whether one stays open (frames stall). Load the shared slot on a guest and check it loads.

### Expected
One save per moment, shared by every guest waiting for the world. A new guest joins a save that is already running or due instead of posting another one, and every "game saved" box that appears gets confirmed.

## Root cause
**What happens**

1. With guests B and C (and D), the host evolves or enters a new stage. `on_mode_change` sets `needs_world` on every remote. `follow_phase` shares with each of them in one frame, which adds one waiting entry per guest with `need_save = true`.
2. In the same `world::tick`, the loop over the waiting entries calls `save_game` for each entry. Nothing blocks the second or third call, so N save messages (0x1CD20F0, the pause menu's save) are posted in one frame. `g_saved_at` and `g_box_seen_at` are overwritten by the last one.
3. Second path: guest C joins 1-10 s after guest B while the host plays. B's entry has already saved (`need_save = false`) and waits for its files to be written. C's new entry saves again while `save_busy()` is true.
4. docs/re/space.md (search "Two saves at once break the slot too") records a slot whose load faulted at 0xBA7F87 or stayed in LoadGame forever. That case was the quit message's save plus our share save. Two pause-menu saves breaking a slot the same way is not verified, but it is the same kind of overlap. The guests get exactly that slot.
5. `box_task` closes the first "game saved" box and then sets `g_box_watch_until = 0` as soon as it sees no box. If a later save shows its own box after that moment, nobody confirms it. While that box is up the host's simulation is paused (per the comment above `kSavedBoxId`) and sometimes its frames too.

`tools/tests/coop_multi.py` joins its extra guests 1 s apart, which is path 3. Its `wait_all` presses Enter on the host whenever the host's frame counter stalls ("the host's game saved box"), which may hide the stuck box. coop_multi is in no lab suite, so this path never runs in the lab.

### Confidence
- CONFIRMED by reading that N saves are posted for N waiting guests in one frame, and that a join during a running save posts another one.
- PLAUSIBLE that this breaks the slot or leaves a box open; not seen at runtime in this review.
- Verified by a second reviewer: medium-high severity, more common than first stated (every stage change and evolution with 2+ guests).

## Fix
Fixed on fix/w2-world (df2f0a9). In `world::tick` a share that needs a save first looks for another
share whose save is posted and not written yet (`!need_save`, before its `latest`); if there is one,
it takes that save's `at` / `latest` / `mode` and posts none of its own. N guests waiting at a stage
change, or a guest joining during another one's save, get one save. `coop.status` counts
"world saves posted N, for shares M". Old behaviour: `coop.worldfix share_one_save 0`.

The review suggested:

In `world::tick`, before calling `save_game` for an entry, check two things: is a save busy (`save_busy()`), or did another entry already post one in this tick? If so, do not save again. Copy that save's `at` / `latest` / `mode` into this entry and set `need_save = false`. Equivalently, `share_with(peer, err, true)` can attach the peer to an existing saving entry. The box watcher should keep watching until `latest` of every waiting entry has passed. Keep the old behaviour behind `coop.fix share_one_save off`.

## Verification
Two-game test `coop_world_queue.py` part 1: `coop.worldfix queue 1 0` queues two shares that both ask
for a save in one frame (the guest and the host's own id 0, which is in the session, as the stand-in
for a second guest: a third game needs a second lane). FAIL without the fix: 2 saves posted; PASS:
1 save, and the guest still gets the world and plays in it. Lane 3, 2026-10-28, PASS: lab runs 20261027-230646 and the branch's smoke run. FAIL runs: `coop_world_queue.py --without share_one_save,share_drop_peer,world_reset` and `--without load_in_editor` (outputs kept in the wave's scratch folder, named in the branch report).

The planned test was:

A new `tools/tests/coop_multi_share.py` on top of `twoinst.start` plus `coop_multi.start_extra` (3 players).
- Case 1: the host plays and evolves (or `coop.share` with no id), so both guests need the world.
- Case 2: guest C joins 3 s after guest B.
- Assert on the host: the number of saves posted (a counter in `world::status()`) is 1, and the host's frames keep running (no stall over 2 s, without pressing Enter).
- Assert on both guests: the received world loads and the guest reaches its avatar.
- It must FAIL on main (2 saves counted, or a stalled host) and PASS with the fix. Add it to a lab suite so the 3-player share path runs at all.

### Related
#13, #65 (other world-share slot problems), #21 (votes, the other host-side flow that runs at a stage change). Not a duplicate of any of them.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #91.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: one save for every share that waits at the same time (coop_world_queue).
- 2026-10-28: released in 0.7.3.0.
