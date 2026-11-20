#!/usr/bin/env python3
"""BUG-9259: Civilization, every player's own money (the session setting civ.own_money, off by default).

The host leaves its Civ world for the galaxy menu, switches the setting on (a session setting: only before the start)
and plays the world again; the guest joins. Checks the money each game shows, not the mod's bookkeeping:
  1. the guest's game shows its own money, an equal part of the host's (money moves, none is made)
  2. an income of the host's game (`coop.wallet income`, through the game's AddMoney) reaches the guest's game as its
     part of the cities, and the host keeps the rest
  3. a vehicle the guest buys (the host's game makes it) is paid from the guest's money: the guest's game shows less,
     the host's money does not pay it
  4. the guest leaves and joins again: its money is the same (kept by its store key)
A new feature: on main `coop.wallet` is an unknown command and the setting does not exist, so the test fails there.
"""
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in

PLANET = 'Game0/Arigine'  # the 'civ' fixture (fixtures.py)


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError):
            pass
        time.sleep(step)
    return False


def wallet(con):
    return con.call('coop.wallet').get('out', '')


def money(con):
    m = re.search(r'^money=(-?\d+)', wallet(con), re.MULTILINE)
    return int(m.group(1)) if m else None


def counter(con, name):
    m = re.search(rf'\b{name}=(-?[\d.]+)', wallet(con))
    return float(m.group(1)) if m else -1


def guest_has_wallet(guest):
    return 'have=1' in wallet(guest).split('\n', 1)[0]


def main():
    host, guest = start('civ')
    ck = Checks()
    # the setting before the start
    for con in (guest, host):
        con.call('coop.leave')
    host.call('coop.menu')
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90), 'the host is in the galaxy menu')
    r = host.call('coop.settings set civ.own_money on')['out'].strip()
    ck(r.startswith('civ.own_money=1'), f'the setting is accepted before the start ({r})')
    host.call('wait.frames 240 90000')
    slot, name = PLANET.split('/')
    host.call(f'coop.load {slot} {name}')
    ck(wait(lambda: 'Civ' in host.call('game.mode')['out'] and money(host) is not None, 240, 2), 'the host plays Civ again')
    host.call('wait.frames 300 90000')
    total = money(host)
    host.call('coop.host')
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined')
    ck(wait(lambda: guest_has_wallet(guest), 120, 2), 'the guest has its own money')
    time.sleep(3)
    h, g = money(host), money(guest)
    print(f'  money before the join {total}; now host {h}, guest {g}', flush=True)
    # 1. an equal part (natural income may have come in meanwhile: a little more is fine, never more than the whole)
    ck(g is not None and g >= total // 2 - 1 and g < total, f"the guest's game shows its own part ({g} of {total})")
    ck(h is not None and h < total + 500 and h + g <= total + 2000, f"the host's game shows its own part ({h})")
    # 2. an income
    g0, out0 = money(guest), counter(host, 'shared_out')
    print('  ', host.call('coop.wallet income 400')['out'].strip(), flush=True)
    ck(wait(lambda: money(guest) > g0, 20), f"the guest's game got its part of an income ({g0} -> {money(guest)})")
    ck(counter(host, 'shared_out') > out0, "the host's game gave a part of the income away")
    # 3. a vehicle the guest buys
    city = re.search(r'^city (\d+) \S+ pol=\S+ \(player\) owner=(1|253)\b', host.call('coop.civ')['out'], re.MULTILINE)
    ck(city is not None, 'a city the guest may buy in')
    if city:
        time.sleep(3)
        g0, h0, ch0 = money(guest), money(host), counter(host, 'charges')
        print('  guest buys:', guest.call(f'coop.civ buy {city.group(1)} 0')['out'].strip(), flush=True)
        bought = wait(lambda: counter(host, 'charges') > ch0, 30)
        time.sleep(4)
        g1, h1 = money(guest), money(host)
        print(f'  guest {g0} -> {g1}, host {h0} -> {h1}', flush=True)
        if not bought and 'not enough money' in guest.call('coop.wallet')['out']:
            print('  (the guest could not afford it)')
        ck(bought, 'the purchase was charged to the guest (the host made the vehicle)')
        ck(g1 < g0 - 50, f"the guest's game shows the price paid ({g0} -> {g1})")
        ck(h1 >= h0 - 50, f"the host's money did not pay it ({h0} -> {h1})")
    # 4. a rejoin keeps it
    g0, kb0 = money(guest), counter(host, 'kept_back')
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined again')
    ck(wait(lambda: guest_has_wallet(guest) and abs(money(guest) - g0) < 1000, 120, 2),
       f"after the rejoin the guest's money is its own again ({g0} -> {money(guest)})")
    ck(counter(host, 'kept_back') > kb0, 'the host gave the guest its kept wallet back')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
