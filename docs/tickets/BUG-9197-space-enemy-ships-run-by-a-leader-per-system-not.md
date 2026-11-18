---
id: BUG-9197
title: Space: enemy ships run by a leader per system, not only where the host is
status: fixed
kind: feature
stage: space
reported: 2026-11-14
source: owner
role: both
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 59ca2bbc
tests: tools/qa_win (by hand: three TITAN QA games, coop.fleet spawn / shoot / hurt)
signature:
docs: docs/re/space.md#space-fleet-leader
related: BUG-0037 BUG-0190 BUG-9187
---

## Report
Roadmap phase 6, decision 1Z and 3D A (the owner: per-system enemy fleets): only the host's NPC ships were shared, and only with guests where the host was; two guests in a system without the host each saw their own natives (other ships, other fights).

## Root cause
Observed (code): squadron.cpp simulated and sent the NPC fleet on the host only (host_tick), a guest mirrored it only while `with_host` (the host in its place); elsewhere every game ran its own natives.

## Fix
fleet.leader (on): a leader per place (the host where it is, else the lowest seat there) runs the place's NPC ships; MSG_UFO_STATE carries the leader and the place in front and goes to the players of that place (a guest leader's through the host, which forwards it); MSG_UFO_HIT carries the leader (the host forwards a shot on a guest leader's ship with the shooter in front); MSG_UFO_DAMAGE carries the victim (a guest leader's through the host). A new leader keeps the ships it shows (its mirrors become its own). Status: `coop.status` line "fleet leader".

## Verification
Live, three TITAN QA games (2026-11-14, host DuoQAJiffSpace in its solar system, Fourth landed on the home planet, Late launched into Space through its History and the ship editor): PASS: Fourth led the planet, the host relayed its snapshots (387), Late mirrored Fourth's two spawned ships at the same places; Late's shot took ship #3 from 50 to 40 on both; Fourth's ship hit Late (130 -> 105); Fourth took off and Late took over with both ships kept (40/50 too). FAIL (`coop.fix fleet.leader off` on all three): Fourth's new ship was not on Late, and Late kept ships Fourth did not have. no-fail-run: not a lab run (TITAN QA games, by hand).

## History
- 2026-11-14: opened.
- 2026-11-14: fixed (the leader per place), live-tested with three games.
