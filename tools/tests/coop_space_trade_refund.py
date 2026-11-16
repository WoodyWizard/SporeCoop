#!/usr/bin/env python3
"""BUG-0058: tool charges that come back while the sender is not in Space are owed, not lost.

The host gives 5 Antimatter Bomb charges to the guest, which does not answer yet (`trade hold 1`:
the answer is still on its way). The host is then "outside Space" (`trade away 1`: the feature
acts as outside Space, no inventory, which is what an editor or the menu gives it) when the guest
leaves the session, so the gift comes back while the host cannot take it. Back in Space
(`away 0`) the host's tool must have its 12 charges again (the game's cSpaceToolData ammo).
Before the fix the refund ran at once into no inventory: "no room, lost", 7 charges.

The outside-Space state is forced by a switch: entering an editor from Space with a gift on its
way and the partner leaving in that window cannot be timed reliably in a test.

  coop_space_trade_refund.py [--without-fix]   (--without-fix: `trade refundfix 0` on the host,
                                                 the old immediate refund, so the test must FAIL)"""
import re
import sys
import time

from harness import sp, waiter
from twoinst import Checks, start

BOMB = '144DF50A'  # Antimatter Bomb


def ammo(con, inst=BOMB):
    m = re.search(r'tool %s \S+ \S+ \'[^\']*\' ammo=(-?\d+)' % inst, sp(con, 'trade all'))
    return int(m.group(1)) if m else None


wait = waiter(timeout=20, step=0.5, final=True)


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    if without:
        print(' ', sp(host, 'trade refundfix 0'))
    print('  host bomb:', sp(host, 'tool add', '0x' + BOMB), '|', sp(host, 'trade ammo', BOMB, 12))
    ck(ammo(host) == 12, f'the host has 12 Antimatter Bomb charges ({ammo(host)})')
    print(' ', sp(guest, 'trade hold 1'))
    print('  give:', sp(host, 'trade give', BOMB, 5))
    ck(wait(lambda: ammo(host) == 7, 5), f'the 5 charges left the host\'s hold: 7 ({ammo(host)})')
    ck('pending=1' in sp(host, 'trade'), 'the gift is on its way (no answer yet)')
    print(' ', sp(host, 'trade away 1'))
    print('  guest leaves:', guest.call('coop.leave')['out'].strip())
    ck(wait(lambda: 'pending=0' in sp(host, 'trade'), 20), 'the host saw the partner leave with the gift open')
    st = sp(host, 'trade')
    print('  host trade (away):', st)
    print(' ', sp(host, 'trade away 0'))
    ok = wait(lambda: ammo(host) == 12, 10)
    print('  host trade (back):', sp(host, 'trade'))
    ck(ok, f'back in Space the host\'s bomb has its 12 charges again ({ammo(host)})')
    time.sleep(2)
    ck(ammo(host) == 12, f'and only once ({ammo(host)})')

    sp(host, 'trade refundfix 1')
    sp(guest, 'trade hold 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
