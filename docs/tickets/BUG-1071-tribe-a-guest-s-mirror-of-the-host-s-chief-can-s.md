---
id: BUG-1071
title: Tribe: a guest's mirror of the host's chief can stay in a returned gesture for 9 s right after the session start (relayed animation 0; 1 of ~12 tribe_mirror_stale rounds)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-13
source: fix/w49-mirror, lab 20261113-054031-2 lane 7
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-tribe3
commits:
tests: tribe_mirror_stale
signature:
docs: docs/re/tribe.md#tribe-chief-load-gesture
related: BUG-0972, BUG-0936, BUG-1069
---

## Report
tribe_mirror_stale on fix/w49-mirror (BUG-0972's fix on), out/lab/20261113-054031-2 round 1: the first round of the
session (the mirror's last relayed animation still 0), `coop.tribe mplay 1 04C6CB02` while the host's chief stood: the
mirror stayed in the gesture for the 9 s of the round (the other 11 rounds of the branch's runs: back within 0.7-2.1 s).
Not analysed. Candidates: the forced idle refresh after the pending tracks are ended picks the gesture again on this
mirror (its own save's chief), or the host's idle news (kind 10) for the chief did not come then.
Reproduce: `lab.py run tribe_mirror_stale --repeat 3`; read `coop.tribe holdtrace 1` (Z / I events) on the guest.

## Root cause
Observed (lab 20261113-060910 round 1, the test now prints the host's chief's `coop.tribe anims` row): the host's chief
stood in its idle 0x02481DE5 with `bhv=00000000` (its behaviour tree had started nothing since the host began tracking:
the save's chief came out of its load gesture by itself) and the mirror's last relayed animation was 0. The host's
once-a-second animation news (send_member_anims) read a member's current animation only when bhv_anim was set:
`if (m.bhv_anim && ... drawn_state(c, &cur) ...)`, so cur stayed 0 for this member and no idle news (kind 10) went
for it. Without kind 10 the BUG-0972 stale cut never ran; the gesture played to its own end (6.4 s in this run, 9 s in
20261113-054031-2). Later rounds had a behaviour start (bhv set) and were cut in 0.3-2.1 s.
Inferred (code): a member whose behaviour start is its locomotion's idle / walk with cur == bhv_anim went as kind 3,
which starts the idle like a relayed start (queued behind a gesture, no cut): the same miss; covered by the same fix.

## Fix
`tribe.mirror_idle_loco` (tribe.cpp send_member_anims): the host reads every member's current animation for the
idle news, also with no behaviour start yet, and a member in its locomotion's idle / walk always goes as idle news
(kind 10), never as kind 3. Off: as before. The test prints the host's chief's animation and bhv at each round and the
guest's hold trace of a round that never came back.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
- 2026-11-13: cause found and fixed on fix/w49-tribe3.
