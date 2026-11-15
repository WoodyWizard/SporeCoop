#!/usr/bin/env python3
"""Space finale: "Fly to" a partner at the galactic core from the galaxy view is free and refused while disabled
(BUG-1045).

space_finale.cpp fly_step called SpaceTeleportTo directly in the galaxy view: the game's TravelToStar spent the ship's
energy for the jump (and with no energy left damaged the ship), and a ship disabled after the order (the step runs up
to 30 s later) still flew. The co-op jump elsewhere (spacecoop teleport_in_galaxy, BUG-0063) refunds the energy and
refuses a disabled ship.

Fixture space; the guest gets the interstellar drive and zooms out to the galaxy view (mouse wheel). `coop.space finale
goto <star>` is what the "Fly to <partner>" button queues (fly_to_star; the star near the core is the partner's star
in the finale).
  1. full energy: the jump next to the core leaves the ship's energy as it was (the game's field +0x7F4)
  2. no energy: the jump does not damage the ship (its hp in the game)
  3. the ship disabled after the order (`goto ... delay 4`, then `coop.space disable 1`): no flight
SPORE_FIX_OFF=space.finale_fly_free: energy spent / hp lost / the disabled ship flies (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_finale_fly.py
"""
import re
import time

from coop_space_map import no_movie, sp, to_galaxy
from twoinst import Checks, keyframe, start


def energy(con):
    m = re.search(r'energy (-?[\d.]+) max ([\d.]+) hp (-?[\d.]+) max ([\d.]+)', sp(con, 'energy'))
    return tuple(float(m.group(i)) for i in (1, 2, 3, 4)) if m else (0, 0, 0, 0)


def active(con):
    m = re.search(r'active (\d+)', sp(con, 'finale core'))
    return int(m.group(1)) if m else 0


def fly(con, star, extra=''):
    r = sp(con, 'finale goto', star, extra)
    m = re.search(r'flying to (\d+)', r)
    return (int(m.group(1)) if m else 0), r


def wait(pred, t=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < t:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def main():
    host, guest = start('space')
    ck = Checks()
    print('  drive:', sp(guest, 'tool add interstellardrive'), flush=True)
    no_movie(guest)
    ck(to_galaxy(guest), 'the guest is in the galaxy view')
    time.sleep(3)
    no_movie(guest)  # (the drive's first galaxy view shows a tutorial caption)
    home = active(guest)
    print(f'  guest active star {home}; {sp(guest, "finale core")}', flush=True)

    # 1. full energy: the jump next to the core costs nothing
    e = energy(guest)
    sp(guest, 'energy', e[1])
    e0 = energy(guest)
    near, r = fly(guest, 'near')
    print(f'  goto near: {r}; energy/hp before {e0}', flush=True)
    ck(near, f'a star near the core to fly to ({r})')
    arrived = wait(lambda: active(guest) == near, 20)
    time.sleep(1)
    e1 = energy(guest)
    print(f'  arrived={bool(arrived)} active {active(guest)}; energy/hp after {e1}', flush=True)
    ck(arrived, f'the guest is next to the star near the core ({active(guest)} / {near})')
    ck(abs(e1[0] - e0[0]) < 0.5, f'the flight cost no energy ({e0[0]} -> {e1[0]})')
    keyframe('finale_fly', "the guest's galaxy view next to the galactic core (the bright centre), the host's game at "
             'its home system; no caption, no black screen')

    # 2. no energy left: the jump back home does not hurt the ship
    sp(guest, 'energy', 0)
    e2 = energy(guest)
    back, r = fly(guest, str(home))
    arrived = wait(lambda: active(guest) == home, 20)
    time.sleep(1)
    e3 = energy(guest)
    print(f'  goto home: {r} arrived={bool(arrived)}; energy/hp {e2} -> {e3}', flush=True)
    ck(arrived, f'with no energy the guest flies home ({active(guest)} / {home})')
    ck(e3[2] >= e2[2], f'the flight with no energy did not damage the ship (hp {e2[2]} -> {e3[2]})')
    sp(guest, 'energy', e[1])

    # 3. disabled after the order: no flight
    near, r = fly(guest, 'near', 'delay 4')
    print(f'  goto near (in 4 s): {r};', sp(guest, 'disable 1'), flush=True)
    time.sleep(8)
    a = active(guest)
    print(f'  active {a} (home {home}, near {near}); {sp(guest, "finale")[:200]}', flush=True)
    ck(near and a == home, f'a ship disabled after the order does not fly ({a}, home {home})')
    print(' ', sp(guest, 'disable 0'), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
