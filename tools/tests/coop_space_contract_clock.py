#!/usr/bin/env python3
"""Space: a contract's time does not run out while the host is outside Space (BUG-0695; class BUG-0423).

1. Two games on the space fixture. The host offers a survey contract (`coop.space contracts offer survey`): its offer
   has a deadline (`coop.space contracts clock`: the time left).
2. The host's Space modules act as outside Space for 12 s (`coop.space away 1`, as an editor or an adventure entered
   from Space does), then back (`away 0`).
3. The offer's time left went down by at most 3 s over those 12 s, and the guest's game shows the same offer
   (state offered). 0.7.10 (SPORE_FIX_OFF=space.contract_clock): the wall clock ran on: 12 s less.
"""
import re
import time

from twoinst import Checks, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def left(con):
    m = re.search(r'left=(-?[\d.]+)', sp(con, 'contracts clock'))
    return float(m.group(1)) if m else None


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    print(' ', sp(host, 'contracts offer survey'))
    time.sleep(1)
    l0 = left(host)
    if not ck(l0 is not None and l0 > 20, f'the host offers a contract ({sp(host, "contracts clock")})'):
        return ck.result()
    print(' ', sp(host, 'away 1'))
    time.sleep(12)
    print(' ', sp(host, 'away 0'))
    time.sleep(1)
    l1 = left(host)
    print(f'  time left: {l0:.1f} s before, {l1} s after 12 s outside Space ({sp(host, "contracts clock")})', flush=True)
    ck(l1 is not None and l0 - l1 <= 3.0, f'the offer kept its time while the host was outside Space ({l0:.1f} -> {l1})')
    time.sleep(6)  # the host's state every 5 s
    print('  guest:', sp(guest, 'contracts clock'))
    ck('state=1' in sp(guest, 'contracts clock'), "the guest's game still shows the offer")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
