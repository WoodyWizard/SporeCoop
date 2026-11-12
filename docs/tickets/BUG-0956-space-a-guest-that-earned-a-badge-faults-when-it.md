---
id: BUG-0956
title: Space: a guest that earned a badge faults when it loads the host's world at a rejoin from the same game (a call into freed memory)
status: open
kind: crash
stage: Space
reported: 2026-11-12
source: lab, fix/w48-reports (coop_space_badges_rejoin.py)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature: guest, Space rejoin load: 0xC0000005 / 0xC0000096 / 0xC000001D at a changing address 0x18..0x20 million (not the exe), edx=0x00420065, ebp=01414BA0, 4 ms after "world: loading Madas.spo from ...Game2"
docs:
related: BUG-0786, BUG-0678
---

## Report
Lab, fix/w48-reports (coop_space_badges_rejoin.py, 2026-11-12, lane 4, `space` fixture). The guest earns a badge (by the
game's own Update after `coop.space badges event 2 60`, or by GiveBadge from the console), leaves the session and
joins again from the same running game; the host shares its world; the guest quits to the galaxy menu ("leaving the
stage to the galaxy menu before loading Madas.spo"), loads the received world from slot Game2 and faults 4 ms after
the load starts: `exception 0xC0000005 at 0x1F7441E4 (data 0x00420065)`, other runs 0xC0000096 (privileged
instruction) / 0xC000001D (illegal instruction) at other addresses in the 0x18000000..0x20400000 range, always
edx=0x00420065 and ebp=01414BA0: the game executes or calls through freed memory. The game dies without a crash
report. Seen in 9 of 10 such runs, with `coop.fix space.keep_badges` on and off (runs 20261112-041609, -042344,
-042734, -043458, -044420, -044524, -044626); one run without the fault (20261112-042130).
Not seen: the same guest closed and started again before it joins (the test's default since then, 6 runs); a host
that earned a badge and loads its own save from the galaxy menu (coop.menu + coop.load, lane 5); the hold test
space_items_rejoin (no badge earned).

## Root cause
Not found. Inferred (low confidence): an object made when the badge was earned (the badge's caption / card UI, the
event log entry, a listener of message 0x38CF2FC "badge earned") outlives the Space world the guest leaves and is
called during the next load. Not the mod's badge module (space.keep_badges off: the same fault).

## Fix
Not yet. Next: a crash report (the game dies before SporeKit writes one: check why), `stack.here` / a probe at the load
start; try the same without co-op (a solo game: earn a badge, quit to the galaxy menu, load a save of another slot).

## Verification
Reproduce: `BADGES_RESTART=0 SPORE_DETECTOR=20 python3 tools/lab.py run space_badges_rejoin --lane 4 --retry 0`.

## History
- 2026-11-12: opened (fix/w48-reports, met while testing BUG-0786).
