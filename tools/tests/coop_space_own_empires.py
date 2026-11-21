#!/usr/bin/env python3
"""Every player's own empire in Space (BUG-9279, space_empires.cpp, docs/re/space.md#space-own-empires).

The lab's Space fixtures have no guest on a species of its own (the Civ launch of one that had is in the by-hand
run of the ticket), so the host founds the guest's empire with the test seam `coop.space empires found`.

1. The guest is told its own empire (its game's money set to its own) and its empire carries its name there.
2. Own money: the guest's change reaches the host's record of its empire, not the host's money; the host's change
   does not reach the guest.
3. Own diplomacy: the guest's war with an NPC empire stays off the host; the host's alliance stays off the guest.
4. A leave and rejoin: the guest's money and its war come back, the host's alliance does not.

  SPORE_LANE=4 python3 tools/tests/coop_space_own_empires.py

SPORE_FIX_OFF=space.own_empires: the guest's money change moves the host's money (FAIL at check 2);
SPORE_FIX_OFF=diplomacy.own_empires: the guest's war reaches the host (FAIL at check 3).
"""
import re
import time

from coop_space_diplomacy import near, space_planet
from harness import sp, waiter
import lane
from twoinst import Checks, start, wait_guest_in

FIXTURE = 'space_empire'
wait = waiter(timeout=30, step=1.0, as_bool=True)


def table(con):
    return {m[0]: int(m[1]) for m in re.findall(r'^rec (\w+) flags=(\d+)', sp(con, 'diplomacy table'), re.M)}


def wstate_money(con):
    m = re.search(r'st res money=(\d+)', con.call('coop.wstate')['out'])
    return int(m.group(1)) if m else None


def own_money(guest):
    m = re.search(r'guest own=1 seat=\d+ money=(-?\d+)', sp(guest, 'empires'))
    return int(m.group(1)) if m else None


def record(host):
    m = re.search(r'^empire seat=\d+ .*?money=(-?\d+)', sp(host, 'empires'), re.M)
    return int(m.group(1)) if m else None


def main():
    host, guest = start(FIXTURE, planet=space_planet())
    ck = Checks()
    time.sleep(4)
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    print('  host:', sp(host, 'empires found', gid.group(1) if gid else 1, '7CA81684'))
    ok = wait(lambda: own_money(guest) is not None, 60)
    print('  guest:', sp(guest, 'empires').splitlines()[0])
    ck(ok, 'guest: its own empire (1)')
    ck(wait(lambda: "name='" in sp(guest, 'empires') and "'s empire'" in sp(guest, 'empires'), 20),
       "guest: its empire carries its name on its game (1)")
    if not ok:
        return ck.result()
    h0 = wstate_money(host)
    print('  ', sp(guest, 'empires money 55555'))
    ck(wait(lambda: record(host) == 55555, 20), 'host: its record of the guest empire follows the guest (2)')
    time.sleep(4)
    ck(wstate_money(host) == h0, f'host: its own money unchanged ({h0} -> {wstate_money(host)}) (2)')
    print('  ', sp(host, 'empires money 77777'))
    time.sleep(6)
    ck(own_money(guest) == 55555, f'guest: its money unchanged by the host ({own_money(guest)}) (2)')
    empires = [x[0] for x in near(host)]
    war, ally = empires[0], empires[1]
    print('  guest:', sp(guest, 'diplomacy contact', war), sp(guest, 'diplomacy war', war))
    print('  host:', sp(host, 'diplomacy contact', ally, 'talk'), sp(host, 'diplomacy apply', ally, '0526E51F 4'),
          sp(host, 'diplomacy ally', ally))
    time.sleep(10)
    ck(table(guest).get(war, 0) & 1, f'guest: at war with {war} (3)')
    ck(not table(host).get(war, 0) & 1, f'host: not at war with {war} (3)')
    ck(not table(guest).get(ally, 0) & 2, f'guest: not allied with the host ally {ally} (3)')
    time.sleep(6)  # the guest's table reaches the host (K_DIPLO every 5 s)
    guest.call('coop.leave')
    time.sleep(6)
    guest.call(lane.JOIN)
    wait_guest_in(host, guest)
    ok = wait(lambda: 'ready=1' in sp(guest, 'empires') and own_money(guest) is not None, 240)
    time.sleep(6)
    ck(ok and own_money(guest) == 55555, f'guest: its own money back after the rejoin ({own_money(guest)}) (4)')
    ck(table(guest).get(war, 0) & 1, f'guest: at war with {war} again (4)')
    ck(not table(guest).get(ally, 0) & 2, f'guest: the host alliance with {ally} not taken from the save (4)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
