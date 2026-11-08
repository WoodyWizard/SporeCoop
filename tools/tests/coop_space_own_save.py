#!/usr/bin/env python3
"""BUG-0709 (class BUG-0420): a guest that plays a Space save of its own in the session keeps its own money.

Space was never world-gated (host_world.h: the place is the star system), so the host's shared Space money total
was written into a guest's own Space save, and the guest's own earnings went to the host. The host now names its
save (the CRC of its name) in WELCOME also in Space, and a guest in another Space save holds the shared money.

  1. the usual session (fixture space), then the guest leaves and loads a Space save of its own: a copy of the
     fixture's slot under another planet name (coop.worldfix autoload 0), and joins again
  2. the host earns 5000 (coop.res add money 5000): the guest's game money stays its own for 10 s
  3. the guest earns 700 in its own save: the host's game money does not change

  SPORE_FIX_OFF=res.space_save coop_space_own_save.py   (step 2 or 3 FAILS)
"""
import glob
import os
import re
import shutil
import time

import lane
from twoinst import Checks, start


def money(con):
    m = re.search(r'shared money=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def connected(con):
    st = con.call('coop.status')['out'].lower()
    return 'client' in st or 'connected' in st


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    print(' ', guest.call('coop.leave')['out'].strip())
    guest.call('coop.worldfix autoload 0')
    games = os.path.join(lane.GUEST_DIR, 'Games')
    srcs = sorted(glob.glob(os.path.join(games, '*', 'Madas.spo')), key=os.path.getmtime)
    ck(srcs, 'the guest has the fixture save')
    if not srcs:
        return ck.result()
    src = os.path.dirname(srcs[-1])
    dst = os.path.join(games, 'Game9')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    if os.path.exists(os.path.join(dst, 'sporecoop.txt')):
        os.remove(os.path.join(dst, 'sporecoop.txt'))
    os.rename(os.path.join(dst, 'Madas.spo'), os.path.join(dst, 'Otherspace.spo'))
    print(' ', guest.call('coop.load Game9 Otherspace')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 150:
        time.sleep(3)
        if 'Space' in guest.call('game.mode')['out'] and money(guest) is not None:
            break
    guest.call('wait.frames 300 60000')
    time.sleep(5)
    print(' ', guest.call(lane.JOIN)['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 60 and not connected(guest):
        time.sleep(2)
    ck(connected(guest), 'the guest is in the session with its own save')
    time.sleep(8)
    g0, h0 = money(guest), money(host)
    print(f'  guest money {g0}, host money {h0}')
    print(' ', host.call('coop.res add money 5000')['out'].strip().replace('\n', ' | '))
    seen = set()
    t0 = time.time()
    while time.time() - t0 < 10:
        seen.add(money(guest))
        time.sleep(1)
    print(f'  guest money over 10 s after the host earned 5000: {sorted(x for x in seen if x is not None)}')
    ck(seen == {g0}, f"the guest's own save keeps its money ({sorted(x for x in seen if x is not None)}, was {g0})")
    h1 = money(host)
    print(' ', guest.call('coop.res add money 700')['out'].strip().replace('\n', ' | '))
    time.sleep(6)
    h2, g2 = money(host), money(guest)
    print(f'  the guest earned 700: guest {g2}, host {h1} -> {h2}')
    ck(h1 is not None and h2 is not None and abs(h2 - h1) < 1, f"the host's money does not get the guest's own earning ({h1} -> {h2})")
    print('  guest:', guest.call('coop.res')['out'].strip().replace('\n', ' | '))
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
