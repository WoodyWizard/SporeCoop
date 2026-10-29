#!/usr/bin/env python3
"""Cell stage: a guest's meal that reaches a host in an editor is acknowledged (BUG-0239).

BUG-0051 keeps a guest's meal "on the way" until the host echoes its number in a food total. The
host ignored MSG_CELL_EAT / MSG_CELL_FOOD outside the Cell stage (its editor) and without an avatar,
so that meal was never acknowledged: the guest stayed its points above the host's total, and lost
them all at once at its next acknowledged meal (a level down and up when a line lay between).

`coop.pool away 1` on the host handles the Cell messages as in an editor (the host keeps playing and
sending totals). The food total is the game's own S+0x1C on each machine (`food=`).

1. Host away, the guest eats 10 points, host back: within 3 s the guest's game has the host's total.
2. A normal meal of the guest (1 point) still arrives on the host, and the guest has no dip.

SPORE_FIX_OFF=pool.food_ack_away must FAIL (the guest stays 10 above, then drops by 10).
"""
import re
import time

from twoinst import Checks, start


def food(con):
    m = re.search(r'^cell\((?:host|guest)\): food=(-?\d+)', con.call('coop.pool')['out'], re.MULTILINE)
    return int(m.group(1)) if m else None


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def wait(cond, secs, step=0.25):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    host, guest = start('cell')
    ck = Checks()
    try:
        ck(wait(lambda: (num(guest.call('coop.pool')['out'], 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
        ck(wait(lambda: food(host) is not None and food(host) == food(guest), 10), f'same food total ({food(host)} / {food(guest)})')
        h0 = food(host)
        # 1. the meal while the host is away
        print('  host: ' + host.call('coop.pool away 1')['out'])
        guest.call('coop.pool food 10')
        time.sleep(1.0)
        print(f'  during: host {food(host)} guest {food(guest)}')
        print('  host: ' + host.call('coop.pool away 0')['out'])
        same = wait(lambda: food(host) == food(guest), 3)
        h1, g1 = food(host), food(guest)
        print(f'  back: host {h1} guest {g1}')
        ck(h1 == h0, f'the host did not count a meal it could not take ({h0} -> {h1})')
        ck(same, f'the guest\'s game has the host\'s total within 3 s of the return (host {h1}, guest {g1})')
        time.sleep(1.0)
        # 2. a normal meal afterwards: on the host, and no dip on the guest
        g_before = food(guest)
        guest.call('coop.pool food 1')
        lows = []
        t0 = time.time()
        while time.time() - t0 < 3.0:
            lows.append(food(guest))
            time.sleep(0.1)
        h2, g2 = food(host), food(guest)
        print(f'  next meal: host {h1} -> {h2}, guest {g_before} -> {g2}, guest low {min(x for x in lows if x is not None)}')
        ck(h2 == h1 + 1, f'the next meal reached the host ({h1} -> {h2})')
        ck(g2 == h2, f'both have the same total after it ({h2} / {g2})')
        ck(min(x for x in lows if x is not None) >= g_before, f'the guest\'s total did not dip at the next meal (low {min(x for x in lows if x is not None)}, before {g_before})')
    finally:
        host.call('coop.pool away 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
