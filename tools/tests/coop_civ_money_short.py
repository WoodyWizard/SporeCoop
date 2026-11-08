#!/usr/bin/env python3
"""Civilization: a building the money does not cover is not built, and the shared money stays the
same on both games (BUG-0613).

The mod's own paid build (`coop.civ build`, what the tests and the guest's command path use) paid
with the game's cCivilization::SpendMoney, which goes below 0: civ_coop's host showed -800 while
the guest floors the shared total at 0, and the divergence detector reported
`Civilization:state:money` (host -800, guest 0) in every civ_coop run. The game's own planner only
offers what the money covers.

The host sets the shared money to 500 (setup), then builds a factory (1200) and the guest a house
(1600). Checks, on each game's own money and city: nothing is built, the money is 500 on both and
never below 0 (SPORE_FIX_OFF=civ.build_money fails: host -700, guest 0). Then, with enough money,
the same builds are made and paid once (the fix does not stop a build the money covers).
"""
import time

from coop_civ import civ
from play_civ import money
from twoinst import Checks, start


def same_money(host, guest, wait=4.0):
    """(host, guest) money once both agree or after `wait` s (a total reaches the guest within ~0.3 s)"""
    t0 = time.time()
    while True:
        mh, mg = money(host), money(guest)
        if (mh is not None and mg is not None and abs(mh - mg) <= 1) or time.time() - t0 > wait:
            return mh, mg
        time.sleep(0.3)


def main():
    host, guest = start('civ')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    pc = [k for k, v in hc.items() if v['player']]
    ck(pc, f'a player city on the host: {pc}')
    if not pc:
        return ck.result()
    city = pc[0]
    m0 = money(host)
    host.call(f'coop.res add money {500 - m0:.0f}')  # setup: the shared money at 500
    mh, mg = same_money(host, guest)
    ck(mh == 500 and mg == 500, f'setup: the shared money is 500 on both (host {mh}, guest {mg})')
    for who, con, what, cost in (('host', host, 'factory', 1200), ('guest', guest, 'house', 1600)):
        b0h, b0g = civ(host)[0][city]['buildings'], civ(guest)[0][city]['buildings']
        out = con.call(f'coop.civ build {city} {what}')['out'].strip()
        print(f'  {who} builds a {what} ({cost}) with 500: {out}', flush=True)
        time.sleep(2.5)
        mh, mg = same_money(host, guest)
        b1h, b1g = civ(host)[0][city]['buildings'], civ(guest)[0][city]['buildings']
        ck(mh is not None and mg is not None and abs(mh - mg) <= 1 and mh >= 0,
           f'{who}\'s {what} the money does not cover: the money is the same on both and not below 0 '
           f'(host {mh}, guest {mg})')
        ck(b1h == b0h and b1g == b0g, f'{who}\'s {what} was not built (buildings host {b0h} -> {b1h}, guest {b0g} -> {b1g})')
        host.call(f'coop.res add money {500 - (money(host) or 0):.0f}')  # back to 500 for the next case
        same_money(host, guest)
    # with the money: built and paid once, the same total on both
    host.call('coop.res add money 9500')
    mh, mg = same_money(host, guest)
    print(f'  money {mh} / {mg}', flush=True)
    b0h = civ(host)[0][city]['buildings']
    out = host.call(f'coop.civ build {city} factory')['out'].strip()
    time.sleep(2.5)
    mh2, mg2 = same_money(host, guest)
    b1h, b1g = civ(host)[0][city]['buildings'], civ(guest)[0][city]['buildings']
    ck(out == 'built' and b1h == b0h + 1 and b1g == b1h, f'with 10000 the host\'s factory is built on both ({out}, {b0h} -> {b1h} / {b1g})')
    ck(mh2 is not None and mg2 is not None and abs(mh2 - mg2) <= 1 and mh - 1300 < mh2 < mh,
       f'and paid once, the same on both ({mh} -> host {mh2}, guest {mg2})')
    host.call('coop.res add money 20000')  # leave money for the next test of the lane
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
