#!/usr/bin/env python3
"""BUG-0226: spice of the co-op spice gift that comes back while the sender is not in Space is put
back once the sender is in Space again, exactly once.

The host has no room for spice (`coop.space gift noroom 1`) and does not answer yet (`gift hold 1`:
the answer is still on its way). The guest gives 5 spice (`coop.space give 5`, the co-op window's
Give button), then acts as outside Space (`coop.space away 1`: no cargo hold, as in the galaxy
menu or an editor). The host answers (`gift hold 0`): the spice comes back while the guest cannot
take it. Back in Space (`away 0`) the guest's cargo must hold its spice again, once.
Before the fix the answer was dropped: 5 spice lost without a notice.

  coop_space_gift_refund.py      (SPORE_FIX_OFF=space.gift_refund: FAILS, 5 spice less)"""
import re
import time

from twoinst import Checks, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def spice(con):
    m = re.search(r'spice slot -?\d+ count (\d+)', sp(con, 'spice'))
    return int(m.group(1)) if m else None


def wait(pred, timeout=20):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(0.5)
    return pred()


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if not spice(guest):
        print('  guest spice:', sp(guest, 'spice make 20'))
    c0 = spice(guest)
    print('  guest spice before:', sp(guest, 'spice'))
    ck(c0 and c0 >= 5, f'the guest has spice to give ({c0})')
    print('  host:', sp(host, 'gift noroom 1'), '|', sp(host, 'gift hold 1'))
    print('  guest give:', sp(guest, 'give 5'))
    ck(wait(lambda: spice(guest) == c0 - 5, 5), f'the 5 spice left the guest\'s cargo ({spice(guest)})')
    ck(wait(lambda: 'held=1' in sp(host, 'gift hold 1'), 10), 'the gift reached the host, unanswered')
    print('  guest:', sp(guest, 'away 1'))
    print('  host answers:', sp(host, 'gift hold 0'))
    time.sleep(3)
    print('  guest (away):', sp(guest, 'gift'))
    print('  guest:', sp(guest, 'away 0'))
    ok = wait(lambda: spice(guest) == c0, 10)
    print('  guest (back):', sp(guest, 'gift'), '|', sp(guest, 'spice'))
    ck(ok, f'back in Space the guest\'s cargo holds its spice again ({spice(guest)} of {c0})')
    time.sleep(10)
    ck(spice(guest) == c0, f'and only once, 10 s later ({spice(guest)})')

    sp(host, 'gift noroom 0')
    sp(guest, 'away 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
