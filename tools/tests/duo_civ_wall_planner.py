"""Exercise a guest founder's native wall planner on two disposable Clyth profiles."""
import argparse
import json
from pathlib import Path
import random
import re
import struct
import time

import play_civ
from coop_ability_bar import mem, u32
from duo_civ_founder_control import cities
from duo_civ_turret_combat import wall
from duo_orders import wait_until
from duo_villages import Console, command


def drag(peer, start, end, before_release=None):
    x, y = start
    tx, ty = end
    command(peer, f'move {x} {y}')
    time.sleep(.35)
    command(peer, f'button {x} {y} down')
    try:
        time.sleep(.25)
        for step in range(1, 11):
            command(peer, f'move {round(x+(tx-x)*step/10)} {round(y+(ty-y)*step/10)}')
            time.sleep(.10)
        time.sleep(.45)
        if before_release:
            before_release()
    finally:
        command(peer, f'button {tx} {ty} up')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    receipts = parser.add_mutually_exclusive_group()
    receipts.add_argument('--save-receipt', type=Path,
                          help='Save a purchased wall, then close the guest while holding a second turret.')
    receipts.add_argument('--cold-receipt', type=Path,
                          help='Check the saved wall after launching fresh host and guest processes.')
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for peer in (host, guest):
        assert 'duoqa' in command(peer, 'coop.savedb').lower()
        assert "world 'Clyth.spo'" in command(peer, 'coop.avatar')
    towns = [next(ptr for _, ptr, _, owner in cities(peer) if owner == 1) for peer in (host, guest)]
    driver = play_civ.Player('Wall planner', guest, 1, random.Random(0), [])
    play_civ.W, play_civ.H = map(int, re.search(r'screen (\d+) (\d+)', command(guest, 'coop.uistate')).groups())
    assert (play_civ.W, play_civ.H) == (1176, 664), 'Fixture palette coordinates require 1176x664'
    play_civ.HUD_BOTTOM = play_civ.H - 95

    def editing():
        return driver.where(play_civ.PLANNER_NAME).get(play_civ.PLANNER_NAME)

    def slot_point(index, aim=0):
        first = u32(guest, towns[1] + 0x504)
        position = struct.unpack('<3f', mem(guest, first + index*24 + 12, 12))
        x, y = driver.project(driver.wait_still(), position)[0]
        return round(x), round(y) + aim

    def layouts():
        return [wall(peer, city) for peer, city in zip((host, guest), towns)]

    def has_slots(indices):
        return all([i for i, obj in enumerate(slots) if obj] == indices for slots in layouts())

    def money():
        return float(re.search(r'shared money=([-\d.]+)', command(host, 'coop.res'))[1])

    def no_guest_charges():
        assert 'from_guests=+0/0' in command(host, 'coop.res')

    if args.cold_receipt:
        saved = json.loads(args.cold_receipt.read_text())
        wait_until(lambda: has_slots([saved['slot']]), 'Cold peers did not restore the same wall', 20)
        native = layouts()[0][saved['slot']]
        assert u32(host, native + 0x24) == saved['serial']
        assert mem(host, native + 0x5C0, 4).hex() == saved['hp']
        assert money() >= saved['money']
        no_guest_charges()
        for peer in (host, guest):
            assert 'total faults: 0' in command(peer, 'exc')
        print('PASS cold shared wall, native identity, health and retained funds', saved, flush=True)
        return
    if not editing():
        town = next(t for t in play_civ.civ_state(guest)[0].values() if t['owner'] == 1)
        point = driver.look_at(town['pos'], box=(330,240,820,390), tries=30)
        assert point
        for dy in (0, -18, -38):
            command(guest, f'click {int(point[0])} {int(point[1])+dy}')
            time.sleep(.5)
            if editing() or driver.click_button(play_civ.BTN_PLANNER):
                break
        wait_until(editing, 'Guest city planner did not open', 10)
    assert has_slots([]), 'Use a disposable founder city with an empty wall'
    palette = (120, 550)
    no_guest_charges()
    before = money()
    assert before >= 2000
    drag(guest, palette, (80, 400), no_guest_charges)
    time.sleep(1)
    assert has_slots([]) and money() >= before
    print('PASS invalid drop leaves both walls empty and the treasury whole', flush=True)
    before = money()
    drag(guest, palette, slot_point(0), no_guest_charges)
    wait_until(lambda: has_slots([0]), 'Bought turret missing from host or guest wall', 10)
    no_guest_charges()
    paid = before - money()
    assert 1900 <= paid <= 2000, paid  # concurrent native city income can arrive
    turret = layouts()[0][0]
    serial = u32(host, turret + 0x24)
    hp = mem(host, turret + 0x5C0, 4)
    print('PASS guest purchase on both walls, host charged once', paid, flush=True)
    time.sleep(1.5)  # receive the canonical serial before picking it up again
    drag(guest, slot_point(0,-6), slot_point(1))
    wait_until(lambda: has_slots([1]), 'Moved turret did not reach the same slot on both walls', 10)
    moved = layouts()[0][1]
    assert serial == u32(host, moved + 0x24) and hp == mem(host, moved + 0x5C0, 4)
    print('PASS turret move preserved host identity and health', serial, flush=True)
    time.sleep(1.5)
    before = money()
    drag(guest, slot_point(1,-6), palette)
    wait_until(lambda: has_slots([]), 'Sale left a turret on the host or guest', 10)
    wait_until(lambda: money() >= before + 2000, 'Shared sale refund did not settle', 5)
    refund = money() - before
    assert 2000 <= refund <= 2100, refund
    no_guest_charges()
    for peer in (host, guest):
        assert 'total faults: 0' in command(peer, 'exc')
    print('PASS sale credited shared funds once; zero native faults', refund, flush=True)

    if args.save_receipt:
        assert not args.save_receipt.exists(), 'Use a fresh receipt'
        drag(guest, palette, slot_point(0))
        wait_until(lambda: has_slots([0]), 'Second purchase did not settle', 10)
        native = layouts()[0][0]
        funds = money()
        assert funds >= 2000, 'Fixture needs enough money to pick up a second turret'
        command(guest, 'move 120 550')
        command(guest, 'button 120 550 down')
        x, y = slot_point(1)
        for n in range(1, 11):
            command(guest, f'move {round(120+(x-120)*n/10)} {round(550+(y-550)*n/10)}')
            time.sleep(.1)
        wait_until(lambda: layouts()[1][1], 'Guest never held a preview in the second wall slot', 5)
        assert not layouts()[0][1]
        assert 'players=2/4' in command(host, 'coop.status').splitlines()[0]
        command(guest, 'quit')
        wait_until(lambda: 'players=1/4' in command(host, 'coop.status').splitlines()[0],
                   'Host did not expire the closed guest', 120)
        assert not wall(host, towns[0])[1] and money() >= funds
        no_guest_charges()
        planet = Path(re.search(r'location=(.+?)\s+access=', command(host, 'coop.savedb'))[1]) / 'Clyth.spo'
        stamp = planet.stat().st_mtime_ns
        command(host, 'coop.save')
        wait_until(lambda: planet.stat().st_mtime_ns != stamp, 'Native host save did not complete', 30)
        command(host, 'coop.civ')
        args.save_receipt.write_text(json.dumps(dict(slot=0, serial=u32(host,native+0x24),
                                      hp=mem(host,native+0x5C0,4).hex(), money=funds)))
        print('PASS saved committed turret; guest exit mid-drag left no charge or phantom turret', flush=True)


if __name__ == '__main__':
    main()
