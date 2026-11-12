#!/usr/bin/env python3
"""BUG-0823: in the frame of a level rebuild the host's own cell is never taken for a partner's cell (Cell, host).

cellsync::puppet_cell returned the remote table's cell of a partner. The own cell's level rebuild (the host's death
without a helper) frees every cell and makes the own avatar first, often at the address a puppet had (BUG-0342);
the remote loop drops that puppet only after cellsync::tick and the frame's messages, so in that frame the host's own
cell was the partner's: a knocked-out partner's look (opacity 0.35) went onto the host's own cell, a guest's bite
had the host's cell as the attacker, the puppet's poison step ran for it.

Two games on the cell fixture. Each round: the guest swims away and the host knocks it out (`coop.pool hurt`), then
the host dies without a helper (`coop.pool selfhit 50` until its game rebuilds the level: a new own cell). While the
guest is out, the host's own cell must never get the knockout look (`coop.pool selfstate`: its opacity) and no game
faults. The rebuilt cell lands on the puppet's address in some rounds only (BUG-0342: about a third).
SPORE_FIX_OFF=cell.puppet_cell_index must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_cell_puppet_cell.py [--rounds 6]"""
import argparse
import re
import time

from coop_cell_ko_food import pool, wait_downed
from twoinst import Checks, start


def selfstate(con):
    out = con.call('coop.pool selfstate')['out']
    m = re.search(r'cell=(\S+) opacity=([-\d.]+) hp=(-?\d+) table_names_it=(\d) refused=(\d+)', out)
    return (m.group(1), float(m.group(2)), int(m.group(3)), m.group(4) == '1', int(m.group(5))) if m else None


def puppet_cell(con):
    m = re.search(r'remote 1 .*?puppet=([0-9A-F]{8})', con.call('coop.status')['out'])
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=6)
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    t0 = time.time()
    while time.time() - t0 < 30:
        m = re.search(r'mirrors=(\d+)', guest.call('coop.pool')['out'])
        if m and int(m.group(1)) > 10:
            break
        time.sleep(1)
    gm = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gm.group(1) if gm else '1'
    looks, rebuilds, at_puppet = [], 0, 0
    for k in range(a.rounds):
        for _ in range(3):
            guest.call('coop.pool tp 30 0')
            time.sleep(1.0)
        print('  host: ' + host.call(f'coop.pool hurt {gid} 50')['out'].strip(), flush=True)
        if not wait_downed(guest, 1, 4):
            print(f'  round {k + 1}: the guest was not knocked out', flush=True)
            time.sleep(3)
            continue
        old_pup = puppet_cell(host)
        s0 = selfstate(host)
        cell = s0 and s0[0]
        t1 = time.time()
        low = 1.0
        rebuilt = False
        while time.time() - t1 < 8 and pool(guest)['downed'] == 1:
            st = selfstate(host)
            if st and st[0] != cell and not rebuilt:
                rebuilt = True
                rebuilds += 1
                at_puppet += st[0] == old_pup
                print(f'  round {k + 1}: the host\'s level rebuilt: own cell {cell} -> {st[0]} (the guest\'s puppet was '
                      f'{old_pup})', flush=True)
            if st and not rebuilt and st[2] > 0:
                host.call('coop.pool selfhit 50')
            if st:
                low = min(low, st[1])
            time.sleep(0.2)
        print(f'  round {k + 1}: host own cell lowest opacity {low:.2f}; {selfstate(host)}', flush=True)
        if low < 0.5:
            looks.append(k + 1)
        t2 = time.time()
        while time.time() - t2 < 14 and pool(guest)['downed'] == 1:
            time.sleep(0.5)
        time.sleep(4)
    print(f'  rebuilds {rebuilds}, of them at the guest\'s puppet address {at_puppet}', flush=True)
    ck(rebuilds >= 1, f'the host\'s level was rebuilt while the guest was out ({rebuilds} times)')
    ck(not looks, f'the host\'s own cell never got the knockout look (rounds: {looks})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
