#!/usr/bin/env python3
"""Space: a partner's terraforming record of a star this game never generated, or a bad planet id, is skipped
without a fault (BUG-1042).

A terraforming partner's game sends its planet record values (K_TERRA_REC, BUG-0883); a receiver not on that planet
wrote them through cStarManager::GetPlanetRecord with no check: the star table is indexed with no bound and the
records of a star whose planets were never generated here are read as a null array (BUG-0284's read).

Fixture space. The guest names a star of the galaxy its game has not generated planets for (`coop.space nogen`); the
host sends K_TERRA_REC as its terraforming does (`coop.space terrarec <place>`) for
  1. planet 0 of that star,
  2. a planet id past the star table,
  3. control: the guest's home system's planet 01460035 (generated): the guest's record of it takes the values.
PASS: no fault in the guest's game (`exc`), and the control record changed. SPORE_FIX_OFF=space.terra_rec_checked:
the guest's game faults on 1 or 2 (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_terra_far.py
"""
import re
import time

from twoinst import Checks, start

PLANET = '01460035'


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def faults(con):
    m = re.search(r'total faults: (\d+)', (con.call('exc')['out'].splitlines() or [''])[0])
    return int(m.group(1)) if m else 0


def rec(con, planet):
    m = re.search(r'b0=([\d.-]+) b4=([\d.-]+) b8=([\d.-]+)', sp(con, 'planet', planet))
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def num(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    ng = sp(guest, 'nogen')
    m = re.search(r'nogen ([0-9A-F]{8})', ng)
    star = int(m.group(1), 16) if m else 0
    print(f'  guest: {ng}', flush=True)
    ck(star, f"a star without generated planets in the guest's game ({ng})")
    f0 = faults(guest)
    got0 = num(guest, 'terra_rec_got')
    cases = []
    if star:
        cases.append(('a star not generated here', 0x40000000 | star))
    cases.append(('a planet id past the star table', 0x40000000 | 0xFFF000 | 0xFFF))
    for what, place in cases:
        print(f'  host sends K_TERRA_REC for {place:08X} ({what}):', sp(host, 'terrarec', f'{place:08X}', 0.5, 0.5, 0.5),
              flush=True)
        time.sleep(2)
        f = faults(guest)
        print(f'  guest faults {f0} -> {f}; {guest.call("exc")["out"].splitlines()[:3]}', flush=True)
        ck(f == f0, f"{what}: the guest's game does not fault ({f0} -> {f})")
        f0 = f
    # control: a generated planet of the home system changes
    r0 = rec(guest, PLANET)
    want = (0.25, 0.75, 0.4) if not r0 or abs(r0[0] - 0.25) > 0.05 else (0.6, 0.3, 0.2)
    print('  host sends the control:', sp(host, 'terrarec', '4' + PLANET[1:], *want), flush=True)
    time.sleep(2)
    r1 = rec(guest, PLANET)
    print(f'  guest record of {PLANET}: {r0} -> {r1}; terra_rec_got {got0} -> {num(guest, "terra_rec_got")}', flush=True)
    ck(r1 and abs(r1[0] - want[0]) < 0.01 and abs(r1[1] - want[1]) < 0.01,
       f"control: the guest's record of a generated planet takes the values ({r1} / {want})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
