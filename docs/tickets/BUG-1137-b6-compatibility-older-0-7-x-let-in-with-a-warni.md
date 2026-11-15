---
id: BUG-1137
title: B6: compatibility: older 0.7.x let in with a warning, message family revisions refuse an explicit conflict
status: open
kind: bug
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-manifest
commits:
tests: net_tests test_families_wire, test_family_check (--off family_check fails); validate_tests test_compat; msg_manifest.py --self-test; lab compat, mixed_plant_guest, mixed_plant_host, mixed_plant_unused, mixed_version_guest, mixed_version_host (v0.7.14)
signature:
docs: docs/04-netcode.md#netcode-compat, docs/06-testing.md#testing-manifest-compat
related: BUG-1136, BUG-0219, BUG-1005
---

## Report
Plan 0.7.15 B6 (owner decision 7, audit NET-6): any older 0.7.x is let in with a warning on both sides unless there
is an explicit conflict; each message family has a revision; a mismatch in a family the current stage uses refuses
the join with a text naming both versions; both versions in problem reports.

## Root cause
Behaviour before (0.7.14): net::Session's HELLO handler refused only `h.proto != kProtoVersion` (7; REJ_VERSION
"protocol version mismatch") and another exe build (REJ_BUILD); any other SporeCoop of protocol 7 joined. Both games
logged "versions differ" and fed "X has another SporeCoop version (V): update both games to the same one" when the
coop part differed; a peer before 0.7.4 (no caps) got "X has an older SporeCoop: update both games to the latest".
Nothing could tell where two versions' messages really differ.

## Fix
* messages.def FAMILY rows (session, world, creature, npc, cell, tribe, civ, space, shared, creations; rev 1.0 each),
  each with the stages that use it and wire files; `mods/coop/net/messages.lock` holds each family's hash (its rows,
  for space also the kinds, and the comment-stripped code of its wire files: session = net/protocol.h);
  `tools/msg_manifest.py check` in `make test` fails a changed family without a bumped rev / minor (`lock --rebase
  FAMILY "reason"` for a byte-identical refactoring).
* CAP_FAMILIES (caps bit 3) in HELLO and WELCOME: `u8 n, n x {u8 family, u16 rev, u16 minor}` after CAP_VERSION's
  payload. 0.7.4..0.7.14 readers ignore it (unknown bit, trailing bytes: v0.7.14's Hello::read copied verbatim into
  net_tests reads it); a peer without it counts as rev 1 of the ten base families.
* Behaviour after (`coop.fix net.family_check`, net/compat.h): the host refuses a guest whose rev differs in a family
  its current stage uses, REJ_VERSION with "Alice plays SporeCoop X; you have SporeCoop Y: the Creature messages of
  these versions do not fit together (revision 2 there, 1 here). Update both games to the same version to play
  together" (every 0.7.x guest shows a REJECT text); the host feeds "Bob (SporeCoop Y) was not let in: ...". A guest
  refuses an older host itself (always-used families at WELCOME, stage families at the host's first state). A later
  stage change into a differing family warns both games (the guest by NOTICE). Otherwise another version joins with
  "<player> plays SporeCoop 0.7.13; you have SporeCoop 0.7.15: some things may not match" on both sides (a 0.7.14 peer
  shows its own old text). Protocol and build checks unchanged; no check of malformed data was loosened (a cut or
  overlong family list leaves the HELLO readable without families, kMaxFamilies 64).
* Versions and families of every player: coop.status `versions:` lines (also in each report's state) and the
  session report's SESSION.txt "Versions" block. Test seam `coop.manifest plant <family> <rev>` / `reset`.

## Verification
- Unit: net_tests test_family_check PASS, `--off family_check` FAIL (8 checks); test_families_wire PASS;
  validate_tests test_compat PASS; fuzz_decoders with families PASS.
- Lab `compat` (lane 10): PASS 20261115-031537; SPORE_FIX_OFF=net.family_check FAIL 20261115-031742 (the planted
  Creature conflict joined).
- Mixed against v0.7.14 (lanes 10 + 9): see History.

## History
- 2026-11-15: opened.
