#!/usr/bin/env python3
"""BUG-0286: GetAdaptersAddresses (the "Friends connect to" hint of the co-op window, the LAN scan)
crashed a guest inside Wine's nsi.dll (a memcpy with a garbage count) on its main thread, 1.4 s
after it left its session, from the co-op window's refresh. Two games of one Wine prefix that
enumerate at the same moment get garbage from the shared Nsi device (Wine 10.0).

Two games of the lane (one bottle):
1. both look up their addresses 300 times at the same moment, three rounds
   (`coop.adapters stress 300`): with the fix the lookups of the two games take turns (a named
   mutex per prefix) on worker threads; without it they run on the main threads together and a
   game crashes within seconds.
2. the crash path: the guest leaves (Leave), the next lookup faults the way Wine's nsi did
   (`coop.adapters fault`: a read of 0xD54CF6CC through ucrt's memcpy) and the co-op window opens
   on its connect page (its refresh asks for the addresses). The guest's game keeps running (frames
   go on, the console answers, no crash report) and its address list keeps its last value.

  SPORE_LANE=3 python3 tools/tests/coop_adapters.py
  (SPORE_FIX_OFF=core.adapters: the old synchronous lookup on the main thread: the test must FAIL)"""
import glob
import os
import re
import threading
import time

import lane
from diag_check import LOGDIR
from twoinst import Checks, alive, start


def crashes():
    return set(glob.glob(os.path.join(LOGDIR, 'crash-*.txt')))


def frame(c):
    try:
        m = re.search(r'frame=(\d+)', c.call('frame').get('out', '') or '')
    except OSError:  # the console is gone (the game crashed)
        return None
    return int(m.group(1)) if m else None


def state(c):
    try:
        return c.call('coop.adapters')['out']
    except OSError:
        return ''


def num(st, key):
    m = re.search(key + r'=(\d+)', st)
    return int(m.group(1)) if m else -1


def main():
    before = crashes()
    host, guest = start()
    ck = Checks()
    games = (('host', host, lane.HOST), ('guest', guest, lane.GUEST))

    # 1. both games look up at the same moment
    def stress(name, c):
        try:
            print(f'  {name}:', c.call('coop.adapters stress 300')['out'].strip(), flush=True)
        except Exception as e:  # noqa: BLE001  a game that crashed does not answer
            print(f'  {name}: no answer ({type(e).__name__})', flush=True)

    for _ in range(3):
        ts = [threading.Thread(target=stress, args=(n, c)) for n, c, _ in games]
        for t in ts:
            t.start()
        for t in ts:
            t.join()
        for _ in range(120):  # the workers finish their queues
            if all(num(state(c), 'stress_left') <= 0 for _, c, _ in games):
                break
            time.sleep(1)
    new = sorted(crashes() - before)
    if new:  # the crash handler keeps the console up for a while: let it finish
        time.sleep(10)
    ok1 = all(alive(i) for _, _, i in games) and not new
    ck(ok1, 'both games live through 900 address lookups each at the same moment (crash reports: '
       f'{[os.path.basename(p) for p in new]})')
    if ok1:
        for n, c, _ in games:
            st = state(c)
            print(f'  {n}:', st.strip().splitlines()[0])
            ck(num(st, 'faults') == 0, f'{n}: no lookup faulted')

    # 2. the crash path: Leave, then the window's refresh asks for the addresses and the lookup faults
    if ok1:
        guest.call('coop.leave')
        print(' ', guest.call('coop.adapters fault')['out'].strip())
        try:
            guest.call('coop.uistate 1')
        except Exception as e:  # noqa: BLE001  the game may be gone before it answers
            print('  (no answer)', type(e).__name__)
        time.sleep(6)
        new = sorted(crashes() - before)
        if new:
            time.sleep(10)
        up = alive(lane.GUEST) and not new
        ck(up, f'the guest survives a fault inside the address lookup after Leave ({[os.path.basename(p) for p in new]})')
        if up:
            f0 = frame(guest)
            time.sleep(3)
            f1 = frame(guest)
            ck(f0 is not None and f1 is not None and f1 > f0 + 30, f'its frames go on ({f0} -> {f1})')
            st = state(guest)
            print(' ', st.strip())
            ck(num(st, 'faults') == 1, 'the fault was contained on the lookup thread')
            ck(num(st, 'main_thread_lookups') == 0, 'no lookup ran on the main thread')
            ck(re.search(r'^\s+\d+\.\d+\.\d+\.\d+ broadcast', st, re.M), 'the address list keeps its last value')
            ck('open=1' in guest.call('coop.uistate')['out'], 'the co-op window is open')
            guest.call('coop.uistate 0')
    lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
