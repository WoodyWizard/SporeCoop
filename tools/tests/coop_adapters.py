#!/usr/bin/env python3
"""BUG-0286: GetAdaptersAddresses (the "Friends connect to" hint of the co-op window, the LAN scan)
crashed the game inside Wine's nsi.dll (a memcpy with a garbage count) on the main thread, 1.4 s
after a guest left its session, from the co-op window's refresh.

One game of the lane:
1. stress: 5 rounds of a session opened and closed (coop.host / coop.leave) with 200 address lookups
   each (`coop.adapters stress 200`) and the co-op window open: the game lives.
2. the crash path: a session ends (Leave), the next lookup faults the way Wine's nsi did
   (`coop.adapters fault`: a read of 0xD54CF6CC through ucrt's memcpy) and the co-op window opens
   on its connect page (its refresh asks for the addresses). The game must keep running (frames go
   on, the console answers, no crash report) and the window must keep its address hint.

  SPORE_LANE=3 python3 tools/tests/coop_adapters.py [--without-fix]
  (--without-fix or SPORE_FIX_OFF=core.adapters: the old synchronous lookup on the main thread,
   the injected fault kills the game: the test must FAIL)"""
import glob
import os
import re
import sys
import time

import lane
from diag_check import LOGDIR, start_host
from twoinst import Checks, alive


def crashes():
    return set(glob.glob(os.path.join(LOGDIR, 'crash-*.txt')))


def frame(c):
    m = re.search(r'frame=(\d+)', c.call('frame').get('out', '') or '')
    return int(m.group(1)) if m else None


def main():
    without = '--without-fix' in sys.argv or 'core.adapters' in os.environ.get('SPORE_FIX_OFF', '')
    ck = Checks()
    c = start_host(ck)
    if without:
        print(' ', c.call('coop.fix core.adapters off')['out'].strip())
    time.sleep(2)
    print(' ', c.call('coop.adapters')['out'].strip())
    before = crashes()
    port = lane.PORT + 170

    # 1. stress while sessions open and close (sockets come and go)
    c.call('coop.uistate 1')
    for r in range(5):
        print(' ', c.call(f'coop.host {port} Stress')['out'].strip().splitlines()[0])
        print(' ', c.call('coop.adapters stress 200')['out'].strip())
        time.sleep(2)
        c.call('coop.leave')
        time.sleep(1)
    for _ in range(60):  # the worker finishes its queue
        if not alive(lane.HOST) or 'stress_left=0' in c.call('coop.adapters')['out']:
            break
        time.sleep(1)
    ok1 = alive(lane.HOST)
    ck(ok1, 'the game lives through 1000 address lookups while sessions open and close')
    if ok1:
        st = c.call('coop.adapters')['out']
        print(' ', st.strip())
        ck('faults=0' in st, 'no lookup faulted on its own (Wine nsi)')

    # 2. the crash path: Leave, then the window's refresh asks for the addresses and the lookup faults
    if ok1:
        c.call('coop.uistate 0')
        print(' ', c.call(f'coop.host {port} Fault')['out'].strip().splitlines()[0])
        time.sleep(2)
        c.call('coop.leave')
        print(' ', c.call('coop.adapters fault')['out'].strip())
        try:
            c.call('coop.uistate 1')
        except Exception as e:  # noqa: BLE001  the game may be gone before it answers
            print('  (no answer)', type(e).__name__)
        time.sleep(6)
    up = alive(lane.HOST)
    ck(up, 'the game survives a fault inside the address lookup (no crash)')
    new = sorted(crashes() - before)
    ck(not new, f'no crash report ({[os.path.basename(p) for p in new]})')
    if up:
        f0 = frame(c)
        time.sleep(3)
        f1 = frame(c)
        ck(f0 is not None and f1 is not None and f1 > f0 + 30, f'frames go on ({f0} -> {f1})')
        st = c.call('coop.adapters')['out']
        print(' ', st.strip())
        ck('faults=1' in st, 'the fault was contained on the lookup thread')
        ck('main_thread_lookups=0' in st, 'no lookup ran on the main thread')
        ui = c.call('coop.uistate')['out']
        ck('open=1' in ui, 'the co-op window is open')
        c.call('coop.uistate 0')
    lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
