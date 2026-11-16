#!/usr/bin/env python3
"""Own cells: the guest chooses another cell (a premade one: the test profiles have no own cells;
the transfer itself is covered by coop_creations.py), plays it after the level rebuild, and each
player sees the other's cell."""
import re
import time

from harness import field
from twoinst import Checks, shot, start

OTHER_CELL = 'BAF1AD40'  # a premade player cell in Spore_Content


def main():
    host, guest = start('cell')
    ck = Checks()
    print(' ', guest.call(f'coop.creations choose cell {OTHER_CELL}')['out'])
    t0 = time.time()
    while time.time() - t0 < 40:
        gc = guest.call('coop.creations')['out']
        if field(gc, r'avatar model ([0-9A-F]{8})') == OTHER_CELL:
            break
        time.sleep(2)
    time.sleep(12)  # the level rebuild hatches the cell again
    hc, gc = host.call('coop.creations')['out'], guest.call('coop.creations')['out']
    print('  host :', hc.replace('\n', ' | '))
    print('  guest:', gc.replace('\n', ' | '))
    ck(field(gc, r'player model ([0-9A-F]{8})') == OTHER_CELL, 'the guest player cell is its own')
    ck(field(gc, r'avatar model ([0-9A-F]{8})') == OTHER_CELL, 'the guest plays it')
    host_cell = field(hc, r'avatar model ([0-9A-F]{8})')
    hs, gs = host.call('coop.status')['out'], guest.call('coop.status')['out']
    hp = field(hs, r"remote 1 'Bob'.*\n.*species=([0-9A-F]{8})")
    gp = field(gs, r"remote 0 'Alice'.*\n.*species=([0-9A-F]{8})")
    print(f'  host cell {host_cell}; host shows Bob as {hp}, guest shows Alice as {gp}')
    ck(hp == OTHER_CELL, 'the host shows the guest with its cell')
    ck(gp == host_cell, 'the guest shows the host with the host cell')
    gm = re.search(r'mirrors=(\d+)', guest.call('coop.pool')['out'])
    ck(gm and int(gm.group(1)) > 5, f'the pool is mirrored again after the rebuild ({gm.group(1) if gm else None})')
    shot(host, 'creations_cell_host.png')
    shot(guest, 'creations_cell_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
