---
id: BUG-1282
title: WP-F2: Creature meteors, rain storms and the night predator run separately in each game (fork BUG-9131)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9131
role: guest
versions: 0.7.15.2
fixed_in: unreleased
branch: fix/w56-sync
commits: cdfe214d, 618fbd6e
tests: drama_shared, drama_shared_off
signature:
docs: docs/re/creature.md#creature-nest-alpha, docs/re/creature.md#creature-drama-events
related: BUG-0486, BUG-0558, BUG-0465
---

## Report
Found by the Lineages fork (PR #196), its BUG-9131 (out/research/lineages-bugs.md row 3), checked against our code:
drama.cpp held only the UFO (kind 2) on a guest (BUG-0486). Each game's Drama Manager ran its own night predator (1),
meteor strike (3) and rain storm (5): a guest saw meteors and rain the host never had and its own night predator (a
creature only its game had); the host's meteors and rain never came to the guest. The fork's "the host's UFO never on a
guest" does not hold for us: BUG-0558 shows the host's ship on the guest.

## Root cause
Observed (code): `g_mask = 1u << 2` and h_Start refusing only masked kinds on a guest; nothing relayed from the host.

CORRECTION (2026-11-19, fix/w57-sync, lab run 20261119-104136): the gate never opened on a guest. `host_sends_drama`
(player_rules.cpp) and npc.cpp `host_sends_alpha` asked `Session::peer_families(0, ...)`, which answers only on the
host (a guest's HELLO list), so on a guest it was always false ("host_sends=0" against a 0.7.16 host): the guest kept
its own meteor / rain and refused nothing. Fix: `Session::host_families` (the host's WELCOME list on a guest) feeds both
gates (docs/04-netcode.md#netcode-host-families); net_tests `test_host_families`.

## Fix
`coop.fix creature.shared_events` (drama.cpp): the host's start of kinds 3 and 5 in Creature goes to the guests as
MSG_PLAYER_RULE kind K_DRAMA (11, reliable, player_rules.cpp drama_out); the guest's game starts the same kind with the
game's StartKind (drama::from_host). A guest's own start of kinds 1, 3, 5 in Creature is refused while its host relays
(the host's npc family minor >= 2, i.e. 0.7.16: net/compat.h kNpcMinorWorldEvents; a 0.7.15.2 host: the guest keeps its own events, as before). A 0.7.15.2
guest ignores the kind. Not done: a guest that joins during a running host event does not get it; whether the host's
night predator creature is mirrored by the NPC sync is not checked.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Unit: net_tests `test_host_families` PASS (a 0.7.16 host passes the npc 1.2 gate on the guest, an npc 1.1 host and one
  without revisions do not; `peer_families(0)` stays false on a guest).
- Lab, first night run (rel/0.7.16 c7699dca, run dirs in the rel/0.7.16 worktree): drama_shared FAIL
  out/lab/20261119-104136 ("the host relays its drama events": host_sends=0; the guest's own meteor and rain active):
  the gate bug above.
- Lab after the fix (fix/w57-sync 6fdea267, run dirs in the fix/w57-sync worktree): PASS drama_shared
  out/lab/20261119-111559 (the guest refuses its own night predator, meteor and rain; the host's meteor and rain run in
  the guest's game). FAIL twin drama_shared_off (SPORE_FIX_OFF=creature.shared_events) out/lab/20261119-112631 for the
  right reason: the guest's own meteor and rain are active.
- Rerun on the tree merged with rel/0.7.16 (fix/w57-sync 3801b9f5): PASS drama_shared out/lab/20261119-121505.

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
- 2026-11-19: fixed (fix/w57-sync): see the CORRECTION under Root cause; PASS and FAIL twin run dirs above.
