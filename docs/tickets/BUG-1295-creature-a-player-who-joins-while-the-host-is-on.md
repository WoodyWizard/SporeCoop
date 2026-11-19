---
id: BUG-1295
title: Creature: a player who joins while the host is on its death screen arrives dead (fork BUG-9215)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9215
role: guest
versions: 0.7.15.2
fixed_in: unreleased
branch: fix/w56-general
commits: 4f96bbb2
tests: join_host_dead
signature:
docs:
related:
switch: world.join_after_respawn
---

## Report
Found by the Lineages fork (PR #196), its BUG-9215 (its runs: FAIL before, PASS 15 of 15 after): a player who joins
while the host sits on its death screen loads the world at the host's spot with 0 health and sees "You were killed in
battle". Ours: the join path shares at once when the host plays (session_glue.cpp player_join: world::share_with with
a save first), with no dead check anywhere in session_glue.cpp, world.cpp or rejoin.cpp; not seen in our lab yet.

## Root cause
Inferred (the fork's analysis): the share's save is written while the host's avatar is dead and the joiner's creature
takes the avatar's place and state.

## Fix
world.cpp tick(): a share that saves first waits while the host's own creature is dead (the game's dead flag or 0
health, Creature stage) for 2 minutes at most from its ask; the host's feed says every 30 s "a player is joining:
respawn so they can enter your world"; `coop.status` "shares that waited for the host's respawn N". Switch
`coop.fix world.join_after_respawn` (off: the save at once).

## Verification
- Lab (first 0.7.16 night run, rel/0.7.16 c7699dca, run dirs in the rel/0.7.16 worktree's out/lab/):
  PASS join_host_dead, out/lab/20261119-104136 (no share save while the host is dead, the share held; after the
  respawn the guest arrives with hp 10, no death caption).
  FAIL twin (SPORE_FIX_OFF=world.join_after_respawn), out/lab/20261119-105127, for the right reason: the host's game
  posted a share save while dead, the guest arrived with hp 0.0 and the death caption.

## History
- 2026-11-19: opened.
- 2026-11-19: fix committed on fix/w56-general (WP-F1), lab runs pending; found by the Lineages fork (PR #196), its BUG-9215.
- 2026-11-19: fixed (fix/w57-sync): the first lab run passed, its twin failed for the right reason (run dirs above).
