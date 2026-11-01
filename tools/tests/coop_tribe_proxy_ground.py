#!/usr/bin/env python3
"""Tribe: the host's animals walking up and down a slope stay on the guest's ground (BUG-0399).

A guest walks a host animal's proxy with drive::walk_place at its own height (BUG-0329: the radius it
has, the guests' ground can lie metres off the host's). Walking uphill it was placed under the ground
rising ahead of it, the game does not lift a creature placed under its terrain, and it stood there
afterwards: 1.3-1.7 m under the guest's ground (the monitor's `proxies` rule after a join, tribe_coop).

1. Two games on the tribe fixture.
2. The host picks up to WALKERS animals the guest has as proxies and, for each, the steepest way up
   from where it stands (the ground radius `coop.ground` in 12 directions RISE_DIST m away). Its game
   walks them there and back (`coop.npctrack walk`: SetDesiredSpeed + MoveTo, as the AI does) at 2.5-4 m/s
   for SECS s.
3. The guest reads its proxies' height over its own ground (`coop.npc list` agl=, cPlanetModel::GetHeightAt
   of the proxy's position: what its game shows) every ~0.3 s.
   PASS: no proxy more than UNDER m under the ground for HOLD s or longer.

Switch: SPORE_FIX_OFF=npc.proxy_ground (own height only) must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_tribe_proxy_ground.py [--secs 40]
"""
import argparse
import math
import re
import time

from twoinst import Checks, start

WALKERS = 4
RISE_DIST = 14.0  # m
UNDER, HOLD = 0.5, 1.0  # m, s

HOST_NPC = re.compile(r'^\s*#(\d+) \S+ species=\S+ hp=([\d.]+) dist=\S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', re.M)
GUEST_PX = re.compile(r'^\s*#(\d+) species=\S+ spawned hp=\S+ dist=\S+ .*?at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) '
                      r'agl=([-\d.]+) want_agl=([-\d.]+)', re.M)


def host_npcs(con):
    return {int(m.group(1)): tuple(map(float, m.group(3, 4, 5)))
            for m in HOST_NPC.finditer(con.call('coop.npc list')['out']) if float(m.group(2)) > 0 and ' DEAD' not in m.group(0)}


def proxies(con):
    """id -> (pos, agl over this game's ground, the host position's agl here)"""
    return {int(m.group(1)): (tuple(map(float, m.group(2, 3, 4))), float(m.group(5)), float(m.group(6)))
            for m in GUEST_PX.finditer(con.call('coop.npc list')['out'])}


def ground(con, p):
    m = re.search(r'ground ([-\d.]+)', con.call('coop.ground %.2f %.2f %.2f' % tuple(p))['out'])
    return float(m.group(1)) if m else 0.0


def around(c, d, ang):
    """the point d m from c on the planet's tangent plane, at angle ang"""
    r = math.sqrt(sum(x * x for x in c))
    up = [x / r for x in c]
    a = [0.0, -up[2], up[1]] if abs(up[0]) < 0.9 else [-up[2], 0.0, up[0]]
    al = math.sqrt(sum(x * x for x in a))
    a = [x / al for x in a]
    b = [up[1] * a[2] - up[2] * a[1], up[2] * a[0] - up[0] * a[2], up[0] * a[1] - up[1] * a[0]]
    return [c[i] + (a[i] * math.cos(ang) + b[i] * math.sin(ang)) * d for i in range(3)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=40)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 40 and len(proxies(guest)) < 3:
        time.sleep(1)
    time.sleep(3)
    hs, gs = host_npcs(host), proxies(guest)
    both = [i for i in hs if i in gs]
    print(f'  host NPCs {len(hs)}, guest proxies {len(gs)}, both {len(both)}', flush=True)
    plan = []
    for i in both:
        home = hs[i]
        g0 = ground(host, home)
        best = max(((ground(host, p) - g0, p) for p in (around(home, RISE_DIST, k * math.pi / 6) for k in range(12))),
                   key=lambda x: x[0])
        plan.append((best[0], i, home, best[1]))
    plan.sort(reverse=True)
    plan = plan[:WALKERS]
    for rise, i, home, top in plan:
        print(f'  walker #{i}: {rise:.2f} m up over {RISE_DIST:.0f} m', flush=True)
    ck(plan and plan[0][0] > 1.0, f'a slope to walk up (steepest rise {plan[0][0] if plan else 0:.2f} m over {RISE_DIST:.0f} m)')
    speeds = [2.5, 3.0, 3.5, 4.0]
    under = {}   # id -> since (guest time) it has been under
    worst = {}   # id -> (lowest agl, longest time under)
    samples = 0
    nxt = {i: 0.0 for _, i, _, _ in plan}
    up = {i: True for _, i, _, _ in plan}
    t_end = time.time() + a.secs
    k = 0
    while time.time() < t_end:
        now = time.time()
        for _, i, home, top in plan:
            if now >= nxt[i]:
                p = top if up[i] else home
                sp = speeds[k % len(speeds)]
                k += 1
                host.call(f'coop.npctrack walk {i} {p[0]:.1f} {p[1]:.1f} {p[2]:.1f} {sp}')
                nxt[i] = now + RISE_DIST / sp + 1.5
                up[i] = not up[i]
        px = proxies(guest)
        samples += 1
        tg = time.time()
        for _, i, _, _ in plan:
            if i not in px:
                under.pop(i, None)
                continue
            agl = px[i][1]
            lo, held = worst.get(i, (9.0, 0.0))
            if agl < -UNDER:
                under.setdefault(i, tg)
                held = max(held, tg - under[i])
            else:
                under.pop(i, None)
            worst[i] = (min(lo, agl), held)
        time.sleep(0.3)
    for _, i, _, _ in plan:
        lo, held = worst.get(i, (0.0, 0.0))
        print(f'  #{i}: lowest {lo:.2f} m over the guest\'s ground, longest under -{UNDER} m: {held:.1f} s', flush=True)
    bad = {i: w for i, w in worst.items() if w[1] >= HOLD}
    ck(samples > 50 and len(worst) >= 1, f'the guest showed the walkers ({samples} samples, {len(worst)} proxies)')
    ck(not bad, f'no proxy more than {UNDER} m under the guest\'s ground for {HOLD} s or longer '
       f'({len(bad)}: {sorted((i, round(w[0], 2), round(w[1], 1)) for i, w in bad.items())})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
