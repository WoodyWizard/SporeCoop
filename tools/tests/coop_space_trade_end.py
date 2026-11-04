#!/usr/bin/env python3
"""BUG-0542: a gift on its way when the own session ends is not doubled.

The guest gives 5 of its 12 Antimatter Bomb charges to the host. The host's game keeps them but its
answer is lost (`trade hold 2`: added, no answer; in play: the guest leaves right after "Give", or the
connection drops, before the answer is back). The guest then leaves the session. The host's game
has the 5 charges, so the guest's bomb must stay at 7: before the fix every gift without an answer
came back at the session end, 5 charges doubled (12 on the guest and 5 more on the host).

  coop_space_trade_end.py   (SPORE_FIX_OFF=trade.delivered: FAILS, the guest has 12 again)"""
import re
import time

from twoinst import Checks, start

BOMB = '144DF50A'  # Antimatter Bomb


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def ammo(con, inst=BOMB):
    m = re.search(r'tool %s \S+ \S+ \'[^\']*\' ammo=(-?\d+)' % inst, sp(con, 'trade all'))
    return int(m.group(1)) if m else 0


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
    time.sleep(5)
    try:
        print('  guest bomb:', sp(guest, 'tool add', '0x' + BOMB), '|', sp(guest, 'trade ammo', BOMB, 12))
        ck(ammo(guest) == 12, f'the guest has 12 Antimatter Bomb charges ({ammo(guest)})')
        h0 = ammo(host)
        print(f'  host bomb before: {h0}')
        print(' ', sp(host, 'trade hold 2'))
        print('  give:', sp(guest, 'trade give', BOMB, 5, 0))
        ck(wait(lambda: ammo(guest) == 7, 5), f'the 5 charges left the guest\'s hold: 7 ({ammo(guest)})')
        ck(wait(lambda: ammo(host) >= h0 + 5 or (h0 == 0 and ammo(host) > 0), 10),
           f'the host\'s game has the gift ({h0} -> {ammo(host)})')
        h1 = ammo(host)
        ck(wait(lambda: 'pending=1' in sp(guest, 'trade') and 'delivered=1' in sp(guest, 'trade'), 10),
           f'the gift is unanswered and the host\'s side has it ({sp(guest, "trade")})')
        print('  guest leaves:', guest.call('coop.leave')['out'].strip())
        time.sleep(4)
        print('  guest trade:', sp(guest, 'trade'))
        g = ammo(guest)
        ck(g == 7, f'the guest\'s bomb stays at 7: the gift is on the host ({g}; 12 = doubled)')
        ck(ammo(host) == h1, f'the host keeps the gift ({ammo(host)})')
    finally:
        sp(host, 'trade hold 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
