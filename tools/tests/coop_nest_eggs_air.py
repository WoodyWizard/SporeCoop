#!/usr/bin/env python3
"""BUG-0443: an egg at an NPC nest hangs in the air on the host (player report 2026-11-01, the host's
screenshot: "in enemy nests eggs are ... bugged (flying mid air)").

An NPC egg has no gravity while it is asleep: the lay node puts it at the layer's position (not snapped
onto the surface, unlike the avatar's eggs), the physics settles it for a moment and then it stays where it
is; an egg 2 m up stayed there until a creature came close (docs/re/creature.md#creature-egg-gravity).

1. Two games on the creature fixture, both avatars next to the nearest nest of another species (the host
   6 m, the guest 8 m away: neither touches the eggs).
2. The host's herd misses two members: the game's AI lays two eggs (as coop_nest_eggs.py).
3. The eggs the game laid lie on the surface (`coop.nest objs`: over_surface, the height over the terrain
   or the nest mound as cEgg_SnapToGround finds it).
   Without the fix they hang 0.38-0.45 m over it (fixture, lane 4): pushed up out of the layer's body after
   laying, then asleep without gravity.
4. One egg goes 2 m up (`coop.nest lift`: the SetPosition the lay node makes, as for a layer in the air).
   PASS: within 6 s it lies on the surface again on the host and stays there, every egg of the nest lies
   on the surface (none pushed into it) and the guest's copy lies on its surface.

SPORE_FIX_OFF=nest.egg_ground fails: the eggs hang over the nest on the host, the lifted one 2 m.

  coop_nest_eggs_air.py [--lift M]"""
import argparse
import math
import re
import time

from twoinst import Checks, shot, start
import coop_nest_eggs as ne

HANG = 0.3  # m over the surface (settled eggs: 0.0-0.1)
OS = re.compile(r'egg (\S+) pos=\((\S+) (\S+) (\S+)\) .*? serial=(\d+) mirror_of=(\d+) .*?over_surface=(\S+)')


def eggs(con, home):
    """{ptr: (pos, over_surface, serial, mirror_of)} of the eggs within 15 m of the nest"""
    out = {}
    for m in OS.finditer(con.call('coop.nest objs 60')['out']):
        pos = tuple(map(float, m.group(2, 3, 4)))
        if math.dist(pos, home) < ne.NEAR:
            out[m.group(1)] = (pos, float(m.group(7)), int(m.group(5)), int(m.group(6)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lift', type=float, default=2.0)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    ns = ne.nest_herds(host)
    if not ck(ns, 'a nest of another species with members on the host'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    dist, herd, members, home = ns[0]
    hx = '%.2f %.2f %.2f' % home
    print('  host:', host.call(f'coop.nest look {hx} 6')['out'].strip(), flush=True)
    print('  guest:', guest.call(f'coop.nest look {hx} 8')['out'].strip(), flush=True)
    time.sleep(3)
    host.call(f'coop.nest respawn {herd} 2000')
    host.call(f'coop.nest target {herd} {members + 2}')
    t0 = time.time()
    ho = {}
    while time.time() - t0 < 90:
        ho = eggs(host, home)
        lay = re.search(r'eggs_to_lay=(-?\d+)', host.call(f'coop.nest herd {herd}')['out'])
        if len(ho) >= 2 and lay and int(lay.group(1)) <= 0:
            break
        time.sleep(2)
    time.sleep(6)  # the last egg settles
    ho = eggs(host, home)
    print(f'  host eggs after {time.time() - t0:.0f} s (ptr: over surface):',
          {p: round(v[1], 2) for p, v in ho.items()}, flush=True)
    if not ck(len(ho) >= 2, f'the host\'s herd laid two eggs ({len(ho)})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    high = {p: round(v[1], 2) for p, v in ho.items() if v[1] > HANG}
    ck(not high, f'the eggs the game laid lie on the surface (over {HANG} m: {high})')

    egg = sorted(ho)[0]
    serial = ho[egg][2]
    print('  host:', host.call(f'coop.nest lift {egg} {a.lift}')['out'].strip(), f'(egg {serial})', flush=True)
    shot(host, 'air_lifted_host')
    trace = []
    t1 = time.time()
    while time.time() - t1 < 10:
        e = eggs(host, home).get(egg)
        trace.append((round(time.time() - t1, 1), round(e[1], 2) if e else None))
        time.sleep(0.5)
    print('  the lifted egg over the surface on the host (s, m):', trace, flush=True)
    after = [h for t, h in trace if t >= 6 and h is not None]
    ck(after and max(after) <= HANG,
       f'the lifted egg lies on the surface on the host 6 s later (over it: {after[-1] if after else None} m)')
    now = eggs(host, home)
    off = {p: round(v[1], 2) for p, v in now.items() if not -HANG <= v[1] <= HANG}
    ck(len(now) >= 2 and not off, f'every egg at the nest lies on the surface on the host (off: {off})')
    ge = [v for v in eggs(guest, home).values() if v[3] == serial]
    print('  guest copy of the lifted egg (over surface):', [round(v[1], 2) for v in ge], flush=True)
    ck(ge and ge[0][1] <= HANG, 'the guest\'s copy of the egg lies on its surface')
    ps = [v[0] for v in now.values()]
    print('  host eggs now (over surface, m apart):', [round(v[1], 2) for v in now.values()],
          [round(math.dist(p, q), 2) for i, p in enumerate(ps) for q in ps[i + 1:]], flush=True)
    host.call(f'coop.nest look {hx} 4')  # the nest on the host's screen
    time.sleep(2)
    shot(host, 'air_host')
    shot(guest, 'air_guest')
    print('  host:', host.call('coop.nest')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
