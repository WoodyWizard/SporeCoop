---
id: BUG-1149
title: Space: a guest's own progress (colonies, badges) is lost between sessions: the host keeps it in memory only
status: fixed
kind: bug
stage: Space
reported: 2026-11-15
source: discord
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-store
commits: 1bb53ffb, f5e9e809, f20b267f
tests: space_session, store_tests (make test)
signature:
docs: docs/01-architecture.md#arch-player-store, docs/06-testing.md#testing-session-restart
related: BUG-1107, BUG-1109, BUG-1135, BUG-1150, BUG-1151, BUG-1152, BUG-1153, BUG-1154, BUG-0195, BUG-0786
switch: store.space_colonies, store.space_badges
---

## Report
Discord (bug question, 2026-11-15): when two players play Space over several sessions, what the guest did while the
host hosted is more or less lost each time they reopen the game.

## Root cause
Observed in code and docs: a guest always loads the host's share save at join; its own colonies and badges are kept
by the host in memory only (space_kept.h KeptStore, space_map.cpp, space_badges.cpp; docs/re/space.md
#space-kept-key), for a rejoin within the running host session. Nothing of the guest's own Space state is written
into the host's save (the allies mirror is undone before every save) and the guest keeps only its cargo hold on its
own disk (BUG-0679). When the host quits, the kept state is gone. Ship upgrades and claimed planets: not checked.

## Fix
Owner decision 13 (wave E, E1 of docs/plan-0.7.15.md): a per-player store on the HOST's disk next to its save
(docs/01-architecture.md#arch-player-store).
* player_store.h (portable, tests/store_tests.cpp): one file per world `<Spore>/Games-sporecoop-store/<slot>/<planet>.txt`
  (beside the Games folder: a world share sends every file of the slot, so never inside it), versioned records per
  feature and per player, a check sum, unknown records kept; written atomically (.tmp + MoveFileEx) after every save of
  the game (the save hook, outside the frame), read when the host's world opens (store.cpp, `coop.store`).
* Player key: a salted digest of net::Session::player_key (the rejoin token); the name is never a key. A player with a
  new token is a new player; the host is told and may hand over by its word (`coop.store adopt <player>`).
* Space consumers through the KeptStore (space_kept.h restore / rekey, kept_disk.h): the guest's own colonies
  (`space.colonies`) and badges / rank (`space.badges`) come back in the next session exactly as after a rejoin.
  Switches `coop.fix store.space_colonies off`, `coop.fix store.space_badges off`: memory only (0.7.14).
Related parts with their own tickets: BUG-1150 (the token's 30 days), BUG-1151 (colony cities, NPC stars), BUG-1152
(Rivals), BUG-1153 (Civ owners), BUG-1154 (Tribe owners).

## Verification
`coop_space_session.py` (suite space_session, fixture space): colony, badges, Rivals budget and war, then
twoinst.session_restart() (save, both quit, the host loads its save, the guest joins), the guest's game checked.
* FAIL (SPORE_FIX_OFF=store.space_colonies): out/lab/20261115-125125 (the planet is tech 1, no colony of the guest)
* FAIL (SPORE_FIX_OFF=store.space_badges): out/lab/20261115-125402 (the earned badge missing)
* PASS: out/lab/20261115-124902, out/lab/20261115-130851, out/lab/20261115-133047, out/lab/20261115-134708
* unit: store_tests (file format, damaged / foreign files, keys, the name fallback only by adopt, KeptStore restore).
Not run in the lab: `coop.store adopt` (unit tested); a host whose store file is damaged (unit tested: empty store,
moved to .bad).

* PASS post-merge of E3 / E4 (rel/0.7.15 at 94118b08): out/lab/20261115-141219, out/lab/20261115-141234 (space_session).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-store (E1): the per-player store on the host's disk, Space colonies and badges.
- 2026-11-18: released in 0.7.15.
