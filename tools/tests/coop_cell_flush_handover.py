#!/usr/bin/env python3
"""BUG-1000: Cell: the host's partner tile bookkeeping on a flush.

Audit 0.7.14 (WLD-21). On a level change (or a rebuild, or a short tile pool) the host flushes every
partner's tile list in one loop; each deleted tile that another list shadowed was handed over to that
list (its cells made in the HOST's game), often to the list the same loop flushed next: cells made for a
tile deleted a moment later. And the between-frames tile check ran before the cleanup of the lists a
rebuild freed: a false "dead entries" warning after every rebuild, using up the 20 the check logs.

Three games in the Cell fixture: two guests 30 m and 42 m from the host on one side (their tile areas
overlap: the shared tiles are populated in one list, a shadow in the other). The host changes the
shared food by +150 / -150 three times (level changes: a flush of both lists each). Checked in the
host's game (`coop.pool tilestat`): flushes happened, no tile was handed to a list flushed in the same
pass, and the check logged no between-frames warning after a rebuild; the guests' games still show
the host's cells round them.

  coop_cell_flush_handover.py   (SPORE_FIX_OFF=cell.flush_handover: hand-overs into flushed lists, it FAILS)
"""
import re
import time

from party import Party
from twoinst import Checks


def stat(p):
    out = p.call('coop.pool tilestat')
    return {k: int(v) for k, v in re.findall(r'\b(\w+)=(\d+)', out)}, out.strip()


def main():
    party = Party.start('cell', players=3)
    ck = Checks()
    host, bob, carol = party.host, party.guests[0], party.guests[1]
    time.sleep(3)
    for pl in party.all:
        pl.call('coop.pool invulnfor 600')
    bob.call('coop.pool tp 30 0')
    carol.call('coop.pool tp 42 0')
    time.sleep(10)
    s0, out = stat(host)
    print('  host before: ' + out, flush=True)
    for i in range(4):
        host.call(f'coop.pool food {150 if i % 2 == 0 else -150}')
        time.sleep(6)
        s, out = stat(host)
        print(f'  host after change {i + 1}: ' + out, flush=True)
    d = {k: s.get(k, 0) - s0.get(k, 0) for k in s}
    ck(d.get('flushes', 0) > 0 and s.get('lists', 0) >= 2,
       f'the host flushed its partner lists on the level changes ({d.get("flushes", 0)} flushes, {s.get("lists", 0)} lists)')
    ck(d.get('handed_flushed', 0) == 0,
       f'no tile was handed to a partner list flushed in the same pass ({d.get("handed_flushed", 0)}; {d.get("handed", 0)} hand-overs in all)')
    ck(d.get('between_warns', 0) == 0 or d.get('resets', 0) == 0,
       f'no false between-frames warning after a rebuild ({d.get("between_warns", 0)} warnings, {d.get("resets", 0)} list resets)')
    time.sleep(4)
    for g in (bob, carol):
        n = g.call('coop.pool mirrors').count('mirror_local')
        ck(n >= 10, f'{g.name}\'s game shows the host\'s cells round it ({n} mirrors)')
    ck.faults(**{pl.name: pl.con for pl in party.all})
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
