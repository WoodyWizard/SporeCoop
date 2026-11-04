#!/usr/bin/env python3
"""Space: a guest's ship shot down by the host's pirates next to the host is disabled, not destroyed,
and the host repairs it by flying close (real fight, real input; docs/stability/space.md).

Two bugs made the guest die in every real fight next to its partner (play_space.py, 2026-10-23):
* the NPC hits the host relays to the guest (MSG_UFO_DAMAGE) went straight to the game's
  TakeDamage, past the rescue filter that turns a lethal hit into "disabled";
* a ship whose HP had come down to 1 or less through small hits was not disabled by the next one.

Both land on the home planet, the host spawns a pirate (setup), the guest's ship is left with a few
HP (setup: one big hit), then the pirate's shots on the guest's puppet reach the guest (the
relay real fights use; the shots themselves come from the debug command coop.fleet hurt, because
pirates pick their own targets, mostly the host). Checks: the guest is disabled
(down=1, state 0), the host is told (partners_down), the host flies to it with the keys and the
ship is repaired (rescues=1); also with HP 0.5 (the second bug). In round 1 the guest flies higher
(zoomed out 6 notches): the repair measures along the ground (GOG 2024 lanes: a 3D distance kept the ships of
two players at different zooms 200 m apart, docs/re/space.md 7.10)."""
import re
import time

import fixtures
import lane
from play_space import Player
from twoinst import Checks, shot, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def repair_dist(con):
    """the distance the repair measures to the partner (along the ground on a planet)"""
    m = re.search(r'repair ([\d.]+) m;', sp(con, 'dist'))
    return float(m.group(1)) if m else 0.0


def field(text, key):
    m = re.search(r'\b' + key + (r' ' if key == 'hp' else r'=') + r'([\d.]+)', text)
    return float(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    ph.partner, pg.partner = pg, ph
    home = fixtures.FIXTURES['space']['home']  # the fixture's home planet (PlanetID)
    for rnd, left in enumerate((5, 0.5)):
        pg.end = time.time() + 600
        while field(sp(guest, 'hp'), 'state') == 2 or field(sp(guest, 'hp'), 'movie') == 1:
            pg.captions()  # a ship lost between the rounds: confirm its captions, it respawns at home
        for p in (ph, pg):
            # GOG 2024 (lane 10): right after the load the zoom does not land for up to ~80 s
            # (docs/known-issues.md): try again
            t1 = time.time()
            while p.ctx() == 'system' and time.time() - t1 < 150:
                p.land(home)
        ck(ph.place()[0] == pg.place()[0] and ph.ctx() == 'planet',
           f'round {rnd + 1}: both on the home planet ({sp(guest, "dist")})')
        if rnd == 0:
            # the players fly at their own heights: on a planet a ship's altitude is its zoom (~520 m
            # zoomed in .. ~970 m from the centre, ~15 m per wheel notch). The repair measures along
            # the ground, as the top-down camera shows it: a 3D distance never got below the height
            # difference (GOG 2024 lanes, where retried landings left the two at different zooms)
            pg.zoom(-6)
            time.sleep(2)
            print('   guest zoomed out:', sp(guest, 'dist'), pg.ctx())
        # the host flies off a bit: it has to come back to repair. Out of the 60 m repair range by a
        # margin, in short pulses: 2.5 s of W took the faster ship of the `space` fixture ~550 m off,
        # too far to come back within the 20 s (BUG-0193)
        t1 = time.time()
        while time.time() - t1 < 2.5 and repair_dist(guest) < 120:
            ph.key_hold(['0x57'], 0.3)
        print('  ', sp(guest, 'dist'))
        r0 = field(sp(guest), 'rescues')
        # a pirate of the host shoots the guest's ship: its hit on the guest's puppet is relayed to the
        # guest (MSG_UFO_DAMAGE), the path real fights take (the pirates choose their own targets, so
        # the shot itself is the debug command coop.fleet hurt <ship> <player> [dmg])
        host.call('coop.fleet spawn 2')
        time.sleep(1)
        ids = re.findall(r'ship #(\d+) \S+ type=2', host.call('coop.fleet list')['out'])
        # battle damage first (one big relayed hit leaves `left` HP), then the pirate's small hits
        hp = field(sp(guest, 'hp'), 'hp')
        print('  ', host.call(f'coop.fleet hurt {ids[-1]} 1 {hp - left:.2f}')['out'])
        time.sleep(1.5)
        print('  big hit:', sp(guest, 'hp'))
        t0, st = time.time(), ''
        while ids and time.time() - t0 < 20:
            st = sp(guest, 'hp')
            if field(st, 'down') == 1 or field(st, 'state') == 2:
                break
            host.call(f'coop.fleet hurt {ids[-1]} 1')
            time.sleep(0.5)
        print(f'  after {time.time() - t0:.0f} s: {st} | {guest.call("coop.fleet list")["out"].splitlines()[0]}')
        ck(field(st, 'down') == 1 and field(st, 'state') == 0,
           f'round {rnd + 1}: the pirate\'s hits disabled the guest with {left} HP left, not destroyed')
        if field(st, 'down') != 1:
            continue
        told = 'partners_down=1' in host.call('coop.status')['out'] or field(sp(guest), 'rescues') > r0
        ck(told, f'round {rnd + 1}: the host is told')
        shot(guest, f'space_rescue_down_{rnd + 1}.png')
        ph.rescue()  # the host flies to the guest with the keys
        t0 = time.time()
        while time.time() - t0 < 20 and field(sp(guest), 'rescues') == r0:
            time.sleep(1)
        ck(field(sp(guest), 'rescues') == r0 + 1, f'round {rnd + 1}: the host repaired the guest ({sp(guest, "hp")})')
        if rnd == 0:
            time.sleep(62)  # the rescue has a 60 s cooldown
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
