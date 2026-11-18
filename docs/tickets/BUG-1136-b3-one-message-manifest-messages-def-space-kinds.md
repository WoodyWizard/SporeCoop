---
id: BUG-1136
title: B3: one message manifest (messages.def, space_kinds.def) and a host-side direction gate
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-manifest
commits: 911cbf15, 5389d944, e8f08925, bd4fd00b
tests: validate_tests test_manifest; net_tests test_dir_gate (--off dir_gate fails); tools/msg_manifest.py check / --self-test; lab compat
signature:
docs: docs/04-netcode.md#netcode-manifest, docs/01-architecture.md#arch-message-manifest, docs/06-testing.md#testing-manifest-compat
related: BUG-1137, BUG-1041, BUG-0701, BUG-0420, BUG-0144
---

## Report
Plan 0.7.15 B3 (audit NET-3, CORE-7): direction, class, owner and route of a message type lived in five places
(host_world.h msg_class, validate.h space_dir, the hand-written if chain of `g_sess.cb.custom` with silent id ranges,
about 360 per-handler role checks, a docs table listing 22 of 67 types). A new id inside a range went to that module
without anyone deciding it; a guest sending a host-only type was dropped only where the handler remembered to check.

## Root cause
No single description of the message layer.

## Fix
* `mods/coop/net/messages.def` (every top-level type once: id, name, channel, sender, class, route, receiving role,
  family) and `mods/coop/net/space_kinds.def` (every MSG_SPACE kind once, its feature and sender; feature ranges).
  Generated from them: `enum MsgType` (protocol.h), `host_world::msg_class`, `net::space_dir`, `net::msg_dir`,
  `net::dispatch_route` (session_glue.cpp's dispatch is now one switch over it), the docs tables of 04-netcode 4.3
  (`tools/msg_manifest.py docs`, checked in `make test`). The Space kind enums stay in the feature files (C6 works on
  them); validate_tests checks them against the rows and the registered ranges.
* Golden: `tests/manifest_golden.h` holds 0.7.14's enum values, msg_class, space_dir and the dispatch chain verbatim;
  the generated tables equal them for 0..255 and both roles (the wire values are unchanged). The world gate moved
  before every route; the test proves no route the chain tested before it takes a type the gate drops.
* Host-side direction gate (behaviour change, `coop.fix net.dir_gate`): a message of a type only the host sends,
  arriving at the host from a guest, is dropped in net::Session before any handler and counted (coop.status
  `manifest: dir_drops=N`, `coop.manifest`). A sender inventory of every type (each send site, its role) found no
  host-only type the host acted on: every handler ignored them already, so the gate drops nothing normal play sends.
  CIV_CMD and CELL_HIT turned out to be sent by both sides (host: specialty ask BUG-0982, Space planner BUG-0884;
  test command coop.pool hurt), DAMAGE too (friendly fire): they are Any.
* Test seam `coop.manifest send <type> [hex]` (a crafted message), `coop.manifest types`.

## Verification
- Unit: validate_tests test_manifest PASS; a mutated row (ROLE recv, SPECIES class, space kind 12 direction) fails it
  (checked by hand). net_tests test_dir_gate PASS, `--off dir_gate` FAIL (host-only types delivered 3, dropped 0).
- Lab `compat` (lane 10): PASS 20261115-031537, 20261115-032912, 20261115-034921 (after merging rel/0.7.15);
  SPORE_FIX_OFF=net.dir_gate FAIL 20261115-031637 ("dir_drops 0 -> 0").
- refactor_dump before 20261115-025133 / after 20261115-032912 (`tools/refactor_dump.py compare`): lifecycle, players,
  guards, hooks identical on both games; fix list +2 (net.dir_gate, net.family_check), commands +1 (coop.manifest);
  source.msgtable differs as intended (the if chain became `case net::Route::X`, each MSG_ name's count one lower
  where the chain named it), source.manifest new. The routing itself is proven equal by the golden unit test.
- play_creature --quick PASS 20261115-032912; make test PASS.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-manifest (manifest, golden tests, direction gate, docs tables).
- 2026-11-18: released in 0.7.15.
