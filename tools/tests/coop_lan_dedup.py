#!/usr/bin/env python3
"""The co-op window lists a host on two network addresses once (BUG-0210).

A PC on the network twice (cable and Wi-Fi on the same network, a laptop on a dock) answers the
guest's LAN query from both addresses; the list showed that game twice (once per address). The
lab PC has two addresses on one network, so the lane's own host is such a host.

1. Host and guest of the lane in a session (twoinst.start), the guest leaves and opens the co-op
   window: its LAN list runs (the queries go to 255.255.255.255, 127.0.0.1 and each subnet).
2. For ~10 s the guest's list (`coop.lanfake 0` prints its rows) is read: the rows of the lane's
   host (name Alice, the lane's port). PASS: exactly one row, every time, from the moment it
   appears. Other lanes' hosts may be listed too; they are not counted.

Skipped (passes with a note) on a machine with one network address: nothing to show.
Switch: SPORE_FIX_OFF=net.lan_dedup (no game id in the answer, one row per address) must FAIL.

  SPORE_LANE=10 python3 tools/tests/coop_lan_dedup.py
"""
import re
import subprocess
import time

import lane
from twoinst import Checks, start


def addresses():
    out = subprocess.run(['ip', '-4', '-br', 'addr'], capture_output=True, text=True).stdout
    return [m.group(1) for m in re.finditer(r'^(?!lo\s)\S+\s+UP\s+(\d+\.\d+\.\d+\.\d+)/', out, re.M)]


def rows(con):
    out = con.call('coop.lanfake 0')['out']
    return [ln.strip() for ln in out.splitlines()
            if ln.strip().startswith('game Alice ') and f':{lane.PORT} ' in ln.strip() + ' ']


def main():
    ck = Checks()
    addrs = addresses()
    print('  this machine:', addrs, flush=True)
    host, guest = start('creature')
    guest.call('coop.leave')
    time.sleep(1)
    guest.call('coop.uistate 1')
    seen, most = [], 0
    t0 = time.time()
    while time.time() - t0 < 12:
        r = rows(guest)
        if r:
            seen.append(r)
            most = max(most, len(r))
        time.sleep(0.5)
    for r in seen[-1:]:
        print('  rows of the lane host:', r, flush=True)
    guest.call('coop.uistate 0')
    ck(seen, f'the guest lists the lane host ({len(seen)} reads with it)')
    if len(addrs) < 2:
        print('  one network address only: a second row cannot appear here', flush=True)
    else:
        ck(most == 1, f'the lane host is one row, not one per address (at most {most} rows, {len(addrs)} addresses)')
    guest.call(lane.JOIN)  # back in the session for the next warm test
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
