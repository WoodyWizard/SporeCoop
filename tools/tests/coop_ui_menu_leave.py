#!/usr/bin/env python3
"""The galaxy menu's "Co-op" item is not touched after the game left the menu (BUG-0163): the item
belongs to the menu window, which the game frees; the mod kept the pointer until its next refresh
(up to 1 s) and a click meanwhile made a virtual call on the freed item.

One game of the lane: the menu with the item, a load from the menu (coop.load), clicks on the
item's place while the game leaves the menu; the item pointer must be gone within two frames of
the mode change, and the game must not fault.

  SPORE_LANE=2 python3 tools/tests/coop_ui_menu_leave.py [--without-fix]
  (--without-fix: coop.fix core.menu_drop off, the test must FAIL)"""
import re
import sys
import time

import fixtures
import lane
from diag_check import start_host
from twoinst import Checks


def main():
    without = '--without-fix' in sys.argv
    ck = Checks()
    lane.sh('kill')
    lane.sh('fixture', 'restore', 'creature', lane.HOST_PROFILE)
    c = start_host(ck)
    print(' ', c.call(f'coop.fix core.menu_drop {"off" if without else "on"}')['out'].strip().replace('\n', ', '))
    st = ''
    for _ in range(20):
        st = c.call('coop.uistate')['out']
        if 'menu_button=yes' in st:
            break
        time.sleep(1)
    ck('menu_button=yes' in st, 'the galaxy menu has the Co-op item')
    m = re.search(r'^menu (\d+) (\d+) (\d+) (\d+)', st, re.M)
    x, y = ((int(m.group(1)) + int(m.group(3))) // 2, (int(m.group(2)) + int(m.group(4))) // 2) if m else (0, 0)
    slot, name = fixtures.planet_of('creature').split('/')
    print(' ', c.call(f'coop.load {slot} {name}')['out'].strip())
    # the first answers after the game left the menu: the item must already be gone
    seen = []
    t0 = time.time()
    while time.time() - t0 < 30 and len(seen) < 5:
        st = c.call('coop.uistate')['out'].splitlines()[0]
        if 'in_menu=0' in st:
            seen.append(st)
            if m:
                c.call(f'click {x} {y}')  # a click on the item's place while the game leaves the menu
        time.sleep(0.02)
    for s_ in seen:
        print('  after the mode change:', s_)
    ck(seen, 'the game left the galaxy menu')
    ck(seen and all('menu_button=no' in s_ for s_ in seen),
       'the item pointer is dropped as soon as the game left the menu')
    for _ in range(120):  # the load goes on without a fault
        if c.call('coop.avatar')['ok']:
            break
        time.sleep(2)
    c.call('coop.fix core.menu_drop on')
    ck.faults(host=c)
    lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
