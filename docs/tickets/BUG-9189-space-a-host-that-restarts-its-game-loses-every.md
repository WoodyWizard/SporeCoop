---
id: BUG-9189
title: Space: a host that restarts its game loses every guest's own colonies; the list is kept by player name
status: fixed
kind: bug
stage: space
reported: 2026-11-13
source: test
role: host
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 3d3d7d9f
tests: tools/qa_win (manual, TITAN host + 2 TITAN guests + the MSI, 2026-11-13)
signature:
docs:
related: BUG-0195 BUG-9135
switch: space.kept_side_file
---

## Report
Roadmap phase 6 step 2-3 (decision 3C A, 2A "Space progress per identity in a side file"): the survey found every Space per-player list in memory only and keyed by network id or name. The one that loses a player's own progress: the host's list of each guest's own colonies (BUG-0195), "Memory only: a host that restarts its game starts with no list" (space_map.cpp), by player name.

## Root cause
Observed: space_map.cpp g_kept, std::map<player name, Kept>, cleared on every new Space world and never written; docs/11-rivals.md#rivals-guest-colonies step 1 (the side file) was left open.

## Fix
space.kept_side_file (on): the list is keyed by the persistent player identity ("id:<16 hex>"; the name only for a player without one) and written (5 s after a change, outside the frame) to a side file next to the host's save, `<planet>.spo.duospace` (KPT1: u8 players, str8 key, u8 colonies, u32 planet ids); read when the host's Space world loads. founders::read_side / write_side are the generic side-file helpers (the .duoparty way). Off: memory only, by name.

## Verification
no-fail-run: manual, not a lab run. The four-founder Space save (DuoQAJiffSpace). Fourth founded its own colony (02460035 Athalky, colony tool, coop.space map colonize); the host kept it under id:DA5E9579D914D855 and wrote the file (30 bytes). PASS twice: the host's game killed and started again, the save loaded ("kept colonies of 1 player(s) read from this save's side file"), Fourth joined again: "Your colonies are back in your empire.", its roster lists Athalky as own. FAIL (switch off before the load): the list empty after the restart (kept 'Fourth' colonies=0), Fourth's roster without Athalky after its rejoin. Switch on and one more restart: back again. Fourth's network id changed from 2 to 1 between the runs and the identity key still matched. Total faults 0 on the games.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on feat/lineages-47 (decision 3C A).
