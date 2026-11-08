#!/usr/bin/env python3
"""Shared food / money: a notice says whose purchase or earning moved the shared total (BUG-0696; class BUG-0423).

1. Two games on the tribe fixture (the Tribe food: no income in every frame). The host's tribe gets 300 food
   (`coop.res add food 300`, the r.set path of an earning): within 4 s the guest's game shows a notice that the
   host earned 300 food (`coop.status`: the notification toasts on screen).
2. The guest spends 200 food (`coop.res add food -200`): within 4 s the host's game shows that the guest spent 200.
0.7.10 (SPORE_FIX_OFF=res.cause): no notice on either game, the totals only.
"""
import re
import time

from twoinst import Checks, start


def toast(con, pat):
    t0 = time.time()
    while time.time() - t0 < 4:
        st = con.call('coop.status')['out']
        for line in st.splitlines():
            if line.startswith('toast ') and re.search(pat, line):
                return line
        time.sleep(0.2)
    return None


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(3)
    print(' ', host.call('coop.res add food 300')['out'].strip().replace('\n', ' | '))
    t = toast(guest, r'earned 300 food')
    print('  guest:', t)
    ck(t is not None, "the guest's game says that the host earned 300 food")
    time.sleep(2)
    print(' ', guest.call('coop.res add food -200')['out'].strip().replace('\n', ' | '))
    t = toast(host, r'spent 200 food')
    print('  host:', t)
    ck(t is not None, "the host's game says that the guest spent 200 food")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
