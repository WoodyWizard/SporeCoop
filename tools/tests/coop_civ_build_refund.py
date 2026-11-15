#!/usr/bin/env python3
"""Civilization: a guest's building the host's game cannot build gives its price back (BUG-0807).

A guest pays a building in its own city planner (the shared money) and the host's game builds the same one
(CMD_BUILD). When the host's game cannot (the city's last free plot went to the host's own building in the same
moment, the city is another player's now, the model is missing), the host only counted a rejected command; the
guest's reconcile then demolished the guest's building as an extra one and the price stayed paid.

1. Two games on the civ fixture; the shared money is set to 60000. The host builds houses in the players' city until
   one plot is free (`coop.civ build`, the game's own building path, paid; `coop.civ slots`).
2. The guest and the host build a house in that city in the same moment (the suite runs it over a link with 300 ms
   delay, so the guest's build reaches the host after the host's own): the guest's game (its copy of the city still
   has the plot free) builds and pays and sends the build, the host's game takes the last plot with its own; the
   guest's build finds no plot left there.
3. After 6 s: the city has one building more in each game (the host's), the money (each game's own) went down by the
   price of one house only, not two.
SPORE_FIX_OFF=civ.build_refund: the money went down by two prices (FAIL).
"""
import re
import time

from coop_civ import civ
from play_civ import money
from twoinst import Checks, start


def free_slots(con, city):
    return sum(1 for ln in con.call(f'coop.civ slots {city}')['out'].splitlines() if ln.startswith('slot ') and ' free ' in ln)


def same_money(host, guest, wait=5.0):
    t0 = time.time()
    while True:
        mh, mg = money(host), money(guest)
        if (mh is not None and mg is not None and abs(mh - mg) <= 1) or time.time() - t0 > wait:
            return mh, mg
        time.sleep(0.3)


def refunds(con):
    m = re.search(r'build_refunds=(\d+)/([-\d.]+)', con.call('coop.civ')['out'] + con.call('coop.status')['out'])
    return m.group(0) if m else '?'


def main():
    host, guest = start('civ')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    pc = [k for k, v in hc.items() if v['player']]
    if not ck(pc, f'a player city on the host: {pc}'):
        return ck.result()
    city = pc[0]
    host.call(f'coop.res add money {60000 - (money(host) or 0):.0f}')
    same_money(host, guest)
    price = None
    for _ in range(30):
        n = free_slots(host, city)
        if n <= 1:
            break
        m0 = money(host)
        out = host.call(f'coop.civ build {city} house')['out'].strip()
        time.sleep(0.6)
        m1 = money(host)
        if out == 'built' and m0 is not None and m1 is not None:
            price = m0 - m1
        print(f'  host build: {out}, free plots {n} -> {free_slots(host, city)}, price {price}', flush=True)
        if out != 'built':
            break
    time.sleep(3)
    hf, gf = free_slots(host, city), free_slots(guest, city)
    print(f'  free plots: host {hf}, guest {gf}; a house costs {price}', flush=True)
    if not ck(hf == 1 and gf >= 1 and price, f'one plot left in the host\'s city, the guest\'s copy has it free too ({hf}, {gf})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    host.call(f'coop.res add money {60000 - (money(host) or 0):.0f}')
    m0h, m0g = same_money(host, guest)
    b0h, b0g = civ(host)[0][city]['buildings'], civ(guest)[0][city]['buildings']
    go = guest.call(f'coop.civ build {city} house')['out'].strip()
    ho = host.call(f'coop.civ build {city} house')['out'].strip()
    print(f'  same moment: host "{ho}", guest "{go}" (money {m0h} / {m0g})', flush=True)
    ck(ho == 'built' and go.startswith('built locally'), 'both games built the house in their own game')
    time.sleep(6)
    mh, mg = same_money(host, guest)
    b1h, b1g = civ(host)[0][city]['buildings'], civ(guest)[0][city]['buildings']
    print(f'  after: money host {mh} guest {mg}; buildings host {b0h} -> {b1h}, guest {b0g} -> {b1g}; {refunds(host)}', flush=True)
    ck(b1h == b0h + 1 and b1g == b1h, f'the city has one more building in each game ({b0h} -> {b1h}, guest {b1g})')
    spent = m0h - mh if mh is not None and m0h is not None else None
    ck(spent is not None and spent < 1.5 * price,
       f'the money went down by one house only (spent {spent}, a house {price}), the same on both (host {mh}, guest {mg})')
    ck(mh is not None and mg is not None and abs(mh - mg) <= 1, f'the money is the same on both ({mh}, {mg})')
    host.call('coop.res add money 20000')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
