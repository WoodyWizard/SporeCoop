#!/usr/bin/env python3
"""Joining by host name while the name server is slow (BUG-0204), two instances (creature fixture).

The guest leaves, every name lookup of the guest is made to wait 4 s (`coop.slowdns 4000`, a name
server that answers late), and the guest joins `localhost:<port>`. The game's frames must go on
while the name is looked up (the lookup ran in the join command on the main thread: the frames
stopped for the whole lookup), and the guest must end up in the host's world again.

  coop_join_name.py [--without-fix]   (--without-fix: coop.fix net.async_resolve off, the old
                                       behaviour: FAILS)"""
import re
import sys
import threading
import time

import lane
from twoinst import Checks, Console, start, wait_guest_in

DELAY_MS = 4000


def frame(con):
    m = re.search(r'frame=(\d+)', con.call('frame')['out'])
    return int(m.group(1)) if m else -1


def connected(con):
    out = con.call('coop.status')['out']
    return 'client id=' in out and ' connected' in out


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('creature')
    ck = Checks()
    guest.call(f'coop.fix net.async_resolve {"off" if without else "on"}')
    guest.call('coop.leave')
    time.sleep(2)
    ck('offline' in guest.call('coop.status')['out'], 'the guest left the session')
    guest.call(f'coop.slowdns {DELAY_MS}')

    side = Console(lane.GUEST)  # a second connection: `frame` answers from any thread
    done = {}

    def join():
        t0 = time.time()
        done['out'] = guest.call(f'cheat coop join localhost:{lane.PORT} Bob')['out']
        done['secs'] = time.time() - t0

    t = threading.Thread(target=join)
    f0 = frame(side)
    t.start()
    time.sleep(0.5)
    f1 = frame(side)
    time.sleep(2.0)
    f2 = frame(side)  # 2.5 s into a 4 s lookup
    t.join(30)
    print(f'  frames {f0} -> {f1} -> {f2}; the join command took {done.get("secs", -1):.1f} s')
    ck(f2 - f1 >= 20, f'the frames go on during the name lookup ({f2 - f1} frames in 2 s)')
    ck(done.get('secs', 99) < DELAY_MS / 1000 - 1, f'the join command does not wait for the name server ({done.get("secs", -1):.1f} s)')
    ck(re.search(r'looking up localhost|joining', guest.call('log 60')['out']) is not None, 'the guest looks the name up')

    ok = False
    t0 = time.time()
    while time.time() - t0 < 30 and not ok:
        ok = connected(guest)
        time.sleep(0.5)
    ck(ok, 'the guest is connected after the lookup')
    ck(wait_guest_in(host, guest, timeout=180), 'the guest is in the host world again')

    # a name that does not exist: the window learns it through the session end, no freeze
    guest.call('coop.slowdns 0')
    guest.call('coop.leave')
    time.sleep(2)
    t0 = time.time()
    guest.call('cheat coop join no-such-host.invalid:47810 Bob')
    secs = time.time() - t0
    gone = False
    while time.time() - t0 < 30 and not gone:
        gone = 'offline' in guest.call('coop.status')['out']
        time.sleep(0.5)
    log = guest.call('log 60')['out']
    ck(gone and 'cannot resolve no-such-host.invalid' in log, 'an unknown name ends the join with "cannot resolve"')
    print(f'  (unknown name: the join command took {secs:.1f} s)')

    guest.call('coop.fix net.async_resolve on')
    guest.call(lane.JOIN)  # leave the lane joined for the next test
    wait_guest_in(host, guest, timeout=180)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
