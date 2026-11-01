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
import re
import sys
import time

import census
from twoinst import Checks, start

R = 510.0  # the fixture planet's radius is about 505 m
# BUG-0377: the fixture planet's water surface (the guest's game holds a creature above deep water
# there). 0.7.3.3's walk went 450 m in a fixed direction at R into the sea: the game's deep water
# caption cinematic 5E205931 ("Dangerous creatures lurk in the deep") froze the host's avatar in
# the air 10.7 m above the water, the guest drew its puppet on the water (sync-check `p0 10.7`).
SEA = 499.2


def on_sphere(a, b, t):
    p = [x + (y - x) * t for x, y in zip(a, b)]
    n = math.sqrt(sum(x * x for x in p))
    return [x / n * R for x in p]


def away(p, metres, turn=0.0):
    """a point `metres` along the surface from p (`turn`: the direction, radians)"""
    n = math.sqrt(sum(x * x for x in p))
    u = [x / n for x in p]
    s1 = [0.0, -u[2], u[1]] if abs(u[0]) < 0.9 else [-u[2], 0.0, u[0]]
    sl = math.sqrt(sum(x * x for x in s1))
    s1 = [x / sl for x in s1]
    s2 = [u[1] * s1[2] - u[2] * s1[1], u[2] * s1[0] - u[0] * s1[2], u[0] * s1[1] - u[1] * s1[0]]
    side = [math.cos(turn) * x + math.sin(turn) * y for x, y in zip(s1, s2)]
    ang = metres / n
    return [n * (math.cos(ang) * a + math.sin(ang) * s) for a, s in zip(u, side)]


def ground(con, p):
    m = re.search(r'ground ([-\d.]+)', con.call('coop.abilities ground %.2f %.2f %.2f' % tuple(p))['out'])
    return float(m.group(1)) if m else 0.0


def land_path(con, a, metres, steps=20, dirs=16):
    """BUG-0377: the end of a walk of `metres` from a that stays on land (the direction whose lowest
    ground along the path is highest) and that lowest ground's height over the water"""
    best = None
    for d in range(dirs):
        b = away(a, metres, 2 * math.pi * d / dirs)
        low = min(ground(con, on_sphere(a, b, k / steps)) for k in range(steps + 1))
        if best is None or low > best[1]:
            best = (b, low)
    return best[0], best[1] - SEA


def walk_to(con, a, b, t):
    """the avatar onto the ground at fraction t of the way a -> b"""
    con.call('coop.abilities goto %.2f %.2f %.2f' % tuple(on_sphere(a, b, t)))


def drawn_apart(host, guest):
    """how far the guest draws the host's creature from where the host's own game has it (m): the
    puppet as the guest's game placed it in its last update (coop.track's d column), not the
    mod's own write"""
    hp = next(r['pos'] for r in census.dump(host).rows if r['own'] == 'avatar')
    guest.call('coop.track start')
    time.sleep(1.0)
    guest.call('coop.track stop')
    from coop_puppet_walk import dump
    rows = [r for r in dump(guest) if r['who'] == 0]
    return math.dist(hp, rows[-1]['d']) if rows else None


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print(' ', host.call('coop.fix npc.pooled off')['out'].strip())
    hc = census.dump(host)
    av = next(r for r in hc.rows if r['own'] == 'avatar')
    a = list(av['pos'])
    b, dry = land_path(host, a, 300)
    print(f'  the walk stays {dry:.1f} m or more over the water', flush=True)
    guest.call('coop.abilities goto %.1f %.1f %.1f' % tuple(a))
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
        walk_to(host, a, b, k / 20)
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
    apart = drawn_apart(host, guest)
    print(f'  the guest draws the host {apart if apart is not None else -1:.1f} m from where the host is', flush=True)
    ck(dry > 0, f'the host walks on land ({dry:.1f} m over the water at the lowest)')
    ck(apart is not None and apart < 3.0, 'the guest draws the host\'s creature where the host is (BUG-0377)')
    ck(samples >= 40, f'censuses of both games compared ({samples})')
    ck(not ghosts, f'every proxy of the guest is an NPC the host has in its world ({len(ghosts)} ghosts)')
    host.call('coop.fix npc.pooled on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
