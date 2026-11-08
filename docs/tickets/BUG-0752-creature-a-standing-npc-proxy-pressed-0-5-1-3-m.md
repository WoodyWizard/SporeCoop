---
id: BUG-0752
title: Creature: a standing NPC proxy pressed 0.5-1.3 m into the ground after the host's evolution editor stays there for over a minute (campaign_ct monitor gate proxies)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-08
source: rel/0.7.11 fa4f5df3 labs 20261108-072547, 20261108-075905-4
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-k4-ground
commits: 6cd28ceb
tests: campaign_ct
signature: npc: proxy N (species ...) 0.5-1.3 m under this game's ground ... (driven), made 1-2 s ago at 0.00 m over the ground
docs: docs/re/creature.md#creature-proxy-ground
related:
---

## Report
campaign_ct on rel/0.7.11 fa4f5df3 failed 2 of 3 on the monitor gate `proxies: guest: 1 NPC proxies under the ground
(Creature) on 2 lines from s 430/440` (out/lab/20261108-072547 lane 4, 20261108-075905-4 lane 6; passed 20261108-075905
lane 7), right after the host's evolution editor.

## Root cause
Observed: docs/re/creature.md#creature-proxy-ground (new proxies of a wild herd made on the ground after the host's
editor, under it 1-2 s later, one standing 0.52-0.55 m under for over 70 s). The Tribe case BUG-0399 in Creature: the
standing proxy's `ground` rule of drive::step was Tribe only. Not a K4 / K2b / K7 switch (campaign_ct 3 of 3 with
SPORE_FIX_OFF=drive.lead_fall and 3 of 3 with all on, lab 20261108-081330 / -2). Why it began to show on rel/0.7.11 is not
known.

## Fix
`coop.fix npc.proxy_ground_creature` (npc.cpp): a standing proxy in the Creature stage gets drive::step's `ground` (more
than 0.25 m under this game's terrain: put onto it, the target never under it), as Tribe proxies have had since BUG-0399.
Off: Tribe only (0.7.10).

## Verification
- FAIL: out/lab/20261108-072547 and 20261108-075905-4 of the rel/0.7.11 worktree (fa4f5df3, the code without the fix: the
  switch's off side) failed the monitor's proxies gate. The same build passed 6 of 6 on lanes 6 / 7 (lab 20261108-081330 / -2):
  the event is rare there; `coop.npc sink` does not reproduce it (back on the ground in 0.25 s, fix on or off,
  out/lab/20261108-085102 / -2).
- PASS: out/lab/20261108-085348 (campaign_ct 3 of 3, lane 7) and -085348-2 (lane 6: #1, #3 OK; #2 failed only the sync-check
  gate at the host's share save, BUG-0754, its proxies gate clean). The fix acted in both labs: `npc: proxy N ... was
  0.25-0.30 m under this game's ground, made 0.2-0.5 s ago: put onto it (npc.proxy_ground)` on three games of -085348-2 #2
  right after the host's editor, and in -085348 #1; no `proxies` violation in 6 runs.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k4-ground): cause found (the Tribe BUG-0399 case in Creature), fixed (npc.proxy_ground_creature).
