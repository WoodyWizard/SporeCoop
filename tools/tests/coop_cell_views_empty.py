#!/usr/bin/env python3
"""BUG-0999: Cell: the host's partner tile pass looped over an empty view set.

Audit 0.7.14 (WLD-3). The host's h_PopTiles builds its view set from its own view box centre and the
partners' cells; a centre that views::finite_pos refuses (not finite, or |x| = 1e7: view_bad lets a box
with both edges at exactly 1e7 through, also with cell.nan_guard on) left the set empty, and the partner
pass computed `np = size() - 1` = SIZE_MAX and read both lists past their end (a fault, or a hang, on the
host).

Two games in the Cell fixture, the guest 30 m from the host (a partner list of tiles). The host's own view
box is put at the 1e7 edge for 30 of its tile passes (`coop.pool viewedge 30`; the game's box comes back
after each pass). Checked: both games answer and fault nowhere, the passes were skipped
(`coop.pool tilestat` views_empty_skips), and the guest's game still has the host's cells round it after it
(`coop.pool mirrors`).

  coop_cell_views_empty.py   (SPORE_FIX_OFF=cell.views_empty: the host faults or hangs, it FAILS)
"""
import re
import time

from twoinst import Checks, start


def stat(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', con.call('coop.pool tilestat')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    guest.call('coop.pool tp 30 0')
    time.sleep(6)
    s0 = stat(host, 'views_empty_skips')
    print('  host: ' + host.call('coop.pool tilestat')['out'].strip(), flush=True)
    print('  host: ' + host.call('coop.pool viewedge 30')['out'].strip(), flush=True)
    time.sleep(4)
    out = host.call('coop.pool tilestat')['out'].strip()
    print('  host: ' + out, flush=True)
    s1 = stat(host, 'views_empty_skips')
    ck(s1 > s0, f'the host skipped its partner pass while its view centre was not usable ({s0} -> {s1})')
    time.sleep(3)
    mirrors = guest.call('coop.pool mirrors')['out'].count('mirror_local')
    ck(mirrors >= 10, f'the guest\'s game still shows the host\'s cells round it ({mirrors} mirrors)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
