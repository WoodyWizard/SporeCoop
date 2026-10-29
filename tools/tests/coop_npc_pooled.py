#!/usr/bin/env python3
"""Every NPC proxy a guest shows is a creature the host's game has in its world (BUG-0274).

The creature stage keeps NPCs only around the host's avatar: one farther away goes back to the
game's creature pool (no herd, position (0 0 0), no more updates) and a nest spawns its missing
members again and again. The host's NPC sync forgot such an object only after 2 s without an
update, so a guest standing there kept the proxy on the surface while the host's object sat in the
planet's centre (the campaign census: "c90: 504 m apart").

The guest stays where both start (at the nest), the host walks 300 m away and waits there. Every
0.6 s both censuses (`coop.census`): each proxy of the guest must match an NPC of the host with the
same net id that is in the world (not at the planet's centre). A proxy without one in two samples
in a row is a ghost.

  coop_npc_pooled.py [--without-fix]   (--without-fix: `coop.fix npc.pooled off`, must FAIL;
                                        SPORE_FIX_OFF=npc.pooled does the same)"""
import math
import sys
import time

import census
from twoinst import Checks, start

R = 510.0  # the fixture planet's radius is about 505 m


def on_sphere(a, b, t):
    p = [x + (y - x) * t for x, y in zip(a, b)]
    n = math.sqrt(sum(x * x for x in p))
    return [x / n * R for x in p]


def away(p, metres):
    """a point `metres` along the surface from p (any direction)"""
    n = math.sqrt(sum(x * x for x in p))
    u = [x / n for x in p]
    side = [0.0, -u[2], u[1]] if abs(u[0]) < 0.9 else [-u[2], 0.0, u[0]]
    sl = math.sqrt(sum(x * x for x in side))
    ang = metres / n
    return [n * (math.cos(ang) * a + math.sin(ang) * s / sl) for a, s in zip(u, side)]


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print(' ', host.call('coop.fix npc.pooled off')['out'].strip())
    hc = census.dump(host)
    av = next(r for r in hc.rows if r['own'] == 'avatar')
    a = list(av['pos'])
    b = away(a, 300)
    guest.call('coop.npc gopos %.1f %.1f %.1f' % tuple(on_sphere(a, a, 0)))
    time.sleep(4)
    streak, ghosts, centre_rows, matched, samples = {}, {}, 0, [], 0

    def sample():
        nonlocal centre_rows, samples
        h, g = census.dump(host), census.dump(guest)
        if not (h.complete and g.complete) or h.world != g.world:
            return
        samples += 1
        hn = h.by_id('c', ('npc',))
        centre_rows += sum(1 for r in hn.values() if math.dist(r['pos'], (0, 0, 0)) < 50)
        bad = set()
        for i, p in g.by_id('c', ('proxy',)).items():
            r = hn.get(i)
            if r is None or math.dist(r['pos'], (0, 0, 0)) < 50:
                bad.add(i)
                streak[i] = streak.get(i, 0) + 1
                if streak[i] >= 2:
                    ghosts[i] = (r['pos'] if r else None, p['pos'])
            else:
                matched.append(math.dist(r['pos'], p['pos']))
        for i in list(streak):
            if i not in bad:
                del streak[i]

    # the host walks away in 20 steps, then stays there 30 s
    for k in range(21):
        host.call('coop.npc gopos %.1f %.1f %.1f' % tuple(on_sphere(a, b, k / 20)))
        t0 = time.time()
        while time.time() - t0 < 1.2:
            sample()
            time.sleep(0.3)
    t0 = time.time()
    while time.time() - t0 < 30:
        sample()
        time.sleep(0.3)
    st = host.call('coop.npc list')['out'].splitlines()[0]
    print(f'  {samples} sample pairs, host NPC rows at the planet\'s centre: {centre_rows}, matched proxies: '
          f'{len(matched)} (median {sorted(matched)[len(matched) // 2] if matched else 0:.1f} m from the host\'s)')
    print('  host:', ' '.join(w for w in st.split() if w.startswith(('tracked=', 'pooled=', 'mirrored='))))
    for i, (hp, gp) in sorted(ghosts.items())[:8]:
        print(f'  ghost c{i}: host {census.fmt(hp) if hp else "none"}, guest proxy {census.fmt(gp)}')
    ck(samples >= 40, f'censuses of both games compared ({samples})')
    ck(not ghosts, f'every proxy of the guest is an NPC the host has in its world ({len(ghosts)} ghosts)')
    host.call('coop.fix npc.pooled on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
